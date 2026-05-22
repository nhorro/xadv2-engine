#include "engine/core/diagnostics.hpp"
#include "engine/core/scripting.hpp"
#include "engine/pnc/dialog.hpp"
#include "pnc/dialog_internal.hpp"

#include <doctest/doctest.h>
#include <sol/sol.hpp>

#include <set>
#include <string>
#include <utility>
#include <vector>

using pac::core::Diagnostics;
using pac::core::LogLevel;
using pac::core::ScopeId;
using pac::core::Scripting;
using pac::core::TaskId;
using pac::pnc::DialogHost;
using pac::pnc::DialogInternal;
using pac::pnc::DialogOption;
using pac::pnc::DialogRunFn;
using pac::pnc::DialogRuntime;

namespace {

/// A capture host that records what the dialog asked us to speak and lets the
/// test simulate the bubble clearing by toggling `speaking`. `consumed` backs
/// the `once`-flag callbacks the runtime calls into — in production the host
/// stores these in StateStore so they survive across dialog sessions and
/// save/load.
struct TestHost {
    std::vector<std::string> npc;
    std::vector<std::string> player;
    bool speaking = false;
    std::set<std::pair<std::string, int>> consumed;

    DialogHost host() {
        DialogHost h;
        h.speak_npc = [this](const std::string& t) {
            npc.push_back(t);
            speaking = true;
        };
        h.speak_player = [this](const std::string& t) {
            player.push_back(t);
            speaking = true;
        };
        h.is_speaking = [this]() { return speaking; };
        h.is_option_consumed = [this](const std::string& node, int idx) {
            return consumed.count({node, idx}) > 0;
        };
        h.mark_option_consumed = [this](const std::string& node, int idx) {
            consumed.insert({node, idx});
        };
        return h;
    }
};

/// A loaded dialog tree paired with its unique END sentinel. Mirrors the
/// hand-off engine code does: \`DialogRuntime::start\` injects a fresh sentinel
/// table per dialog; \`DialogInternal::from_table\` stores it for runtime
/// identity comparisons.
struct LoadedTree {
    sol::table tree;
    sol::table end_sentinel;
};

LoadedTree load_tree(Scripting& s, Diagnostics& log, const std::string& src) {
    sol::state& L = s.lua();
    sol::table end_sentinel = L.create_table();
    L["END"] = end_sentinel;
    sol::load_result chunk = L.load(src, "@dialog_test");
    REQUIRE(chunk.valid());
    const sol::protected_function_result r = sol::protected_function(chunk)();
    if (!r.valid()) {
        const sol::error e = r;
        log.error(std::string("dialog_test load: ") + e.what());
        FAIL("dialog tree failed to load");
    }
    sol::optional<sol::table> t = r;
    REQUIRE(t.has_value());
    return {*t, end_sentinel};
}

DialogRuntime build(Scripting& s, Diagnostics& log, TestHost& host, LoadedTree lt) {
    return DialogInternal::from_table(s,
                                      log,
                                      "test",
                                      host.host(),
                                      std::move(lt.tree),
                                      lt.end_sentinel);
}

Diagnostics quiet() {
    return Diagnostics(LogLevel::ERROR);
}

} // namespace

