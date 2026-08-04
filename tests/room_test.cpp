#include "engine/core/diagnostics.hpp"
#include "engine/core/resource_cache.hpp"
#include "engine/core/resource_source.hpp"
#include "engine/core/scripting.hpp"
#include "engine/pnc/data_error.hpp"
#include "engine/pnc/room.hpp"
#include "engine/pnc/room_runtime.hpp"
#include "loader_diag.hpp"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

using namespace pac::pnc;
using pac::test::error_code;

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
  - { id: julia, start: player_start, enter_from: at_door, player: true }
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
    CHECK(door.requires_approach); // omitted -> defaults to walk-then-act

    REQUIRE(r.avatars.size() == 1);
    CHECK(r.avatars[0].player == true);
    CHECK(r.avatars[0].start == "player_start");
    CHECK(r.avatars[0].enter_from == "at_door");
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

TEST_CASE("parse_room reads per-layer uniform scale (default 1.0)") {
    const char* yaml = R"YAML(
id: r
background:
  layers:
    - { id: bg,    image: c/bg.png }
    - { id: chair, image: c/chair.png, origin: { x: 400, y: 500 }, scale: 1.5 }
)YAML";
    const RoomData r = parse_room(yaml);
    REQUIRE(r.layers.size() == 2);
    CHECK(r.layers[0].scale == doctest::Approx(1.0f)); // omitted -> native size
    CHECK(r.layers[1].scale == doctest::Approx(1.5f));
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

TEST_CASE("parse_room reads whether a layer extends the room bounds") {
    const char* yaml = R"YAML(
id: r
background:
  layers:
    - { id: room, image: c/room.png }
    - { id: chair, image: c/chair.png, extend_bounds: false }
)YAML";
    const RoomData r = parse_room(yaml);
    REQUIRE(r.layers.size() == 2);
    CHECK(r.layers[0].extend_bounds == true); // omitted -> preserves union behavior
    CHECK(r.layers[1].extend_bounds == false);
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

TEST_CASE("parse_room reads perspective and interpolates avatar scale by depth") {
    const char* yaml = R"YAML(
id: r
perspective:
  top:    { y: 380, scale: 0.70 }
  bottom: { y: 700, scale: 1.15 }
)YAML";
    const RoomData r = parse_room(yaml);
    REQUIRE(r.perspective.has_value());
    CHECK(r.perspective->top_scale == doctest::Approx(0.70f));
    CHECK(r.perspective->bottom_scale == doctest::Approx(1.15f));
    CHECK(r.avatar_scale_at(380.0f) == doctest::Approx(0.70f));  // at top line
    CHECK(r.avatar_scale_at(700.0f) == doctest::Approx(1.15f));  // at bottom line
    CHECK(r.avatar_scale_at(540.0f) == doctest::Approx(0.925f)); // midway
    CHECK(r.avatar_scale_at(100.0f) == doctest::Approx(0.70f));  // above top -> clamped
    CHECK(r.avatar_scale_at(900.0f) == doctest::Approx(1.15f));  // below bottom -> clamped
}

TEST_CASE("avatar_scale_at returns the fallback when no perspective is defined") {
    const RoomData r = parse_room("id: r\n");
    CHECK_FALSE(r.perspective.has_value());
    CHECK(r.avatar_scale_at(500.0f, 1.1f) == doctest::Approx(1.1f));
}

TEST_CASE("parse_room rejects malformed perspective") {
    CHECK_THROWS_AS(parse_room("id: r\nperspective:\n  top: { y: 0, scale: 1 }\n"),
                    DataError); // missing 'bottom'
    CHECK_THROWS_AS(parse_room("id: r\nperspective:\n"
                               "  top: { y: 0, scale: 0 }\n  bottom: { y: 9, scale: 1 }\n"),
                    DataError); // non-positive scale
}

TEST_CASE("parse_room rejects non-positive layer scale") {
    CHECK_THROWS_AS(parse_room("id: r\nbackground:\n  layers:\n"
                               "    - { id: bg, image: a.png, scale: 0 }\n"),
                    DataError);
    CHECK_THROWS_AS(parse_room("id: r\nbackground:\n  layers:\n"
                               "    - { id: bg, image: a.png, scale: -1 }\n"),
                    DataError);
}

