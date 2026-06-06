#include "engine/core/diagnostics.hpp"
#include "engine/core/scripting.hpp"
#include "engine/core/scripting_sol.hpp"

#include <doctest/doctest.h>
#include <sol/sol.hpp>

using namespace pac::core;

namespace {
// ERROR level keeps normal output quiet; the error-reporting test still logs the
// (expected) script errors it triggers.
Diagnostics quiet() {
    return Diagnostics(LogLevel::ERROR);
}
} // namespace

TEST_CASE("run_string reports load and runtime errors") {
    Diagnostics log = quiet();
    Scripting s(log);
    CHECK(s.run_string("local x = 1 + 1"));
    CHECK_FALSE(s.run_string("this is not lua )(")); // syntax error
    CHECK_FALSE(s.run_string("error('boom')"));      // runtime error
}

TEST_CASE("spawn + wait advances by timer then finishes") {
    Diagnostics log = quiet();
    Scripting s(log);
    CHECK(s.run_string("spawn(function() wait(1.0) end)"));
    CHECK(s.active_task_count() == 1);
    s.update(0.5f); // starts task, yields a 1.0s timer
    CHECK(s.active_task_count() == 1);
    s.update(0.6f); // 1.0 -> 0.4
    CHECK(s.active_task_count() == 1);
    s.update(0.6f); // 0.4 -> elapsed -> coroutine returns -> done
    CHECK(s.active_task_count() == 0);
}

TEST_CASE("sleep is an alias for wait") {
    Diagnostics log = quiet();
    Scripting s(log);
    CHECK(s.run_string("spawn(function() sleep(1.0) end)"));
    CHECK(s.active_task_count() == 1);
    s.update(0.5f); // starts task, yields a 1.0s timer
    CHECK(s.active_task_count() == 1);
    s.update(0.6f); // 1.0 -> 0.4
    CHECK(s.active_task_count() == 1);
    s.update(0.6f); // elapsed -> coroutine returns -> done
    CHECK(s.active_task_count() == 0);
}

TEST_CASE("spawn inherits the current scope") {
    Diagnostics log = quiet();
    Scripting s(log);
    const ScopeId sc = s.open_scope();
    s.set_current_scope(sc);
    s.run_string("spawn(function() wait(1.0) end)");
    s.set_current_scope(s.global_scope());
    CHECK(s.active_task_count(sc) == 1);
    CHECK(s.active_task_count(s.global_scope()) == 0);
}

TEST_CASE("wait_event resumes on emit, and events do not cross scopes") {
    Diagnostics log = quiet();
    Scripting s(log);
    const ScopeId a = s.open_scope();
    const ScopeId b = s.open_scope();

    s.set_current_scope(a);
    CHECK(s.run_string("spawn(function() wait_event('go') end)"));
    s.set_current_scope(s.global_scope());

    s.update(0.0f); // start -> waits on event 'go'
    CHECK(s.active_task_count(a) == 1);

    s.emit(b, "go"); // wrong scope
    s.update(0.0f);
    CHECK(s.active_task_count(a) == 1);

    s.emit(a, "go"); // right scope -> wakes
    s.update(0.0f);
    CHECK(s.active_task_count(a) == 0);
}

TEST_CASE("cancel_scope removes tasks and never resumes them") {
    Diagnostics log = quiet();
    Scripting s(log);
    const ScopeId sc = s.open_scope();
    s.set_current_scope(sc);
    CHECK(s.run_string("spawn(function() wait(100.0) end)"));
    s.set_current_scope(s.global_scope());

    s.update(0.0f); // start -> long timer
    CHECK(s.active_task_count() == 1);
    CHECK(s.active_task_count(sc) == 1);

    s.cancel_scope(sc);
    CHECK(s.active_task_count(sc) == 0);
    CHECK(s.active_task_count() == 0);

    s.update(200.0f); // the timer would have fired; it must stay gone
    CHECK(s.active_task_count() == 0);
}

