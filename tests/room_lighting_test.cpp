#include "engine/pnc/room.hpp"
#include "pnc/room_lighting.hpp"

#include <doctest/doctest.h>

using namespace pac::pnc;

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