TEST_CASE("is_walkable respects the walkable area and obstacles") {
    const RoomData r = parse_room(kRoom);
    CHECK(r.is_walkable({200, 650}));       // on the floor
    CHECK_FALSE(r.is_walkable({200, 200})); // above the floor
    CHECK_FALSE(r.is_walkable({650, 550})); // inside the obstacle
}

TEST_CASE("parse_room reads named obstacles and bare polygons (back-compat)") {
    const char* yaml = R"YAML(
id: r
walkable: [ {x: 0, y: 0}, {x: 100, y: 0}, {x: 100, y: 100}, {x: 0, y: 100} ]
obstacles:
  - [ {x: 10, y: 10}, {x: 30, y: 10}, {x: 30, y: 30}, {x: 10, y: 30} ]
  - { id: crate, area: [ {x: 60, y: 60}, {x: 90, y: 60}, {x: 90, y: 90}, {x: 60, y: 90} ] }
  - { id: gate, enabled: false, area: [ {x: 0, y: 60}, {x: 20, y: 60}, {x: 20, y: 90}, {x: 0, y: 90} ] }
)YAML";
    RoomData r = parse_room(yaml);
    REQUIRE(r.obstacles.size() == 3);
    CHECK(r.obstacles[0].id.empty()); // bare polygon -> no id, enabled
    CHECK(r.obstacles[0].enabled);
    CHECK(r.obstacles[1].id == "crate");
    CHECK(r.obstacles[2].id == "gate");
    CHECK_FALSE(r.obstacles[2].enabled);     // initial enabled: false
    CHECK(r.active_obstacles().size() == 2); // bare + crate (gate is disabled)
    CHECK_FALSE(r.is_walkable({75, 75}));    // inside the enabled crate
    CHECK(r.is_walkable({10, 75}));          // inside the disabled gate -> walkable
}

TEST_CASE("RoomRuntime toggles a named obstacle and the walkable test follows") {
    const char* yaml = R"YAML(
id: r
walkable: [ {x: 0, y: 0}, {x: 100, y: 0}, {x: 100, y: 100}, {x: 0, y: 100} ]
obstacles:
  - { id: crate, area: [ {x: 40, y: 40}, {x: 60, y: 40}, {x: 60, y: 60}, {x: 40, y: 60} ] }
)YAML";
    RoomRuntime room(parse_room(yaml));
    CHECK(room.obstacle_enabled("crate"));
    CHECK_FALSE(room.data().is_walkable({50, 50})); // crate blocks
    room.set_obstacle_enabled("crate", false);
    CHECK_FALSE(room.obstacle_enabled("crate"));
    CHECK(room.data().is_walkable({50, 50})); // disabled -> walkable
    CHECK(room.data().active_obstacles().empty());
    CHECK(room.obstacle_enabled("missing")); // unknown id -> defaults true
}

TEST_CASE("parse_room reads object sprite + initial sequence (animated objects, #142)") {
    const char* yaml = R"YAML(
id: r
objects:
  fountain: { sprite: fx/fountain.anim.yml, sequence: bubble, position: { x: 50, y: 80 } }
  crate:    { sprite: o/crate.png, position: { x: 10, y: 20 } }
)YAML";
    const RoomData r = parse_room(yaml);
    CHECK(r.objects.at("fountain").sprite == "fx/fountain.anim.yml");
    CHECK(r.objects.at("fountain").sequence == "bubble");
    CHECK(r.objects.at("crate").sequence.empty()); // static object: no sequence
}

