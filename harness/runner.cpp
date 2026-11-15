#include "harness.hpp"
#include "src/platform.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincrypt.h>
#endif
namespace nextagent::harness {
using Object = lx::host::JsonObject;
using Array = lx::host::JsonArray;
namespace {
Json convert(J *input) {
    if (!input)
        return Json(nullptr);
    char *text = json_dump(input);
    auto result = parse(text);
    free(text);
    json_free(input);
    return result;
}
J *native(const Json &value) {
    auto text = value.dump();
    return json_parse(text.data(), text.size());
}
const std::set<std::string> reads{"view",          "outline",    "select",           "search",
                                  "hybrid_search", "context",    "callers",          "callees",
                                  "find_symbols",  "references", "language_modules", "analyze", "tree"};
std::string redact(std::string value, const std::string &secret) {
    if (!secret.empty())
        for (size_t p = 0; (p = value.find(secret, p)) != std::string::npos;)
            value.replace(p, secret.size(), "[redacted]");
    return value;
}
} // namespace
Workspace::Workspace(const std::string &root) {
    pf_mkdirs(root.c_str());
    store = store_open(root.c_str());
    if (!store)
        throw std::runtime_error(na_errmsg());
    rpc.store = store;
    rpc.autoflush = true;
    rpc.session = "harness";
    rpc.languages = language_new(store_root(store));
    if (!rpc.languages) {
        store_close(store);
        throw std::runtime_error(na_errmsg());
    }
}
Workspace::~Workspace() {
    rpc_release(&rpc);
    language_free(rpc.languages);
    store_close(store);
}
Json Workspace::invoke(const std::string &method, const Json &params, std::stop_token stop, bool read_only) {
    std::lock_guard lock(mutex);
    rpc.cancelled = +[](void *ctx) { return static_cast<std::stop_token *>(ctx)->stop_requested(); };
    rpc.cancel_context = &stop;
    struct Reset {
        Rpc &rpc;
        ~Reset() {
            rpc.cancelled = nullptr;
            rpc.cancel_context = nullptr;
        }
    } reset{rpc};
    auto dispatch = [&](const std::string &name, const Json &arguments) {
        J *request = native(Object{{"jsonrpc", "2.0"}, {"id", 1}, {"method", name}, {"params", arguments}});
        auto response = convert(rpc_dispatch(&rpc, request));
        json_free(request);
        if (response.find("error"))
            return Json(Object{{"error", response.at("error")}});
        return response.at("result");
    };
    if (reads.contains(method) && params.at("target").is_string()) {
        auto merged = dispatch(
            "merge",
            Object{{"paths", Array{params.at("target")}}, {"strategy", "auto"}, {"dry_run", read_only}});
        std::function<bool(const Json &)> conflict = [&](const Json &value) {
            if (string(value, "status") == "conflict" ||
                (read_only && !string(value, "status").empty() && string(value, "status") != "unchanged"))
                return true;
            for (auto &child : value.as_array())
                if (conflict(child))
                    return true;
            for (auto &item : value.as_object())
                if (conflict(item.second))
                    return true;
            return false;
        };
        if (merged.find("error") || conflict(merged))
            return Object{{"error", "External edits conflict; resolve using merge before continuing"},
                          {"report", merged}};
    }
    auto result = dispatch(method, params);
    if (result.at("written").is_bool() && !result.at("written").as_bool())
        result["error"] = "Native flush failed; reconcile external changes using merge";
    return result;
}
Json Workspace::tools() {
    std::lock_guard lock(mutex);
    J *req = native(Object{{"jsonrpc", "2.0"}, {"id", 1}, {"method", "tools/list"}});
    auto value = convert(rpc_mcp_dispatch(&rpc, req));
    json_free(req);
    return value.at("result").at("tools");
}
Json Workspace::tree(const std::string &path) {
    return invoke("tree", Object{{"path", path}, {"limit", 500}});
}
std::string Workspace::read(const std::string &path, bool read_only) {
    auto result = invoke("view", Object{{"target", path}}, {}, read_only);
    if (result.find("error"))
        throw std::runtime_error(result.at("error").dump());
    return string(result, "text");
}
void Workspace::write(const std::string &path, const std::string &text) {
    auto old = read(path);
    auto result =
        invoke("edit", Object{{"ops", Array{Object{{"path", path},
                                                   {"start", 0},
                                                   {"end", int64_t(utf8_cp_count(old.data(), old.size()))},
                                                   {"text", text}}}},
                              {"label", "Harness session state"}});
    if (result.find("error"))
        throw std::runtime_error(result.at("error").dump());
}
std::string Workspace::root() const { return store_root(store); }
std::string default_state_directory() {
#ifdef _WIN32
    const char *base = getenv("LOCALAPPDATA");
    return std::string(base ? base : ".") + "/NextAgentHarness";
#else
    if (auto base = getenv("XDG_STATE_HOME"))
        return std::string(base) + "/nextagent-harness";
    auto home = getenv("HOME");
    return std::string(home ? home : ".") + "/.local/state/nextagent-harness";
#endif
}
std::string protect_key(std::string_view text) {
#ifdef _WIN32
    DATA_BLOB in{DWORD(text.size()), reinterpret_cast<BYTE *>(const_cast<char *>(text.data()))}, out{};
    if (!CryptProtectData(&in, L"NextAgent OpenRouter", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN,
                          &out))
        throw std::runtime_error("Could not protect API key");
    std::string value(out.cbData * 2 + 1, '0');
    hex_encode(out.pbData, out.cbData, value.data());
    value.resize(out.cbData * 2);
    LocalFree(out.pbData);
    return value;
#else
    if (!text.empty())
        throw std::runtime_error("Use OPENROUTER_API_KEY on Linux");
    return {};
#endif
}
std::string unprotect_key(std::string_view text) {
#ifdef _WIN32
    if (text.empty())
        return {};
    std::vector<uint8_t> data(text.size() / 2);
    if (text.size() % 2 || !hex_decode(text.data(), data.data(), data.size()))
        return {};
    DATA_BLOB in{DWORD(data.size()), data.data()}, out{};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
        return {};
    std::string value(reinterpret_cast<char *>(out.pbData), out.cbData);
    LocalFree(out.pbData);
    return value;
#else
    return {};
#endif
}
Session::Session(std::shared_ptr<Workspace> w, std::shared_ptr<Workspace> s, std::shared_ptr<Transport> t,
                 std::string id)
    : workspace(std::move(w)), state(std::move(s)), transport(std::move(t)) {
    static std::atomic<uint64_t> serial = 0;
    current.id = id.empty() ? std::to_string(pf_unix_ms()) + "-" + std::to_string(++serial) : std::move(id);
    current.title = "Новая сессия";
}
Session::~Session() {
    cancel();
    wait();
}
View Session::snapshot() const {
    std::lock_guard lock(mutex);
    return current;
}
void Session::cancel() {
    if (worker.joinable())
        worker.request_stop();
}
void Session::wait() {
    if (worker.joinable())
        worker.join();
}
bool Session::start(std::string prompt, Config config) {
    {
        std::lock_guard lock(mutex);
        if (current.running || prompt.empty())
            return false;
        current.running = true;
        current.status = "Подключение к OpenRouter";
        current.error.clear();
        current.answer.clear();
        if (current.title == "Новая сессия")
            current.title =
                prompt.substr(0, (std::min)(prompt.size(), cp_to_byte(prompt.data(), prompt.size(), 50)));
    }
    wait();
    worker = std::jthread([this, p = std::move(prompt), c = std::move(config)](std::stop_token stop) mutable {
        run(std::move(p), std::move(c), stop);
    });
    return true;
}
Json Session::serialize() const {
    auto view = snapshot();
    return Object{{"id", view.id},
                  {"title", view.title},
                  {"workspace", workspace->root()},
                  {"messages", view.messages},
                  {"input_tokens", view.input_tokens},
                  {"output_tokens", view.output_tokens},
                  {"cost", view.cost},
                  {"cost_reported", view.cost_reported},
                  {"status", view.status},
                  {"error", view.error},
                  {"running", view.running},
                  {"tool_calls", view.tool_calls},
                  {"elapsed", view.elapsed}};
}
void Session::restore(const Json &data) {
    std::lock_guard lock(mutex);
    if (current.running)
        return;
    current.title = string(data, "title");
    if (data.at("messages").is_array())
        current.messages = data.at("messages");
    current.input_tokens = data.at("input_tokens").as_uint64();
    current.output_tokens = data.at("output_tokens").as_uint64();
    current.cost = data.at("cost").as_double();
    current.cost_reported = data.at("cost_reported").as_bool() || current.cost > 0;
    current.tool_calls = data.at("tool_calls").as_uint64();
    current.elapsed = data.at("elapsed").as_double();
    current.status = string(data, "status");
    if (current.status.empty())
        current.status = "Готов";
    current.error = string(data, "error");
    if (data.at("running").as_bool()) {
        current.status = "Прервано";
        current.error = "Предыдущий запуск прерван. Проверьте историю next-agent перед продолжением.";
    }

    for (auto &m : current.messages.as_array())
        if (string(m, "role") == "assistant" && !string(m, "content").empty())
            current.answer = string(m, "content");
}
void Session::save() {
    if (!state)
        return;
    try {
        auto data = serialize();
        state->write("sessions/" + string(data, "id") + ".json", data.dump());
    } catch (const std::exception &e) {
        std::lock_guard lock(mutex);
        current.events.push_back({"storage", "Не удалось сохранить сессию", e.what(), 0, true});
    }
}
void Session::run(std::string prompt, Config config, std::stop_token stop) {
    const auto started = std::chrono::steady_clock::now();
    auto event = [&](Event e) {
        {
            std::lock_guard lock(mutex);
            current.events.push_back(std::move(e));
            if (current.events.size() > 500)
                current.events.erase(current.events.begin());
        }
        if (notify)
            notify();
    };
    auto append = [&](Json message) {
        std::lock_guard lock(mutex);
        current.messages.as_array_mut().push_back(std::move(message));
    };
    append(Object{{"role", "user"}, {"content", prompt}});
    save();
    try {
        if (config.api_key.empty() && config.endpoint.starts_with("https://"))
            throw std::runtime_error("Добавьте OpenRouter API key в настройках или OPENROUTER_API_KEY");
        if (config.api_key.find_first_of("\r\n") != std::string::npos)
            throw std::runtime_error("API key contains a newline");
        Array tools;
        std::set<std::string> allowed;
        for (auto &tool : workspace->tools().as_array()) {
            auto name = string(tool, "name");
            if (config.read_only && !reads.contains(name))
                continue;
            allowed.insert(name);
            tools.push_back(Object{{"type", "function"},
                                   {"function", Object{{"name", name},
                                                       {"description", string(tool, "description")},
                                                       {"parameters", tool.at("inputSchema")}}}});
        }
        std::string system =
            "You are NextAgent, a coding agent working in " + workspace->root() +
            ". ALL file and codebase operations MUST use the provided NextAgent tools. Never invent file "
            "contents, tool outcomes or test results. Use tree and context to inspect the workspace, read "
            "applicable AGENTS.md before editing. Use atomic patch/edit and current revisions for changes. "
            "Preserve user changes. Report limitations honestly. Native file tools are confined to this "
            "workspace. run_check executes project checks as the OS user; never use checks as a workaround "
            "for source-file access. " +
            (config.read_only
                 ? "This run is read-only: analyze and propose changes, do not modify files."
                 : "You may implement the user's task in this workspace using NextAgent transactions.");
        auto instructions = workspace->read("AGENTS.md", config.read_only);
        if (instructions.size() > 16000) {
            size_t end = 16000;
            while (end && (static_cast<unsigned char>(instructions[end]) & 0xc0) == 0x80)
                --end;
            instructions.resize(end);
        }
        if (!instructions.empty())
            system += "\nWorkspace AGENTS.md:\n" + instructions;
        bool completed = false;
        for (int step = 0; step < config.max_steps && !stop.stop_requested(); step++) {
            save();
            event({"model", "Запрос модели", config.model});
            {
                std::lock_guard lock(mutex);
                current.status = "Модель: " + config.model;
                current.answer.clear();
            }
            Array messages{Object{{"role", "system"}, {"content", system}}};
            auto previous = snapshot().messages.as_array();
            messages.insert(messages.end(), previous.begin(), previous.end());
            Json request = Object{{"model", config.model},
                                  {"messages", std::move(messages)},
                                  {"tools", tools},
                                  {"stream", true},
                                  {"stream_options", Object{{"include_usage", true}}},
                                  {"parallel_tool_calls", false},
                                  {"provider", Object{{"require_parameters", true}}}};
            std::string content, finish;
            Json usage;
            std::map<int, Json> calls;
            std::map<int, Json> reasoning;
            Sse stream([&](const Json &chunk) {
                if (auto u = chunk.find("usage"))
                    usage = *u;
                for (auto &choice : chunk.at("choices").as_array()) {
                    auto &delta = choice.at("delta");
                    auto piece = string(delta, "content");
                    content += piece;
                    if (!piece.empty()) {
                        std::lock_guard lock(mutex);
                        current.answer += piece;
                        current.status = "Модель отвечает";
                    }
                    for (auto &c : delta.at("tool_calls").as_array()) {
                        int index = int(c.at("index").as_int());
                        auto &tool = calls[index];
                        if (tool.is_null())
                            tool = Object{{"id", ""},
                                          {"type", "function"},
                                          {"function", Object{{"name", ""}, {"arguments", ""}}}};
                        if (auto id = c.find("id"))
                            tool["id"] =
                                std::string(tool.at("id").as_string()) + std::string(id->as_string());
                        for (auto key : {"name", "arguments"})
                            if (auto value = c.at("function").find(key))
                                tool["function"][key] = std::string(tool.at("function").at(key).as_string()) +
                                                        std::string(value->as_string());
                    }
                    for (auto &detail : delta.at("reasoning_details").as_array()) {
                        int index = int(detail.at("index").as_int());
                        auto &target = reasoning[index];
                        if (target.is_null())
                            target = Object{};
                        for (auto &[key, value] : detail.as_object())
                            if (value.is_string() && target.at(key).is_string() && key != "type" &&
                                key != "id" && key != "format")
                                target[key] =
                                    std::string(target.at(key).as_string()) + std::string(value.as_string());
                            else
                                target[key] = value;
                    }
                    if (!choice.at("finish_reason").is_null())
                        finish = string(choice, "finish_reason");
                }
                if (notify)
                    notify();
            });
            const auto sent = std::chrono::steady_clock::now();
            transport->stream(config, request, stop, [&](std::string_view bytes) {
                if (stop.stop_requested())
                    throw std::runtime_error("Cancelled");
                stream.feed(bytes);
            });
            stream.finish();
            {
                std::lock_guard lock(mutex);
                current.input_tokens += usage.at("prompt_tokens").as_uint64();
                current.output_tokens += usage.at("completion_tokens").as_uint64();
                current.cached_tokens += usage.at("prompt_tokens_details").at("cached_tokens").as_uint64();
                current.cost += usage.at("cost").as_double();
                if (usage.at("cost").is_number())
                    current.cost_reported = true;
            }
            event({"model_done", "Ответ модели получен", finish,
                   std::chrono::duration<double>(std::chrono::steady_clock::now() - sent).count()});
            if (calls.size() > 16)
                throw std::runtime_error("Model returned too many simultaneous tool calls");
            Json assistant =
                Object{{"role", "assistant"}, {"content", content.empty() ? Json(nullptr) : Json(content)}};
            Array call_array;
            for (auto &[index, tool] : calls) {
                if (string(tool, "id").empty())
                    tool["id"] = "call_" + std::to_string(step) + "_" + std::to_string(index);
                call_array.push_back(tool);
            }
            if (!call_array.empty())
                assistant["tool_calls"] = call_array;
            if (!reasoning.empty()) {
                Array details;
                for (auto &[index, value] : reasoning)
                    details.push_back(value);
                assistant["reasoning_details"] = std::move(details);
            }
            append(assistant);
            if (calls.empty()) {
                if (finish.empty())
                    throw std::runtime_error("Empty or incomplete model response");
                if (finish == "content_filter")
                    throw std::runtime_error("Provider filtered this response");
                if (finish == "length")
                    throw std::runtime_error("Модель достигла лимита вывода; ответ не завершён");
                completed = true;
                break;
            }
            for (auto &tool : call_array) {
                const auto name = string(tool.at("function"), "name");
                Json result;
                auto t0 = std::chrono::steady_clock::now();
                event({"tool", "Выполняется: " + name, string(tool.at("function"), "arguments")});
                try {
                    if (stop.stop_requested())
                        throw std::runtime_error("Cancelled");
                    if (!allowed.contains(name))
                        throw std::runtime_error("Tool not permitted in this profile");
                    result = workspace->invoke(name, parse(string(tool.at("function"), "arguments")), stop,
                                               config.read_only);
                } catch (const std::exception &e) {
                    result = Object{{"error", e.what()}};
                }
                {
                    std::lock_guard lock(mutex);
                    current.tool_calls++;
                    current.status = "Инструмент: " + name;
                    for (auto &path : result.at("paths").as_array()) {
                        std::string p(path.as_string());
                        if (std::find(current.changed.begin(), current.changed.end(), p) ==
                            current.changed.end())
                            current.changed.push_back(p);
                    }
                }
                auto text = result.dump();
                size_t cap = 60000;
                while (cap < text.size() && cap && (static_cast<unsigned char>(text[cap]) & 0xc0) == 0x80)
                    --cap;
                if (text.size() > 64000)
                    text = Json(Object{{"truncated", true},
                                       {"excerpt", text.substr(0, cap)},
                                       {"instruction", "Narrow the next query; this result was truncated"}})
                               .dump();
                append(Object{{"role", "tool"},
                              {"tool_call_id", string(tool, "id")},
                              {"name", name},
                              {"content", text}});
                event({"tool_done", name, result.dump().substr(0, 4000),
                       std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(),
                       result.find("error") != nullptr});
            }
        }
        {
            std::lock_guard lock(mutex);
            if (stop.stop_requested())
                current.status = "Остановлено";
            else if (!completed) {
                current.status = "Лимит шагов";
                current.error = "Достигнут лимит шагов; задача не объявлена завершённой";
            } else
                current.status = "Завершено";
        }
    } catch (const std::exception &e) {
        std::lock_guard lock(mutex);
        current.status = stop.stop_requested() ? "Остановлено" : "Ошибка";
        if (!stop.stop_requested())
            current.error = redact(e.what(), config.api_key);
    }
    {
        std::lock_guard lock(mutex);
        current.elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        current.running = false;
    }
    save();
    if (notify)
        notify();
}
namespace {
class Offline : public Transport {
    int turn = 0;

