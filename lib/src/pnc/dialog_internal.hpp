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

/// Internal factory used by `RoomScene::api_start_dialog` (after it loads the
/// dialog table itself) and by `dialog_test`. Calls `on_enter` (if present)
/// and enters the start node before returning.
struct DialogInternal {
    static DialogRuntime from_table(pac::core::Scripting& scripting,
                                    pac::core::Diagnostics& log,
                                    const std::string& npc_id,
                                    DialogHost host,
                                    sol::table dialog_table);
};

} // namespace pac::pnc
