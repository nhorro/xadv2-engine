#include "engine/core/user_data.hpp"

#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

TEST_CASE("user_data_dir resolves and creates a per-app subdirectory") {
    // Unique app name to avoid polluting a real game's save dir; cleaned up
    // before the test returns.
    const auto epoch = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::string app = "pac_test_" + std::to_string(epoch);

    const auto p = pac::core::user_data_dir(app);
    CHECK(p.filename() == app);
    CHECK(std::filesystem::is_directory(p));

    std::error_code ec;
    std::filesystem::remove_all(p, ec);
    CHECK_FALSE(ec);
}

TEST_CASE("user_config_dir resolves and creates a per-app subdirectory") {
    const auto epoch = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::string app = "pac_cfg_test_" + std::to_string(epoch);

    const auto p = pac::core::user_config_dir(app);
    CHECK(p.filename() == app);
    CHECK(std::filesystem::is_directory(p));

    std::error_code ec;
    std::filesystem::remove_all(p, ec);
    CHECK_FALSE(ec);
}

TEST_CASE("portable marker keeps saves beside the executable") {
    const auto epoch = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto portable = std::filesystem::temp_directory_path() /
                          ("pac_portable_test_" + std::to_string(epoch));
    std::filesystem::create_directories(portable);
    {
        std::ofstream marker(portable / "portable.flag");
        marker << "portable\n";
    }

    CHECK(pac::core::save_data_dir("ignored-app-id", portable) == portable / "saves");

    std::error_code ec;
    std::filesystem::remove_all(portable, ec);
    CHECK_FALSE(ec);
}
