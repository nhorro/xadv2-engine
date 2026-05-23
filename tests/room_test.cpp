#include "engine/pnc/data_error.hpp"
#include "engine/pnc/room.hpp"
#include "engine/pnc/room_runtime.hpp"

#include <doctest/doctest.h>

#include <optional>
#include <string>

using namespace pac::pnc;

namespace {

const char* kRoom = R"YAML(
version: 1
id: study
background:
  color: { r: 10, g: 12, b: 14 }
  layers:
    - { id: room, image: a/bg.png, z: 0 }
walkable:
  - { x: 0,    y: 400 }
  - { x: 1280, y: 400 }
  - { x: 1280, y: 700 }
  - { x: 0,    y: 700 }
obstacles:
  - [ {x: 600, y: 500}, {x: 700, y: 500}, {x: 700, y: 600}, {x: 600, y: 600} ]
points:
  player_start: { x: 200, y: 650 }
  at_door: { x: 170, y: 560 }
hotspots:
  door:
    name: "la puerta"
    area: [ {x: 100, y: 100}, {x: 240, y: 100}, {x: 240, y: 400}, {x: 100, y: 400} ]
    approach: at_door
    affordances: [ look_at, open ]
avatars:
  - { id: julia, start: player_start, player: true }
)YAML";

} // namespace

TEST_CASE("parse_room reads layout, points, hotspots, and avatars") {
    const RoomData r = parse_room(kRoom);
    CHECK(r.id == "study");
    REQUIRE(r.layers.size() == 1);
    CHECK(r.layers[0].image == "a/bg.png");
    CHECK(r.layers[0].z == doctest::Approx(0.0f));
    CHECK(r.walkable.size() == 4);
    CHECK(r.obstacles.size() == 1);

    REQUIRE(r.point("player_start") != nullptr);
    CHECK(r.point("player_start")->x == doctest::Approx(200.0f));
    CHECK(r.point("nope") == nullptr);

    REQUIRE(r.hotspots.count("door") == 1);
    const RoomHotspot& door = r.hotspots.at("door");
    CHECK(door.name == "la puerta");
    CHECK(door.area.size() == 4);
    REQUIRE(door.approach.has_value());
    CHECK(door.approach->x == doctest::Approx(170.0f)); // resolved from points
    CHECK(door.affordances.size() == 2);

    REQUIRE(r.avatars.size() == 1);
    CHECK(r.avatars[0].player == true);
    CHECK(r.avatars[0].start == "player_start");
}

TEST_CASE("parse_room reads per-layer origins (native-size layers)") {
    const char* yaml = R"YAML(
id: r
background:
  layers:
    - { id: sky,      image: c/sky.png,  z: 0 }
    - { id: building, image: c/bld.png,  z: 1, origin: { x: 0, y: 0 } }
    - { id: flag,     image: c/flag.png, z: 9, origin: { x: 760, y: 40 } }
)YAML";
    const RoomData r = parse_room(yaml);
    REQUIRE(r.layers.size() == 3);
    CHECK(r.layers[0].origin.x == doctest::Approx(0.0f)); // omitted -> world origin
    CHECK(r.layers[0].origin.y == doctest::Approx(0.0f));
    CHECK(r.layers[1].origin.x == doctest::Approx(0.0f));
    CHECK(r.layers[2].origin.x == doctest::Approx(760.0f));
    CHECK(r.layers[2].origin.y == doctest::Approx(40.0f));
}

TEST_CASE("parse_room reads per-layer visibility (default true)") {
    const char* yaml = R"YAML(
id: r
background:
  layers:
    - { id: sky,  image: c/sky.png,  z: 0 }
    - { id: cart, image: c/cart.png, z: 9, visible: false }
)YAML";
    const RoomData r = parse_room(yaml);
    REQUIRE(r.layers.size() == 2);
    CHECK(r.layers[0].visible == true); // omitted -> visible
    CHECK(r.layers[1].visible == false);
}

