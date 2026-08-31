#include "engine/pnc/room_viewport.hpp"

#include <doctest/doctest.h>

using namespace pac::pnc;

TEST_CASE("room viewport uses the complete runtime resolution") {
    const RoomViewport viewport = RoomViewport::from_runtime({1280u, 720u});

    CHECK(viewport.valid());
    CHECK(viewport.size.x == doctest::Approx(1280.0f));
    CHECK(viewport.size.y == doctest::Approx(720.0f));
    CHECK(viewport.virtual_rect().left == doctest::Approx(0.0f));
    CHECK(viewport.virtual_rect().top == doctest::Approx(0.0f));
    CHECK(viewport.virtual_rect().width == doctest::Approx(1280.0f));
    CHECK(viewport.virtual_rect().height == doctest::Approx(720.0f));
}

TEST_CASE("room viewport accepts arbitrary runtime dimensions") {
    const RoomViewport portrait = RoomViewport::from_runtime({900u, 1200u});
    const RoomViewport wide = RoomViewport::from_runtime({1915u, 821u});

    CHECK(portrait.valid());
    CHECK(portrait.virtual_rect().width == doctest::Approx(900.0f));
    CHECK(portrait.virtual_rect().height == doctest::Approx(1200.0f));
    CHECK(wide.valid());
    CHECK(wide.virtual_rect().width == doctest::Approx(1915.0f));
    CHECK(wide.virtual_rect().height == doctest::Approx(821.0f));
}

TEST_CASE("empty room viewport is invalid") {
    CHECK_FALSE(RoomViewport{{0.0f, 720.0f}}.valid());
    CHECK_FALSE(RoomViewport{{1280.0f, 0.0f}}.valid());
    CHECK_FALSE(RoomViewport{{-1.0f, 720.0f}}.valid());
}
