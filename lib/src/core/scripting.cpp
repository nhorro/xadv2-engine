#include "engine/core/scripting.hpp"

#include "engine/core/diagnostics.hpp"
#include "engine/core/resource_cache.hpp"
#include "engine/core/script_handle.hpp"
#include "engine/core/scripting_sol.hpp"

#include <sol/sol.hpp>

#include <algorithm>
#include <cstdlib>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace pac::core {

namespace {

// Lua-side prelude: the blocking primitives are thin wrappers over
// coroutine.yield, so the C++ scheduler interprets the yielded request table.
// spawn / emit and the engine API are bound from C++.
constexpr char kPrelude[] = R"LUA(
local function default_dur(text)
  local d = 0.5 + 0.06 * #text
  if d < 1.0 then return 1.0 elseif d > 7.0 then return 7.0 else return d end
end
function wait(seconds) return coroutine.yield({ kind = 'timer', seconds = seconds }) end
sleep = wait -- familiar alias for wait(seconds)
function wait_event(name) return coroutine.yield({ kind = 'event', name = name }) end
function show_text(text, dur)
  return coroutine.yield({ kind = 'text', text = text, seconds = dur or default_dur(text) })
end
)LUA";

} // namespace

struct Scripting::Impl {
    enum class Wait { READY, TIMER, EVENT, TEXT, DONE };

    struct Task {
        TaskId id = 0;
        ScopeId scope = 0;
        sol::thread thread; // owns the coroutine's lua thread
        sol::coroutine co;  // resumable handle
        Wait wait = Wait::READY;
        float timer = 0.0f;       // for TIMER / TEXT
        std::string event_name;   // for EVENT
        sol::object resume_value; // value delivered to the next resume
    };

    explicit Impl(Diagnostics& log) : log(log) {
        lua.open_libraries(sol::lib::base,
                           sol::lib::coroutine,
                           sol::lib::string,
                           sol::lib::math,
                           sol::lib::table);
        lua.safe_script(kPrelude, sol::script_pass_on_error);

        // Strong GC root for every live coroutine's lua thread, keyed by task id.
        // Each Task holds a sol::thread, but that handle is move/copy-shuffled as
        // the `tasks` vector grows and compacts (spawns, cancel_scope); under GC
        // pressure that anchor could be lost and the live thread collected, so a
        // pending resume hit a freed lua_State (SEGV in lua_resume). This table is
        // an independent anchor we clear only when a task is actually removed.
        live_threads = lua.create_table();

        // Dev/playtest GC stress: when PAC_GC_STRESS is set (and not "0"), force a
        // full Lua GC every scheduler tick. This turns latent coroutine-lifetime
        // bugs (a thread freed while still scheduled) from rare, timing-dependent
        // crashes into immediate, deterministic ones — run a playthrough with it on
        // to flush them out before release. Off (and free) by default.
        if (const char* e = std::getenv("PAC_GC_STRESS"); e != nullptr && std::string(e) != "0") {
            gc_stress_ = true;
            log.warn("PAC_GC_STRESS on: forcing a full Lua GC every scripting tick "
                     "(dev/playtest only — slow)");
        }

        lua.set_function("spawn", [this](sol::function fn) -> TaskId {
            return spawn(std::move(fn), current_scope);
        });
        lua.set_function("emit", [this](const std::string& name, sol::object payload) {
            deliver(current_scope, name, std::move(payload));
        });

        // Id-only handle usertype (no constructor from Lua; entity-owning layers
        // add factories like avatar(id) in later milestones). Read-only so scripts
        // cannot forge ids.
        lua.new_usertype<ScriptHandle>(
            "ScriptHandle",
            sol::no_constructor,
            "id",
            sol::readonly(&ScriptHandle::id),
            "kind",
            sol::readonly(&ScriptHandle::kind),
            sol::meta_function::to_string,
            [](const ScriptHandle& h) { return h.kind + "(" + h.id + ")"; });
    }

