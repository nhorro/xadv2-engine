#include "engine/pnc/cutscene.hpp"

#include "core/load_error_yaml.hpp"
#include "engine/pnc/data_error.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <utility>

namespace pac::pnc {

namespace {

constexpr const char* kSource = "cutscene-loader";

[[noreturn]] void cutscene_fail(const std::string& code,
                                const std::string& msg,
                                const YAML::Node& at = YAML::Node()) {
    pac::core::fail_at<DataError>(kSource, code, msg, at);
}

CutsceneAdvanceMode parse_mode(const YAML::Node& node) {
    const std::string s = node.as<std::string>();
    if (s == "auto") {
        return CutsceneAdvanceMode::Auto;
    }
    if (s == "manual") {
        return CutsceneAdvanceMode::Manual;
    }
    if (s == "timed") {
        return CutsceneAdvanceMode::Timed;
    }
    cutscene_fail("cutscene.advance-mode-unknown",
                  "advance_mode must be auto|manual|timed (got '" + s + "')",
                  node);
}

CutsceneTextAlign parse_text_align(const YAML::Node& node) {
    const std::string s = node.as<std::string>();
    if (s == "left") {
        return CutsceneTextAlign::Left;
    }
    if (s == "center") {
        return CutsceneTextAlign::Center;
    }
    if (s == "right") {
        return CutsceneTextAlign::Right;
    }
    cutscene_fail("cutscene.text-align-unknown",
                  "text_align must be left|center|right (got '" + s + "')",
                  node);
}

CutsceneImageFit parse_image_fit(const YAML::Node& node) {
    const std::string s = node.as<std::string>();
    if (s == "contain") {
        return CutsceneImageFit::Contain;
    }
    if (s == "stretch") {
        return CutsceneImageFit::Stretch;
    }
    cutscene_fail("cutscene.image-fit-unknown",
                  "image_fit must be contain|stretch (got '" + s + "')",
                  node);
}

// Read a 2-number sequence as a Vec2 (used for positions and sizes). Both
// numbers are required; the loader fails loudly on a shape mismatch so a typo
// surfaces at startup rather than as silently-zero coordinates.
sf::Vector2f parse_vec2(const YAML::Node& node, const std::string& field) {
    if (!node.IsSequence() || node.size() != 2) {
        cutscene_fail("cutscene." + field + "-shape",
                      "'" + field + "' must be a 2-number sequence",
                      node);
    }
    return {node[0].as<float>(), node[1].as<float>()};
}

// Hex color, accepting "#RRGGBB" and "#RRGGBBAA" (both case-insensitive). The
// `#` is optional. Falls back to a fail() on anything that doesn't parse to a
// 6 or 8 hex-digit string — text color is too important to be silently wrong.
sf::Color parse_color(const YAML::Node& node) {
    std::string s = node.as<std::string>();
    if (!s.empty() && s.front() == '#') {
        s.erase(s.begin());
    }
    if (s.size() != 6 && s.size() != 8) {
        cutscene_fail("cutscene.color-shape",
                      "color must be '#RRGGBB' or '#RRGGBBAA' (got '" + s + "')",
                      node);
    }
    for (char c : s) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) {
            cutscene_fail("cutscene.color-shape",
                          "color must be hex digits (got '" + s + "')",
                          node);
        }
    }
    auto hex = [&](std::size_t i) -> sf::Uint8 {
        return static_cast<sf::Uint8>(std::stoul(s.substr(i, 2), nullptr, 16));
    };
    sf::Color out;
    out.r = hex(0);
    out.g = hex(2);
    out.b = hex(4);
    out.a = s.size() == 8 ? hex(6) : 255;
    return out;
}

