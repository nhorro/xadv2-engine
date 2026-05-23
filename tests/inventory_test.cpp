#include "engine/pnc/data_error.hpp"
#include "engine/pnc/inventory.hpp"
#include "loader_diag.hpp"

#include <doctest/doctest.h>

using namespace pac::pnc;
using pac::test::error_code;

namespace {
const char* kInventory = R"YAML(
items:
  key:
    name: "la llave"
    affordances: [ look_at, use ]
    combinable: true
  map:
    name: "el mapa"
    description: "Un mapa viejo."
    affordances: [ look_at, give ]
)YAML";
} // namespace

TEST_CASE("parse_inventory reads item definitions") {
    const auto items = parse_inventory(kInventory);
    REQUIRE(items.count("key") == 1);
    CHECK(items.at("key").name == "la llave");
    CHECK(items.at("key").combinable == true);
    CHECK(items.at("key").affordances.size() == 2);
    CHECK(items.at("map").description == "Un mapa viejo.");
    CHECK(items.at("map").combinable == false);
}

TEST_CASE("parse_inventory rejects a missing items map") {
    CHECK_THROWS_AS(parse_inventory("nope: 1\n"), DataError);
    CHECK(error_code([] { parse_inventory("nope: 1\n"); }) == "inventory.items-not-map");
}

TEST_CASE("InventoryModel add / remove / has / list / item") {
    InventoryModel inv;
    inv.set_definitions(parse_inventory(kInventory));

    CHECK_FALSE(inv.has("key"));
    inv.add("key");
    inv.add("map");
    inv.add("key"); // no duplicate
    CHECK(inv.has("key"));
    CHECK(inv.list().size() == 2);
    CHECK(inv.list()[0] == "key"); // insertion order
    CHECK(inv.list()[1] == "map");

    REQUIRE(inv.item("key") != nullptr);
    CHECK(inv.item("key")->combinable == true);
    CHECK(inv.item("missing") == nullptr);

    inv.remove("key");
    CHECK_FALSE(inv.has("key"));
    CHECK(inv.list().size() == 1);
}
