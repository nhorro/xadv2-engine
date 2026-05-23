#pragma once

#include <string>

namespace pac::core {

struct AudioSettings {
    float music_volume = 1.0f; // [0, 1]
    float sfx_volume = 1.0f;   // [0, 1]
};

/// Player-facing settings (in-memory; manifest defaults applied at startup).
/// Disk persistence to the per-user location arrives with the settings-persistence
/// work; the values here are already the single source of truth the window is
/// built from, so persistence only needs to load/store this struct.
class Settings {
public:
    AudioSettings audio;
    bool fullscreen = false;
    // Windowed client size. Ignored while `fullscreen` is true (the fullscreen
    // mode covers the desktop and is letterboxed to the virtual resolution), but
    // retained so toggling fullscreen off restores the player's chosen size.
    unsigned window_width = 1280;
    unsigned window_height = 720;
    // Active UI-strings language id (matches a manifest `languages` entry).
    // Empty means "unset" — the manifest's default language is used at startup.
    std::string language;

    /// Clamp values into their valid ranges.
    void clamp();
};

} // namespace pac::core
