#include "host/lsp_server.hpp"
#include "host/lsp_transport.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace {

std::filesystem::path get_executable_dir(const char* argv0) {
#if defined(_WIN32)
  std::wstring buf(MAX_PATH, L'\0');
  for (;;) {
    DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    if (n == 0) break;
    if (n < buf.size()) {
      buf.resize(n);
      return std::filesystem::path(buf).parent_path();
    }
    if (buf.size() >= 32768) break;
    buf.resize(buf.size() * 2, L'\0');
  }
#endif
  if (argv0 && *argv0) {
    std::error_code ec;
    auto p = std::filesystem::weakly_canonical(std::filesystem::path(argv0), ec);
    if (!ec && p.has_parent_path()) {
      return p.parent_path();
    }
    auto raw = std::filesystem::path(argv0);
    if (raw.has_parent_path()) return raw.parent_path();
  }
  return {};
}

void index_dir_once(lx::host::LspServer& server,
                    const std::filesystem::path& dir,
                    std::unordered_set<std::string>& seen) {
  if (dir.empty()) return;
  std::error_code ec;
  if (!std::filesystem::exists(dir, ec) || !std::filesystem::is_directory(dir, ec)) {
    return;
  }
  std::filesystem::path norm = std::filesystem::weakly_canonical(dir, ec);
  if (ec || norm.empty()) {
    norm = dir.lexically_normal();
  }
  std::string key = norm.string();
#if defined(_WIN32)
  for (char& ch : key) {
    if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
  }
#endif
  if (!seen.insert(std::move(key)).second) {
    return;
  }
  server.registry().index_directory(norm);
}

}  // namespace

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
              << "Usage: lxserver [--plugin-dir <dir>] [--manifest <file>] [--debounce-ms <ms>]\n"
              << "Auto-discovers plugins in <exe_dir>/plugins, ./plugins, and LX_PLUGIN_DIRS.\n";
    return 0;
  }

  lx::host::LspServer server(config);
  std::unordered_set<std::string> seen_dirs;

  // Auto-discover plugins near executable and current working directory first,
  // so explicit LX_PLUGIN_DIRS / CLI options take precedence on extension conflicts.
  std::filesystem::path exe_dir = get_executable_dir(argc > 0 ? argv[0] : nullptr);
  if (!exe_dir.empty()) {
    index_dir_once(server, exe_dir / "plugins", seen_dirs);
  }
  {
    std::error_code ec;
    auto cwd = std::filesystem::current_path(ec);
    if (!ec && !cwd.empty()) {
      index_dir_once(server, cwd / "plugins", seen_dirs);
    }
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
        index_dir_once(server, std::filesystem::path(item), seen_dirs);
      }
      if (sep == std::string::npos) break;
      start = sep + 1;
    }
  }

  for (const auto& dir : plugin_dirs) {
    index_dir_once(server, std::filesystem::path(dir), seen_dirs);
  }
  for (const auto& m : manifests) {
    server.registry().register_manifest(std::filesystem::path(m));
  }

  return server.run(std::cin, std::cout);
}
