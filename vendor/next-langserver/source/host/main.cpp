#include "host/lsp_server.hpp"
#include "host/lsp_transport.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
  lx::host::LspTransport::set_stdio_binary();

  lx::host::HostConfig config;
  std::vector<std::string> plugin_dirs;
  std::vector<std::string> manifests;
  bool show_help = false;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if ((arg == "--plugin-dir" || arg == "-p") && i + 1 < argc) {
      plugin_dirs.emplace_back(argv[++i]);
    } else if ((arg == "--manifest" || arg == "-m") && i + 1 < argc) {
      manifests.emplace_back(argv[++i]);
    } else if (arg == "--debounce-ms" && i + 1 < argc) {
      config.debounce = std::chrono::milliseconds(std::strtol(argv[++i], nullptr, 10));
    } else if (arg == "--help" || arg == "-h") {
      show_help = true;
    }
  }

  if (show_help) {
    std::cout << "lxserver - incremental multi-language LSP server\n"
              << "Usage: lxserver [--plugin-dir <dir>] [--manifest <file>] [--debounce-ms <ms>]\n";
    return 0;
  }

  lx::host::LspServer server(config);

  for (const auto& dir : plugin_dirs) {
    server.registry().index_directory(std::filesystem::path(dir));
  }
  for (const auto& m : manifests) {
    server.registry().register_manifest(std::filesystem::path(m));
  }
  std::string env_dirs_str;
#if defined(_WIN32)
  {
    char* buf = nullptr;
    size_t len = 0;
    if (_dupenv_s(&buf, &len, "LX_PLUGIN_DIRS") == 0 && buf != nullptr) {
      env_dirs_str = buf;
      std::free(buf);
    }
  }
#else
  if (const char* env_dirs = std::getenv("LX_PLUGIN_DIRS")) {
    env_dirs_str = env_dirs;
  }
#endif
  if (!env_dirs_str.empty()) {
    const std::string& s = env_dirs_str;
    size_t start = 0;
    while (start < s.size()) {
      size_t sep = s.find(';', start);
      std::string item = (sep == std::string::npos) ? s.substr(start) : s.substr(start, sep - start);
      if (!item.empty()) {
        server.registry().index_directory(std::filesystem::path(item));
      }
      if (sep == std::string::npos) break;
      start = sep + 1;
    }
  }

  return server.run(std::cin, std::cout);
}
