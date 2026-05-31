#include "engine/core/state_store.hpp"

#include <doctest/doctest.h>

#include <string>
#include <variant>

using namespace pac::core;

TEST_CASE("StateStore set / get / has / overwrite / clear") {
    StateStore s;
    CHECK_FALSE(s.has("a"));
    CHECK_FALSE(s.get("a").has_value());

    s.set("a", true);
    s.set("hp", 5.0);
    s.set("name", std::string("julia"));
    CHECK(s.has("a"));
    CHECK(s.size() == 3);

    REQUIRE(s.get("a").has_value());
    CHECK(std::get<bool>(*s.get("a")) == true);
    CHECK(std::get<double>(*s.get("hp")) == doctest::Approx(5.0));
    CHECK(std::get<std::string>(*s.get("name")) == "julia");

    s.set("hp", 10.0); // overwrite
    CHECK(std::get<double>(*s.get("hp")) == doctest::Approx(10.0));
    CHECK(s.size() == 3);

    s.clear();
    CHECK(s.size() == 0);
    CHECK_FALSE(s.has("a"));
}

TEST_CASE("StateStore erases individual keys and namespaces") {
    StateStore s;
    s.set("notebook.evidence.a.discovered", true);
    s.set("notebook.evidence.b.discovered", true);
    s.set("notebook.hypothesis.a.conclusion", std::string("x"));
    s.set("other", true);

    CHECK(s.erase("notebook.evidence.a.discovered"));
    CHECK_FALSE(s.erase("notebook.evidence.missing"));
    CHECK(s.erase_prefix("notebook.evidence.") == 1);
    CHECK(s.erase_prefix("notebook.") == 1);
    CHECK(s.size() == 1);
    CHECK(s.has("other"));
}
