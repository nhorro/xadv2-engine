#include "engine/core/display.hpp"

#include <doctest/doctest.h>

#include <algorithm>

using namespace pac::core;

namespace {
bool has_size(const std::vector<sf::Vector2u>& v, unsigned x, unsigned y) {
    return std::find(v.begin(), v.end(), sf::Vector2u{x, y}) != v.end();
}
} // namespace

TEST_CASE("letterbox: exact 2x fit leaves no bars") {
    const Viewport vp = letterbox({2560, 1440}, {1280, 720});
    CHECK(vp.scale == doctest::Approx(2.0f));
    CHECK(vp.offset.x == doctest::Approx(0.0f));
    CHECK(vp.offset.y == doctest::Approx(0.0f));
    CHECK(vp.size.x == doctest::Approx(2560.0f));
    CHECK(vp.size.y == doctest::Approx(1440.0f));
}

TEST_CASE("letterbox: ultrawide window pillarboxes horizontally") {
    const Viewport vp = letterbox({3440, 1440}, {1280, 720});
    CHECK(vp.scale == doctest::Approx(2.0f)); // limited by height
    CHECK(vp.size.x == doctest::Approx(2560.0f));
    CHECK(vp.offset.x == doctest::Approx(440.0f));
    CHECK(vp.offset.y == doctest::Approx(0.0f));
}

TEST_CASE("letterbox: tall window letterboxes vertically") {
    const Viewport vp = letterbox({1280, 800}, {1280, 720});
    CHECK(vp.scale == doctest::Approx(1.0f)); // limited by width
    CHECK(vp.offset.x == doctest::Approx(0.0f));
    CHECK(vp.offset.y == doctest::Approx(40.0f));
}

TEST_CASE("letterbox: degenerate sizes do not divide by zero") {
    const Viewport vp = letterbox({0, 0}, {1280, 720});
    CHECK(vp.scale == doctest::Approx(1.0f));
}

TEST_CASE("window_to_virtual inverts the letterbox transform") {
    // Center of an ultrawide window maps to the center of virtual space.
    sf::Vector2f center = window_to_virtual({1720, 720}, {3440, 1440}, {1280, 720});
    CHECK(center.x == doctest::Approx(640.0f));
    CHECK(center.y == doctest::Approx(360.0f));

    // The top-left of the active (non-bar) region maps to the virtual origin.
    sf::Vector2f origin = window_to_virtual({440, 0}, {3440, 1440}, {1280, 720});
    CHECK(origin.x == doctest::Approx(0.0f));
    CHECK(origin.y == doctest::Approx(0.0f));
}

TEST_CASE("windowed_size_options returns aspect-matching multiples that fit") {
    // On a 4K desktop, 1x/1.5x/2x/3x of 1280x720 all fit.
    const auto opts = windowed_size_options({3840, 2160}, {1280, 720});
    CHECK(has_size(opts, 1280, 720));
    CHECK(has_size(opts, 1920, 1080));
    CHECK(has_size(opts, 2560, 1440));
    CHECK(has_size(opts, 3840, 2160));

    // A 1080p desktop drops the 2x/3x options.
    const auto small = windowed_size_options({1920, 1080}, {1280, 720});
    CHECK(has_size(small, 1280, 720));
    CHECK(has_size(small, 1920, 1080));
    CHECK_FALSE(has_size(small, 2560, 1440));

    // Tiny desktop still yields at least the native size (never empty).
    const auto tiny = windowed_size_options({640, 480}, {1280, 720});
    REQUIRE_FALSE(tiny.empty());
    CHECK(has_size(tiny, 1280, 720));

    // Zero desktop means "no limit" (headless): all multiples present.
    const auto headless = windowed_size_options({0, 0}, {1280, 720});
    CHECK(has_size(headless, 2560, 1440));
    CHECK(has_size(headless, 3840, 2160));
}

TEST_CASE("Display pending-mode request round-trips and tracks fullscreen") {
    Display d({1280, 720}, {1280, 720}, /*fullscreen=*/false);
    CHECK_FALSE(d.fullscreen());
    CHECK_FALSE(d.take_pending_mode().has_value()); // nothing pending initially

    d.request_mode({{1920, 1080}, true});
    const auto pending = d.take_pending_mode();
    REQUIRE(pending.has_value());
    CHECK(pending->size == sf::Vector2u{1920, 1080});
    CHECK(pending->fullscreen == true);
    CHECK_FALSE(d.take_pending_mode().has_value()); // consumed once

    // The main loop reports the applied mode back to Display.
    d.set_window_size({1920, 1080});
    d.set_fullscreen(true);
    CHECK(d.current_mode() == DisplayMode{{1920, 1080}, true});
}
