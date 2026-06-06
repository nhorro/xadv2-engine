#include "engine/core/diagnostics.hpp"
#include "engine/core/facts.hpp"
#include "engine/core/lua_api.hpp"
#include "engine/core/scripting.hpp"
#include "engine/core/state_store.hpp"

#include <doctest/doctest.h>
#include <sol/sol.hpp>

#include <string>
#include <variant>
#include <vector>

using pac::core::Diagnostics;
using pac::core::FactsError;
using pac::core::FactsRegistry;
using pac::core::LogLevel;
using pac::core::Scripting;
using pac::core::StateStore;
using pac::core::StateValue;

namespace {

Diagnostics quiet() {
    return Diagnostics(LogLevel::ERROR);
}

// Minimal get_state/set_state over a StateStore — the only globals the facts
// proxy depends on (the full engine binding is in bind_core_api).
void bind_state(Scripting& s, StateStore& store) {
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

const char* kFacts = R"YAML(
version: 1
namespaces:
  act1: [bones_glanced, context_glanced]
  finding: [radial_fractures]
)YAML";

} // namespace

TEST_CASE("FactsRegistry::parse builds the declared key + namespace sets") {
    const FactsRegistry reg = FactsRegistry::parse(kFacts);
    CHECK_FALSE(reg.empty());
    CHECK(reg.has_namespace("act1"));
    CHECK(reg.has_namespace("finding"));
    CHECK_FALSE(reg.has_namespace("nope"));
    CHECK(reg.is_declared("act1.bones_glanced"));
    CHECK(reg.is_declared("finding.radial_fractures"));
    CHECK_FALSE(reg.is_declared("act1.bons_glanced"));
}

TEST_CASE("FactsRegistry::parse rejects malformed documents") {
    CHECK_THROWS_AS(FactsRegistry::parse("version: 1\n"), FactsError);         // no namespaces
    CHECK_THROWS_AS(FactsRegistry::parse("namespaces: [a, b]\n"), FactsError); // not a map
    CHECK_THROWS_AS(FactsRegistry::parse("namespaces:\n  act1: true\n"),
                    FactsError); // ns not a seq
    CHECK_THROWS_AS(FactsRegistry::parse("namespaces:\n  act1: [x, x]\n"),
                    FactsError); // duplicate key
}

TEST_CASE("facts.<ns>.<name> round-trips through state and never warns for declared keys") {
    Diagnostics log = quiet();
    Scripting s(log);
    StateStore state;
    bind_state(s, state);
    std::vector<std::string> warnings;
    const FactsRegistry reg = FactsRegistry::parse(kFacts);
    pac::core::bind_facts(s, reg, /*dev_warn=*/true, [&](const std::string& m) {
        warnings.push_back(m);
    });

    // Unset declared fact reads as false.
    REQUIRE(s.run_string("r = facts.act1.bones_glanced"));
    CHECK(s.lua()["r"].get<bool>() == false);

    // Assignment routes through set_state -> the dotted key is what persists.
    REQUIRE(s.run_string("facts.act1.bones_glanced = true"));
    CHECK(state.get("act1.bones_glanced") == StateValue{true});

    // ...and reads back through the proxy and through plain get_state alike.
    REQUIRE(s.run_string("r = facts.act1.bones_glanced"));
    CHECK(s.lua()["r"].get<bool>() == true);
    REQUIRE(s.run_string("r = (get_state('act1.bones_glanced') == true)"));
    CHECK(s.lua()["r"].get<bool>() == true);

    CHECK(warnings.empty());
}

TEST_CASE("an undeclared fact warns in dev but still reads/writes") {
    Diagnostics log = quiet();
    Scripting s(log);
    StateStore state;
    bind_state(s, state);
    std::vector<std::string> warnings;
    const FactsRegistry reg = FactsRegistry::parse(kFacts);
    pac::core::bind_facts(s, reg, /*dev_warn=*/true, [&](const std::string& m) {
        warnings.push_back(m);
    });

    // A typo in a declared namespace: warns, and the read still returns false.
    REQUIRE(s.run_string("r = facts.act1.bons_glanced"));
    CHECK(s.lua()["r"].get<bool>() == false);
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0].find("act1.bons_glanced") != std::string::npos);

    // Writing an undeclared key still persists (release never blocks on it).
    warnings.clear();
    REQUIRE(s.run_string("facts.act1.bons_glanced = true"));
    CHECK(state.get("act1.bons_glanced") == StateValue{true});
    CHECK(warnings.size() == 1);

    // An undeclared namespace warns too.
    warnings.clear();
    REQUIRE(s.run_string("r = facts.nope.foo"));
    CHECK(s.lua()["r"].get<bool>() == false);
    CHECK(warnings.size() >= 1);
}

TEST_CASE("the typo guard is silent outside development builds") {
    Diagnostics log = quiet();
    Scripting s(log);
    StateStore state;
    bind_state(s, state);
    std::vector<std::string> warnings;
    const FactsRegistry reg = FactsRegistry::parse(kFacts);
    pac::core::bind_facts(s, reg, /*dev_warn=*/false, [&](const std::string& m) {
        warnings.push_back(m);
    });

    REQUIRE(s.run_string("facts.act1.bons_glanced = true; r = facts.act1.bons_glanced"));
    CHECK(s.lua()["r"].get<bool>() == true);
    CHECK(warnings.empty()); // dev off -> no diagnostics, behaviour unchanged
}

TEST_CASE("an empty registry (no facts.yaml) is plain state sugar with the guard off") {
    Diagnostics log = quiet();
    Scripting s(log);
    StateStore state;
    bind_state(s, state);
    std::vector<std::string> warnings;
    const FactsRegistry empty; // no facts.yaml declared
    pac::core::bind_facts(s, empty, /*dev_warn=*/true, [&](const std::string& m) {
        warnings.push_back(m);
    });

    REQUIRE(s.run_string("facts.whatever.x = true; r = facts.whatever.x"));
    CHECK(s.lua()["r"].get<bool>() == true);
    CHECK(state.get("whatever.x") == StateValue{true});
    CHECK(warnings.empty()); // nothing declared -> nothing to guard against
}
