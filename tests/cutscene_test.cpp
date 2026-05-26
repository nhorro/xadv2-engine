// Cutscene YAML parser (issue #116). Headless: the slide-rendering runtime
// needs a render target and is exercised by the sample; this covers the data
// path — modes, defaults composition, per-slide overrides, and the error codes
// the loader emits for malformed input.

#include "engine/pnc/cutscene.hpp"
#include "engine/pnc/data_error.hpp"
#include "loader_diag.hpp"

#include <doctest/doctest.h>

#include <string>

using namespace pac::pnc;
using pac::test::error_code;

TEST_CASE("parse_cutscene reads modes + audio + slides") {
    const Cutscene c = parse_cutscene(R"YAML(
advance_mode: manual

defaults:
  text_style: { size: 28, color: "#E0E0E0" }
  text_position: [0.5, 0.85]

slides:
  - text: "First page."
  - text: "Second page."
    text_style: { color: "#FFEEBB" }
)YAML");

    CHECK(c.mode == CutsceneAdvanceMode::Manual);
    CHECK(c.audio.empty());
    REQUIRE(c.slides.size() == 2);

    // Defaults flowed into slide 0.
    CHECK(c.slides[0].text == std::optional<std::string>("First page."));
    CHECK(c.slides[0].text_position.x == doctest::Approx(0.5f));
    CHECK(c.slides[0].text_position.y == doctest::Approx(0.85f));
    CHECK(c.slides[0].text_style.size == 28u);
    CHECK(c.slides[0].text_style.color.r == 0xE0);

    // Slide 1 keeps the default size but overrides the color.
    CHECK(c.slides[1].text_style.size == 28u);
    CHECK(c.slides[1].text_style.color.r == 0xFF);
    CHECK(c.slides[1].text_style.color.g == 0xEE);
    CHECK(c.slides[1].text_style.color.b == 0xBB);
}

TEST_CASE("auto mode bakes default_duration into each slide") {
    const Cutscene c = parse_cutscene(R"YAML(
advance_mode: auto

defaults:
  duration: 2.5

slides:
  - text: "A"
  - text: "B"
    duration: 4.0
)YAML");

    CHECK(c.mode == CutsceneAdvanceMode::Auto);
    CHECK(c.default_duration == doctest::Approx(2.5f));
    REQUIRE(c.slides[0].duration);
    CHECK(*c.slides[0].duration == doctest::Approx(2.5f));
    REQUIRE(c.slides[1].duration);
    CHECK(*c.slides[1].duration == doctest::Approx(4.0f));
}

TEST_CASE("timed mode requires `at` on every slide") {
    const Cutscene c = parse_cutscene(R"YAML(
advance_mode: timed
audio: assets/audio/intro.mp3

slides:
  - at: 0.0
    text: "Opening"
  - at: 4.5
    image: assets/cutscenes/island.png
    text: "Continuation"
)YAML");

    CHECK(c.mode == CutsceneAdvanceMode::Timed);
    CHECK(c.audio == "assets/audio/intro.mp3");
    REQUIRE(c.slides.size() == 2);
    CHECK(*c.slides[0].at == doctest::Approx(0.0f));
    CHECK(*c.slides[1].at == doctest::Approx(4.5f));
    CHECK(c.slides[1].image == std::optional<std::string>("assets/cutscenes/island.png"));

    // A timed slide without `at` is a fatal authoring error.
    CHECK(error_code([] {
              parse_cutscene("advance_mode: timed\nslides:\n  - text: \"oops\"\n");
          }) == "cutscene.slide-at-missing");

    // Out-of-order `at` is rejected too (would silently skip an earlier slide).
    CHECK(error_code([] {
              parse_cutscene("advance_mode: timed\nslides:\n  - at: 1.0\n    text: A\n  - at: "
                             "0.5\n    text: B\n");
          }) == "cutscene.timed-out-of-order");
}

TEST_CASE("defaults fall back to engine values when omitted") {
    const Cutscene c = parse_cutscene(R"YAML(
slides:
  - text: "Just text."
)YAML");

    CHECK(c.mode == CutsceneAdvanceMode::Auto); // auto is the default
    CHECK(c.default_duration == doctest::Approx(3.0f));
    REQUIRE(c.slides.size() == 1);
    CHECK(c.slides[0].text_align == CutsceneTextAlign::Center);
    CHECK(c.slides[0].image_fit == CutsceneImageFit::Contain);
    CHECK(c.slides[0].text_style.color.r == 0xFF); // white
}

TEST_CASE("colors accept #RRGGBB and #RRGGBBAA") {
    const Cutscene c = parse_cutscene(R"YAML(
defaults:
  text_style: { color: "#11223344" }
slides:
  - text: "x"
    text_style: { color: "#AABBCC" }
)YAML");
    CHECK(c.slides[0].text_style.color.r == 0xAA);
    CHECK(c.slides[0].text_style.color.g == 0xBB);
    CHECK(c.slides[0].text_style.color.b == 0xCC);
    CHECK(c.slides[0].text_style.color.a == 0xFF); // 6-digit form is opaque

    // The `defaults` value's alpha is preserved when no per-slide override
    // touches the color.
    const Cutscene c2 = parse_cutscene(R"YAML(
defaults:
  text_style: { color: "#11223344" }
slides:
  - text: "y"
)YAML");
    CHECK(c2.slides[0].text_style.color.a == 0x44);
}

TEST_CASE("malformed inputs carry stable error codes") {
    CHECK(error_code([] { parse_cutscene("not a map"); }) == "cutscene.root-not-map");
    CHECK(error_code([] { parse_cutscene("advance_mode: dance\nslides: [{text: x}]\n"); }) ==
          "cutscene.advance-mode-unknown");
    CHECK(error_code([] { parse_cutscene("slides: []\n"); }) == "cutscene.slides-missing");
    CHECK(error_code([] { parse_cutscene("slides:\n  - text_position: [0.5]\n"); }) ==
          "cutscene.text_position-shape");
    CHECK(error_code([] { parse_cutscene("slides:\n  - text_style: { color: 'green' }\n"); }) ==
          "cutscene.color-shape");
    CHECK(error_code([] { parse_cutscene("slides:\n  - image_fit: cover\n"); }) ==
          "cutscene.image-fit-unknown");
}