    Task* find(TaskId id) {
        for (Task& t : tasks) {
            if (t.id == id) {
                return &t;
            }
        }
        return nullptr;
    }

    TaskId spawn(sol::function fn, ScopeId scope) {
        Task t;
        t.id = next_task_id++;
        t.scope = scope;
        t.thread = sol::thread::create(lua.lua_state());
        t.co = sol::coroutine(t.thread.thread_state(), fn);
        t.wait = Wait::READY;
        t.resume_value = sol::object(); // nil
        tasks.push_back(std::move(t));
        const Task& added = tasks.back();
        live_threads[added.id] = added.thread; // GC-anchor for the task's lifetime
        return added.id;
    }

    // Drop the GC anchor for a removed task so its thread can be collected once it
    // is no longer scheduled. Call for every task removal (DONE sweep + cancel).
    void release_thread(TaskId id) { live_threads[id] = sol::lua_nil; }

    /// Outcome of a single coroutine resume, for callers (currently
    /// `spawn_call`) that need to know whether the task ran to completion and,
    /// if so, what it returned as its first value.
    struct ResumeOutcome {
        bool done = true;                               // true if the task is no longer scheduled
        std::optional<std::string> first_string_return; // first return value if it was a string
    };

    // Post-resume bookkeeping: interpret the coroutine's status + yield value
    // and write the task's next wait state. Used by both the per-tick `resume`
    // and by `spawn_call`'s inline first-resume. `id` is the task to re-resolve
    // (the call may have invalidated any pre-resume Task pointer by spawning
    // new tasks and growing the vector). Returns whether the task completed
    // and the first string return value, if any.
    ResumeOutcome
    interpret_resume(TaskId id, sol::coroutine& co, const sol::protected_function_result& r) {
        ResumeOutcome out;
        Task* t = find(id);
        if (!t) {
            return out;
        }
        if (!r.valid()) {
            const sol::error err = r;
            log.error(std::string("script error: ") + err.what());
            t->wait = Wait::DONE;
            return out;
        }
        if (co.status() != sol::call_status::yielded) {
            t->wait = Wait::DONE;
            if (r.return_count() > 0) {
                sol::object v(r[0]);
                if (v.is<std::string>()) {
                    out.first_string_return = v.as<std::string>();
                }
            }
            return out;
        }
        out.done = false;
        sol::object req_obj =
            (r.return_count() > 0) ? sol::object(r[0]) : sol::object(sol::lua_nil);
        if (!req_obj.is<sol::table>()) {
            t->wait = Wait::READY; // bare yield -> reschedule
            return out;
        }
        const sol::table req = req_obj.as<sol::table>();
        const std::string kind = req.get_or("kind", std::string("ready"));
        if (kind == "timer") {
            t->wait = Wait::TIMER;
            t->timer = static_cast<float>(req.get_or("seconds", 0.0));
        } else if (kind == "event") {
            t->wait = Wait::EVENT;
            t->event_name = req.get_or("name", std::string());
        } else if (kind == "text") {
            t->wait = Wait::TEXT;
            t->timer = static_cast<float>(req.get_or("seconds", 0.0));
            current_text = req.get_or("text", std::string());
            text_owner = t->id;
        } else {
            t->wait = Wait::READY;
        }
        return out;
    }

    // Resumes by id, never by reference: t.co(arg) below can run Lua that calls
    // spawn(), which push_back()s into `tasks` and may reallocate it. A held
    // Task& would dangle for the rest of this function (use-after-free), so we
    // copy the coroutine handle before the call and re-resolve the task after.
    void resume(TaskId id) {
        Task* t = find(id);
        if (!t) {
            return;
        }
        const ScopeId previous = current_scope;
        current_scope = t->scope; // spawn/emit inside the task inherit its scope
        const sol::object arg = t->resume_value;
        t->resume_value = sol::object(); // nil

        // The handle copy keeps the coroutine alive independently of `t`; the
        // underlying lua thread is owned by the Task and outlives this call
        // (tasks are only erased after update()'s loop).
        sol::coroutine co = t->co;
        const sol::protected_function_result r = co(arg);

        current_scope = previous;
        interpret_resume(id, co, r); // updates t->wait; per-tick path ignores return
    }