TEST_CASE("RoomRuntime moves and scales objects from script (#142)") {
    const char* yaml = R"YAML(
id: r
objects:
  cart: { sprite: o/cart.png, position: { x: 0, y: 0 }, scale: 1.0, rotation: 15 }
)YAML";
    RoomRuntime room(parse_room(yaml));
    CHECK(room.object_position("cart").x == doctest::Approx(0.0f)); // seeded from def
    CHECK(room.object_scale("cart") == doctest::Approx(1.0f));
    CHECK(room.object_rotation("cart") == doctest::Approx(15.0f));

    room.set_object_scale("cart", 2.0f);
    CHECK(room.object_scale("cart") == doctest::Approx(2.0f));
    room.set_object_scale("cart", -1.0f); // non-positive ignored
    CHECK(room.object_scale("cart") == doctest::Approx(2.0f));
    room.set_object_rotation("cart", 90.0f);
    CHECK(room.object_rotation("cart") == doctest::Approx(90.0f));

    room.object_move_to("cart", {100, 0}, 100.0f); // 100 px/s toward x=100
    CHECK(room.object_moving("cart"));
    room.update_objects(0.5f); // 50 px
    CHECK(room.object_position("cart").x == doctest::Approx(50.0f));
    CHECK(room.object_moving("cart"));
    room.update_objects(1.0f); // would overshoot -> snap to target and stop
    CHECK(room.object_position("cart").x == doctest::Approx(100.0f));
    CHECK_FALSE(room.object_moving("cart"));

    room.set_object_position("cart", {5, 7}); // explicit placement stops + teleports
    CHECK(room.object_position("cart").x == doctest::Approx(5.0f));
    CHECK(room.object_position("cart").y == doctest::Approx(7.0f));
    CHECK_FALSE(room.object_moving("cart"));
}

TEST_CASE("parse_room reads declarative configs + managed-set unions (#185)") {
    const char* yaml = R"YAML(
id: lab
points:
  schneider_start: { x: 100, y: 200 }
objects:
  box:   { sprite: o/box.png }
  truck: { sprite: o/truck.png }
obstacles:
  - { id: box_block,   area: [{x: 0, y: 0}, {x: 1, y: 0}, {x: 1, y: 1}] }
  - { id: truck_block, area: [{x: 2, y: 0}, {x: 3, y: 0}, {x: 3, y: 1}] }
configs:
  start: intro
  intro:
    present: {}
  puzzle:
    present:
      npcs:
        schneider: { at: schneider_start, facing: left }
      objects: [box, truck]
      obstacles: [box_block, truck_block]
  box_only:
    present:
      objects: [box]
      obstacles: [box_block]
)YAML";
    const RoomData r = parse_room(yaml);
    REQUIRE(r.configs.has_value());
    const RoomConfigs& c = *r.configs;
    CHECK(c.start == "intro");
    CHECK(c.order.size() == 3);

    // intro is exhaustively empty: presence reconcile turns everything off.
    const RoomConfig* intro = c.find("intro");
    REQUIRE(intro);
    CHECK(intro->npcs.empty());
    CHECK(intro->objects.empty());
    CHECK(intro->obstacles.empty());

    // puzzle names an NPC (with at/facing) plus objects + obstacles.
    const RoomConfig* puzzle = c.find("puzzle");
    REQUIRE(puzzle);
    REQUIRE(puzzle->npcs.count("schneider") == 1);
    CHECK(puzzle->npcs.at("schneider").at == "schneider_start");
    CHECK(puzzle->npcs.at("schneider").facing == "left");
    CHECK(puzzle->objects == std::vector<std::string>{"box", "truck"});
    CHECK(puzzle->obstacles == std::vector<std::string>{"box_block", "truck_block"});

    // Managed sets = sorted-unique union across all configs.
    CHECK(c.managed_npcs == std::vector<std::string>{"schneider"});
    CHECK(c.managed_objects == std::vector<std::string>{"box", "truck"});
    CHECK(c.managed_obstacles == std::vector<std::string>{"box_block", "truck_block"});

    CHECK(c.find("missing") == nullptr);
}