TEST_CASE("dialog speaks NPC line, takes a choice, runs `run`, follows `to`, ends") {
    Diagnostics log = quiet();
    Scripting s(log);
    s.lua()["counter"] = 0;
    TestHost host;
    LoadedTree tree = load_tree(s, log, R"lua(
        return {
            start = "greet",
            greet = {
                npc = "Hola.",
                options = {
                    { "Contame.", to = "more", run = function() counter = counter + 1 end },
                    { "Chau.", to = END },
                },
            },
            more = {
                npc = "Es una historia larga.",
                to = "greet",
            },
        }
    )lua");
    DialogRuntime d = build(s, log, host, tree);

    // After construction the first NPC line is already spoken.
    CHECK(d.state() == DialogRuntime::State::SPEAKING_NPC);
    REQUIRE(host.npc.size() == 1);
    CHECK(host.npc[0] == "Hola.");

    // Bubble clears -> options shown.
    host.speaking = false;
    d.update();
    CHECK(d.state() == DialogRuntime::State::AWAITING_CHOICE);
    REQUIRE(d.options().size() == 2);
    CHECK(d.options()[0].text == "Contame.");

    // Pick the first option -> speaks the player line.
    d.choose(0);
    CHECK(d.state() == DialogRuntime::State::SPEAKING_PLAYER);
    REQUIRE(host.player.size() == 1);
    CHECK(host.player[0] == "Contame.");

    // Bubble clears -> run executes and we move to `more`.
    host.speaking = false;
    d.update();
    CHECK(s.lua()["counter"].get<int>() == 1);
    CHECK(d.state() == DialogRuntime::State::SPEAKING_NPC);
    CHECK(d.current_node() == "more");
    CHECK(host.npc.back() == "Es una historia larga.");

    // `more` has no options and `to = greet` -> first update follows to greet
    // and speaks "Hola." again (SPEAKING_NPC), next clear -> AWAITING_CHOICE.
    host.speaking = false;
    d.update();
    CHECK(d.current_node() == "greet");
    CHECK(d.state() == DialogRuntime::State::SPEAKING_NPC);
    host.speaking = false;
    d.update();
    CHECK(d.state() == DialogRuntime::State::AWAITING_CHOICE);

    // Pick "Chau." -> SPEAKING_PLAYER, clear -> END.
    d.choose(1);
    CHECK(d.state() == DialogRuntime::State::SPEAKING_PLAYER);
    host.speaking = false;
    d.update();
    CHECK(d.ended());
}

TEST_CASE("multi-line NPC speaks each line in order before showing options") {
    Diagnostics log = quiet();
    Scripting s(log);
    TestHost host;
    LoadedTree tree = load_tree(s, log, R"lua(
        return {
            start = "n",
            n = {
                npc = { "Uno.", "Dos.", "Tres." },
                options = { { "Vale.", to = END } },
            },
        }
    )lua");
    DialogRuntime d = build(s, log, host, tree);
    CHECK(host.npc.size() == 1);
    CHECK(host.npc[0] == "Uno.");

    host.speaking = false;
    d.update();
    CHECK(host.npc.size() == 2);
    CHECK(host.npc[1] == "Dos.");
    CHECK(d.state() == DialogRuntime::State::SPEAKING_NPC);

    host.speaking = false;
    d.update();
    CHECK(host.npc.size() == 3);
    CHECK(host.npc[2] == "Tres.");

    host.speaking = false;
    d.update();
    CHECK(d.state() == DialogRuntime::State::AWAITING_CHOICE);
    REQUIRE(d.options().size() == 1);
}

TEST_CASE("`when` filters options and `once` removes them after first use") {
    Diagnostics log = quiet();
    Scripting s(log);
    s.lua()["unlocked"] = false;
    TestHost host;
    LoadedTree tree = load_tree(s, log, R"lua(
        return {
            start = "n",
            n = {
                npc = "X",
                options = {
                    { "secret", to = "n", when = function() return unlocked end, once = true },
                    { "stay",   to = "n" },
                    { "leave",  to = END },
                },
            },
        }
    )lua");
    DialogRuntime d = build(s, log, host, tree);

    // npc spoken; advance.
    host.speaking = false;
    d.update();
    REQUIRE(d.state() == DialogRuntime::State::AWAITING_CHOICE);

    // 'unlocked' is false -> the gated option is hidden.
    CHECK(d.options().size() == 2);
    CHECK(d.options()[0].text == "stay");
    CHECK(d.options()[1].text == "leave");

    // Unlock and re-enter the node by taking 'stay'.
    s.lua()["unlocked"] = true;
    d.choose(0); // "stay"
    host.speaking = false;
    d.update(); // speaks player "stay"... wait we're at SPEAKING_PLAYER, then run/follow
    host.speaking = false;
    d.update(); // back to node 'n' -> SPEAKING_NPC ("X")
    host.speaking = false;
    d.update(); // AWAITING_CHOICE with the gated option now visible
    REQUIRE(d.state() == DialogRuntime::State::AWAITING_CHOICE);
    REQUIRE(d.options().size() == 3);
    CHECK(d.options()[0].text == "secret");

    // Use 'secret' once -> it disappears even though `when` would still pass.
    d.choose(0);
    host.speaking = false;
    d.update(); // SPEAKING_PLAYER -> run/follow
    host.speaking = false;
    d.update(); // node 'n' again, SPEAKING_NPC
    host.speaking = false;
    d.update(); // AWAITING_CHOICE
    REQUIRE(d.state() == DialogRuntime::State::AWAITING_CHOICE);
    CHECK(d.options().size() == 2);
    CHECK(d.options()[0].text == "stay");
}

