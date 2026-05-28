#pragma once

namespace pac::core {

/// Development-only flags from the manifest `development` block. Distinct from
/// player-facing settings; never persisted as settings.
///
/// `edit_mode` is the master gate for the in-room debug overlays (#37): when it
/// is off, the overlays never render and the F1-F4 toggle keys are inert. The
/// `show_*` flags seed the initial on/off state of each overlay layer.
struct DevFlags {
    bool edit_mode = false;
    bool show_walkboxes = false; // walkable area + obstacles (F1)
    bool show_hotspots = false;  // hotspot areas + approach points (F2)
    bool show_anchors = false;   // avatar anchors + z values (F3)
    bool show_state = false;     // command-builder + room/world state HUD (F4)
    bool allow_room_reload = false;
    // Resource-profiling mode (#112): when on, the harness samples frame timing,
    // RAM, and resource-cache footprint, logs periodic lines, and writes a report
    // at exit. Development-only; never a player setting. `profiling_interval` is
    // the seconds between samples (default 2.0).
    bool profiling = false;
    double profiling_interval = 2.0;
};

} // namespace pac::core
