#include "engine/pnc/data_error.hpp"
#include "engine/pnc/room.hpp"
#include "engine/pnc/room_runtime.hpp"
#include "loader_diag.hpp"

#include <doctest/doctest.h>

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
    CHECK(door.requires_approach); // omitted -> defaults to walk-then-act

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
  cart: { sprite: o/cart.png, position: { x: 0, y: 0 }, scale: 1.0 }
)YAML";
    RoomRuntime room(parse_room(yaml));
    CHECK(room.object_position("cart").x == doctest::Approx(0.0f)); // seeded from def
    CHECK(room.object_scale("cart") == doctest::Approx(1.0f));

    room.set_object_scale("cart", 2.0f);
    CHECK(room.object_scale("cart") == doctest::Approx(2.0f));
    room.set_object_scale("cart", -1.0f); // non-positive ignored
    CHECK(room.object_scale("cart") == doctest::Approx(2.0f));

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
