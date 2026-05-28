#include "engine/core/diagnostics.hpp"
#include "engine/core/profiler.hpp"
#include "engine/core/resource_stats.hpp"

#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>

using namespace pac::core;

namespace {

std::filesystem::path unique_report_path() {
    const auto epoch = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("pac_profiling_" + std::to_string(epoch) + ".txt");
}

std::string read_file(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

} // namespace

TEST_CASE("profiler aggregates frame timing per scene") {
    Diagnostics log(LogLevel::ERROR); // keep periodic info lines out of test output
    ResourceStats sample;
    sample.texture_count = 5;
    sample.texture_bytes = 1000;
    sample.shader_count = 2;

    Profiler p(log, unique_report_path(), "testgame", [&] { return sample; }, /*interval=*/0.05);

    // 10 healthy frames (10 ms each, under the 20 ms slow threshold) trigger two
    // interval samples, then one slow frame (30 ms) that does not reach the next.
    for (int i = 0; i < 10; ++i) {
        p.frame(0.01, 0.004, "room");
    }
    p.frame(0.030, 0.010, "room");

    REQUIRE(p.scenes().count("room") == 1);
    const Profiler::SceneProfile& room = p.scenes().at("room");
    CHECK(room.frames == 11);
    CHECK(room.slow_frames == 1);
    CHECK(room.frame_ms_worst == doctest::Approx(30.0));
    // The interval samples copied the resource snapshot into the scene peak.
    CHECK(room.peak.texture_bytes == 1000);
    CHECK(room.peak.texture_count == 5);
    CHECK(room.peak.shader_count == 2);

    CHECK(p.total_frames() == 11);
    CHECK(p.total_seconds() == doctest::Approx(0.13));
}

TEST_CASE("profiler keeps scenes separate and writes a report") {
    Diagnostics log(LogLevel::ERROR);
    const auto report = unique_report_path();
    Profiler p(log, report, "testgame", [] { return ResourceStats{}; }, /*interval=*/0.1);

    for (int i = 0; i < 2; ++i) {
        p.frame(0.05, 0.004, "room");
    }
    for (int i = 0; i < 2; ++i) {
        p.frame(0.05, 0.004, "title");
    }
    p.finish();

    CHECK(p.scenes().count("room") == 1);
    CHECK(p.scenes().count("title") == 1);
    CHECK(p.total_frames() == 4);

    REQUIRE(std::filesystem::is_regular_file(report));
    const std::string text = read_file(report);
    CHECK(text.find("profiling report") != std::string::npos);
    CHECK(text.find("testgame") != std::string::npos);
    CHECK(text.find("room") != std::string::npos);
    CHECK(text.find("title") != std::string::npos);

    // finish() is idempotent: a second call must not throw or duplicate work.
    p.finish();

    std::error_code ec;
    std::filesystem::remove(report, ec);
}

TEST_CASE("process_rss_bytes is non-negative and plausible on Linux") {
    const std::size_t rss = process_rss_bytes();
#if defined(__linux__)
    CHECK(rss > 0);
#else
    CHECK(rss >= 0);
#endif
}
