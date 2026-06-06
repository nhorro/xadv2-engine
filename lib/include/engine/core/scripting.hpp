#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace sol {
class state;
}

namespace pac::core {

class Diagnostics;
class ResourceCache;

using TaskId = std::uint64_t;
using ScopeId = std::uint64_t;

/// The scripting service: owns the single Lua 5.4 state (via sol2) and a
/// cooperative coroutine scheduler. sol2 is kept out of this header (pimpl) so the
/// heavy single header only compiles in the scripting translation units; only
/// `lua()` exposes it, for API-binding code that already includes sol2.
///
/// Blocking-looking script calls (`wait`, `wait_event`, `show_text`) yield the
/// running coroutine; the scheduler resumes it when the wait completes. Every task
/// belongs to a script scope and is cancelled when that scope ends.
class Scripting {
public:
    explicit Scripting(Diagnostics& log);
    ~Scripting();
    Scripting(const Scripting&) = delete;
    Scripting& operator=(const Scripting&) = delete;

    /// Underlying sol2 state, for API-binding code (TUs that include sol2).
    sol::state& lua();

    /// Run a chunk under a protected call. Returns false and logs on error.
    bool run_string(const std::string& code, const std::string& chunk_name = "=chunk");
    bool run_resource(ResourceCache& resources, const std::string& logical);

    /// Load a resource chunk and spawn it as a coroutine task in the current
    /// scope (used to run cutscene/room scripts). Returns 0 on a load error.
    TaskId spawn_resource(ResourceCache& resources, const std::string& logical);

    // --- script scopes ---
    ScopeId global_scope() const;
    ScopeId open_scope();
    void set_current_scope(ScopeId scope);
    ScopeId current_scope() const;
    void cancel_scope(ScopeId scope); // cancel + remove every task in the scope

    // --- scheduler ---
    void update(float dt);
    std::size_t active_task_count() const;
    std::size_t active_task_count(ScopeId scope) const;
    /// True if a task with `id` still exists in the scheduler (READY, waiting,
    /// or yielded). Returns false once the task has returned or been cancelled.
    /// Used by the dialog runtime to poll a spawned `run` callback.
    [[nodiscard]] bool is_task_alive(TaskId id) const;

    /// Engine-side event emit: wake waiters of `name` in `scope`.
    void emit(ScopeId scope, const std::string& name);

    /// The current speaker-less text page (for a cutscene scene to draw), or "".
    const std::string& current_text() const;

    /// Sol-aware accessor for the pimpl, used by `scripting_sol.hpp`. The Impl
    /// type is defined only in `scripting.cpp`, so this is meaningful only to
    /// TUs that include both this header and the Impl definition (in practice:
    /// `scripting.cpp` itself, via the `spawn_call` free function).
    struct Impl;
    Impl* sol_impl();

private:
    std::unique_ptr<Impl> impl_;
};

} // namespace pac::core
