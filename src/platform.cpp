/* Platform layer: Windows (kernel32) or Linux (POSIX system calls). */

#if defined(__linux__)

#include "platform_posix.cpp"

#elif defined(__APPLE__)

#include "platform_apple.cpp"

#else

#include "platform_win.cpp"

#endif

#include <filesystem>
int pf_remove_dir(const char *path) {
    std::error_code error;
    auto p = std::filesystem::path(std::u8string(reinterpret_cast<const char8_t *>(path)));
    if (!std::filesystem::is_directory(p, error) || std::filesystem::is_symlink(p, error))
        return na_fail("not a regular directory: %s", path);
    bool removed = std::filesystem::remove(p, error);
    return removed && !error ? 0 : na_fail("cannot remove directory: %s", path);
}
