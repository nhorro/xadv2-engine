#pragma once

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
///              black background).
///   - Stretch: scale to exactly fill the box, regardless of aspect.
enum class CutsceneImageFit {
    Contain,
    Stretch,
};

/// Author-facing text styling. Empty `font` means "use the scene's default
/// font" (the `font` parameter on the `Cutscene` manifest entry).
struct CutsceneTextStyle {
    std::string font;   // optional logical path; empty = default
    unsigned size = 30; // virtual pixels
    sf::Color color{255, 255, 255, 255};
};

/// One slide in a `Cutscene`. Fields are evaluated against the cutscene's
/// `defaults` block first, then per-slide overrides. `text` and `image` are
/// independent — a slide may have one, both, or neither (a blank pause).
struct CutsceneSlide {
    std::optional<std::string> text;
    std::optional<std::string> image; // logical path

    sf::Vector2f text_position{0.5f, 0.8f}; // normalized; (0.5, 0.8) = lower-center
    CutsceneTextAlign text_align = CutsceneTextAlign::Center;
    CutsceneTextStyle text_style;

    sf::Vector2f image_position{0.5f, 0.5f}; // normalized anchor (image is centered on it)
    sf::Vector2f image_size{0.6f, 0.6f};     // normalized maximum extent in screen space
    CutsceneImageFit image_fit = CutsceneImageFit::Contain;

    std::optional<float> duration; // Auto mode; falls back to the cutscene default
    std::optional<float> at;       // Timed mode; required there
};

/// Parsed `cutscenes/<id>.yaml`. Headless and testable; runtime lives in
/// `CutsceneScene`.
struct Cutscene {
    int version = 1;
    CutsceneAdvanceMode mode = CutsceneAdvanceMode::Auto;
    std::string audio; // logical path; optional (Timed mode for narrated cutscenes)

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
