#include "engine/core/diagnostics.hpp"
#include "engine/core/scripting.hpp"
#include "engine/pnc/dialog.hpp"
#include "pnc/dialog_internal.hpp"

#include <doctest/doctest.h>
#include <sol/sol.hpp>

#include <string>
#include <utility>
#include <vector>

using pac::core::Diagnostics;
using pac::core::LogLevel;
using pac::core::Scripting;
using pac::pnc::DialogHost;
using pac::pnc::DialogInternal;
using pac::pnc::DialogOption;
using pac::pnc::DialogRuntime;

namespace {

/// A capture host that records what the dialog asked us to speak and lets the
/// test simulate the bubble clearing by toggling `speaking`.
struct TestHost {
    std::vector<std::string> npc;
    std::vector<std::string> player;
    bool speaking = false;

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
        return h;
    }
};

/// Load a dialog table from an inline Lua string. Binds `END` and any extra
/// helpers the dialog might reference (kept minimal — tests should not need a
/// full RoomScene API surface).
sol::table load_tree(Scripting& s, Diagnostics& log, const std::string& src) {
    sol::state& L = s.lua();
    L["END"] = std::string("__END__");
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
    return *t;
}

DialogRuntime build(Scripting& s, Diagnostics& log, TestHost& host, sol::table tree) {
    return DialogInternal::from_table(s, log, "test", host.host(), std::move(tree));
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
    sol::table tree = load_tree(s, log, R"lua(
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
    sol::table tree = load_tree(s, log, R"lua(
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
    sol::table tree = load_tree(s, log, R"lua(
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
    sol::table tree = load_tree(s, log, R"lua(
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

TEST_CASE("dialog with a missing node id ends cleanly instead of crashing") {
    Diagnostics log = quiet();
    Scripting s(log);
    TestHost host;
    sol::table tree = load_tree(s, log, R"lua(
        return {
            start = "ghost",
            other = { npc = "unused", to = END },
        }
    )lua");
    DialogRuntime d = build(s, log, host, tree);
    CHECK(d.ended());
    CHECK(host.npc.empty());
}
