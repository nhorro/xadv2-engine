#include "engine/core/user_data.hpp"

#include <cstdlib>
#include <system_error>

namespace pac::core {

namespace {

const char* env(const char* name) {
    const char* v = std::getenv(name);
    return (v && *v) ? v : nullptr;
}

std::filesystem::path platform_base() {
#if defined(_WIN32)
    if (const char* appdata = env("APPDATA")) {
        return std::filesystem::path(appdata);
    }
#elif defined(__APPLE__)
    if (const char* home = env("HOME")) {
        return std::filesystem::path(home) / "Library" / "Application Support";
    }
#else // Linux + other Unix
    if (const char* xdg = env("XDG_DATA_HOME")) {
        return std::filesystem::path(xdg);
    }
    if (const char* home = env("HOME")) {
        return std::filesystem::path(home) / ".local" / "share";
    }
#endif
    // Fallback: current directory. Dev-only path; saves end up next to the
    // executable's working dir, which is fine for tests and headless runs.
    return std::filesystem::path(".");
}

} // namespace

std::filesystem::path user_data_dir(const std::string& app_name) {
    std::filesystem::path p = platform_base() / app_name;
    std::error_code ec;
    std::filesystem::create_directories(p, ec);
    // If creation failed we still return the resolved path; callers that try
    // to write into it will see the I/O error and report it. Suppressing here
    // keeps the helper side-effect-light.
    return p;
}

} // namespace pac::core
