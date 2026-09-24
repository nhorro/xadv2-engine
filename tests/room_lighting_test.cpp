#include "engine/pnc/room.hpp"
#include "pnc/room_lighting.hpp"
#include "pnc/room_smoke.hpp"

#include <doctest/doctest.h>

using namespace pac::pnc;

TEST_CASE("room smoke simulation is deterministic and respects its fixed pool") {
    RoomSmoke smoke;
    RoomSmokeEmitter emitter;
    emitter.id = "haze";
    emitter.area = {{0.0f, 0.0f}, {100.0f, 0.0f}, {100.0f, 50.0f}, {0.0f, 50.0f}};
    emitter.max_particles = 5;
    emitter.emission_rate = 100.0f;
    emitter.lifetime_min = 2.0f;
    emitter.lifetime_max = 2.0f;
    emitter.prewarm = false;
    emitter.seed = 42;
    smoke.emitters.push_back(emitter);

    RoomSmokeSystem a;
    RoomSmokeSystem b;
    a.configure(smoke);
    b.configure(smoke);
    a.update(0.25f);
    b.update(0.25f);

    REQUIRE(a.particle_count() == 5);
    REQUIRE(b.particle_count() == a.particle_count());
    for (std::size_t i = 0; i < a.particle_count(); ++i) {
        CHECK(a.particles()[i].position.x == doctest::Approx(b.particles()[i].position.x));
        CHECK(a.particles()[i].position.y == doctest::Approx(b.particles()[i].position.y));
        CHECK(a.particles()[i].lifetime == doctest::Approx(b.particles()[i].lifetime));
    }

    for (int i = 0; i < 20; ++i) {
        a.update(0.25f);
        CHECK(a.particle_count() <= emitter.max_particles);
    }
}

TEST_CASE("parse_room reads atmospheric smoke controls") {
    const RoomData room = parse_room(R"YAML(
id: stage
smoke:
  resolution_scale: 0.25
  color: [0.7, 0.8, 0.9]
  extinction: 1.6
  scattering: 1.2
  emitters:
    - id: haze
      area: [{x: 0, y: 10}, {x: 100, y: 10}, {x: 100, y: 30}, {x: 0, y: 30}]
      max_particles: 32
      emission_rate: 4
      lifetime: {min: 6, max: 9}
      size: {start: 40, end: 120}
      velocity: {x: 2, y: -3}
      turbulence: 5
      density: 0.12
      seed: 7
      prewarm: false
)YAML");
    REQUIRE(room.smoke.has_value());
    CHECK(room.smoke->resolution_scale == doctest::Approx(0.25f));
    CHECK(room.smoke->extinction == doctest::Approx(1.6f));
    REQUIRE(room.smoke->emitters.size() == 1);
    const RoomSmokeEmitter& emitter = room.smoke->emitters.front();
    CHECK(emitter.id == "haze");
    CHECK(emitter.max_particles == 32);
    CHECK(emitter.lifetime_min == doctest::Approx(6.0f));
    CHECK(emitter.size_end == doctest::Approx(120.0f));
    CHECK_FALSE(emitter.prewarm);
}

TEST_CASE("parse_room rejects unbounded or malformed smoke") {
    CHECK_THROWS(parse_room("id: stage\nsmoke: true\n"));
    CHECK_THROWS(parse_room("id: stage\nsmoke:\n  resolution_scale: 0.01\n"
                            "  emitters: []\n"));
    CHECK_THROWS(parse_room("id: stage\nsmoke:\n  emitters:\n    - id: haze\n"
                            "      area: [{x: 0, y: 0}, {x: 1, y: 1}]\n"));
}

TEST_CASE("spotlight aim resolves from the live source position") {
    RoomLight light;
    light.type = RoomLight::Type::SPOT;
    light.direction = 12.0f;
    CHECK(resolve_room_light_direction(light, {0.0f, 100.0f}, 0.0f, {{100.0f, 100.0f}}) ==
          doctest::Approx(0.0f));
    CHECK(resolve_room_light_direction(light, {100.0f, 0.0f}, 0.0f, {{100.0f, 100.0f}}) ==
          doctest::Approx(90.0f));

    light.follow_facing = true;
    CHECK(resolve_room_light_direction(light, {0.0f, 0.0f}, 90.0f) == doctest::Approx(102.0f));
}

