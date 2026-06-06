#include "engine/core/diagnostics.hpp"
#include "engine/core/resource_cache.hpp"
#include "engine/core/resource_source.hpp"
#include "engine/core/scripting.hpp"
#include "engine/core/state_store.hpp"
#include "engine/pnc/dialog.hpp"
#include "pnc/dialog_internal.hpp"

#include <doctest/doctest.h>
#include <sol/sol.hpp>

#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <utility>
#include <variant>
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
    // Records what the dialog asked us to anchor NPC speech to, and the npc
    // count at the moment it was set (to assert it lands before the first line).
    std::string anchor;
    std::size_t anchor_set_before_npc = 0;

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
        h.set_text_anchor = [this](const std::string& point_name) {
            anchor = point_name;
            anchor_set_before_npc = npc.size();
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

TEST_CASE("options() is empty while a line is being spoken (panel hides during speech)") {
    Diagnostics log = quiet();
    Scripting s(log);
    TestHost host;
    LoadedTree tree = load_tree(s, log, R"lua(
        return {
            start = "greet",
            greet = {
                npc = "Hola.",
                options = {
                    { "Contame.", to = "greet" },
                    { "Chau.", to = END },
                },
            },
        }
    )lua");
    DialogRuntime d = build(s, log, host, tree);

    // While the NPC line is on screen, no options are offered.
    CHECK(d.state() == DialogRuntime::State::SPEAKING_NPC);
    CHECK(d.options().empty());

    // Bubble clears -> options become available.
    host.speaking = false;
    d.update();
    CHECK(d.state() == DialogRuntime::State::AWAITING_CHOICE);
    CHECK(d.options().size() == 2);

    // Choosing speaks the player line; options vanish again until the next node
    // settles into AWAITING_CHOICE (this is what makes the panel disappear while
    // the avatar talks).
    d.choose(0);
    CHECK(d.state() == DialogRuntime::State::SPEAKING_PLAYER);
    CHECK(d.options().empty());
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

TEST_CASE("a node whose options all become hidden ends the dialog (runs dry)") {
    // Backs design 04 §Evolving dialogs: an options node has no `to`, so once
    // every option is consumed/filtered the conversation ends on its own.
    Diagnostics log = quiet();
    Scripting s(log);
    TestHost host;
    LoadedTree tree = load_tree(s, log, R"lua(
        return {
            start = "n",
            n = {
                npc = "X",
                options = {
                    { "a", to = "n", once = true },
                    { "b", to = "n", once = true },
                },
            },
        }
    )lua");
    DialogRuntime d = build(s, log, host, tree);

    // npc spoken -> options.
    host.speaking = false;
    d.update();
    REQUIRE(d.state() == DialogRuntime::State::AWAITING_CHOICE);
    REQUIRE(d.options().size() == 2);

    // Consume "a": player line, then back to node 'n', npc spoken again.
    d.choose(0);
    host.speaking = false;
    d.update(); // run/consume, follow to 'n', SPEAKING_NPC
    host.speaking = false;
    d.update(); // AWAITING_CHOICE with only "b" left
    REQUIRE(d.state() == DialogRuntime::State::AWAITING_CHOICE);
    REQUIRE(d.options().size() == 1);
    CHECK(d.options()[0].text == "b");

    // Consume "b": node 'n' now has no visible options and no `to`.
    d.choose(0);
    host.speaking = false;
    d.update(); // run/consume, follow to 'n', SPEAKING_NPC ("X")
    CHECK_FALSE(d.ended());
    host.speaking = false;
    d.update(); // after_npc: no options, no `to` -> end
    CHECK(d.ended());
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

TEST_CASE("dialog text_anchor is read and handed to the host before the first line") {
    Diagnostics log = quiet();
    Scripting s(log);
    TestHost host;
    LoadedTree tree = load_tree(s, log, R"lua(
        return {
            text_anchor = "skull_talk_spot",
            start = "n",
            n = { npc = "Boo.", options = { { "Bye.", to = END } } },
        }
    )lua");
    DialogRuntime d = build(s, log, host, tree);
    CHECK(d.text_anchor() == "skull_talk_spot");
    CHECK(host.anchor == "skull_talk_spot");
    // The anchor must be set before any NPC line is spoken so even the opening
    // bubble is placed correctly.
    CHECK(host.anchor_set_before_npc == 0);
    REQUIRE(host.npc.size() == 1);
}

TEST_CASE("dialog without text_anchor leaves it empty and never calls the host") {
    Diagnostics log = quiet();
    Scripting s(log);
    TestHost host;
    LoadedTree tree = load_tree(s, log, R"lua(
        return {
            start = "n",
            n = { npc = "Hi.", options = { { "Bye.", to = END } } },
        }
    )lua");
    DialogRuntime d = build(s, log, host, tree);
    CHECK(d.text_anchor().empty());
    CHECK(host.anchor.empty());
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

// M9 #187: the `dialog { ... }` / `topic` sugar. Goes through the real
// DialogRuntime::start (which defines the sugar prelude), so it exercises the
// expansion + the validator + the runtime together. Binds minimal get_state /
// set_state over a StateStore (the sugar's only engine dependency).
namespace {
void bind_state(Scripting& s, pac::core::StateStore& store) {
    sol::state& L = s.lua();
    L.set_function("get_state", [&store, &L](const std::string& key) -> sol::object {
        const auto v = store.get(key);
        if (!v) {
            return sol::make_object(L, sol::lua_nil);
        }
        return std::visit([&L](const auto& x) { return sol::make_object(L, x); }, *v);
    });
    L.set_function("set_state", [&store](const std::string& key, sol::object value) {
        if (value.is<bool>()) {
            store.set(key, value.as<bool>());
        } else if (value.is<double>()) {
            store.set(key, value.as<double>());
        } else if (value.is<std::string>()) {
            store.set(key, value.as<std::string>());
        }
    });
}

bool spoke(const TestHost& host, const std::string& line) {
    for (const std::string& s : host.npc) {
        if (s == line) {
            return true;
        }
    }
    return false;
}

bool has_option(const DialogRuntime& d, const std::string& text) {
    for (const DialogOption& o : d.options()) {
        if (o.text == text) {
            return true;
        }
    }
    return false;
}

// Clear the speech bubble and pump update() until the dialog awaits a choice or
// ends — the test's stand-in for the player clicking through the spoken lines.
void advance_to_choice(DialogRuntime& d, TestHost& host) {
    for (int i = 0; i < 50; ++i) {
        if (d.state() == DialogRuntime::State::AWAITING_CHOICE || d.ended()) {
            return;
        }
        host.speaking = false;
        d.update();
    }
    FAIL("dialog did not settle on a choice");
}

// Pick the visible option whose text matches (the topic constructor's generated
// options are addressed by the player line, like any other).
void choose_text(DialogRuntime& d, const std::string& text) {
    const std::vector<DialogOption> opts = d.options();
    for (std::size_t i = 0; i < opts.size(); ++i) {
        if (opts[i].text == text) {
            d.choose(static_cast<int>(i));
            return;
        }
    }
    FAIL("no visible option with text: " << text);
}
} // namespace

TEST_CASE("dialog topic{} expands with requires / after gating, once-per-claim, uttered()") {
    namespace fs = std::filesystem;
    Diagnostics log = quiet();
    Scripting s(log);
    pac::core::StateStore state;
    bind_state(s, state);

    const fs::path root = fs::temp_directory_path() / "pac_dialog_topic_test";
    fs::remove_all(root);
    fs::create_directories(root / "dialogs");
    std::ofstream(root / "dialogs" / "schneider.lua") << R"LUA(
        -- cross-topic predicate built on the uttered() helper
        local function basics() return uttered("frac") and uttered("cuts") end
        return dialog {
          start = "hub",
          hub = {
            npc = "Que puede sostener?",
            topics = {
              topic "frac" { requires = "finding.frac",
                             player = "Fracturas radiales.", npc = { "Bien.", "Util." } },
              topic "cuts" { requires = "finding.cuts",
                             player = "Sin marcas de corte.", npc = "Correcto." },
              topic "discard" { after = "cuts",
                                player = "El filo no explica el patron.", npc = "De acuerdo." },
            },
            options = {
              { "Fue asesinato.", when = basics, to = "overstated" },
              { "Despues seguimos.", to = END },
            },
          },
          overstated = { npc = "No. Demasiado comodo.", to = "hub" },
        }
    )LUA";

    pac::core::FilesystemResourceSource source(root.string());
    pac::core::ResourceCache resources(source, log);
    TestHost host;

    // Both findings discovered up front, so frac + cuts are offerable from the
    // first hub; discard / overstated still gated on topics being uttered.
    state.set("finding.frac", true);
    state.set("finding.cuts", true);

    auto dopt = DialogRuntime::start(s, resources, log, "schneider", host.host());
    REQUIRE(dopt.has_value());
    DialogRuntime& d = *dopt;
    advance_to_choice(d, host);

    // requires holds for frac/cuts; discard hidden (cuts not uttered); overstated
    // hidden (basics() false). The raw exit is always there.
    CHECK(has_option(d, "Fracturas radiales."));
    CHECK(has_option(d, "Sin marcas de corte."));
    CHECK_FALSE(has_option(d, "El filo no explica el patron."));
    CHECK_FALSE(has_option(d, "Fue asesinato."));
    CHECK(has_option(d, "Despues seguimos."));

    // State the "cuts" claim. The generated option speaks the player line, marks
    // the topic uttered, plays the response node, and routes back to the hub.
    choose_text(d, "Sin marcas de corte.");
    advance_to_choice(d, host);
    CHECK(state.get("__uttered.cuts") == pac::core::StateValue{true});
    CHECK(host.player.back() == "Sin marcas de corte.");
    CHECK(spoke(host, "Correcto."));  // the generated response node was played
    CHECK(d.current_node() == "hub"); // ...and routed back to the hub

    // cuts is now consumed (offered once); discard unlocks (after = "cuts").
    CHECK_FALSE(has_option(d, "Sin marcas de corte."));
    CHECK(has_option(d, "El filo no explica el patron."));
    CHECK(has_option(d, "Fracturas radiales."));  // still available
    CHECK_FALSE(has_option(d, "Fue asesinato.")); // basics() needs frac too

    // State "frac" -> basics() now true, so the raw overstated option appears.
    choose_text(d, "Fracturas radiales.");
    advance_to_choice(d, host);
    CHECK(state.get("__uttered.frac") == pac::core::StateValue{true});
    CHECK(has_option(d, "Fue asesinato."));

    fs::remove_all(root);
}
