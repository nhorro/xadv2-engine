#include "engine/core/diagnostics.hpp"
#include "engine/core/scripting.hpp"
#include "engine/pnc/closeup_runtime.hpp"

#include <doctest/doctest.h>

using namespace pac::core;
using namespace pac::pnc;

namespace {
// ERROR level keeps normal output quiet; the bad-shape test still logs its
// (expected) error.
Diagnostics quiet() {
    return Diagnostics(LogLevel::ERROR);
}
} // namespace

TEST_CASE("close-up runtime: loads, reports hotspots, runs hooks and a handler") {
    Diagnostics log = quiet();
    Scripting s(log);
    // Pure-Lua observation helpers, so the test needs no sol2/C++ bindings.
    REQUIRE(s.run_string("counter = {}; function bump(k) counter[k] = (counter[k] or 0) + 1 end"));

    const char* sidecar = R"LUA(
return {
  on_enter = function() bump("enter") end,
  on_exit  = function() bump("exit") end,
  hotspots = {
    skull = function() bump("skull") end,
  },
}
)LUA";

    CloseUpRuntime rt;
    REQUIRE(rt.load(s, sidecar, "closeups/test.lua", log));
    CHECK(rt.valid());
    CHECK(rt.has_hotspot("skull"));
    CHECK_FALSE(rt.has_hotspot("missing"));

    // on_enter / on_exit are direct calls.
    rt.run_on_enter();
    CHECK(s.run_string("assert(counter.enter == 1)"));

    // A hotspot handler runs as a spawned coroutine task in the given scope.
    const ScopeId scope = s.open_scope();
    const TaskId tid = rt.spawn_hotspot(s, scope, "skull");
    CHECK(tid != 0);
    s.update(0.0f); // resume the spawned handler to completion
    CHECK(s.run_string("assert(counter.skull == 1)"));

    rt.run_on_exit();
    CHECK(s.run_string("assert(counter.exit == 1)"));
}

TEST_CASE("close-up runtime: a multi-step handler blocks on talk between lines") {
    Diagnostics log = quiet();
    Scripting s(log);
    REQUIRE(s.run_string("steps = 0"));
    // Stand-in for the close-up-local talk: yields on a per-line event the test
    // emits, exactly like CloseUpScene drives pending_speech_.
    REQUIRE(s.run_string("pending = nil\n"
                         "function talk(_, _) steps = steps + 1; pending = 'line'; "
                         "wait_event('line') end"));

    const char* sidecar = R"LUA(
return { hotspots = { sign = function()
  talk("npc", "uno")
  talk("npc", "dos")
end } }
)LUA";

    CloseUpRuntime rt;
    REQUIRE(rt.load(s, sidecar, "closeups/multi.lua", log));
    const ScopeId scope = s.open_scope();
    rt.spawn_hotspot(s, scope, "sign");

    s.update(0.0f); // runs to the first talk, then blocks
    CHECK(s.run_string("assert(steps == 1)"));
    s.emit(scope, "line"); // "dismiss" line 1
    s.update(0.0f);        // resumes -> second talk, blocks again
    CHECK(s.run_string("assert(steps == 2)"));
    s.emit(scope, "line"); // dismiss line 2
    s.update(0.0f);        // handler returns -> task done
    CHECK(s.active_task_count(scope) == 0);
}

TEST_CASE("close-up runtime: empty and malformed sidecars are handled") {
    Diagnostics log = quiet();
    Scripting s(log);

    CloseUpRuntime empty;
    REQUIRE(empty.load(s, "return {}", "closeups/empty.lua", log));
    CHECK(empty.valid());
    CHECK_FALSE(empty.has_hotspot("x"));
    empty.run_on_enter(); // no hooks -> no-op, no crash
    empty.run_on_exit();
    CHECK(empty.spawn_hotspot(s, s.open_scope(), "x") == 0);

    CloseUpRuntime bad;
    CHECK_FALSE(bad.load(s, "return { hotspots = 5 }", "closeups/bad.lua", log)); // wrong type
    CHECK_FALSE(bad.valid());

    CloseUpRuntime not_table;
    CHECK_FALSE(not_table.load(s, "return 42", "closeups/nt.lua", log));
}