    /// Spawn `fn` and resume it once, inline, to its first yield or completion.
    /// `arg0` / `arg1` (when set) are passed positionally to the function;
    /// trailing nullopts are omitted so a zero-arg handler sees no args and
    /// `select('#', ...)` matches today's direct-call semantics. On completion
    /// the first string return is captured. Used by the dispatch sites (M9
    /// #183) so verb handlers don't have to wrap themselves in `spawn(...)`.
    SpawnCallResult
    spawn_call(sol::function fn, std::optional<std::string> arg0, std::optional<std::string> arg1) {
        SpawnCallResult out;
        out.task_id = spawn(fn, current_scope);
        Task* t = find(out.task_id);
        if (!t) {
            return out; // unreachable: spawn just pushed it
        }
        const ScopeId previous = current_scope;
        current_scope = t->scope;
        // Copy the coroutine handle before the call: the call may run Lua that
        // calls spawn(), reallocating `tasks` and invalidating `t`.
        sol::coroutine co = t->co;
        const sol::protected_function_result r = arg1 ? co(*arg0, *arg1) : arg0 ? co(*arg0) : co();
        current_scope = previous;
        const ResumeOutcome dec = interpret_resume(out.task_id, co, r);
        out.done = dec.done;
        out.string_return = dec.first_string_return;
        return out;
    }

    void update(float dt) {
        if (gc_stress_) {
            lua.collect_garbage(); // dev/playtest: surface coroutine-lifetime bugs now
        }
        // Snapshot ids: resuming may spawn new tasks (run next frame) or mutate the
        // vector, so we never iterate it directly.
        std::vector<TaskId> ids;
        ids.reserve(tasks.size());
        for (const Task& t : tasks) {
            ids.push_back(t.id);
        }
        for (const TaskId id : ids) {
            Task* t = find(id);
            if (!t) {
                continue;
            }
            bool run = false;
            if (t->wait == Wait::READY) {
                run = true;
            } else if (t->wait == Wait::TIMER || t->wait == Wait::TEXT) {
                t->timer -= dt;
                run = (t->timer <= 0.0f);
            }
            if (!run) {
                continue;
            }
            if (t->wait == Wait::TEXT && text_owner == t->id) {
                current_text.clear(); // page finished
                text_owner = 0;
            }
            resume(id);
        }
        for (const Task& t : tasks) {
            if (t.wait == Wait::DONE) {
                release_thread(t.id);
            }
        }
        tasks.erase(std::remove_if(tasks.begin(),
                                   tasks.end(),
                                   [](const Task& t) { return t.wait == Wait::DONE; }),
                    tasks.end());
    }

    void deliver(ScopeId scope, const std::string& name, sol::object payload) {
        for (Task& t : tasks) {
            if (t.wait == Wait::EVENT && t.scope == scope && t.event_name == name) {
                t.wait = Wait::READY;
                t.resume_value = payload;
            }
        }
    }

    void cancel_scope(ScopeId scope) {
        if (text_owner != 0) {
            const Task* owner = find(text_owner);
            if (owner && owner->scope == scope) {
                current_text.clear();
                text_owner = 0;
            }
        }
        for (const Task& t : tasks) {
            if (t.scope == scope) {
                release_thread(t.id);
            }
        }
        tasks.erase(std::remove_if(tasks.begin(),
                                   tasks.end(),
                                   [scope](const Task& t) { return t.scope == scope; }),
                    tasks.end());
    }

    std::size_t count(ScopeId scope) const {
        std::size_t n = 0;
        for (const Task& t : tasks) {
            if (t.scope == scope) {
                ++n;
            }
        }
        return n;
    }

