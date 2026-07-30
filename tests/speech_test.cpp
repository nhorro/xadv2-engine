#include "engine/pnc/speech_manager.hpp"

#include <doctest/doctest.h>

#include <string>

using namespace pac::pnc;

// wrap_text / layout_text now live in pac::core and are covered by
// text_layout_test.cpp; this file keeps the speech-balloon geometry tests.

TEST_CASE("speech font size defaults to 24 and is configurable") {
    SpeechManager speech;
    CHECK(speech.font_size() == 24u);
    speech.set_font_size(32);
    CHECK(speech.font_size() == 32u);
}

TEST_CASE("contain_block centers on the anchor when there is room") {
    // 100x40 block, anchor at the middle of a 1000x600 view -> centered top-left.
    const sf::FloatRect bounds(0.0f, 0.0f, 1000.0f, 600.0f);
    const pac::geom::Point tl = contain_block({500.0f, 300.0f}, {100.0f, 40.0f}, bounds, 10.0f);
    CHECK(tl.x == doctest::Approx(450.0f));
    CHECK(tl.y == doctest::Approx(280.0f));
}

TEST_CASE("contain_block clamps to the margin near the edges") {
    const sf::FloatRect bounds(0.0f, 0.0f, 1000.0f, 600.0f);
    // Anchor hard against the left/top: block pinned to the margin.
    const pac::geom::Point tl_lo = contain_block({0.0f, 0.0f}, {100.0f, 40.0f}, bounds, 10.0f);
    CHECK(tl_lo.x == doctest::Approx(10.0f));
    CHECK(tl_lo.y == doctest::Approx(10.0f));
    // Anchor hard against the right/bottom: block's far edge sits at bounds-margin.
    const pac::geom::Point tl_hi = contain_block({1000.0f, 600.0f}, {100.0f, 40.0f}, bounds, 10.0f);
    CHECK(tl_hi.x == doctest::Approx(890.0f)); // 1000 - 10 - 100
    CHECK(tl_hi.y == doctest::Approx(550.0f)); // 600 - 10 - 40
}

TEST_CASE("contain_block respects a non-zero view origin (scrolled room)") {
    // A view scrolled to world x in [2000, 3000]: clamping uses that origin.
    const sf::FloatRect bounds(2000.0f, 0.0f, 1000.0f, 600.0f);
    const pac::geom::Point tl = contain_block({2000.0f, 300.0f}, {100.0f, 40.0f}, bounds, 10.0f);
    CHECK(tl.x == doctest::Approx(2010.0f)); // pinned to the left margin in world space
}

TEST_CASE("contain_block pins to the low edge when the block is too big") {
    const sf::FloatRect bounds(0.0f, 0.0f, 80.0f, 600.0f);
    // Block (100) wider than the available span (80 - 2*10) -> pinned to left margin.
    const pac::geom::Point tl = contain_block({40.0f, 300.0f}, {100.0f, 40.0f}, bounds, 10.0f);
    CHECK(tl.x == doctest::Approx(10.0f));
}

TEST_CASE("place_speech floats the balloon above the head when there is room") {
    const sf::FloatRect bounds(0.0f, 0.0f, 1000.0f, 600.0f);
    // 100x40 block, 48 side gap, 14 tail. Head anchor mid-screen -> centered in x,
    // bottom edge `tail` above the anchor (top = 300 - 14 - 40).
    const pac::geom::Point tl =
        place_speech({500.0f, 300.0f}, {100.0f, 40.0f}, bounds, 10.0f, 48.0f, 14.0f);
    CHECK(tl.x == doctest::Approx(450.0f));
    CHECK(tl.y == doctest::Approx(246.0f)); // 300 - 14 - 40
}

TEST_CASE("place_speech stays above the head but nudges off a side edge") {
    const sf::FloatRect bounds(0.0f, 0.0f, 1000.0f, 600.0f);
    // Speaker hugging the left edge with room above: float above, clamp x only.
    const pac::geom::Point tl =
        place_speech({0.0f, 300.0f}, {100.0f, 40.0f}, bounds, 10.0f, 48.0f, 14.0f);
    CHECK(tl.x == doctest::Approx(10.0f));
    CHECK(tl.y == doctest::Approx(246.0f)); // still above the head
}

TEST_CASE("place_speech moves beside the head when there is no room above") {
    const sf::FloatRect bounds(0.0f, 0.0f, 1000.0f, 600.0f);
    // Head near the top: a floating-above block would clip, so go to the right of
    // the head, side_gap past the anchor x, vertically centered on the head.
    const pac::geom::Point tl =
        place_speech({500.0f, 20.0f}, {100.0f, 40.0f}, bounds, 10.0f, 48.0f, 14.0f);
    CHECK(tl.x == doctest::Approx(548.0f)); // 500 + side_gap
    CHECK(tl.y == doctest::Approx(10.0f));  // centered on head (20-20=0) clamped to top margin
}

TEST_CASE("place_speech falls back to the left when the right side won't fit") {
    const sf::FloatRect bounds(0.0f, 0.0f, 1000.0f, 600.0f);
    // No room above AND close to the right edge: right placement (1028) overruns
    // hi_x (890), so it falls to the left of the head.
    const pac::geom::Point tl =
        place_speech({980.0f, 20.0f}, {100.0f, 40.0f}, bounds, 10.0f, 48.0f, 14.0f);
    CHECK(tl.x == doctest::Approx(832.0f)); // 980 - side_gap - width
    CHECK(tl.y == doctest::Approx(10.0f));
}
