#include "engine/core/user_data.hpp"

#include <cstdlib>
#include <system_error>

#if defined(__ANDROID__)
#include <SFML/System/NativeActivity.hpp>
#include <android/native_activity.h>
#endif

namespace pac::core {

namespace {

const char* env(const char* name) {
    const char* v = std::getenv(name);
    return (v && *v) ? v : nullptr;
}

std::filesystem::path platform_base() {
#if defined(__ANDROID__)
    if (const ANativeActivity* activity = sf::getNativeActivity();
        activity && activity->internalDataPath && *activity->internalDataPath) {
        return std::filesystem::path(activity->internalDataPath);
    }
#elif defined(_WIN32)
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

// Config base mirrors platform_base() but follows the per-platform *config*
// convention: on Linux that is $XDG_CONFIG_HOME / ~/.config, distinct from the
// data dir; Windows and macOS reuse the same base they use for data.
std::filesystem::path platform_config_base() {
#if defined(__ANDROID__)
    if (const ANativeActivity* activity = sf::getNativeActivity();
        activity && activity->internalDataPath && *activity->internalDataPath) {
        return std::filesystem::path(activity->internalDataPath);
    }
#elif defined(_WIN32)
    if (const char* appdata = env("APPDATA")) {
        return std::filesystem::path(appdata);
    }
#elif defined(__APPLE__)
    if (const char* home = env("HOME")) {
        return std::filesystem::path(home) / "Library" / "Application Support";
    }
#else // Linux + other Unix
    if (const char* xdg = env("XDG_CONFIG_HOME")) {
        return std::filesystem::path(xdg);
    }
    if (const char* home = env("HOME")) {
        return std::filesystem::path(home) / ".config";
    }
#endif
    return std::filesystem::path(".");
}

std::filesystem::path resolve_and_create(const std::filesystem::path& base,
                                         const std::string& app_name) {
    std::filesystem::path p = base / app_name;
    std::error_code ec;
    std::filesystem::create_directories(p, ec);
    // If creation failed we still return the resolved path; callers that try
    // to write into it will see the I/O error and report it. Suppressing here
    // keeps the helper side-effect-light.
    return p;
}

} // namespace

std::filesystem::path user_data_dir(const std::string& app_name) {
    return resolve_and_create(platform_base(), app_name);
}

std::filesystem::path save_data_dir(const std::string& app_name,
                                    const std::filesystem::path& executable_dir) {
    std::error_code ec;
    if (!executable_dir.empty() &&
        std::filesystem::is_regular_file(executable_dir / "portable.flag", ec)) {
        return executable_dir / "saves";
    }
    return user_data_dir(app_name) / "saves";
}

std::filesystem::path user_config_dir(const std::string& app_name) {
    return resolve_and_create(platform_config_base(), app_name);
}

} // namespace pac::core
