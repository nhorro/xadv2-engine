#include "engine/pnc/avatar.hpp"

#include <doctest/doctest.h>

using namespace pac::pnc;

TEST_CASE("nearest_direction snaps a delta to the dominant axis") {
    CHECK(nearest_direction({10.0f, 1.0f}) == Direction::Right);
    CHECK(nearest_direction({-10.0f, 1.0f}) == Direction::Left);
    CHECK(nearest_direction({1.0f, 10.0f}) == Direction::Down); // +y is down
    CHECK(nearest_direction({1.0f, -10.0f}) == Direction::Up);
}

TEST_CASE("nearest_direction resolves diagonal ties and the zero vector to horizontal") {
    CHECK(nearest_direction({5.0f, 5.0f}) == Direction::Right);
    CHECK(nearest_direction({-5.0f, -5.0f}) == Direction::Left);
    CHECK(nearest_direction({0.0f, 0.0f}) == Direction::Right);
}

TEST_CASE("direction string vocabulary round-trips") {
    for (Direction d : {Direction::Up, Direction::Right, Direction::Down, Direction::Left}) {
        CHECK(direction_from_string(to_string(d)) == d);
    }
    CHECK(to_string(Direction::Up) == doctest::String("up"));
    CHECK(to_string(Direction::Down) == doctest::String("down"));
}

TEST_CASE("direction_from_string falls back to down on unknown input") {
    CHECK(direction_from_string("sideways") == Direction::Down);
    CHECK(direction_from_string("") == Direction::Down);
}
