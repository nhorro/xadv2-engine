#include "engine/core/display.hpp"

#include <doctest/doctest.h>

using namespace pac::core;

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
