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
background_color: "#12345678"

defaults:
  text_style: { size: 28, color: "#E0E0E0" }
  text_position: [0.5, 0.85]

slides:
  - text: "First page."
  - text: "Second page."
    text_style: { color: "#FFEEBB" }
)YAML");

    CHECK(c.mode == CutsceneAdvanceMode::Manual);
    CHECK(c.background_color == sf::Color(0x12, 0x34, 0x56, 0x78));
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
audio_delay: 3.25

slides:
  - at: 0.0
    text: "Opening"
  - at: 4.5
    image: assets/cutscenes/island.png
    text: "Continuation"
)YAML");

    CHECK(c.mode == CutsceneAdvanceMode::Timed);
    CHECK(c.audio == "assets/audio/intro.mp3");
    CHECK(c.audio_delay == doctest::Approx(3.25f));
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

    CHECK(error_code([] { parse_cutscene("audio_delay: -0.1\nslides:\n  - text: A\n"); }) ==
          "cutscene.audio-delay-negative");
}

TEST_CASE("defaults fall back to engine values when omitted") {
    const Cutscene c = parse_cutscene(R"YAML(
slides:
  - text: "Just text."
)YAML");

    CHECK(c.mode == CutsceneAdvanceMode::Auto); // auto is the default
    CHECK(c.background_color == sf::Color::Black);
    CHECK(c.audio_delay == doctest::Approx(0.0f));
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
    CHECK(error_code([] { parse_cutscene("slides:\n  - image_fit: crop\n"); }) ==
          "cutscene.image-fit-unknown");
    CHECK(error_code([] { parse_cutscene("fade: [1, 2]\nslides: [{text: x}]\n"); }) ==
          "cutscene.fade-shape");
    CHECK(error_code([] { parse_cutscene("slides:\n  - text_band: [1]\n"); }) ==
          "cutscene.text-band-shape");
    CHECK(error_code([] {
              parse_cutscene("slides:\n  - text_style: { outline_thickness: -1 }\n");
          }) == "cutscene.outline-thickness-negative");
    CHECK(error_code([] { parse_cutscene("defaults: {text_width: 0}\nslides: [{text: x}]\n"); }) ==
          "cutscene.text-width-non-positive");
    CHECK(error_code([] { parse_cutscene("backdrop: []\nslides: [{text: x}]\n"); }) ==
          "cutscene.backdrop-shape");
    CHECK(error_code([] { parse_cutscene("timed_crossfade: -0.1\nslides: [{text: x}]\n"); }) ==
          "cutscene.timed-crossfade-negative");
}

TEST_CASE("musical backdrop, timed crossfade, and narrow text blocks parse") {
    const Cutscene c = parse_cutscene(R"YAML(
advance_mode: timed
timed_crossfade: 0.4
show_skip_hint: true
backdrop:
  image: rooms/plaza.png
  position: [0.5, 0.5]
  size: [1.0, 1.0]
  fit: cover
  tint: "#DDE2E8"
  shader:
    source: shaders/color_grade.frag
    params: {saturation: 0.25, strength: 0.9}
  motion:
    from: [0.5, 0.5]
    to: [0.44, 0.48]
    scale_from: 1.0
    scale_to: 1.05
    duration: 144
  pulse: {period: 5.6, strength: 0.025}
  reveal: {at: 4.2, duration: 2.0}
  sway: {period: 19, offset: [0.004, 0.0015], scale: 0.0025}
defaults:
  text_width: 0.34
slides:
  - {at: 0, text: A}
  - {at: 2, text: B, text_width: 0.28}
)YAML");

    CHECK(c.timed_crossfade == doctest::Approx(0.4f));
    CHECK(c.show_skip_hint);
    REQUIRE(c.backdrop);
    CHECK(c.backdrop->image == "rooms/plaza.png");
    CHECK(c.backdrop->fit == CutsceneImageFit::Cover);
    REQUIRE(c.backdrop->shaders.size() == 1);
    CHECK(c.backdrop->shaders.front().source == "shaders/color_grade.frag");
    CHECK(c.backdrop->shaders.front().params.size() == 2);
    CHECK(c.backdrop->motion_to.x == doctest::Approx(0.44f));
    CHECK(c.backdrop->scale_to == doctest::Approx(1.05f));
    CHECK(c.backdrop->motion_duration == doctest::Approx(144.0f));
    CHECK(c.backdrop->pulse_period == doctest::Approx(5.6f));
    CHECK(c.backdrop->pulse_strength == doctest::Approx(0.025f));
    CHECK(c.backdrop->reveal_at == doctest::Approx(4.2f));
    CHECK(c.backdrop->reveal_duration == doctest::Approx(2.0f));
    CHECK(c.backdrop->sway_period == doctest::Approx(19.0f));
    CHECK(c.backdrop->sway_offset.x == doctest::Approx(0.004f));
    CHECK(c.backdrop->sway_scale == doctest::Approx(0.0025f));
    CHECK(c.slides[0].text_width == doctest::Approx(0.34f));
    CHECK(c.slides[1].text_width == doctest::Approx(0.28f));
}

TEST_CASE("fade, audio_persist, text_band, and text outline parse + compose") {
    const Cutscene c = parse_cutscene(R"YAML(
advance_mode: manual
audio: music/intro.mp3
audio_persist: true
fade: { in: 0.4, out: 0.6 }

defaults:
  text_band: { color: "#0A0A0CF0", height: 0.25 }
  text_style: { outline_color: "#001122", outline_thickness: 2 }

slides:
  - text: "A"
  - text: "B"
    text_band: { height: 0.4 }
    text_style: { outline_thickness: 3 }
)YAML");

    CHECK(c.audio == "music/intro.mp3");
    CHECK(c.audio_persist == true);
    CHECK(c.fade.in == doctest::Approx(0.4f));
    CHECK(c.fade.out == doctest::Approx(0.6f));

    // text_band: defaults flow into slide 0; slide 1 overrides height, keeps color.
    CHECK(c.slides[0].text_band.height == doctest::Approx(0.25f));
    CHECK(c.slides[0].text_band.color.r == 0x0A);
    CHECK(c.slides[0].text_band.color.a == 0xF0);
    CHECK(c.slides[1].text_band.height == doctest::Approx(0.4f));
    CHECK(c.slides[1].text_band.color.a == 0xF0); // inherited from defaults

    // text outline composes like the rest of text_style.
    CHECK(c.slides[0].text_style.outline_thickness == doctest::Approx(2.0f));
    CHECK(c.slides[0].text_style.outline_color.r == 0x00);
    CHECK(c.slides[0].text_style.outline_color.b == 0x22);
    CHECK(c.slides[1].text_style.outline_thickness == doctest::Approx(3.0f));
}

TEST_CASE("scalar fade sets both halves; the new fields default to off") {
    const Cutscene c = parse_cutscene("fade: 0.5\nslides:\n  - text: x\n");
    CHECK(c.fade.in == doctest::Approx(0.5f));
    CHECK(c.fade.out == doctest::Approx(0.5f));

    const Cutscene d = parse_cutscene("slides:\n  - text: x\n");
    CHECK(d.fade.in == doctest::Approx(0.0f)); // 0/0 = hard cuts
    CHECK(d.fade.out == doctest::Approx(0.0f));
    CHECK(d.audio_persist == false);
    CHECK(d.slides[0].text_band.height == doctest::Approx(0.0f)); // no band
    CHECK(d.slides[0].text_style.outline_thickness == doctest::Approx(0.0f));
}