TEST_CASE("parse_room rejects malformed configs with stable codes (#185)") {
    auto with = [](const std::string& configs) {
        return "id: r\npoints:\n  p: { x: 0, y: 0 }\nobjects:\n  box: { sprite: o/b.png }\n"
               "obstacles:\n  - { id: blk, area: [{x: 0, y: 0}, {x: 1, y: 0}, {x: 1, y: 1}] }\n" +
               configs;
    };
    CHECK(error_code([&] { parse_room(with("configs: 5\n")); }) == "room.configs-not-map");
    CHECK(error_code([&] { parse_room(with("configs:\n  a: { present: {} }\n")); }) ==
          "room.config-start-missing");
    CHECK(error_code([&] { parse_room(with("configs:\n  start: zzz\n  a: {}\n")); }) ==
          "room.config-start-unknown");
    CHECK(error_code([&] { parse_room(with("configs:\n  start: a\n  a: { present: 5 }\n")); }) ==
          "room.config-present-not-map");
    CHECK(error_code([&] {
              parse_room(with(
                  "configs:\n  start: a\n  a: { present: { npcs: { n: { facing: up } } } }\n"));
          }) == "room.config-npc-at-missing");
    CHECK(error_code([&] {
              parse_room(
                  with("configs:\n  start: a\n  a: { present: { npcs: { n: { at: ghost } } } }\n"));
          }) == "room.config-npc-point-unknown");
    CHECK(error_code([&] {
              parse_room(with("configs:\n  start: a\n  a: { present: { objects: [ghost] } }\n"));
          }) == "room.config-object-unknown");
    CHECK(error_code([&] {
              parse_room(with("configs:\n  start: a\n  a: { present: { obstacles: [ghost] } }\n"));
          }) == "room.config-obstacle-unknown");
}

TEST_CASE("parse_room rejects malformed rooms") {
    CHECK_THROWS_AS(parse_room("version: 1\n"), DataError); // no id
    CHECK_THROWS_AS(parse_room("id: x\nhotspots:\n  h: { name: n }\n"),
                    DataError); // hotspot without area or bind
    CHECK(error_code([] { parse_room("id: x\nhotspots:\n  h: { name: n }\n"); }) ==
          "room.hotspot-no-area-or-bind");
}

TEST_CASE("parse_room enforces the M6 tightened validations with stable codes") {
    // The YAML `id` must match the filename id, when one is supplied.
    CHECK_THROWS_AS(parse_room("id: study\n", "hall"), DataError);
    CHECK(error_code([] { parse_room("id: study\n", "hall"); }) == "room.id-mismatch");
    CHECK_NOTHROW(parse_room("id: study\n", "study")); // matches
    CHECK_NOTHROW(parse_room("id: study\n"));          // empty expected -> check skipped

    // default_verb must be `look_at` or one of the hotspot's affordances.
    CHECK(error_code([] {
              parse_room("id: r\nhotspots:\n  h:\n"
                         "    area: [ {x: 0, y: 0}, {x: 1, y: 0}, {x: 1, y: 1} ]\n"
                         "    affordances: [ look_at ]\n"
                         "    default_verb: open\n");
          }) == "room.default-verb-not-in-affordances");
    CHECK_NOTHROW(parse_room("id: r\nhotspots:\n  h:\n"
                             "    area: [ {x: 0, y: 0}, {x: 1, y: 0}, {x: 1, y: 1} ]\n"
                             "    affordances: [ look_at, open ]\n"
                             "    default_verb: open\n"));

    // A region's `initial` must be one of its declared states.
    CHECK(error_code([] {
              parse_room("id: r\nregions:\n  d:\n"
                         "    area: [ {x: 0, y: 0}, {x: 1, y: 0}, {x: 1, y: 1} ]\n"
                         "    states: { shut: a/shut.png }\n"
                         "    initial: open\n");
          }) == "room.region-initial-not-in-states");
}

TEST_CASE("parse_room reads the per-hotspot requires_approach flag") {
    const char* yaml = R"YAML(
id: r
points:
  spot: { x: 50, y: 60 }
hotspots:
  near_only:
    name: "cofre"
    area: [ {x: 0, y: 0}, {x: 10, y: 0}, {x: 10, y: 10}, {x: 0, y: 10} ]
    approach: spot
  distant_ok:
    name: "loro"
    area: [ {x: 20, y: 0}, {x: 30, y: 0}, {x: 30, y: 10}, {x: 20, y: 10} ]
    requires_approach: false
)YAML";
    const RoomData r = parse_room(yaml);
    CHECK(r.hotspots.at("near_only").requires_approach);        // omitted -> default true
    CHECK_FALSE(r.hotspots.at("distant_ok").requires_approach); // explicit opt-out
}

