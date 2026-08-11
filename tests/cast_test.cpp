#include "engine/pnc/cast.hpp"
#include "engine/pnc/data_error.hpp"
#include "loader_diag.hpp"

#include <doctest/doctest.h>

#include <array>
#include <variant>

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

TEST_CASE("parse_cast reads an optional shadow appearance") {
    const char* yaml = R"YAML(
appearances:
  with_shadow:
    type: animated_sprite
    sprite: a.anim.yml
    shadow:
      size: { x: 70, y: 18 }
      offset: { x: 2, y: -6 }
      color: { r: 10, g: 20, b: 30, a: 90 }
  no_shadow:
    type: animated_sprite
    sprite: b.anim.yml
)YAML";
    const Cast c = parse_cast(yaml);

    REQUIRE(c.appearance("with_shadow") != nullptr);
    REQUIRE(c.appearance("with_shadow")->shadow.has_value());
    const Shadow& sh = *c.appearance("with_shadow")->shadow;
    CHECK(sh.size.x == doctest::Approx(70.0f));
    CHECK(sh.size.y == doctest::Approx(18.0f));
    CHECK(sh.offset.x == doctest::Approx(2.0f));
    CHECK(sh.offset.y == doctest::Approx(-6.0f));
    CHECK(static_cast<int>(sh.color.a) == 90);

    REQUIRE(c.appearance("no_shadow") != nullptr);
    CHECK_FALSE(c.appearance("no_shadow")->shadow.has_value());
}

TEST_CASE("parse_cast rejects a shadow without a size") {
    CHECK(error_code([] {
              parse_cast("appearances:\n  a: { type: animated_sprite, sprite: s, shadow: { color: "
                         "{ r: 0, g: 0, b: 0 } } }\n");
          }) == "cast.shadow-size-missing");
}

TEST_CASE("parse_cast rejects a malformed shadow offset") {
    CHECK(error_code([] {
              parse_cast("appearances:\n  a: { type: animated_sprite, sprite: s, shadow: { size: "
                         "{ x: 10, y: 4 }, offset: { y: -2 } } }\n");
          }) == "cast.shadow-offset-shape");
}

TEST_CASE("parse_cast reads an appearance shader stack (issue #106)") {
    const char* yaml = R"YAML(
appearances:
  hero:
    type: animated_sprite
    sprite: characters/hero/hero.anim.yml
    shaders:
      - source: shaders/color_grade.frag
        params:
          tint: [1.0, 0.95, 0.85]
          strength: 0.4
      - shaders/vignette.frag
)YAML";
    const Cast c = parse_cast(yaml);

    REQUIRE(c.appearance("hero") != nullptr);
    REQUIRE(c.appearance("hero")->shaders.size() == 2);
    CHECK(c.appearance("hero")->shaders[0].source == "shaders/color_grade.frag");
    const auto& tint =
        std::get<std::array<float, 3>>(c.appearance("hero")->shaders[0].params[0].value);
    CHECK(tint[0] == doctest::Approx(1.0f));
    CHECK(c.appearance("hero")->shaders[1].source == "shaders/vignette.frag");
}

TEST_CASE("parse_cast surfaces shader parse errors with the cast prefix") {
    CHECK(error_code([] {
              parse_cast("appearances:\n  a: { type: animated_sprite, sprite: s, "
                         "shader: { params: { x: 1 } } }\n");
          }) == "cast.shader-source-missing");
}

TEST_CASE("parse_cast rejects a character without an appearance") {
    CHECK_THROWS_AS(parse_cast("characters:\n  x: { name: n }\n"), DataError);
    CHECK(error_code([] { parse_cast("characters:\n  x: { name: n }\n"); }) ==
          "cast.character-appearance-missing");
    CHECK(error_code([] { parse_cast("appearances:\n  a: { sprite: s }\n"); }) ==
          "cast.appearance-type-missing");
}