TEST_CASE("light modulation is deterministic and bounded") {
    LightModulation modulation;
    modulation.type = LightModulation::Type::FLICKER;
    modulation.amount = 0.2f;
    modulation.speed = 7.0f;
    modulation.seed = 3.0f;

    const float a = evaluate_light_modulation(modulation, 1.25f);
    const float b = evaluate_light_modulation(modulation, 1.25f);
    CHECK(a == doctest::Approx(b));
    CHECK(a >= 0.8f);
    CHECK(a <= 1.2f);
}

TEST_CASE("sine modulation reaches its authored extrema") {
    LightModulation modulation;
    modulation.type = LightModulation::Type::SINE;
    modulation.amount = 0.25f;
    modulation.speed = 1.0f;

    CHECK(evaluate_light_modulation(modulation, 0.25f) == doctest::Approx(1.25f));
    CHECK(evaluate_light_modulation(modulation, 0.75f) == doctest::Approx(0.75f));
}

TEST_CASE("disabled modulation is an identity") {
    LightModulation modulation;
    modulation.type = LightModulation::Type::FAULTY;
    modulation.amount = 0.0f;
    CHECK(evaluate_light_modulation(modulation, 99.0f) == doctest::Approx(1.0f));
}

TEST_CASE("resultant projected shadow combines live light contributions") {
    RoomLight left;
    left.id = "left";
    left.radius = 100.0f;
    RoomLight right = left;
    right.id = "right";

    std::vector<ResolvedRoomLight> lights{
        {&left, {-10.0f, 0.0f}, 0.0f, true, 1.0f},
        {&right, {10.0f, 0.0f}, 0.0f, true, 1.0f},
    };
    ProjectedShadow authored;
    authored.sources = {"left", "right"};
    authored.opacity = 0.4f;
    authored.contact_shadow = 0.0f;

    const ProjectedShadow combined =
        resolve_projected_shadow(authored, lights, {0.0f, 10.0f}, 0.0f);
    CHECK(combined.light.x == doctest::Approx(0.0f));
    CHECK(combined.light.y == doctest::Approx(9.0f));
    CHECK(combined.opacity == doctest::Approx(0.4f));
    CHECK(combined.contact_shadow == doctest::Approx(0.0f));

    lights[1].enabled = false;
    const ProjectedShadow from_left =
        resolve_projected_shadow(authored, lights, {0.0f, 10.0f}, 0.0f);
    CHECK(from_left.light.x < 0.0f);
    CHECK(from_left.light.y < 10.0f);
    CHECK(from_left.opacity > 0.0f);
    CHECK(from_left.opacity < authored.opacity);

    lights[0].enabled = false;
    const ProjectedShadow dark = resolve_projected_shadow(authored, lights, {0.0f, 10.0f}, 0.0f);
    CHECK(dark.opacity == doctest::Approx(0.0f));
    CHECK(dark.contact_shadow == doctest::Approx(0.0f));
}

TEST_CASE("resultant projected shadow ignores a spotlight outside its cone") {
    RoomLight spot;
    spot.id = "spot";
    spot.type = RoomLight::Type::SPOT;
    spot.radius = 100.0f;
    spot.angle = 30.0f;
    spot.softness = 5.0f;
    ProjectedShadow authored;
    authored.sources = {"spot"};

    const std::vector<ResolvedRoomLight> lights{
        {&spot, {0.0f, 0.0f}, 0.0f, true, 1.0f},
    };
    CHECK(resolve_projected_shadow(authored, lights, {0.0f, 20.0f}, 0.0f).opacity ==
          doctest::Approx(0.0f));
    CHECK(resolve_projected_shadow(authored, lights, {20.0f, 0.0f}, 0.0f).opacity > 0.0f);
}

TEST_CASE("trapezoidal spotlight includes its authored source width") {
    RoomLight spot;
    spot.id = "spot";
    spot.type = RoomLight::Type::SPOT;
    spot.radius = 100.0f;
    spot.direction = 0.0f;
    spot.angle = 10.0f;
    spot.softness = 1.0f;
    spot.beam_width = 40.0f;

    ProjectedShadow authored;
    authored.sources = {"spot"};
    const std::vector<ResolvedRoomLight> lights{{&spot, {0.0f, 0.0f}, 0.0f, true, 1.0f}};

    CHECK(resolve_projected_shadow(authored, lights, {10.0f, 15.0f}, 0.0f).opacity > 0.0f);
    CHECK(resolve_projected_shadow(authored, lights, {10.0f, 30.0f}, 0.0f).opacity ==
          doctest::Approx(0.0f));
}