// Merge style fields from `node` on top of `base`. Each field is optional; an
// absent field keeps the base value, so author overrides compose neatly with
// the cutscene-level `defaults.text_style`.
void apply_text_style(const YAML::Node& node, CutsceneTextStyle& style) {
    if (!node || !node.IsMap()) {
        return;
    }
    if (node["font"]) {
        style.font = node["font"].as<std::string>();
    }
    if (node["size"]) {
        style.size = node["size"].as<unsigned>();
    }
    if (node["color"]) {
        style.color = parse_color(node["color"]);
    }
    if (node["outline_color"]) {
        style.outline_color = parse_color(node["outline_color"]);
    }
    if (node["outline_thickness"]) {
        style.outline_thickness = node["outline_thickness"].as<float>();
        if (style.outline_thickness < 0.0f) {
            cutscene_fail("cutscene.outline-thickness-negative",
                          "outline_thickness must be >= 0",
                          node["outline_thickness"]);
        }
    }
}

// Merge band fields from `node` on top of `band` (height/color both optional),
// so per-slide overrides compose with the cutscene-level `defaults.text_band`.
void apply_text_band(const YAML::Node& node, CutsceneTextBand& band) {
    if (!node) {
        return;
    }
    if (!node.IsMap()) {
        cutscene_fail("cutscene.text-band-shape", "'text_band' must be a mapping", node);
    }
    if (node["height"]) {
        band.height = node["height"].as<float>();
    }
    if (node["color"]) {
        band.color = parse_color(node["color"]);
    }
}

// Cutscene-level fade. Accepts a scalar (`fade: 0.5` -> in == out) or a mapping
// (`fade: { in:, out:, color: }`). Absent fields keep the no-fade default.
CutsceneFade parse_fade(const YAML::Node& node) {
    CutsceneFade fade;
    if (!node) {
        return fade;
    }
    if (node.IsScalar()) {
        fade.in = fade.out = node.as<float>();
        return fade;
    }
    if (!node.IsMap()) {
        cutscene_fail("cutscene.fade-shape", "'fade' must be a number or a mapping", node);
    }
    if (node["in"]) {
        fade.in = node["in"].as<float>();
    }
    if (node["out"]) {
        fade.out = node["out"].as<float>();
    }
    if (node["color"]) {
        fade.color = parse_color(node["color"]);
    }
    return fade;
}

struct Defaults {
    CutsceneTextStyle text_style;
    sf::Vector2f text_position{0.5f, 0.8f};
    CutsceneTextAlign text_align = CutsceneTextAlign::Center;
    sf::Vector2f image_position{0.5f, 0.5f};
    sf::Vector2f image_size{0.6f, 0.6f};
    CutsceneImageFit image_fit = CutsceneImageFit::Contain;
    CutsceneTextBand text_band;
    float duration = 3.0f;
};

Defaults parse_defaults(const YAML::Node& root) {
    Defaults d;
    const YAML::Node node = root["defaults"];
    if (!node) {
        return d;
    }
    if (!node.IsMap()) {
        cutscene_fail("cutscene.defaults-shape", "'defaults' must be a mapping", node);
    }
    apply_text_style(node["text_style"], d.text_style);
    if (node["text_position"]) {
        d.text_position = parse_vec2(node["text_position"], "text_position");
    }
    if (node["text_align"]) {
        d.text_align = parse_text_align(node["text_align"]);
    }
    if (node["image_position"]) {
        d.image_position = parse_vec2(node["image_position"], "image_position");
    }
    if (node["image_size"]) {
        d.image_size = parse_vec2(node["image_size"], "image_size");
    }
    if (node["image_fit"]) {
        d.image_fit = parse_image_fit(node["image_fit"]);
    }
    apply_text_band(node["text_band"], d.text_band);
    if (node["duration"]) {
        d.duration = node["duration"].as<float>();
    }
    return d;
}