TEST_CASE("parse_room reads streamed and random room ambience layers") {
    const RoomData r = parse_room(R"YAML(
id: street
ambience:
  transition: 3.0
  base: { sound: sounds/city.ogg, volume: 0.75 }
  random:
    - id: traffic
      sounds: [sounds/car.ogg, sounds/horn.ogg]
      delay: { min: 4, max: 12 }
      volume: { min: 0.2, max: 0.6 }
      pan: { min: -0.9, max: 0.9 }
)YAML");

    REQUIRE(r.ambience.has_value());
    CHECK(r.ambience->transition == doctest::Approx(3.0f));
    CHECK(r.ambience->base.sound == "sounds/city.ogg");
    CHECK(r.ambience->base.volume == doctest::Approx(0.75f));
    REQUIRE(r.ambience->random.size() == 1);
    const pac::core::AmbienceRandomLayer& traffic = r.ambience->random.front();
    CHECK(traffic.id == "traffic");
    REQUIRE(traffic.sounds.size() == 2);
    CHECK(traffic.sounds[0] == "sounds/car.ogg");
    CHECK(traffic.sounds[1] == "sounds/horn.ogg");
    CHECK(traffic.delay.min == doctest::Approx(4.0f));
    CHECK(traffic.delay.max == doctest::Approx(12.0f));
    CHECK(traffic.volume.min == doctest::Approx(0.2f));
    CHECK(traffic.volume.max == doctest::Approx(0.6f));
    CHECK(traffic.pan.min == doctest::Approx(-0.9f));
    CHECK(traffic.pan.max == doctest::Approx(0.9f));
}

TEST_CASE("parse_room validates room ambience layers") {
    CHECK(error_code([] { parse_room("id: r\nambience: []\n"); }) == "room.ambience-not-map");
    CHECK(error_code([] {
              parse_room("id: r\nambience:\n  random:\n    - id: birds\n"
                         "      sound: sounds/bird.ogg\n      pan: {min: 0.5, max: -0.5}\n");
          }) == "room.ambience-range-invalid");
    CHECK(error_code([] {
              parse_room("id: r\nambience:\n  random:\n"
                         "    - {id: birds, sound: a.ogg}\n"
                         "    - {id: birds, sound: b.ogg}\n");
          }) == "room.ambience-random-id-invalid");
}

TEST_CASE("parse_room reads projected avatar shadows from room lighting") {
    const RoomData r = parse_room(R"YAML(
id: r
lighting:
  projected_shadows:
    enabled: true
    light: { x: 180, y: 120 }
    casters: all
    length: 0.42
    width: 0.7
    opacity: 0.16
    softness: 5
    contact_shadow: 0.5
    color: { r: 10, g: 12, b: 16 }
)YAML");

    REQUIRE(r.projected_shadow.has_value());
    const ProjectedShadow& shadow = *r.projected_shadow;
    CHECK(shadow.enabled);
    CHECK(shadow.light.x == doctest::Approx(180.0f));
    CHECK(shadow.light.y == doctest::Approx(120.0f));
    CHECK(shadow.casters == ProjectedShadow::Casters::ALL);
    CHECK(shadow.length == doctest::Approx(0.42f));
    CHECK(shadow.width == doctest::Approx(0.7f));
    CHECK(shadow.opacity == doctest::Approx(0.16f));
    CHECK(shadow.softness == doctest::Approx(5.0f));
    CHECK(shadow.contact_shadow == doctest::Approx(0.5f));
    CHECK(shadow.color == sf::Color(10, 12, 16));
    CHECK_FALSE(shadow.z.has_value());
}

TEST_CASE("parse_room reads an optional fixed projected-shadow depth") {
    const RoomData r = parse_room(R"YAML(
id: r
lighting:
  projected_shadows:
    light: { x: 180, y: 120 }
    z: 1
)YAML");
    REQUIRE(r.projected_shadow.has_value());
    REQUIRE(r.projected_shadow->z.has_value());
    CHECK(*r.projected_shadow->z == doctest::Approx(1.0f));
}