TEST_CASE("RoomRuntime seeds and toggles layer visibility") {
    const char* yaml = R"YAML(
id: r
background:
  layers:
    - { id: sky,  image: c/sky.png,  z: 0 }
    - { id: cart, image: c/cart.png, z: 9, visible: false }
)YAML";
    RoomRuntime room(parse_room(yaml));
    CHECK(room.layer_visible("sky") == true);     // seeded from YAML default
    CHECK(room.layer_visible("cart") == false);   // seeded from YAML
    CHECK(room.layer_visible("unknown") == true); // unknown id defaults visible

    room.set_layer_visible("cart", true);
    CHECK(room.layer_visible("cart") == true);
    room.set_layer_visible("sky", false);
    CHECK(room.layer_visible("sky") == false);
}

TEST_CASE("is_walkable respects the walkable area and obstacles") {
    const RoomData r = parse_room(kRoom);
    CHECK(r.is_walkable({200, 650}));       // on the floor
    CHECK_FALSE(r.is_walkable({200, 200})); // above the floor
    CHECK_FALSE(r.is_walkable({650, 550})); // inside the obstacle
}

TEST_CASE("parse_room rejects malformed rooms") {
    CHECK_THROWS_AS(parse_room("version: 1\n"), DataError); // no id
    CHECK_THROWS_AS(parse_room("id: x\nhotspots:\n  h: { name: n }\n"),
                    DataError); // hotspot without area or bind
}

TEST_CASE("parse_room reads region states and the optional 'over' layer pin") {
    const char* yaml = R"YAML(
id: r
background:
  layers:
    - { id: bg, image: a/bg.png, z: 0 }
    - { id: fg, image: a/fg.png, z: 50 }
regions:
  drawer:
    area: [ {x: 0, y: 0}, {x: 10, y: 0}, {x: 10, y: 10}, {x: 0, y: 10} ]
    over: fg
    states: { shut: a/shut.png, open: a/open.png }
    initial: shut
)YAML";
    const RoomData r = parse_room(yaml);
    REQUIRE(r.regions.count("drawer") == 1);
    const Region& d = r.regions.at("drawer");
    CHECK(d.over == "fg");
    CHECK(d.states.size() == 2);
    CHECK(d.initial == "shut");
}

TEST_CASE("hotspot_at hits a bind:region hotspot via the region polygon") {
    const char* yaml = R"YAML(
id: r
regions:
  shelf:
    area: [ {x: 100, y: 100}, {x: 200, y: 100}, {x: 200, y: 200}, {x: 100, y: 200} ]
    states: { only: a/shelf.png }
hotspots:
  shelf_hs: { name: "estante", bind: "region:shelf" }
)YAML";
    RoomRuntime room(parse_room(yaml));
    const RoomHotspot* hit = room.hotspot_at({150, 150});
    REQUIRE(hit != nullptr);
    CHECK(hit->id == "shelf_hs");
    CHECK(room.hotspot_at({10, 10}) == nullptr); // outside the bound region
}

TEST_CASE("hotspot_at hits a bind:object hotspot via supplied frame bounds") {
    const char* yaml = R"YAML(
id: r
objects:
  vase: { image: a/vase.png, position: { x: 300, y: 300 } }
hotspots:
  vase_hs: { name: "jarron", bind: "object:vase" }
)YAML";
    RoomRuntime room(parse_room(yaml));
    const auto bounds = [](const std::string& id) -> std::optional<sf::FloatRect> {
        if (id == "vase") {
            return sf::FloatRect(300.0f, 300.0f, 50.0f, 80.0f);
        }
        return std::nullopt;
    };
    const RoomHotspot* hit = room.hotspot_at({320, 350}, bounds);
    REQUIRE(hit != nullptr);
    CHECK(hit->id == "vase_hs");
    CHECK(room.hotspot_at({100, 100}, bounds) == nullptr); // outside the frame
    CHECK(room.hotspot_at({320, 350}) == nullptr);         // headless overload skips object bind
}
