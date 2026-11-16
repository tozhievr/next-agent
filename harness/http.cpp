#include "harness.hpp"
#include <algorithm>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#else
#include <curl/curl.h>
#endif
namespace nextagent::harness {
Json parse(std::string_view text) {
    auto value = lx::host::parse_json(text);
    if (!value)
        throw std::runtime_error("Invalid JSON: " + value.error);
    return std::move(*value.value);
}
std::string string(const Json &value, std::string_view key) { return std::string(value.at(key).as_string()); }
void Sse::feed(std::string_view bytes) {
    pending.append(bytes);
    if (pending.size() > 8 * 1024 * 1024)
        throw std::runtime_error("SSE frame exceeds limit");
    size_t at;
    while ((at = pending.find('\n')) != std::string::npos) {
        auto line = pending.substr(0, at);
        pending.erase(0, at + 1);
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (!line.starts_with("data:"))
            continue;
        auto data = line.substr(5);
        if (!data.empty() && data[0] == ' ')
            data.erase(0, 1);
        if (data == "[DONE]") {
            done = true;
            continue;
        }
        if (data.empty())
            continue;
        auto value = parse(data);
        if (value.find("error"))
            throw std::runtime_error("OpenRouter: " + string(value.at("error"), "message"));
        on_event(value);
    }
}
void Sse::finish() {
    if (!pending.empty())
        feed("\n");
    if (!done)
        throw std::runtime_error("OpenRouter stream ended before [DONE]");
}
namespace {
#ifdef _WIN32
std::wstring wide(std::string_view s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring r(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), r.data(), n);
    return r;
}
struct Handle {
    HINTERNET value = nullptr;
    ~Handle() {
        if (value)
            WinHttpCloseHandle(value);
    }
    operator HINTERNET() const { return value; }
};
class Http : public Transport {
  public:
    void stream(const Config &config, const Json &body, std::stop_token stop, const Sink &sink) override {
        auto url = wide(config.endpoint);
        URL_COMPONENTS parts{};
        parts.dwStructSize = sizeof(parts);
        parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = DWORD(-1);
        if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts))
            throw std::runtime_error("Invalid OpenRouter endpoint");
        if (parts.nScheme != INTERNET_SCHEME_HTTPS &&
            std::wstring(parts.lpszHostName, parts.dwHostNameLength) != L"127.0.0.1")
            throw std::runtime_error("HTTPS is required");
        Handle session{WinHttpOpen(L"NextAgentHarness/0.1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                   WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
        if (!session.value)
            throw std::runtime_error("WinHTTP initialization failed");
        WinHttpSetTimeouts(session, 15000, 15000, 30000, 30000);
        std::wstring host(parts.lpszHostName, parts.dwHostNameLength),
            path(parts.lpszUrlPath, parts.dwUrlPathLength);
        if (parts.dwExtraInfoLength)
            path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
        Handle connection{WinHttpConnect(session, host.c_str(), parts.nPort, 0)};
        Handle request{WinHttpOpenRequest(connection, L"POST", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                          WINHTTP_DEFAULT_ACCEPT_TYPES,
                                          parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)};
        if (!request.value)
            throw std::runtime_error("HTTP request initialization failed");
        std::atomic<HINTERNET> active{request.value};
        std::stop_callback cancel(stop, [&] {
            auto h = active.exchange(nullptr);
            if (h)
                WinHttpCloseHandle(h);
        });
        auto finish = [&] { request.value = active.exchange(nullptr); };
        auto text = body.dump();
        auto headers = wide("Authorization: Bearer " + config.api_key +
                            "\r\nContent-Type: application/json\r\nAccept: text/event-stream\r\nX-Title: "
                            "NextAgent Harness\r\n");
        bool ok = WinHttpSendRequest(request, headers.c_str(), DWORD(-1), text.data(), DWORD(text.size()),
                                     DWORD(text.size()), 0) &&
                  WinHttpReceiveResponse(request, nullptr);
        if (!ok) {
            finish();
            throw std::runtime_error(stop.stop_requested() ? "Cancelled"
                                                           : "OpenRouter connection failed (" +
                                                                 std::to_string(GetLastError()) + ")");
        }
        DWORD status = 0, size = sizeof(status);
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
        try {
            std::string error;
            char buffer[8192];
            DWORD n = 0;
            while (!stop.stop_requested()) {
                if (!WinHttpReadData(request, buffer, sizeof(buffer), &n))
                    throw std::runtime_error(stop.stop_requested() ? "Cancelled"
                                                                   : "OpenRouter receive failed");
                if (!n)
                    break;
                if (status >= 200 && status < 300)
                    sink(std::string_view(buffer, n));
                else if (error.size() < 16384)
                    error.append(buffer, n);
            }
            if (status < 200 || status >= 300)
                throw std::runtime_error("HTTP " + std::to_string(status) + ": " + error);
            finish();
        } catch (...) {
            finish();
            throw;
        }
    }
};
#else
class Http : public Transport {
  public:
    void stream(const Config &config, const Json &body, std::stop_token stop, const Sink &sink) override {
        static const int initialized = []() { return curl_global_init(CURL_GLOBAL_DEFAULT); }();
        if (initialized)
            throw std::runtime_error("curl initialization failed");
        CURL *request = curl_easy_init();
        if (!request)
            throw std::runtime_error("curl allocation failed");
        auto text = body.dump();
        struct State {
            const Sink *sink;
            std::stop_token stop;
            std::string data, error;
            std::exception_ptr failure;
        } state{&sink, stop};
        curl_slist *headers = nullptr;
        headers = curl_slist_append(headers, ("Authorization: Bearer " + config.api_key).c_str());
        headers = curl_slist_append(headers, "Content-Type: application/json");
        headers = curl_slist_append(headers, "X-Title: NextAgent Harness");
        curl_easy_setopt(request, CURLOPT_URL, config.endpoint.c_str());
        curl_easy_setopt(request, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(request, CURLOPT_POSTFIELDS, text.c_str());
        curl_easy_setopt(request, CURLOPT_POSTFIELDSIZE_LARGE, curl_off_t(text.size()));
        curl_easy_setopt(request, CURLOPT_CONNECTTIMEOUT, 15L);
        curl_easy_setopt(request, CURLOPT_TIMEOUT, 300L);
        curl_easy_setopt(request, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(request, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(
            request, CURLOPT_XFERINFOFUNCTION,
            +[](void *raw, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
                return static_cast<State *>(raw)->stop.stop_requested() ? 1 : 0;
            });
        curl_easy_setopt(request, CURLOPT_XFERINFODATA, &state);
        curl_easy_setopt(
            request, CURLOPT_WRITEFUNCTION, +[](char *data, size_t a, size_t b, void *raw) -> size_t {
                auto &s = *static_cast<State *>(raw);
                size_t n = a * b;
                try {
                    (*s.sink)(std::string_view(data, n));
                } catch (...) {
                    s.failure = std::current_exception();
                    return 0;
                }
                return n;
            });
        curl_easy_setopt(request, CURLOPT_WRITEDATA, &state);
        auto result = curl_easy_perform(request);
        long status = 0;
        curl_easy_getinfo(request, CURLINFO_RESPONSE_CODE, &status);
        curl_slist_free_all(headers);
        curl_easy_cleanup(request);
        if (state.failure)
            std::rethrow_exception(state.failure);
        if (stop.stop_requested())
            throw std::runtime_error("Cancelled");
        if (result != CURLE_OK)
            throw std::runtime_error(curl_easy_strerror(result));
        if (status < 200 || status >= 300)
            throw std::runtime_error("OpenRouter HTTP " + std::to_string(status));
    }
};
#endif
} // namespace
std::shared_ptr<Transport> http_transport() { return std::make_shared<Http>(); }
} // namespace nextagent::harness