TEST_CASE("a task spawning children mid-resume does not corrupt the scheduler") {
    // Regression: resume() must not hold a Task& across the coroutine call.
    // spawn() push_back()s into the tasks vector and can reallocate it; a held
    // reference would dangle (heap-use-after-free under ASan). The parent spawns
    // enough children in one body to force several reallocations before yielding.
    Diagnostics log = quiet();
    Scripting s(log);
    CHECK(s.run_string(R"(
        spawn(function()
            for i = 1, 64 do
                spawn(function() end)
            end
            wait(1.0)
        end)
    )"));
    CHECK(s.active_task_count() == 1);

    s.update(0.0f); // parent spawns 64 children (forces growth), then waits
    CHECK(s.active_task_count() == 65);

    s.update(2.0f); // parent timer elapses and all children run to completion
    CHECK(s.active_task_count() == 0);
}

TEST_CASE("show_text exposes the current page and clears it when done") {
    Diagnostics log = quiet();
    Scripting s(log);
    s.run_string("spawn(function() show_text('hola', 1.0) end)");
    CHECK(s.current_text().empty());

    s.update(0.0f); // start -> page shown
    CHECK(s.current_text() == "hola");

    s.update(2.0f); // page duration elapses -> resume -> finish, page cleared
    CHECK(s.current_text().empty());
    CHECK(s.active_task_count() == 0);
}

// Scheduler stress for the freed-coroutine-thread fix: a long-lived coroutine
// (like a room watcher) is resumed every tick under constant GC, after a task
// spawned BEFORE it finished and the DONE-sweep compacted the `tasks` vector,
// moving the survivor's Task to a new slot. The survivor's lua thread must stay
// alive across that move + GC (the scheduler now keeps an explicit GC root per
// live task). NOTE: the original crash only reproduced in the full game (intro +
// close-up + real allocation pressure), verified via an integration smoke; this
// unit exercise guards the invariant and the move/anchor path but is not by
// itself a deterministic reproducer of that SEGV.
TEST_CASE("scheduler: a long-lived coroutine survives compaction + heavy GC") {
    Diagnostics log = quiet();
    Scripting s(log);
    // Finisher spawned BEFORE the survivor: when it finishes, the DONE-sweep
    // compacts the `tasks` vector and MOVES the survivor's Task to a new slot. The
    // survivor then resumes ~3000 times, forcing a full GC each tick from inside
    // itself: if compaction dropped its lua thread's anchor, the GC frees the live
    // thread and the next resume SEGVs. It is finite so everything drains (no
    // leaked threads at exit).
    s.run_string("spawn(function() wait(0.05) end)"); // finisher (slot 0)
    s.run_string(
        "spawn(function() for n = 1, 3000 do collectgarbage('collect'); wait(0.0) end end)");
    for (int i = 0; i < 4000 && s.active_task_count() > 0; ++i) {
        s.update(0.016f);
    }
    CHECK(s.active_task_count() == 0); // both ran to completion, no crash
}

// M9 #183: `spawn_call` resumes a fresh task to its first yield/completion and
// captures the synchronous string return, so a verb handler that just does
// `return "caption"` keeps today's "return-value-becomes-caption" semantics
// while a handler that yields hands control back to the scheduler.
TEST_CASE("spawn_call: synchronous return is captured; yielding leaves the task in flight") {
    Diagnostics log = quiet();
    Scripting s(log);
    sol::state& L = s.lua();

    // (a) Returns a string -> done with string_return. (Note: a synchronously-
    // completed task is still in `tasks` until the next update() sweeps it,
    // marked Wait::DONE; consumers (the dispatch sites) only poll is_task_alive
    // when `done == false`, so the sweep-deferred state is irrelevant to them.)
    sol::function f_str = L.script("return function() return 'Una cosa.' end").get<sol::function>();
    auto r_str = spawn_call(s, f_str);
    CHECK(r_str.done);
    REQUIRE(r_str.string_return.has_value());
    CHECK(*r_str.string_return == "Una cosa.");
    s.update(0.0f);
    CHECK_FALSE(s.is_task_alive(r_str.task_id)); // swept after one tick

    // (b) Returns nothing -> done, no string_return.
    sol::function f_void = L.script("return function() end").get<sol::function>();
    auto r_void = spawn_call(s, f_void);
    CHECK(r_void.done);
    CHECK_FALSE(r_void.string_return.has_value());

    // (c) Yields via wait() -> not done, task still alive, drains over time.
    sol::function f_yield =
        L.script("return function() wait(0.5); _G.f_yield_done = true end").get<sol::function>();
    auto r_yield = spawn_call(s, f_yield);
    CHECK_FALSE(r_yield.done);
    CHECK(s.is_task_alive(r_yield.task_id));
    s.update(0.6f); // timer elapses + final resume
    CHECK_FALSE(s.is_task_alive(r_yield.task_id));
    CHECK(s.run_string("assert(_G.f_yield_done == true)"));

    // (d) First arg is forwarded positionally.
    sol::function f_arg =
        L.script("return function(x) return 'got:' .. x end").get<sol::function>();
    auto r_arg = spawn_call(s, f_arg, std::string("alpha"));
    CHECK(r_arg.done);
    REQUIRE(r_arg.string_return.has_value());
    CHECK(*r_arg.string_return == "got:alpha");

    // (e) Two args are forwarded positionally (game.fallbacks shape).
    sol::function f_ab =
        L.script("return function(a, b) return a .. '+' .. b end").get<sol::function>();
    auto r_ab = spawn_call(s, f_ab, std::string("x"), std::string("y"));
    CHECK(r_ab.done);
    REQUIRE(r_ab.string_return.has_value());
    CHECK(*r_ab.string_return == "x+y");

    // (f) Errors are logged + treated as done (no string_return).
    sol::function f_err = L.script("return function() error('boom') end").get<sol::function>();
    auto r_err = spawn_call(s, f_err);
    CHECK(r_err.done);
    CHECK_FALSE(r_err.string_return.has_value());
}

// M9 #184: the `cutscene(body)` wrapper spawns the body in the current scope,
// arms the C++ drain hook with the task id, and returns immediately so the
// caller (a verb handler / a flow-helper-spawned outer task / direct call from
// a beat) doesn't block. The room-view-state flip + restore is C++-side; this
// test exercises the Lua wrapper's shape using a stub `_cutscene_arm` that
// just captures the id, plus a real scheduler that drains the body.
TEST_CASE("cutscene wrapper: spawns body, arms drain hook, drains under scope cancel") {
    Diagnostics log = quiet();
    Scripting s(log);
    sol::state& L = s.lua();

    // Define the same wrapper RoomScene installs in its prelude — keeping this
    // close to the production code makes the test a true characterization, not
    // a re-implementation.
    L.script(R"LUA(
function cutscene(body)
  return function(...)
    local args = { ... }
    local tid = spawn(function() body(table.unpack(args)) end)
    _cutscene_arm(tid)
  end
end
)LUA");

    // Stub the arm hook: capture the armed task id so the test can assert it.
    TaskId armed = 0;
    L.set_function("_cutscene_arm", [&armed](TaskId tid) { armed = tid; });

    // (a) A cutscene whose body yields. The wrapper call returns immediately;
    // the body task is alive in the scheduler with the armed id; drain works
    // under GC pressure (live_threads anchor, PR #171).
    L.script("counter = 0");
    sol::function beat = L.script(R"LUA(
return cutscene(function()
  wait(0.5)
  counter = counter + 1
end)
)LUA")
                             .get<sol::function>();
    beat();
    CHECK(armed != 0);
    CHECK(s.is_task_alive(armed));
    for (int i = 0; i < 100 && s.is_task_alive(armed); ++i) {
        L["collectgarbage"]("collect");
        s.update(0.016f);
    }
    CHECK_FALSE(s.is_task_alive(armed));
    CHECK(s.run_string("assert(counter == 1, 'got ' .. counter)"));

    // (b) Args forwarded through the wrapper, so a handler shaped like
    // `door.use = cutscene(function(item) ... end)` receives `item` (the
    // operand the #183 dispatch path passes in) inside the body.
    sol::function with_arg =
        L.script("return cutscene(function(x) seen = x end)").get<sol::function>();
    with_arg(std::string("hello"));
    for (int i = 0; i < 100 && s.is_task_alive(armed); ++i) {
        s.update(0.016f);
    }
    CHECK(s.run_string("assert(seen == 'hello')"));

    // (c) cancel_scope mid-body kills the cutscene task as a normal scope-tied
    // task. In production, RoomScene::update detects the dead task via
    // is_task_alive(awaiting_handler_task_) and restores COMMAND from C++,
    // since scope cancellation does NOT run Lua cleanup (design 05 §Coroutine
    // rules). Here we just check the cancel + alive bookkeeping.
    const ScopeId sc = s.open_scope();
    s.set_current_scope(sc);
    L.script("survives = false");
    sol::function long_beat = L.script(R"LUA(
return cutscene(function()
  wait(100.0)
  survives = true
end)
)LUA")
                                  .get<sol::function>();
    long_beat();
    s.set_current_scope(s.global_scope());
    CHECK(s.is_task_alive(armed));
    s.cancel_scope(sc); // simulate change_room mid-cutscene
    CHECK_FALSE(s.is_task_alive(armed));
    s.update(200.0f); // would have fired the body's `survives = true`
    CHECK(s.run_string("assert(survives == false)"));
}

// M9 #183 / PR #171: a flood of auto-spawned handlers that each yield once must
// not crash under GC pressure. The scheduler's `live_threads` anchor keeps each
// fresh task's lua thread alive through the spawn/resume churn, and the
// inline-first-resume path used by spawn_call must obey the same invariant.
TEST_CASE("spawn_call: click-storm soak under heavy GC") {
    Diagnostics log = quiet();
    Scripting s(log);
    sol::state& L = s.lua();

    // Handler yields once via wait(0.0), then sets a counter so we can verify
    // all tasks ran to completion. Force a full GC before each spawn so the
    // freshly-allocated lua thread is exercised under the same pressure that
    // surfaced PR #171's SEGV.
    L.script("counter = 0");
    sol::function handler =
        L.script("return function() wait(0.0); counter = counter + 1 end").get<sol::function>();

    constexpr int N = 200;
    for (int i = 0; i < N; ++i) {
        L["collectgarbage"]("collect");
        auto r = spawn_call(s, handler);
        CHECK_FALSE(r.done); // wait(0.0) yields
        CHECK(s.is_task_alive(r.task_id));
    }
    // Drain every task: tick the scheduler until the queue empties. Each tick
    // forces another full GC (via PAC_GC_STRESS-style stress) by running it
    // explicitly from Lua, so we exercise the live_threads anchor through the
    // DONE sweep that follows.
    for (int i = 0; i < 1000 && s.active_task_count() > 0; ++i) {
        L["collectgarbage"]("collect");
        s.update(0.001f);
    }
    CHECK(s.active_task_count() == 0);
    CHECK(s.run_string("assert(counter == 200, 'got ' .. counter)"));
}
