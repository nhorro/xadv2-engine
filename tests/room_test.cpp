#include "engine/pnc/data_error.hpp"
#include "engine/pnc/room.hpp"

#include <doctest/doctest.h>

using namespace pac::pnc;

namespace {

const char* kRoom = R"YAML(
version: 1
id: study
size: { width: 1280, height: 720 }
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
    CHECK(r.size.x == 1280u);
    CHECK(r.size.y == 720u);
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

TEST_CASE("is_walkable respects the walkable area and obstacles") {
    const RoomData r = parse_room(kRoom);
    CHECK(r.is_walkable({200, 650}));       // on the floor
    CHECK_FALSE(r.is_walkable({200, 200})); // above the floor
    CHECK_FALSE(r.is_walkable({650, 550})); // inside the obstacle
}

TEST_CASE("parse_room rejects malformed rooms") {
    CHECK_THROWS_AS(parse_room("id: x\n"), DataError); // no size
    CHECK_THROWS_AS(
        parse_room("id: x\nsize: { width: 1, height: 1 }\nhotspots:\n  h: { name: n }\n"),
        DataError); // hotspot without area or bind
}
