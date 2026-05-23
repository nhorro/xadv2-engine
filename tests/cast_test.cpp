#include "engine/pnc/cast.hpp"
#include "engine/pnc/data_error.hpp"
#include "loader_diag.hpp"

#include <doctest/doctest.h>

using namespace pac::pnc;
using pac::test::error_code;

namespace {

const char* kCast = R"YAML(
appearances:
  julia_body:
    type: animated_sprite
    sprite: characters/julia/body.anim.yml
characters:
  julia:
    appearance: julia_body
    name: "Julia"
    speech_color: { r: 255, g: 240, b: 180 }
)YAML";

} // namespace

TEST_CASE("parse_cast reads appearances and characters") {
    const Cast c = parse_cast(kCast);

    REQUIRE(c.appearance("julia_body") != nullptr);
    CHECK(c.appearance("julia_body")->type == "animated_sprite");
    CHECK(c.appearance("julia_body")->sprite == "characters/julia/body.anim.yml");
    CHECK(c.appearance("missing") == nullptr);

    REQUIRE(c.character("julia") != nullptr);
    CHECK(c.character("julia")->name == "Julia");
    CHECK(c.character("julia")->appearance == "julia_body");
    CHECK(static_cast<int>(c.character("julia")->speech_color.r) == 255);
    CHECK(static_cast<int>(c.character("julia")->speech_color.g) == 240);
    CHECK(c.character("nope") == nullptr);
}

TEST_CASE("parse_cast rejects a character without an appearance") {
    CHECK_THROWS_AS(parse_cast("characters:\n  x: { name: n }\n"), DataError);
    CHECK(error_code([] { parse_cast("characters:\n  x: { name: n }\n"); }) ==
          "cast.character-appearance-missing");
    CHECK(error_code([] { parse_cast("appearances:\n  a: { sprite: s }\n"); }) ==
          "cast.appearance-type-missing");
}
