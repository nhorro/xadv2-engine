// Internal hook for DialogRuntime: lets sol-aware translation units (and tests)
// construct a runtime from an already-loaded dialog table, bypassing the disk
// loader. This header includes sol2, so it lives under `lib/src/` (private) and
// must never be included from a public header.

#pragma once

#include "engine/pnc/dialog.hpp"

#include <sol/sol.hpp>

#include <string>

namespace pac::core {
class Diagnostics;
class Scripting;
} // namespace pac::core

namespace pac::pnc {

/// Carrier for a Lua function passed from the dialog runtime to its host's
/// `spawn_run` callback. Defined here (private header, sol2 visible) so the
/// public `dialog.hpp` can forward-declare it without leaking sol2. Both the
/// dialog runtime and its production host (RoomScene) include this header.
struct DialogRunFn {
    sol::function fn;
};

/// Internal factory used by `RoomScene::api_start_dialog` (after it loads the
/// dialog table itself) and by `dialog_test`. Calls `on_enter` (if present)
/// and enters the start node before returning. `end_sentinel` is the unique
/// table the loader injected into the dialog chunk as the `END` global; the
/// runtime compares `to` fields against it by identity. Tests create their
/// own sentinel per dialog so they can mimic the engine's loader path.
struct DialogInternal {
    static DialogRuntime from_table(pac::core::Scripting& scripting,
                                    pac::core::Diagnostics& log,
                                    const std::string& npc_id,
                                    DialogHost host,
                                    sol::table dialog_table,
                                    sol::table end_sentinel);

    /// Validate a freshly-loaded dialog tree. Checks: node-shape XOR (options
    /// or to, not both); option `to` non-nil; `to` is either a string node id
    /// or the END sentinel; string node ids reference existing nodes. Returns
    /// an empty string on success, or a human-readable error otherwise.
    [[nodiscard]] static std::string validate(sol::table dialog_table, sol::table end_sentinel);
};

} // namespace pac::pnc
