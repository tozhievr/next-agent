#include "harness/harness.hpp"
#include <node_api.h>
#ifdef _WIN32
#include <windows.h>
#include <delayimp.h>
static FARPROC WINAPI delayHook(unsigned event, PDelayLoadInfo info) {
  if (event == dliNotePreLoadLibrary && _stricmp(info->szDll, "node.exe") == 0)
    return reinterpret_cast<FARPROC>(GetModuleHandleW(nullptr));
  return nullptr;
}
extern "C" const PfnDliHook __pfnDliNotifyHook2 = delayHook;
#endif
using namespace nextagent::harness;
using O = lx::host::JsonObject;
using A = lx::host::JsonArray;
struct Bridge {
  std::shared_ptr<Workspace> workspace, state;
  std::unique_ptr<Session> session;
  Json snapshot() {
    if (!session)
      return O{{"running", false},
               {"messages", A{}},
               {"events", A{}},
               {"changed", A{}}};
    auto data = session->serialize();
    auto view = session->snapshot();
    data["answer"] = view.answer;
    A events, changed;
    for (auto &e : view.events)
      events.push_back(O{{"kind", e.kind},
                         {"title", e.title},
                         {"detail", e.detail},
                         {"seconds", e.seconds},
                         {"error", e.error}});
    for (auto &p : view.changed)
      changed.push_back(p);
    data["events"] = events;
    data["changed"] = changed;
    return data;
  }
  Json dispatch(const Json &p) {
    auto action = string(p, "action");
    if (action == "init") {
      state = std::make_shared<Workspace>(string(p, "state"));
      return O{{"ready", true}};
    }
    if (action == "shutdown") {
      session.reset();
      workspace.reset();
      state.reset();
      return O{};
    }
    if (action == "preferencesRead") {
      try {
        return O{{"text", state->read("desktop.json")}};
      } catch (...) {
        return O{{"text", "{}"}};
      }
    }
    if (action == "preferencesWrite") {
      state->write("desktop.json", string(p, "text"));
      return O{};
    }
    if (action == "open") {
      if (session && session->snapshot().running)
        throw std::runtime_error(
            "Stop the current run before switching workspace");
      session.reset();
      workspace = std::make_shared<Workspace>(string(p, "root"));
      session = std::make_unique<Session>(workspace, state);
      return O{{"root", workspace->root()},
               {"capabilities", workspace->invoke("capabilities")},
               {"session", snapshot()}};
    }
    if (action == "list") {
      A sessions;
      if (state)
        try {
          for (auto &e : state->tree("sessions").at("entries").as_array()) {
            auto path = string(e, "path");
            if (!path.ends_with(".json"))
              continue;
            auto saved = parse(state->read(path));
            sessions.push_back(O{{"id", string(saved, "id")},
                                 {"title", string(saved, "title")},
                                 {"workspace", string(saved, "workspace")}});
          }
        } catch (...) {
        }
      return sessions;
    }
    if (action == "snapshot")
      return snapshot();
    if (!workspace || !session)
      throw std::runtime_error("Choose a workspace first");
    if (action == "new" || action == "restore") {
      if (session->snapshot().running)
        throw std::runtime_error("Stop the current run first");
      auto id = string(p, "id");
      if (action == "restore") {
        if (id.empty() ||
            id.find_first_not_of("0123456789-") != std::string::npos)
          throw std::runtime_error("Invalid session ID");
        auto saved = parse(state->read("sessions/" + id + ".json"));
        if (string(saved, "workspace") != workspace->root())
          throw std::runtime_error("Session belongs to another workspace");
        session =
            std::make_unique<Session>(workspace, state, http_transport(), id);
        session->restore(saved);
      } else
        session = std::make_unique<Session>(workspace, state);
      return snapshot();
    }
    if (action == "send") {
      Config c;
      c.model = string(p, "model");
      if (c.model.empty())
        c.model = "openrouter/auto";
      c.api_key = string(p, "key");
      c.read_only = p.at("readOnly").as_bool();
      auto prompt = string(p, "prompt");
      if (prompt.empty() || prompt.size() > 1024 * 1024)
        throw std::runtime_error("Prompt is empty or too large");
      if (p.at("offline").as_bool()) {
        if (session->snapshot().running)
          throw std::runtime_error("Already running");
        session =
            std::make_unique<Session>(workspace, state, offline_transport());
        c.endpoint = "offline://self-test";
      }
      if (!session->start(prompt, c))
        throw std::runtime_error("Already running");
      return snapshot();
    }
    if (action == "cancel") {
      session->cancel();
      return snapshot();
    }
    if (action == "tree")
      return workspace->tree(string(p, "path")).at("entries");
    if (action == "read")
      return O{{"text", workspace->read(string(p, "path"))}};
    if (action == "history")
      return workspace->invoke("history", O{{"limit", 20}});
    if (action == "labels")
      return workspace->invoke("labels", O{{"action", "list"}});
    if (action == "capabilities")
      return workspace->invoke("capabilities");
    throw std::runtime_error("Unknown desktop operation");
  }
};
static napi_value invoke(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value args[1];
  void *data = nullptr;
  napi_get_cb_info(env, info, &argc, args, nullptr, &data);
  try {
    if (argc != 1)
      throw std::runtime_error("Expected JSON request");
    size_t n = 0;
    if (napi_get_value_string_utf8(env, args[0], nullptr, 0, &n) != napi_ok ||
        n > 2 * 1024 * 1024)
      throw std::runtime_error("Invalid request");
    std::string input(n + 1, '\0');
    napi_get_value_string_utf8(env, args[0], input.data(), input.size(), &n);
    input.resize(n);
    auto result = static_cast<Bridge *>(data)->dispatch(parse(input)).dump();
    napi_value value;
    napi_create_string_utf8(env, result.data(), result.size(), &value);
    return value;
  } catch (const std::exception &e) {
    napi_throw_error(env, nullptr, e.what());
    return nullptr;
  }
}
NAPI_MODULE_INIT() {
  auto *bridge = new Bridge;
  napi_add_env_cleanup_hook(
      env, [](void *p) { delete static_cast<Bridge *>(p); }, bridge);
  napi_value fn;
  napi_create_function(env, "invoke", NAPI_AUTO_LENGTH, invoke, bridge, &fn);
  napi_set_named_property(env, exports, "invoke", fn);
  return exports;
}
