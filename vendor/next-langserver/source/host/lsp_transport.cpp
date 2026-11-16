#include "host/lsp_transport.hpp"

#include <charconv>
#include <cctype>
#include <cstdio>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif

namespace lx::host {

namespace {

bool iequals_prefix(std::string_view line, std::string_view prefix) {
  if (line.size() < prefix.size()) return false;
  for (size_t i = 0; i < prefix.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(line[i])) !=
        std::tolower(static_cast<unsigned char>(prefix[i]))) {
      return false;
    }
  }
  return true;
}

}  // namespace

LspTransport::LspTransport(std::istream& in, std::ostream& out) : in_(in), out_(out) {}

void LspTransport::set_stdio_binary() {
#if defined(_WIN32)
  _setmode(_fileno(stdin), _O_BINARY);
  _setmode(_fileno(stdout), _O_BINARY);
#endif
}

std::optional<JsonValue> LspTransport::read_message(std::string* out_error) {
  std::optional<size_t> content_length;
  std::string line;

  while (true) {
    line.clear();
    if (!std::getline(in_, line)) {
      return std::nullopt;
    }
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.empty()) {
      if (content_length.has_value()) break;
      continue;
    }
    constexpr std::string_view kPrefix = "Content-Length:";
    if (iequals_prefix(line, kPrefix)) {
      std::string_view rest(line);
      rest.remove_prefix(kPrefix.size());
      while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\t')) {
        rest.remove_prefix(1);
      }
      size_t len = 0;
      auto [ptr, ec] = std::from_chars(rest.data(), rest.data() + rest.size(), len);
      if (ec == std::errc{}) {
        content_length = len;
      } else {
        if (out_error) *out_error = "invalid Content-Length header";
        return std::nullopt;
      }
    }
  }

  if (!content_length.has_value()) {
    if (out_error) *out_error = "missing Content-Length header";
    return std::nullopt;
  }

  std::string body(*content_length, '\0');
  if (*content_length > 0) {
    in_.read(body.data(), static_cast<std::streamsize>(*content_length));
    if (in_.gcount() != static_cast<std::streamsize>(*content_length)) {
      if (out_error) *out_error = "truncated JSON-RPC body";
      return std::nullopt;
    }
  }

  auto parsed = parse_json(body);
  if (!parsed) {
    if (out_error) *out_error = "JSON parse error: " + parsed.error;
    return std::nullopt;
  }
  return std::move(*parsed.value);
}

bool LspTransport::write_message(const JsonValue& msg) {
  std::string body = msg.dump(false);
  std::string header = "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n";

  std::lock_guard<std::mutex> lock(write_mu_);
  out_.write(header.data(), static_cast<std::streamsize>(header.size()));
  out_.write(body.data(), static_cast<std::streamsize>(body.size()));
  out_.flush();
  return static_cast<bool>(out_);
}

bool LspTransport::send_response(const JsonValue& id, JsonValue result) {
  JsonObject obj;
  obj["jsonrpc"] = "2.0";
  obj["id"] = id;
  obj["result"] = std::move(result);
  return write_message(JsonValue{std::move(obj)});
}

bool LspTransport::send_error(const JsonValue& id, int code, std::string message) {
  JsonObject err;
  err["code"] = code;
  err["message"] = std::move(message);

  JsonObject obj;
  obj["jsonrpc"] = "2.0";
  obj["id"] = id;
  obj["error"] = JsonValue{std::move(err)};
  return write_message(JsonValue{std::move(obj)});
}

bool LspTransport::send_notification(std::string method, JsonValue params) {
  JsonObject obj;
  obj["jsonrpc"] = "2.0";
  obj["method"] = std::move(method);
  obj["params"] = std::move(params);
  return write_message(JsonValue{std::move(obj)});
}

}  // namespace lx::host