TEST_CASE("parse_room validates projected avatar shadow parameters") {
    CHECK(error_code([] {
              parse_room("id: r\nlighting:\n  projected_shadows: { enabled: true }\n");
          }) == "room.projected-shadows-light-missing");
    CHECK(error_code([] {
              parse_room("id: r\nlighting:\n  projected_shadows:\n"
                         "    light: {x: 0, y: 0}\n    opacity: 1.5\n");
          }) == "room.projected-shadows-opacity-invalid");
    CHECK(error_code([] {
              parse_room("id: r\nlighting:\n  projected_shadows:\n"
                         "    light: {x: 0, y: 0}\n    casters: furniture\n");
          }) == "room.projected-shadows-casters-invalid");
    CHECK(error_code([] {
              parse_room("id: r\nlighting:\n  projected_shadows:\n"
                         "    light: {x: 0, y: 0}\n    z: .nan\n");
          }) == "room.projected-shadows-z-invalid");
}

TEST_CASE("parse_room reads the optional object baseline (perspective sort line)") {
    const char* yaml = R"YAML(
id: r
objects:
  cart_front: { image: o/cart_front.png, position: { x: 400, y: 360 }, baseline: 640 }
  vase:       { image: o/vase.png, position: { x: 100, y: 100 } }
)YAML";
    const RoomData r = parse_room(yaml);
    REQUIRE(r.objects.at("cart_front").baseline.has_value());
    CHECK(r.objects.at("cart_front").baseline.value() == doctest::Approx(640.0f));
    CHECK_FALSE(r.objects.at("vase").baseline.has_value()); // omitted -> z/z_auto
}

TEST_CASE("parse_room reads object scale (default 1.0) and rejects non-positive") {
    const char* yaml = R"YAML(
id: r
objects:
  big:   { sprite: o/a.png, position: { x: 0, y: 0 }, scale: 2.0 }
  plain: { sprite: o/b.png, position: { x: 0, y: 0 } }
)YAML";
    const RoomData r = parse_room(yaml);
    CHECK(r.objects.at("big").scale == doctest::Approx(2.0f));
    CHECK(r.objects.at("plain").scale == doctest::Approx(1.0f)); // omitted -> native
    CHECK(error_code([] {
              parse_room(
                  "id: r\nobjects:\n  o: { sprite: a.png, position: {x: 0, y: 0}, scale: 0 }\n");
          }) == "room.object-scale-invalid");
}

TEST_CASE("parse_room reads object 'sprite' (canonical) and 'image' (deprecated alias)") {
    const char* yaml = R"YAML(
id: r
objects:
  by_sprite: { sprite: o/a.png, position: { x: 1, y: 2 } }
  by_image:  { image: o/b.png, position: { x: 3, y: 4 } }
)YAML";
    const RoomData r = parse_room(yaml);
    CHECK(r.objects.at("by_sprite").sprite == "o/a.png");
    CHECK(r.objects.at("by_image").sprite == "o/b.png"); // alias maps onto the same field
}

TEST_CASE("parse_room fails loudly when an object has neither 'sprite' nor 'image'") {
    const char* yaml = "id: r\nobjects:\n  ghost: { position: { x: 0, y: 0 } }\n";
    CHECK_THROWS_AS(parse_room(yaml), DataError);
    CHECK(error_code([&] { parse_room(yaml); }) == "room.object-sprite-missing");
}

