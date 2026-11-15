#pragma once
#include "host/json.hpp"
#include "src/rpc.h"
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <stop_token>
#include <thread>
#include <vector>

namespace nextagent::harness {
using Json = lx::host::JsonValue;
Json parse(std::string_view text);
std::string string(const Json &value, std::string_view key);
struct Config {
    std::string model = "openrouter/auto", api_key,
                endpoint = "https://openrouter.ai/api/v1/chat/completions";
    int max_steps = 24;
    bool read_only = false;
};
struct Event {
    std::string kind, title, detail;
    double seconds = 0;
    bool error = false;
};
struct View {
    std::string id, title, status = "Готов", answer, error;
    Json messages{lx::host::JsonArray{}};
    std::vector<Event> events;
    std::vector<std::string> changed;
    uint64_t input_tokens = 0, output_tokens = 0, cached_tokens = 0, tool_calls = 0;
    double cost = 0, elapsed = 0;
    bool running = false, cost_reported = false;
};
using Sink = std::function<void(std::string_view)>;
class Transport {
  public:
    virtual ~Transport() = default;
    virtual void stream(const Config &, const Json &, std::stop_token, const Sink &) = 0;
};
std::shared_ptr<Transport> http_transport();
std::shared_ptr<Transport> offline_transport();
class Sse {
    std::string pending;
    std::function<void(const Json &)> on_event;

  public:
    bool done = false;
    explicit Sse(std::function<void(const Json &)> callback) : on_event(std::move(callback)) {};
    void feed(std::string_view);
    void finish();
};
class Workspace {
    Store *store = nullptr;
    Rpc rpc{};
    std::mutex mutex;

  public:
    explicit Workspace(const std::string &root);
    ~Workspace();
    Workspace(const Workspace &) = delete;
    Json invoke(const std::string &, const Json &params = Json(lx::host::JsonObject{}),
                std::stop_token stop = {}, bool read_only = false);
    Json tools();
    Json tree(const std::string &path = "");
    std::string read(const std::string &path, bool read_only = false);
    void write(const std::string &path, const std::string &text);
    std::string root() const;
};
class Session {
    std::shared_ptr<Workspace> workspace, state;
    std::shared_ptr<Transport> transport;
    mutable std::mutex mutex;
    View current;
    std::jthread worker;
    std::function<void()> notify;
    void run(std::string prompt, Config config, std::stop_token stop);
    void save();

  public:
    Session(std::shared_ptr<Workspace>, std::shared_ptr<Workspace> state,
            std::shared_ptr<Transport> transport = http_transport(), std::string id = "");
    ~Session();
    bool start(std::string prompt, Config);
    void cancel();
    void wait();
    View snapshot() const;
    void set_notify(std::function<void()> callback) { notify = std::move(callback); }
    void restore(const Json &);
    Json serialize() const;
};
std::string default_state_directory();
std::string protect_key(std::string_view);
std::string unprotect_key(std::string_view);
} // namespace nextagent::harness