    Diagnostics& log;
    sol::state lua;
    sol::table live_threads; // task id -> sol::thread; GC-roots scheduled coroutines
    bool gc_stress_ = false; // PAC_GC_STRESS: full GC each tick (dev lifetime-bug flush)
    std::vector<Task> tasks;
    TaskId next_task_id = 1;
    ScopeId next_scope_id = 1; // 0 reserved for the global scope
    ScopeId current_scope = 0;
    std::string current_text;
    TaskId text_owner = 0;
};

Scripting::Scripting(Diagnostics& log) : impl_(std::make_unique<Impl>(log)) {}
Scripting::~Scripting() = default;

sol::state& Scripting::lua() {
    return impl_->lua;
}

bool Scripting::run_string(const std::string& code, const std::string& chunk_name) {
    sol::load_result chunk = impl_->lua.load(code, chunk_name);
    if (!chunk.valid()) {
        const sol::error err = chunk;
        impl_->log.error(std::string("script load error: ") + err.what());
        return false;
    }
    const sol::protected_function fn = chunk;
    const sol::protected_function_result r = fn();
    if (!r.valid()) {
        const sol::error err = r;
        impl_->log.error(std::string("script error: ") + err.what());
        return false;
    }
    return true;
}

bool Scripting::run_resource(ResourceCache& resources, const std::string& logical) {
    try {
        return run_string(resources.read_text(logical), "@" + logical);
    } catch (const std::exception& e) {
        impl_->log.error(std::string("script: ") + e.what());
        return false;
    }
}

TaskId Scripting::spawn_resource(ResourceCache& resources, const std::string& logical) {
    std::string code;
    try {
        code = resources.read_text(logical);
    } catch (const std::exception& e) {
        impl_->log.error(std::string("script: ") + e.what());
        return 0;
    }
    sol::load_result chunk = impl_->lua.load(code, "@" + logical);
    if (!chunk.valid()) {
        const sol::error err = chunk;
        impl_->log.error(std::string("script load error: ") + err.what());
        return 0;
    }
    return impl_->spawn(sol::function(chunk), impl_->current_scope);
}

ScopeId Scripting::global_scope() const {
    return 0;
}
ScopeId Scripting::open_scope() {
    return impl_->next_scope_id++;
}
void Scripting::set_current_scope(ScopeId scope) {
    impl_->current_scope = scope;
}
ScopeId Scripting::current_scope() const {
    return impl_->current_scope;
}
void Scripting::cancel_scope(ScopeId scope) {
    impl_->cancel_scope(scope);
}

void Scripting::update(float dt) {
    impl_->update(dt);
}
std::size_t Scripting::active_task_count() const {
    return impl_->tasks.size();
}
std::size_t Scripting::active_task_count(ScopeId scope) const {
    return impl_->count(scope);
}

bool Scripting::is_task_alive(TaskId id) const {
    return std::any_of(impl_->tasks.begin(), impl_->tasks.end(), [id](const auto& t) {
        return t.id == id;
    });
}

void Scripting::emit(ScopeId scope, const std::string& name) {
    impl_->deliver(scope, name, sol::object());
}

const std::string& Scripting::current_text() const {
    return impl_->current_text;
}

Scripting::Impl* Scripting::sol_impl() {
    return impl_.get();
}

// Sol-aware free seam (declared in scripting_sol.hpp). Routes through the same
// spawn() + resume() path the scheduler uses, so the spawned task is anchored in
// `live_threads` (PR #171) and obeys the scope discipline (cancelled when its
// scope ends).
SpawnCallResult spawn_call(Scripting& s,
                           sol::function fn,
                           std::optional<std::string> arg0,
                           std::optional<std::string> arg1) {
    return s.sol_impl()->spawn_call(std::move(fn), std::move(arg0), std::move(arg1));
}

} // namespace pac::core