TEST_CASE("silent option skips the player bubble but still runs and follows") {
    Diagnostics log = quiet();
    Scripting s(log);
    s.lua()["ran"] = false;
    TestHost host;
    LoadedTree tree = load_tree(s, log, R"lua(
        return {
            start = "n",
            n = {
                npc = "X",
                options = {
                    { "shh", silent = true, run = function() ran = true end, to = END },
                },
            },
        }
    )lua");
    DialogRuntime d = build(s, log, host, tree);
    host.speaking = false;
    d.update(); // AWAITING_CHOICE
    REQUIRE(d.state() == DialogRuntime::State::AWAITING_CHOICE);

    d.choose(0); // silent -> no player bubble
    CHECK(host.player.empty());
    // is_speaking() returns false immediately; next update runs+follows.
    d.update();
    CHECK(s.lua()["ran"].get<bool>() == true);
    CHECK(d.ended());
}

TEST_CASE("`once` persists across dialog sessions via the host-owned store") {
    // Regression: pre-M5b the runtime owned `consumed_once` locally, so options
    // tagged `once` re-appeared the next time the dialog was started (the
    // runtime died on END and lost the set). Externalizing into the host fixes
    // it: a fresh runtime sharing the same backing store sees the consumption.
    Diagnostics log = quiet();
    Scripting s(log);
    TestHost host;
    const std::string src = R"lua(
        return {
            start = "n",
            n = {
                options = {
                    { "burn", once = true, to = END },
                    { "stay", to = END },
                },
            },
        }
    )lua";

    // First session: consume the `once` option.
    {
        LoadedTree tree = load_tree(s, log, src);
        DialogRuntime d = build(s, log, host, tree);
        REQUIRE(d.state() == DialogRuntime::State::AWAITING_CHOICE);
        REQUIRE(d.options().size() == 2);
        d.choose(0);
        host.speaking = false;
        d.update();
        CHECK(d.ended());
    }

    // Second session reuses the same host (same persistent store). The `once`
    // option must not reappear.
    {
        LoadedTree tree = load_tree(s, log, src);
        DialogRuntime d = build(s, log, host, tree);
        REQUIRE(d.state() == DialogRuntime::State::AWAITING_CHOICE);
        REQUIRE(d.options().size() == 1);
        CHECK(d.options()[0].text == "stay");
    }
}

TEST_CASE("DialogInternal::validate accepts a well-formed tree") {
    Diagnostics log = quiet();
    Scripting s(log);
    LoadedTree lt = load_tree(s, log, R"lua(
        return {
            start = "a",
            a = { npc = "hi", options = { { "x", to = END } } },
            b = { npc = "u", to = "a" },
        }
    )lua");
    CHECK(DialogInternal::validate(lt.tree, lt.end_sentinel).empty());
}

TEST_CASE("DialogInternal::validate rejects a node with both options and to") {
    Diagnostics log = quiet();
    Scripting s(log);
    LoadedTree lt = load_tree(s, log, R"lua(
        return {
            start = "a",
            a = { npc = "x", options = { { "x", to = END } }, to = "b" },
            b = { npc = "y", to = END },
        }
    )lua");
    const std::string err = DialogInternal::validate(lt.tree, lt.end_sentinel);
    CHECK(err.find("both") != std::string::npos);
    CHECK(err.find("'a'") != std::string::npos);
}

