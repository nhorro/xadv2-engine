#include "engine/pnc/speech_manager.hpp"

#include <doctest/doctest.h>

#include <string>

using namespace pac::pnc;

namespace {
// Fake measurer: width == character count, so max_width reads as chars-per-line.
float by_chars(const std::string& s) {
    return static_cast<float>(s.size());
}
} // namespace

TEST_CASE("wrap_text greedily breaks on word boundaries") {
    const auto lines = wrap_text("the quick brown fox", 9.0f, by_chars);
    REQUIRE(lines.size() == 2);
    CHECK(lines[0] == "the quick"); // 9 chars fits exactly
    CHECK(lines[1] == "brown fox");
}

TEST_CASE("wrap_text keeps a word longer than the limit on its own line") {
    const auto lines = wrap_text("hi supercalifragilistic ok", 8.0f, by_chars);
    REQUIRE(lines.size() == 3);
    CHECK(lines[0] == "hi");
    CHECK(lines[1] == "supercalifragilistic"); // unsplit, overflows the limit
    CHECK(lines[2] == "ok");
}

TEST_CASE("wrap_text honors explicit newlines and never returns empty") {
    const auto forced = wrap_text("a\nb", 100.0f, by_chars);
    REQUIRE(forced.size() == 2);
    CHECK(forced[0] == "a");
    CHECK(forced[1] == "b");

    const auto empty = wrap_text("", 100.0f, by_chars);
    REQUIRE(empty.size() == 1);
    CHECK(empty[0].empty());
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
