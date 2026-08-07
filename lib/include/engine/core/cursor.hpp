#pragma once

#include <cmath>
#include <cstdint>

namespace pac::core {

/// The cursor appearances the engine can show. DEFAULT is the resting pointer;
/// INTERACT signals an interactive target under the pointer (issue #73).
enum class CursorKind : std::uint8_t { DEFAULT, INTERACT };

/// Return the resting cursor's eased dark-to-light blend on a cosine wave.
/// Invalid or disabled intervals stay on the dark tone.
[[nodiscard]] inline float cursor_blink_progress(float elapsed_seconds, float interval_seconds) {
    if (!std::isfinite(elapsed_seconds) || !std::isfinite(interval_seconds) ||
        elapsed_seconds < 0.0f || interval_seconds <= 0.0f) {
        return 0.0f;
    }
    constexpr float pi = 3.14159265358979323846f;
    const float phase = std::fmod(elapsed_seconds, interval_seconds * 2.0f) / interval_seconds;
    return 0.5f - 0.5f * std::cos(phase * pi);
}

/// Per-frame cursor request channel shared through `EngineContext`. A scene calls
/// `want()` during update/handle_event to request an appearance for this frame;
/// the harness applies the matching hardware cursor after update, then `reset()`s
/// it to DEFAULT — so a scene must re-assert INTERACT every frame it wants it
/// (when it stops, the cursor falls back to DEFAULT on its own).
struct CursorState {
    CursorKind requested = CursorKind::DEFAULT;
    bool inverted = false;
    bool hidden = false;

    void want(CursorKind kind) { requested = kind; }
    /// Ask the harness for an RGB-inverted variant this frame. Useful when an
    /// overlay has the opposite luminance from the game behind it.
    void want_inverted(bool value = true) { inverted = value; }
    /// Hide the OS cursor for this frame. Cutscene scenes and scripted room
    /// cutscenes re-assert this while they remain active.
    void want_hidden(bool value = true) { hidden = value; }
    void reset() {
        requested = CursorKind::DEFAULT;
        inverted = false;
        hidden = false;
    }
};

} // namespace pac::core
