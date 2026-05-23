#include "engine/pnc/mover.hpp"
#include "engine/pnc/room.hpp"

#include <doctest/doctest.h>

using namespace pac::pnc;

namespace {

// Step the mover to completion over a fixed timestep, with a cap so a stuck
// mover fails the test instead of hanging.
void run_until_stopped(Mover& mover, const RoomData& room) {
    for (int i = 0; i < 10000 && mover.moving(); ++i) {
        mover.update(1.0f / 60.0f, room);
    }
}

} // namespace

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

TEST_CASE("follow_path visits each waypoint, reaching a goal no straight line could") {
    // L-shaped room: a straight line from the top-right arm to the bottom-left
    // arm cuts through the non-walkable corner, so only a routed walk arrives.
    RoomData room;
    room.walkable = {{0, 0}, {100, 0}, {100, 40}, {40, 40}, {40, 100}, {0, 100}};

    Mover mover;
    mover.set_position({80, 20});
    mover.follow_path({{30, 30}, {20, 80}});
    REQUIRE(mover.moving());

    run_until_stopped(mover, room);

    CHECK_FALSE(mover.moving());
    CHECK(mover.position().x == doctest::Approx(20.0f));
    CHECK(mover.position().y == doctest::Approx(80.0f));
}

TEST_CASE("follow_path turns to face each leg") {
    RoomData room;
    room.walkable = {{0, 0}, {100, 0}, {100, 100}, {0, 100}};

    Mover mover;
    mover.set_position({10, 10});
    // First leg heads right, second heads down.
    mover.follow_path({{90, 10}, {90, 90}});
    CHECK(mover.facing() == Direction::Right);

    // Advance until the first leg is done and the second has started.
    for (int i = 0; i < 10000 && mover.facing() == Direction::Right && mover.moving(); ++i) {
        mover.update(1.0f / 60.0f, room);
    }
    CHECK(mover.moving());
    CHECK(mover.facing() == Direction::Down);
}

TEST_CASE("follow_path abandons the rest of the path when a leg is blocked") {
    RoomData room;
    room.walkable = {{0, 0}, {100, 0}, {100, 100}, {0, 100}};

    Mover mover;
    mover.set_position({50, 50});
    // First leg exits the room to the right; the second leg must never run.
    mover.follow_path({{200, 50}, {50, 50}});
    run_until_stopped(mover, room);

    CHECK_FALSE(mover.moving());
    CHECK(mover.position().x > 50.0f);   // it walked right toward the wall
    CHECK(mover.position().x <= 100.0f); // and stopped there, not back at leg 2
    CHECK(mover.action() == Action::Stand);
}

TEST_CASE("move_to walks a single target and an empty path stops") {
    RoomData room;
    room.walkable = {{0, 0}, {100, 0}, {100, 100}, {0, 100}};

    Mover mover;
    mover.set_position({10, 10});
    mover.move_to({90, 90});
    run_until_stopped(mover, room);
    CHECK(mover.position().x == doctest::Approx(90.0f));
    CHECK(mover.position().y == doctest::Approx(90.0f));

    mover.follow_path({});
    CHECK_FALSE(mover.moving());
}