TEST_CASE("DialogInternal::validate rejects an option whose `to` is nil") {
    Diagnostics log = quiet();
    Scripting s(log);
    // Typo: the dialog author wrote `END_` thinking it was the sentinel.
    // The Lua field becomes nil — exactly what the validator must catch.
    LoadedTree lt = load_tree(s, log, R"lua(
        return {
            start = "a",
            a = { npc = "x", options = { { "x", to = END_ } } },
        }
    )lua");
    const std::string err = DialogInternal::validate(lt.tree, lt.end_sentinel);
    CHECK(err.find("nil") != std::string::npos);
    CHECK(err.find("option 1") != std::string::npos);
}

TEST_CASE("DialogInternal::validate rejects a `to` pointing at a missing node") {
    Diagnostics log = quiet();
    Scripting s(log);
    LoadedTree lt = load_tree(s, log, R"lua(
        return {
            start = "a",
            a = { npc = "x", options = { { "x", to = "ghost" } } },
        }
    )lua");
    const std::string err = DialogInternal::validate(lt.tree, lt.end_sentinel);
    CHECK(err.find("ghost") != std::string::npos);
}

// --- #31 part 2: run-as-coroutine, change_room guard, dialog scope ---

/// Host that runs `spawn_run` as a real coroutine task inside a caller-provided
/// scope. Production RoomScene uses the same pattern: `spawn_run` set
/// current_scope to the dialog scope and let `Scripting::spawn` (via the Lua
/// global) place the task there. Tests inline the spawn through sol::function
/// to avoid pulling in the Lua-global bindings.
struct CoHost {
    Scripting* scripting = nullptr;
    Diagnostics* log = nullptr;
    ScopeId scope = 0;
    TaskId task = 0;
    bool should_end = false;
    std::vector<std::string> npc;
    std::vector<std::string> player;
    bool speaking = false;
    std::set<std::pair<std::string, int>> consumed;

    DialogHost host() {
        DialogHost h;
        h.speak_npc = [this](const std::string& t) {
            npc.push_back(t);
            speaking = true;
        };
        h.speak_player = [this](const std::string& t) {
            player.push_back(t);
            speaking = true;
        };
        h.is_speaking = [this]() { return speaking; };
        h.is_option_consumed = [this](const std::string& node, int idx) {
            return consumed.count({node, idx}) > 0;
        };
        h.mark_option_consumed = [this](const std::string& node, int idx) {
            consumed.insert({node, idx});
        };
        h.spawn_run = [this](DialogRunFn& carrier) {
            sol::state& L = scripting->lua();
            scripting->set_current_scope(scope);
            sol::function spawn_fn = L["spawn"];
            const sol::protected_function_result r = sol::protected_function(spawn_fn)(carrier.fn);
            scripting->set_current_scope(scripting->global_scope());
            REQUIRE(r.valid());
            task = r.get<TaskId>();
        };
        h.is_run_running = [this]() { return task != 0 && scripting->is_task_alive(task); };
        h.should_end = [this]() { return should_end; };
        return h;
    }
};

TEST_CASE("`run` is spawned as a coroutine task so blocking APIs yield") {
    Diagnostics log = quiet();
    Scripting s(log);
    s.lua()["counter"] = 0;
    CoHost host;
    host.scripting = &s;
    host.log = &log;
    host.scope = s.open_scope();
    LoadedTree tree = load_tree(s, log, R"lua(
        return {
            start = "n",
            n = {
                options = {
                    { "go", to = END, run = function()
                        coroutine.yield({ kind = "timer", seconds = 1.0 })
                        counter = counter + 1
                    end },
                },
            },
        }
    )lua");
    DialogRuntime d = DialogInternal::from_table(s,
                                                 log,
                                                 "test",
                                                 host.host(),
                                                 std::move(tree.tree),
                                                 tree.end_sentinel);
    REQUIRE(d.state() == DialogRuntime::State::AWAITING_CHOICE);

    d.choose(0);
    host.speaking = false;
    d.update();
    // The runtime should have entered RUNNING_CALLBACK: the coroutine yielded on
    // the timer and hasn't run the `counter += 1` line yet.
    CHECK(d.state() == DialogRuntime::State::RUNNING_CALLBACK);
    CHECK(s.lua()["counter"].get<int>() == 0);

    // Pump the scheduler: first tick runs the coroutine until the yield (sets
    // timer = 1.0); next tick decrements past 0 and resumes the coroutine to
    // completion.
    s.update(0.1f);
    CHECK(d.state() == DialogRuntime::State::RUNNING_CALLBACK);
    CHECK(s.lua()["counter"].get<int>() == 0);
    s.update(1.5f);
    CHECK(s.lua()["counter"].get<int>() == 1);

    // Now the run task is gone — next dialog update follows `to = END`.
    d.update();
    CHECK(d.ended());
    s.cancel_scope(host.scope);
}

