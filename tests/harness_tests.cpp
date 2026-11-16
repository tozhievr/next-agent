#include "harness/harness.hpp"
#include "src/platform.h"
#include <chrono>
#include <iostream>
using namespace nextagent::harness;
using Object = lx::host::JsonObject;
using Array = lx::host::JsonArray;
void require(bool p, const char *message) {
    if (!p)
        throw std::runtime_error(message);
}
class CancelTransport : public Transport {
  public:
    void stream(const Config &, const Json &, std::stop_token stop, const Sink &) override {
        while (!stop.stop_requested())
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        throw std::runtime_error("Cancelled");
    }
};
int main() try {
    int events = 0;
    Sse parser([&](const Json &v) {
        require(string(v, "hello") == "мир", "UTF-8 SSE corrupt");
        events++;
    });
    const auto input = std::string(": keepalive\r\ndata: {\"hello\":\"мир\"}\r\n\r\ndata: [DONE]\n\n");
    for (char c : input)
        parser.feed(std::string_view(&c, 1));
    parser.finish();
    require(events == 1, "SSE framing");
    bool incomplete = false;
    try {
        Sse broken([](const Json &) {});
        broken.feed("data: {}\n");
        broken.finish();
    } catch (...) {
        incomplete = true;
    }
    require(incomplete, "truncated SSE accepted");
    const std::string base = "build/harness-tests-" + std::to_string(pf_unix_ms());
    auto workspace = std::make_shared<Workspace>(base + "/project"),
         state = std::make_shared<Workspace>(base + "/state");
    Config config;
    config.endpoint = "offline://test";
    Session session(workspace, state, offline_transport());
    require(session.start("Verify native edits", config), "start failed");
    session.wait();
    auto view = session.snapshot();
    require(view.error.empty(), view.error.c_str());
    require(view.tool_calls == 1, "native tool missing");
    require(workspace->read("harness-check.txt").find("verified") != std::string::npos,
            "tool did not edit workspace");
    auto saved = parse(state->read("sessions/" + view.id + ".json"));
    require(saved.at("messages").as_array().size() == 4, "conversation lost tool pairing");
    auto isolated = std::make_shared<Workspace>(base + "/readonly");
    config.read_only = true;
    Session readonly(isolated, nullptr, offline_transport());
    readonly.start("Must not write", config);
    readonly.wait();
    require(isolated->read("harness-check.txt").empty(), "read-only tool escaped policy");
    require(readonly.snapshot().messages.dump().find("not permitted") != std::string::npos,
            "denial not reported to model");
    Session cancel(workspace, nullptr, std::make_shared<CancelTransport>());
    cancel.start("cancel", config);
    cancel.cancel();
    cancel.wait();
    require(cancel.snapshot().status == "Остановлено", "cancel failed");

#ifdef _WIN32
    auto cipher = protect_key("self-test-secret");
    require(cipher != "self-test-secret" && unprotect_key(cipher) == "self-test-secret",
            "DPAPI key storage failed");
#endif
    workspace->write(
        "tests/test_wait.py",
        "import unittest, time\nclass Slow(unittest.TestCase):\n def test_slow(self): time.sleep(10)\n");
    auto timed = workspace->invoke("run_check", Object{{"kind", "python"}, {"timeout_ms", 100}});
    require(timed.at("timed_out").as_bool(), "test timeout failed");

    auto second = std::make_shared<Workspace>(base + "/parallel");
    workspace->write("outline.py", "def alpha():\n    return 1\n");
    second->write("outline.py", "def beta():\n    return 2\n");
    std::atomic<bool> outlines_ok = true;
    auto outline = [&](std::shared_ptr<Workspace> w, const std::string &expected) {
        for (int i = 0; i < 30; i++) {
            auto result = w->invoke("outline", Object{{"target", "outline.py"}});
            bool found = false;
            for (auto &symbol : result.at("symbols").as_array())
                if (string(symbol, "name") == expected)
                    found = true;
            if (!found)
                outlines_ok = false;
        }
    };
    std::jthread one(outline, workspace, "alpha"), two(outline, second, "beta");
    one.join();
    two.join();
    require(outlines_ok, "parallel native language state leaked between workspaces");
    auto anchors=workspace->invoke("labels",Object{{"target","outline.py"},{"node_kind","function_def"}}, {},true);
    require(anchors.at("nodes").as_array().size()==1,"read-only labels query failed");
    auto anchor=string(anchors.at("nodes").as_array().front(),"anchor");
    auto denied=workspace->invoke("labels",Object{{"action","bind"},{"label","test.alpha"},{"anchor",anchor}}, {},true);
    require(denied.find("error")!=nullptr,"read-only label binding bypassed native policy");
    auto denied_plan=workspace->invoke("prepare",Object{{"files",lx::host::JsonArray{Object{{"op","create"},{"path","forbidden.txt"},{"text","bad"}}}}}, {},true);
    require(denied_plan.find("error")!=nullptr,"read-only prepare bypassed native policy");
    Json background_check;std::exception_ptr background_error;std::atomic<bool> checking=false;
    std::jthread check_thread([&]{checking=true;try{background_check=workspace->invoke("run_check",Object{{"kind","python"},{"timeout_ms",1200}});}catch(...){background_error=std::current_exception();}});
    while(!checking)std::this_thread::yield();pf_sleep_ms(60);
    auto began=std::chrono::steady_clock::now();auto visible=workspace->invoke("view",Object{{"target","outline.py"}});
    require(std::chrono::steady_clock::now()-began<std::chrono::milliseconds(700),"project check blocked workspace reads/UI");
    workspace->write("outline.py","def alpha():\n    return 3\n");
    check_thread.join();if(background_error)std::rethrow_exception(background_error);
    require(!background_check.at("fresh").as_bool(),"check remained fresh after a concurrent edit");


    std::cout << "Harness: byte-fragmented SSE, UTF-8, truncated stream, native transactions, persistence, "
                 "read-only and cancellation passed\n";
    return 0;
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
