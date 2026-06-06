#pragma once

#include "engine/core/scripting.hpp"

#include <sol/sol.hpp>

#include <optional>
#include <string>

namespace pac::core {

/// Outcome of `spawn_call`: the spawned task's id (anchored in `live_threads`
/// for its lifetime, PR #171), whether it completed before yielding, and the
/// first string returned by the function if it ran to completion.
struct SpawnCallResult {
    TaskId task_id = 0;
    bool done = true;
    std::optional<std::string> string_return;
};

/// Spawn `fn` as a coroutine task in the scheduler's current scope and resume
/// it once, inline, to its first yield or completion. `arg0` / `arg1` are
/// passed positionally when set, and trailing nullopts are omitted — matching
/// the existing `call_hotspot` / `call_inventory` / `call_game` dispatch
/// shapes (0 args for a one-operand verb whose handler owns the target, 1 arg
/// for a two-operand verb, 2 args for a `game.fallbacks` two-operand handler
/// that receives both operands). Used by the dispatch sites (M9 #183) so a
/// hotspot / inventory / game-fallback handler can use blocking primitives
/// (`talk`/`move_to`/`wait`) without an explicit `spawn()` wrapper. Errors in
/// the function are logged like the scheduler's other resumes; the result
/// reports `done=true` in that case (no string_return).
SpawnCallResult spawn_call(Scripting& scripting,
                           sol::function fn,
                           std::optional<std::string> arg0 = std::nullopt,
                           std::optional<std::string> arg1 = std::nullopt);

} // namespace pac::core
