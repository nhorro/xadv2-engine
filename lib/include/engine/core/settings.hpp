#pragma once

namespace pac::core {

struct AudioSettings {
    float music_volume = 1.0f; // [0, 1]
    float sfx_volume = 1.0f;   // [0, 1]
};

/// Player-facing settings (M0 skeleton: in-memory, manifest defaults applied).
/// Persistence to the per-user location and a settings UI arrive in later
/// milestones.
class Settings {
public:
    AudioSettings audio;
    bool fullscreen = false;

    /// Clamp values into their valid ranges.
    void clamp();
};

} // namespace pac::core