TEST_CASE("parse_room reads walk-behind areas and validates the layer reference") {
    const char* yaml = R"YAML(
id: r
background:
  layers:
    - { id: bg, image: a/bg.png, z: 0 }
walkbehinds:
  cart:
    layer: bg
    area: [ {x: 0, y: 0}, {x: 10, y: 0}, {x: 10, y: 10}, {x: 0, y: 10} ]
    baseline: 640
)YAML";
    const RoomData r = parse_room(yaml);
    REQUIRE(r.walkbehinds.size() == 1);
    CHECK(r.walkbehinds[0].id == "cart");
    CHECK(r.walkbehinds[0].layer == "bg");
    CHECK(r.walkbehinds[0].area.size() == 4);
    CHECK(r.walkbehinds[0].baseline == doctest::Approx(640.0f));

    // Unknown layer reference fails loudly.
    CHECK_THROWS_AS(parse_room("id: r\nwalkbehinds:\n  c:\n    layer: ghost\n"
                               "    area: [ {x: 0, y: 0}, {x: 1, y: 0}, {x: 1, y: 1} ]\n"
                               "    baseline: 1\n"),
                    DataError);
    // Missing baseline fails loudly.
    CHECK_THROWS_AS(parse_room("id: r\nbackground:\n  layers:\n    - { id: bg, image: a.png }\n"
                               "walkbehinds:\n  c:\n    layer: bg\n"
                               "    area: [ {x: 0, y: 0}, {x: 1, y: 0}, {x: 1, y: 1} ]\n"),
                    DataError);
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
    CHECK_FALSE(d.baseline.has_value()); // omitted -> over/z
}

TEST_CASE("parse_room reads the optional region baseline (perspective sort line)") {
    const char* yaml = R"YAML(
id: r
regions:
  cart:
    area: [ {x: 0, y: 0}, {x: 10, y: 0}, {x: 10, y: 10}, {x: 0, y: 10} ]
    baseline: 512
    states: { shut: a/shut.png, open: a/open.png }
    initial: shut
)YAML";
    const RoomData r = parse_room(yaml);
    REQUIRE(r.regions.count("cart") == 1);
    REQUIRE(r.regions.at("cart").baseline.has_value());
    CHECK(r.regions.at("cart").baseline.value() == doctest::Approx(512.0f));
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

TEST_CASE("call_hotspot reports handled vs. no-handler (default-caption gating)") {
    // A behavior with: a verb that returns a caption, a verb that runs a silent
    // action (returns nil), and verbs/hotspots with no handler at all.
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "pac_call_hotspot_test";
    fs::remove_all(root);
    fs::create_directories(root / "rooms");
    std::ofstream(root / "rooms" / "r.lua") << R"LUA(
        return {
          hotspots = {
            thing = {
              look_at = function() return "Una cosa." end,
              use     = function() did_use = true end,  -- silent action, no return
            },
          },
        }
    )LUA";

    pac::core::Diagnostics log(pac::core::LogLevel::ERROR);
    pac::core::FilesystemResourceSource source(root.string());
    pac::core::ResourceCache resources(source, log);
    pac::core::Scripting scripting(log);

    RoomRuntime room(parse_room("id: r\n"));
    room.load_behavior(scripting, resources, "rooms/r.lua", log);

    // Handler returns a caption -> handled, caption present.
    const VerbResult look = room.call_hotspot("thing", "look_at");
    CHECK(look.handled);
    REQUIRE(look.caption.has_value());
    CHECK(*look.caption == "Una cosa.");

    // Handler ran a silent action -> handled, but NO caption (default suppressed).
    const VerbResult use = room.call_hotspot("thing", "use");
    CHECK(use.handled);
    CHECK_FALSE(use.caption.has_value());
    CHECK(scripting.run_string("assert(did_use == true)")); // the action did run

    // No handler for this verb on an existing hotspot -> not handled.
    const VerbResult push = room.call_hotspot("thing", "push");
    CHECK_FALSE(push.handled);
    CHECK_FALSE(push.caption.has_value());

    // No such hotspot -> not handled.
    const VerbResult ghost = room.call_hotspot("ghost", "look_at");
    CHECK_FALSE(ghost.handled);

    fs::remove_all(root);
}

