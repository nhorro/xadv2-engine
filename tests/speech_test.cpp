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
