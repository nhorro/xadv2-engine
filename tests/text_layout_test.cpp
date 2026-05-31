#include "engine/core/text_layout.hpp"

#include <doctest/doctest.h>

#include <string>

using namespace pac::core;

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

TEST_CASE("layout_text reports block bounds for a wrapped paragraph") {
    // Wraps to two 9-char lines; block is 9 wide and 2 lines * 10 high.
    const TextLayout lay = layout_text("the quick brown fox", 9.0f, 10.0f, by_chars);
    REQUIRE(lay.lines.size() == 2);
    REQUIRE(lay.line_widths.size() == 2);
    CHECK(lay.line_widths[0] == doctest::Approx(9.0f));
    CHECK(lay.line_widths[1] == doctest::Approx(9.0f));
    CHECK(lay.width == doctest::Approx(9.0f));   // widest line
    CHECK(lay.height == doctest::Approx(20.0f)); // line_height * lines
}

TEST_CASE("layout_text block width is the widest line, not the last") {
    // Uneven lines via an explicit break: "aaa" (3) then "bb" (2).
    const TextLayout lay = layout_text("aaa\nbb", 100.0f, 5.0f, by_chars);
    REQUIRE(lay.lines.size() == 2);
    CHECK(lay.line_widths[0] == doctest::Approx(3.0f));
    CHECK(lay.line_widths[1] == doctest::Approx(2.0f));
    CHECK(lay.width == doctest::Approx(3.0f));   // max(3, 2)
    CHECK(lay.height == doctest::Approx(10.0f)); // 2 lines * 5
}

TEST_CASE("layout_text on empty text is one zero-width line of one line height") {
    const TextLayout lay = layout_text("", 100.0f, 7.0f, by_chars);
    REQUIRE(lay.lines.size() == 1);
    CHECK(lay.lines[0].empty());
    CHECK(lay.width == doctest::Approx(0.0f));
    CHECK(lay.height == doctest::Approx(7.0f));
}
