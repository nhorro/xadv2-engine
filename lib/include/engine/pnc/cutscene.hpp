#pragma once

#include "engine/gfx/shader_effect.hpp"

#include <SFML/Graphics/Color.hpp>
#include <SFML/System/Vector2.hpp>

#include <optional>
#include <string>
#include <vector>

namespace pac::pnc {

/// How a `Cutscene` (issue #116) advances between slides at runtime.
///   - Auto:   the scene moves to the next slide after `duration` seconds (or the
///             cutscene's `default_duration` when the slide doesn't override). The
///             old `StoryText` cutscene maps here.
///   - Manual: the player presses `Enter` / `Space` / left-click to advance; `Esc`
///             skips the whole cutscene. A localized continue/skip hint is shown
///             at the bottom-right.
///   - Timed:  slides become active at their `at` timestamp (seconds since the
///             scene started). When `audio` is set, the music playback offset
///             drives the clock so the slides stay in sync with the narration
///             even when the engine stutters; otherwise the wall-clock is used.
enum class CutsceneAdvanceMode {
    Auto,
    Manual,
    Timed,
};

/// Horizontal alignment of a slide's text block relative to its `text_position`
/// anchor. Vertical alignment is always center-on-anchor (predictable and
/// position is normalized so the author can place the block precisely).
enum class CutsceneTextAlign {
    Left,
    Center,
    Right,
};

/// How the image is fitted into the slide's `image_size` box.
///   - Contain: preserve aspect; the image is scaled so it fits entirely inside
///              the box and centered within it (any remaining area shows the
///              cutscene background color).
///   - Cover:   preserve aspect; the image is scaled until it fills the box and
///              centered (excess at the sides or top/bottom is clipped).
///   - Stretch: scale to exactly fill the box, regardless of aspect.
enum class CutsceneImageFit {
    Contain,
    Cover,
    Stretch,
};

/// Author-facing text styling. Empty `font` means "use the scene's default
/// font" (the `font` parameter on the `Cutscene` manifest entry).
struct CutsceneTextStyle {
    std::string font;   // optional logical path; empty = default
    unsigned size = 30; // virtual pixels
    sf::Color color{255, 255, 255, 255};
    sf::Color outline_color{0, 0, 0, 255};
    float outline_thickness = 0.0f; // virtual px; 0 = no outline
};

/// Optional filled band drawn behind a slide's text — a "lower third" scrim that
/// keeps narration readable over a full-bleed image. Full width, anchored to the
/// bottom edge. `height` is a fraction of the screen height; <= 0 disables it.
/// The color's alpha sets opacity (0xFF = solid band, e.g. 0xE6 = translucent).
struct CutsceneTextBand {
    sf::Color color{0, 0, 0, 255};
    float height = 0.0f; // fraction of screen height; 0 = no band
};

/// Dip-to-black fade timing. `in` fades a slide up from `color` as it appears;
/// `out` fades it down to `color` before the next slide (or the cutscene's end).
/// 0 on either half is a hard cut. Applies to `auto`/`manual`; timed scenes use
/// the separate audio-clock-driven `timed_crossfade` setting.
struct CutsceneFade {
    float in = 0.0f;  // seconds
    float out = 0.0f; // seconds
    sf::Color color{0, 0, 0, 255};
};

/// One slide in a `Cutscene`. Fields are evaluated against the cutscene's
/// `defaults` block first, then per-slide overrides. `text` and `image` are
/// independent — a slide may have one, both, or neither (a blank pause).
struct CutsceneSlide {
    std::optional<std::string> text;
    std::optional<std::string> image; // logical path

    sf::Vector2f text_position{0.5f, 0.8f}; // normalized; (0.5, 0.8) = lower-center
    float text_width = 0.86f;               // normalized maximum line width
    CutsceneTextAlign text_align = CutsceneTextAlign::Center;
    CutsceneTextStyle text_style;

