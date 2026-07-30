#pragma once

#include <cstdint>

namespace pac::core {

/// The cursor appearances the engine can show. DEFAULT is the resting pointer;
/// INTERACT signals an interactive target under the pointer (issue #73).
enum class CursorKind : std::uint8_t { DEFAULT, INTERACT };

/// Per-frame cursor request channel shared through `EngineContext`. A scene calls
/// `want()` during update/handle_event to request an appearance for this frame;
/// the harness applies the matching hardware cursor after update, then `reset()`s
/// it to DEFAULT — so a scene must re-assert INTERACT every frame it wants it
/// (when it stops, the cursor falls back to DEFAULT on its own).
struct CursorState {
    CursorKind requested = CursorKind::DEFAULT;
    bool inverted = false;

    void want(CursorKind kind) { requested = kind; }
    /// Ask the harness for an RGB-inverted variant this frame. Useful when an
    /// overlay has the opposite luminance from the game behind it.
    void want_inverted(bool value = true) { inverted = value; }
    void reset() {
        requested = CursorKind::DEFAULT;
        inverted = false;
    }
};

} // namespace pac::core
