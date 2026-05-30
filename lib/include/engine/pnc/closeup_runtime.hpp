#pragma once

#include "engine/core/scripting.hpp" // TaskId, ScopeId, Scripting (sol-free header)

#include <memory>
#include <string>

namespace pac::core {
class Diagnostics;
} // namespace pac::core

namespace pac::pnc {

/// The optional Lua behavior of a close-up (`closeups/<id>.lua`). The returned
/// table is held opaquely (pimpl) so sol2 never leaks into this header — mirrors
/// `RoomRuntime`/`DialogRuntime`. A close-up sidecar returns:
///
///     return {
///       on_enter = function() ... end,   -- optional, direct (non-blocking) setup
///       on_exit  = function() ... end,   -- optional, direct (non-blocking) cleanup
///       hotspots = { <id> = function() ... end, ... },  -- click handlers
///     }
///
/// Hotspot handlers are auto-run as coroutine tasks (so blocking `talk`/`wait`
/// work); `on_enter`/`on_exit` are direct calls (like room `on_load`/`on_unload`),
/// so authors wrap any blocking sequence in their own `spawn(...)`.
class CloseUpRuntime {
public:
    CloseUpRuntime();
    ~CloseUpRuntime();
    CloseUpRuntime(CloseUpRuntime&&) noexcept;
    CloseUpRuntime& operator=(CloseUpRuntime&&) noexcept;

    /// Load + run the sidecar `lua_text`, keeping its returned table as behavior.
    /// Returns true on success; logs and returns false on any error.
    bool load(pac::core::Scripting& scripting,
              const std::string& lua_text,
              const std::string& logical,
              pac::core::Diagnostics& log);

    [[nodiscard]] bool valid() const;
    [[nodiscard]] bool has_hotspot(const std::string& id) const;

    /// Direct (non-blocking) lifecycle calls. The caller sets the current script
    /// scope first, so any `spawn(...)` inside the hook lands in the close-up scope.
    void run_on_enter();
    void run_on_exit();

    /// Spawn hotspot handler `id` as a coroutine task in `scope` (so blocking
    /// `talk`/`wait`/`sleep` work). Returns the task id (0 if absent / on error).
    pac::core::TaskId
    spawn_hotspot(pac::core::Scripting& scripting, pac::core::ScopeId scope, const std::string& id);

private:
    struct Behavior;
    std::unique_ptr<Behavior> behavior_;
};

} // namespace pac::pnc
