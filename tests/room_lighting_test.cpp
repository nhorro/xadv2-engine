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