    sf::Vector2f image_position{0.5f, 0.5f}; // normalized anchor (image is centered on it)
    sf::Vector2f image_size{0.6f, 0.6f};     // normalized maximum extent in screen space
    CutsceneImageFit image_fit = CutsceneImageFit::Contain;

    CutsceneTextBand text_band; // optional narration band (height 0 = none)

    std::optional<float> duration; // Auto mode; falls back to the cutscene default
    std::optional<float> at;       // Timed mode; required there
};

/// A persistent image underneath every slide. Its optional motion and luminance
/// pulse are evaluated from the same clock as timed slides, keeping a musical
/// presentation synchronized even when frame delivery stutters.
struct CutsceneBackdrop {
    std::string image;
    sf::Vector2f position{0.5f, 0.5f};
    sf::Vector2f size{1.0f, 1.0f};
    CutsceneImageFit fit = CutsceneImageFit::Contain;
    sf::Color tint{255, 255, 255, 255};
    std::vector<gfx::ShaderEffect> shaders;

    sf::Vector2f motion_from{0.5f, 0.5f};
    sf::Vector2f motion_to{0.5f, 0.5f};
    float scale_from = 1.0f;
    float scale_to = 1.0f;
    float motion_duration = 0.0f;

    float reveal_at = 0.0f;
    float reveal_duration = 0.0f;

    float pulse_period = 0.0f;
    float pulse_strength = 0.0f;

    float sway_period = 0.0f;
    sf::Vector2f sway_offset{0.0f, 0.0f};
    float sway_scale = 0.0f;
};

/// A transparent image drawn over the persistent backdrop and under slide
/// images/text. Its transform and alpha window use the cutscene cue clock, so
/// foreground movement stays synchronized with timed audio.
struct CutsceneForeground {
    std::string image;
    sf::Vector2f from{0.5f, 0.5f};
    sf::Vector2f to{0.5f, 0.5f};
    sf::Vector2f size{0.5f, 0.5f};
    CutsceneImageFit fit = CutsceneImageFit::Contain;
    sf::Color tint{255, 255, 255, 255};

    float at = 0.0f;
    std::optional<float> duration; // omitted = remain visible after `at`
    float fade_in = 0.0f;
    float fade_out = 0.0f;

    float sway_period = 0.0f;
    sf::Vector2f sway_offset{0.0f, 0.0f};
    float sway_scale = 0.0f;
};

/// Parsed `cutscenes/<id>.yaml`. Headless and testable; runtime lives in
/// `CutsceneScene`.
struct Cutscene {
    int version = 1;
    CutsceneAdvanceMode mode = CutsceneAdvanceMode::Auto;
    sf::Color background_color{0, 0, 0, 255}; // full-screen fill behind every slide
    std::string audio;          // logical path; Timed: narration sync, auto/manual: background
    float audio_delay = 0.0f;   // seconds of silent scene pre-roll before starting `audio`
    bool audio_persist = false; // keep `audio` playing past the cutscene (next scene stops it)
    CutsceneFade fade;          // dip-to-black between slides (auto/manual); 0/0 = hard cuts
    std::optional<CutsceneBackdrop> backdrop;    // persistent image + subtle musical motion
    std::vector<CutsceneForeground> foregrounds; // ordered timed layers over the backdrop
    float timed_crossfade = 0.0f;                // seconds; audio-clock-driven slide blend
    bool show_skip_hint = false;                 // draw localized ESC hint in non-manual modes

    /// Defaults applied to a slide that doesn't override the field. Already
    /// baked into each `CutsceneSlide` by `parse_cutscene`, so the runtime
    /// doesn't need to walk back to the cutscene-level value.
    float default_duration = 3.0f;

    std::vector<CutsceneSlide> slides;
};

/// Parse + validate a cutscene YAML document. Throws `pac::pnc::DataError`
/// (source `cutscene-loader`) on malformed input. The defaults block is
/// resolved here, so each `CutsceneSlide` returned is fully populated.
Cutscene parse_cutscene(const std::string& yaml_text);

} // namespace pac::pnc
