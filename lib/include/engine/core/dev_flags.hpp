#pragma once

namespace pac::core {

/// Development-only flags from the manifest `development` block. Distinct from
/// player-facing settings; never persisted as settings.
struct DevFlags {
    bool edit_mode = false;
    bool show_walkboxes = false;
    bool show_hotspots = false;
    bool allow_room_reload = false;
};

} // namespace pac::core
