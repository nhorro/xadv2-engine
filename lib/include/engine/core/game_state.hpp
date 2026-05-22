#pragma once

#include "engine/core/state_store.hpp"

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
};

} // namespace pac::core