// M9 #183: an auto-spawned hotspot handler that yields (talk/wait/move_to)
// reports `in_flight = <task id>` so the dispatcher (`RoomScene::
// dispatch_and_feedback`) can defer `finish_execution` until the task drains.
// The synchronous return-string-as-caption rule is preserved by the test
// above; this test covers the yield path that didn't exist before #183.
TEST_CASE("call_hotspot reports in_flight when the handler yields, then drains") {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "pac_call_hotspot_yield_test";
    fs::remove_all(root);
    fs::create_directories(root / "rooms");
    std::ofstream(root / "rooms" / "r.lua") << R"LUA(
        return {
          hotspots = {
            slow = {
              -- yields via wait, then sets a flag. After #183 this handler does
              -- NOT need to wrap itself in `spawn(...)` -- the dispatch site
              -- auto-spawns it.
              use = function() wait(0.5); _G.slow_done = true end,
            },
          },
        }
    )LUA";

    pac::core::Diagnostics log(pac::core::LogLevel::ERROR);
    pac::core::FilesystemResourceSource source(root.string());
    pac::core::ResourceCache resources(source, log);
    pac::core::Scripting scripting(log);

    RoomRuntime room(parse_room("id: r\n"));
    room.load_behavior(scripting, resources, "rooms/r.lua", log);

    const VerbResult use = room.call_hotspot("slow", "use");
    CHECK(use.handled);
    CHECK_FALSE(use.caption.has_value()); // yielded -> no synchronous caption
    REQUIRE(use.in_flight.has_value());
    CHECK(scripting.is_task_alive(*use.in_flight));

    // Drive the scheduler past the wait; the task should drain and the flag
    // should be set in Lua-land.
    for (int i = 0; i < 100 && scripting.is_task_alive(*use.in_flight); ++i) {
        scripting.update(0.016f);
    }
    CHECK_FALSE(scripting.is_task_alive(*use.in_flight));
    CHECK(scripting.run_string("assert(_G.slow_done == true)"));

    fs::remove_all(root);
}

// M9 #185: the engine drives the per-config first-enter / re-enter beat by
// looking it up under `room.configs[<id>].<hook>` and auto-spawning it (same seam
// as a verb handler), so a presence-only config or a config without that hook is a
// clean no-op, and a blocking beat reports `in_flight` for the room to block on.
TEST_CASE("call_config_beat dispatches per-config beats, no-ops when absent (#185)") {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "pac_call_config_beat_test";
    fs::remove_all(root);
    fs::create_directories(root / "rooms");
    std::ofstream(root / "rooms" / "r.lua") << R"LUA(
        return {
          configs = {
            intro = {
              -- a blocking first-enter beat (yields), like a cutscene monologue
              on_first_enter = function() wait(0.3); _G.intro_ran = true end,
            },
            puzzle = {
              on_reenter = function() _G.puzzle_reenter = true end, -- silent, no yield
            },
            -- `alone` has no Lua entry at all (presence-only config).
          },
        }
    )LUA";

    pac::core::Diagnostics log(pac::core::LogLevel::ERROR);
    pac::core::FilesystemResourceSource source(root.string());
    pac::core::ResourceCache resources(source, log);
    pac::core::Scripting scripting(log);

    RoomRuntime room(parse_room("id: r\n"));
    room.load_behavior(scripting, resources, "rooms/r.lua", log);

    // Blocking first-enter beat -> handled, in_flight, then drains.
    const VerbResult intro = room.call_config_beat("intro", "on_first_enter");
    CHECK(intro.handled);
    REQUIRE(intro.in_flight.has_value());
    CHECK(scripting.is_task_alive(*intro.in_flight));
    for (int i = 0; i < 100 && scripting.is_task_alive(*intro.in_flight); ++i) {
        scripting.update(0.016f);
    }
    CHECK(scripting.run_string("assert(_G.intro_ran == true)"));

    // The hook a config doesn't define -> not handled (the engine just skips it).
    CHECK_FALSE(room.call_config_beat("intro", "on_reenter").handled);

    // A silent (non-yielding) re-enter beat -> handled, no in_flight.
    const VerbResult re = room.call_config_beat("puzzle", "on_reenter");
    CHECK(re.handled);
    CHECK_FALSE(re.in_flight.has_value());
    CHECK(scripting.run_string("assert(_G.puzzle_reenter == true)"));

    // A presence-only config (no Lua entry) and an unknown config -> no-op.
    CHECK_FALSE(room.call_config_beat("alone", "on_first_enter").handled);
    CHECK_FALSE(room.call_config_beat("ghost", "on_first_enter").handled);

    fs::remove_all(root);
}
