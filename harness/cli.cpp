#include "harness.hpp"
#include <chrono>
#include <cstdlib>
#include <iostream>
using namespace nextagent::harness;
int main(int argc, char **argv) try {
    std::string root = ".", prompt, state_dir = default_state_directory();
    bool offline = false, json = false;
    Config config;
    if (auto key = getenv("OPENROUTER_API_KEY"))
        config.api_key = key;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        auto value = [&]() {
            if (i + 1 >= argc)
                throw std::runtime_error("Missing argument: " + arg);
            return std::string(argv[++i]);
        };
        if (arg == "--root")
            root = value();
        else if (arg == "--prompt")
            prompt = value();
        else if (arg == "--model")
            config.model = value();
        else if (arg == "--state-dir")
            state_dir = value();
        else if (arg == "--endpoint")
            config.endpoint = value();
        else if (arg == "--max-steps")
            config.max_steps = std::stoi(value());
        else if (arg == "--read-only")
            config.read_only = true;
        else if (arg == "--offline")
            offline = true;
        else if (arg == "--json")
            json = true;
        else
            throw std::runtime_error("Unknown option: " + arg);
    }
    if (prompt.empty()) {
        std::cout
            << "nextagent-harness-cli --root DIR --prompt TEXT [--model openrouter/auto] [--read-only]\nSet "
               "OPENROUTER_API_KEY. --offline runs an isolated scripted transaction check.\n";
        return 0;
    }
    auto workspace = std::make_shared<Workspace>(root), state = std::make_shared<Workspace>(state_dir);
    if (offline) {
        config.endpoint = "offline://self-test";
        config.model = "offline-self-test";
    }
    Session session(workspace, state, offline ? offline_transport() : http_transport());
    session.start(prompt, config);
    session.wait();
    auto view = session.snapshot();
    if (json) {
        auto result = session.serialize();
        result["status"] = view.status;
        result["error"] = view.error;
        result["tool_calls"] = view.tool_calls;
        result["answer"] = view.answer;
        std::cout << result.dump() << '\n';
    } else {
        std::cout << view.answer << '\n';
        if (!view.error.empty())
            std::cerr << view.error << '\n';
        std::cout << "Tools: " << view.tool_calls << " · Tokens: " << view.input_tokens + view.output_tokens
                  << '\n';
    }
    return view.error.empty() && view.status == "Завершено" ? 0 : 1;
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