  public:
    void stream(const Config &, const Json &, std::stop_token stop, const Sink &sink) override {
        if (stop.stop_requested())
            throw std::runtime_error("Cancelled");
        Json delta;
        if (turn++ == 0) {
            Json args = Object{{"edits", Array{Object{{"path", "harness-check.txt"},
                                                      {"new", "Native NextAgent transaction verified\n"},
                                                      {"create", true}}}},
                               {"label", "Offline Harness verification"}};
            Json function = Object{{"name", "patch"}, {"arguments", args.dump()}};
            Json call = Object{{"index", 0}, {"id", "offline_patch"}, {"function", function}};
            delta = Object{{"tool_calls", Array{call}}};
        } else
            delta = Object{{"content",
                            "Проверка завершена. Файл harness-check.txt создан транзакцией next-agent. Это "
                            "автономный self-test, запросы к модели не отправлялись."}};
        auto payload =
            Json(Object{{"choices", Array{Object{{"delta", delta},
                                                 {"finish_reason", turn == 1 ? "tool_calls" : "stop"}}}}})
                .dump();
        sink("data: " + payload + "\n\ndata: [DONE]\n\n");
    }
};
} // namespace
std::shared_ptr<Transport> offline_transport() { return std::make_shared<Offline>(); }
} // namespace nextagent::harness
