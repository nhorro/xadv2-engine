#pragma once

#include "engine/core/state_store.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace pac::core {

/// One save payload — the engine's single source of truth for persistent state
/// (design 02 §"Make persistent state explicit"). Everything serializable lives
/// here; transient state (running coroutines, command builder, speech bubble,
/// dialog mid-conversation, command-builder verb selection, ...) is excluded by
/// construction.
///
/// The struct is data-only and lives in `pac::core` so the save service (also
/// core) can serialize it without depending on `pac::pnc`. RoomScene materializes
/// a snapshot via `snap()` and applies a restore via `restore()`.
///
/// Dialog `once`-consumed flags fold into `global_state` under the engine-reserved
/// `__dialog.<dialog_id>.<node_id>.<option_index>` prefix (see design review of
/// #15 in the M2 milestone). No separate field is needed at this layer; the
/// prefix isolates them from author-set state.
struct GameState {
    int save_version = 1;

    /// Player-supplied short description (issue #108). Optional — the save/load
    /// UI falls back to the file's mtime when this is empty. Round-trips
    /// through the save file; the autosave normally leaves it empty so the UI
    /// always shows its timestamp.
    std::string description;

    /// Unix-epoch seconds at the moment of save. Set by `SaveService::save` so
    /// the UI can render a date/time without re-reading file mtimes (and so a
    /// snapshot timestamp survives a file copy that resets mtime). 0 ⇒ unset
    /// (an older save without the field); the UI then falls back to mtime.
    std::int64_t saved_at = 0;

    /// Top-level scene id (manifest entry). MVP only saves while RoomScene is
    /// active, but the field is recorded so saving from other scenes is a
    /// later extension rather than a format change.
    std::string current_scene_id;

    struct RoomView {
        std::string current_room_id;
        struct Player {
            float x = 0.0f;
            float y = 0.0f;
            std::string facing = "down";
            /// Cast appearance id at save time. Held for forward compatibility:
            /// the MVP fixes appearance at scene start, but a later mid-game
            /// appearance swap is design-for and would round-trip through here.
            std::string appearance_id;
        } player;
    } room_view;

    /// Inventory item ids, in player-visible order.
    std::vector<std::string> inventory;

    /// Global state store, the `set_state`/`get_state` map (`__dialog.*` keys
    /// included).
    std::map<std::string, StateValue> global_state;

    /// Per-room scripted state: `room_state[room_id][key] = value`. Persists
    /// across room loads and is never cleared on unload.
    std::map<std::string, std::map<std::string, StateValue>> room_state;

    /// Per-room region states: `region_states[room_id][region_id] = state_id`.
    /// Wins over a region's YAML `initial` after load.
    std::map<std::string, std::map<std::string, std::string>> region_states;

    /// Per-room hotspot enabled flags: `hotspot_enabled[room_id][hotspot_id] = bool`.
    /// Without this, an author who calls `disable_hotspot("door")` and saves
    /// would see the door re-enabled on load (the YAML `enabled` default would
    /// shadow the scripted change) — silent ghost bug per issue #33 review.
    std::map<std::string, std::map<std::string, bool>> hotspot_enabled;

    /// Per-room object visibility: `object_visible[room_id][object_id] = bool`.
    /// Same shape, same reasoning as `hotspot_enabled`, for `show_object` /
    /// `hide_object`.
    std::map<std::string, std::map<std::string, bool>> object_visible;

    /// Per-room background-layer visibility: `layer_visible[room_id][layer_id] = bool`.
    /// Same shape and reasoning as `object_visible`, for `set_layer_visible`.
    std::map<std::string, std::map<std::string, bool>> layer_visible;
};

} // namespace pac::core
