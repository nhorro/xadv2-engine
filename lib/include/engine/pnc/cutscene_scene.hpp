#pragma once

#include "engine/core/scene.hpp"
#include "engine/pnc/cutscene.hpp"

#include <SFML/Graphics/Color.hpp>

#include <string>

namespace sf {
class Font;
}

namespace pac::core {
struct EngineContext;
class SceneParams;
} // namespace pac::core

namespace pac::pnc {

/// YAML-driven cut-scene scene (issue #116). Plays a list of slides over a
/// solid-black background; each slide may carry text, an image, or both, with
/// per-slide positioning and styling. Advancement is one of:
///   - `auto`   — each slide stays on screen for `duration` seconds.
///   - `manual` — the player advances with click/Enter/Space; Esc skips.
///   - `timed`  — slides become active at their `at` timestamp, optionally
///                driven by an audio file (so a stuttering engine doesn't
///                desync from a narrated track).
///
/// Configured from the manifest scene `parameters`:
///   data       (req)      cutscene YAML logical path
///   on_finish  (opt)      scene id to `goto_scene` when the cutscene ends
///                         (default: quit); `POP` returns from an overlay
///   font       (opt)      fallback font for slides without an explicit one
///
/// The existing `StoryText` (Lua-driven) scene type is left intact for
/// scripted yield-based cutscenes; choose between them per scene in the
/// manifest.
class CutsceneScene : public pac::core::Scene {
public:
    CutsceneScene(pac::core::EngineContext& ctx, const pac::core::SceneParams& params);

    void enter() override;
    void leave() override;
    void handle_event(const sf::Event& event) override;
    void update(float dt) override;
    void draw(sf::RenderTarget& target) const override;

private:
    // Slide transition phase (auto/manual). Timed mode stays in Hold and derives
    // optional crossfades directly from the audio clock.
    enum class Phase { FadeIn, Hold, FadeOut };

    void request_advance();           // Manual / Auto: begin fading the current slide out.
    void begin_fade_in();             // Enter FadeIn for the current slide.
    float fade_overlay_alpha() const; // 0..1 black-overlay opacity for the current phase.
    void start_audio();               // Start the configured track once its delay has elapsed.
    void finish();                    // Skip + natural end: stop audio and exit per `on_finish`.
    void apply_slide(std::size_t i);  // Reset per-slide state when entering slide `i`.

    /// Resolve the slide's text font: per-style override, else the scene's
    /// fallback font (the `font` manifest parameter). `nullptr` when neither
    /// resource resolves — the renderer then skips the text (the slide's
    /// image still draws).
    const sf::Font* font_for(const CutsceneTextStyle& style) const;

    pac::core::EngineContext& ctx_;
    std::string data_path_;
    std::string on_finish_;
    std::string fallback_font_path_;
    const sf::Font* fallback_font_ = nullptr;

    Cutscene data_;
    bool loaded_ = false;
    bool finished_ = false;

    std::size_t current_ = 0;    // index into data_.slides; 0..size when running
    float slide_elapsed_ = 0.0f; // Auto: seconds the current slide has been held
    float scene_elapsed_ = 0.0f; // seconds since enter()
    float timed_clock_ = 0.0f;   // Timed: audio-relative cue clock (negative during pre-roll)
    bool audio_playing_ = false; // true once ctx_.audio.music has been given our track

    Phase phase_ = Phase::Hold;  // auto/manual fade machine; Timed leaves it in Hold
    float phase_elapsed_ = 0.0f; // seconds spent in the current FadeIn/FadeOut
};

} // namespace pac::pnc