TEST_CASE("`should_end()` after run skips `to` follow (change_room guard)") {
    Diagnostics log = quiet();
    Scripting s(log);
    CoHost host;
    host.scripting = &s;
    host.log = &log;
    host.scope = s.open_scope();
    LoadedTree tree = load_tree(s, log, R"lua(
        return {
            start = "n",
            n = {
                options = {
                    { "leave", to = "should_not_visit", run = function() end },
                },
                -- Intentionally no node 'should_not_visit'; the validator would
                -- catch it, but we want to prove the runtime never tries to
                -- enter it once should_end() flips.
            },
            should_not_visit = { npc = "BUG", to = END },
        }
    )lua");
    DialogRuntime d = DialogInternal::from_table(s,
                                                 log,
                                                 "test",
                                                 host.host(),
                                                 std::move(tree.tree),
                                                 tree.end_sentinel);
    REQUIRE(d.state() == DialogRuntime::State::AWAITING_CHOICE);

    d.choose(0);
    host.speaking = false;
    d.update();
    // Run was empty; coroutine returns immediately. Simulate a `change_room`
    // having been queued inside `run` by flipping should_end before the dialog
    // sees the task complete.
    host.should_end = true;
    s.update(0.016f); // reap the run task
    d.update();
    CHECK(d.ended());
    // We never spoke the "BUG" line — proves `to` was skipped.
    bool spoke_bug = false;
    for (const auto& line : host.npc) {
        if (line == "BUG") {
            spoke_bug = true;
        }
    }
    CHECK(!spoke_bug);
    s.cancel_scope(host.scope);
}

TEST_CASE("the run callback is placed in the dialog scope") {
    // Cancelling the dialog scope must reap the in-flight run task — that's the
    // mechanism backing the design's "spawn from run inherits the dialog scope"
    // rule (the scheduler's `resume()` propagates scope into child spawns).
    Diagnostics log = quiet();
    Scripting s(log);
    CoHost host;
    host.scripting = &s;
    host.log = &log;
    host.scope = s.open_scope();
    LoadedTree tree = load_tree(s, log, R"lua(
        return {
            start = "n",
            n = {
                options = {
                    { "go", to = END, run = function()
                        -- Yield indefinitely on a long timer. The task stays
                        -- alive in the dialog scope until cancelled.
                        coroutine.yield({ kind = "timer", seconds = 1000.0 })
                    end },
                },
            },
        }
    )lua");
    DialogRuntime d = DialogInternal::from_table(s,
                                                 log,
                                                 "test",
                                                 host.host(),
                                                 std::move(tree.tree),
                                                 tree.end_sentinel);
    d.choose(0);
    host.speaking = false;
    d.update();
    s.update(0.016f); // run task resumes once, yields on the long timer
    CHECK(s.active_task_count(host.scope) == 1);

    // Cancelling the dialog scope reaps the run task. Without scope binding it
    // would survive in the global scope (silent leak after room change).
    s.cancel_scope(host.scope);
    CHECK(s.active_task_count(host.scope) == 0);
}

TEST_CASE("dialog with a missing node id ends cleanly instead of crashing") {
    Diagnostics log = quiet();
    Scripting s(log);
    TestHost host;
    LoadedTree tree = load_tree(s, log, R"lua(
        return {
            start = "ghost",
            other = { npc = "unused", to = END },
        }
    )lua");
    DialogRuntime d = build(s, log, host, tree);
    CHECK(d.ended());
    CHECK(host.npc.empty());
}