CutsceneSlide parse_slide(const YAML::Node& node,
                          const Defaults& d,
                          CutsceneAdvanceMode mode,
                          std::size_t index) {
    if (!node.IsMap()) {
        cutscene_fail("cutscene.slide-shape",
                      "slide " + std::to_string(index) + " must be a mapping",
                      node);
    }

    CutsceneSlide slide;
    slide.text_position = d.text_position;
    slide.text_align = d.text_align;
    slide.text_style = d.text_style;
    slide.image_position = d.image_position;
    slide.image_size = d.image_size;
    slide.image_fit = d.image_fit;
    slide.text_band = d.text_band;

    if (node["text"]) {
        slide.text = node["text"].as<std::string>();
    }
    if (node["image"]) {
        slide.image = node["image"].as<std::string>();
    }
    if (node["text_position"]) {
        slide.text_position = parse_vec2(node["text_position"], "text_position");
    }
    if (node["text_align"]) {
        slide.text_align = parse_text_align(node["text_align"]);
    }
    apply_text_style(node["text_style"], slide.text_style);

    if (node["image_position"]) {
        slide.image_position = parse_vec2(node["image_position"], "image_position");
    }
    if (node["image_size"]) {
        slide.image_size = parse_vec2(node["image_size"], "image_size");
    }
    if (node["image_fit"]) {
        slide.image_fit = parse_image_fit(node["image_fit"]);
    }
    apply_text_band(node["text_band"], slide.text_band);

    if (node["duration"]) {
        slide.duration = node["duration"].as<float>();
    } else if (mode == CutsceneAdvanceMode::Auto) {
        // Bake the default in so the runtime never has to inspect the
        // cutscene-level value.
        slide.duration = d.duration;
    }
    if (node["at"]) {
        slide.at = node["at"].as<float>();
    }
    if (mode == CutsceneAdvanceMode::Timed && !slide.at) {
        cutscene_fail("cutscene.slide-at-missing",
                      "slide " + std::to_string(index) +
                          " is missing 'at' (required in timed mode)",
                      node);
    }
    return slide;
}

} // namespace

Cutscene parse_cutscene(const std::string& yaml_text) {
    YAML::Node root;
    try {
        root = YAML::Load(yaml_text);
    } catch (const YAML::Exception& e) {
        cutscene_fail("cutscene.invalid-yaml", std::string("invalid YAML: ") + e.what());
    }
    if (!root || !root.IsMap()) {
        cutscene_fail("cutscene.root-not-map", "root must be a mapping");
    }

    Cutscene out;
    out.version = root["version"] ? root["version"].as<int>() : 1;

    if (root["advance_mode"]) {
        out.mode = parse_mode(root["advance_mode"]);
    }
    if (root["background_color"]) {
        out.background_color = parse_color(root["background_color"]);
    }
    if (root["audio"]) {
        out.audio = root["audio"].as<std::string>();
    }
    if (root["audio_persist"]) {
        out.audio_persist = root["audio_persist"].as<bool>();
    }
    out.fade = parse_fade(root["fade"]);

    const Defaults d = parse_defaults(root);
    out.default_duration = d.duration;

    const YAML::Node slides = root["slides"];
    if (!slides || !slides.IsSequence() || slides.size() == 0) {
        cutscene_fail("cutscene.slides-missing",
                      "'slides' must be a non-empty sequence",
                      slides ? slides : root);
    }
    out.slides.reserve(slides.size());
    std::size_t i = 0;
    for (const YAML::Node& s : slides) {
        out.slides.push_back(parse_slide(s, d, out.mode, i++));
    }
    if (out.mode == CutsceneAdvanceMode::Timed) {
        // Sanity: timestamps must be monotonically non-decreasing so the
        // runtime can scan forward through the list. A typo here would cause
        // an earlier slide to never trigger, which is a hard-to-diagnose bug.
        for (std::size_t j = 1; j < out.slides.size(); ++j) {
            const float prev = *out.slides[j - 1].at;
            const float now = *out.slides[j].at;
            if (now < prev) {
                cutscene_fail("cutscene.timed-out-of-order",
                              "slide " + std::to_string(j) + " has 'at: " + std::to_string(now) +
                                  "' before slide " + std::to_string(j - 1) +
                                  "'s 'at: " + std::to_string(prev) + "'");
            }
        }
    }
    return out;
}

} // namespace pac::pnc
