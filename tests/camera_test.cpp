#include "engine/pnc/camera.hpp"

#include <doctest/doctest.h>

using namespace pac::pnc;

TEST_CASE("a room no larger than the viewport stays centered") {
    Camera cam({1280.0f, 612.0f}, {1280u, 612u});
    CHECK(cam.center().x == doctest::Approx(640.0f));
    CHECK(cam.center().y == doctest::Approx(306.0f));
    cam.follow({0.0f, 0.0f}, {50.0f, 50.0f}); // cannot scroll
    CHECK(cam.center().x == doctest::Approx(640.0f));
    cam.snap_to({2000.0f, 2000.0f});
    CHECK(cam.center().x == doctest::Approx(640.0f));
}

TEST_CASE("a wide room clamps the camera at the edges") {
    Camera cam({1280.0f, 612.0f}, {1916u, 821u});
    cam.snap_to({0.0f, 0.0f});                        // far top-left
    CHECK(cam.center().x == doctest::Approx(640.0f)); // half viewport width
    CHECK(cam.center().y == doctest::Approx(306.0f)); // half viewport height
    CHECK(cam.top_left().x == doctest::Approx(0.0f));
    CHECK(cam.top_left().y == doctest::Approx(0.0f));

    cam.snap_to({5000.0f, 5000.0f}); // far bottom-right
    CHECK(cam.center().x == doctest::Approx(1916.0f - 640.0f));
    CHECK(cam.center().y == doctest::Approx(821.0f - 306.0f));
}

TEST_CASE("dead-zone follow only scrolls when the target leaves the band") {
    Camera cam({1280.0f, 612.0f}, {3000u, 612u});
    cam.snap_to({1500.0f, 306.0f}); // mid-room
    const float start = cam.center().x;

    cam.follow({1500.0f + 40.0f, 306.0f}, {80.0f, 80.0f}); // inside dead-zone
    CHECK(cam.center().x == doctest::Approx(start));

    cam.follow({1500.0f + 200.0f, 306.0f}, {80.0f, 80.0f}); // beyond dead-zone by 120
    CHECK(cam.center().x == doctest::Approx(start + 120.0f));
}

TEST_CASE("view_rect reflects center and viewport") {
    Camera cam({1280.0f, 612.0f}, {3000u, 1000u});
    cam.snap_to({1500.0f, 500.0f});
    const sf::FloatRect r = cam.view_rect();
    CHECK(r.left == doctest::Approx(1500.0f - 640.0f));
    CHECK(r.top == doctest::Approx(500.0f - 306.0f));
    CHECK(r.width == doctest::Approx(1280.0f));
    CHECK(r.height == doctest::Approx(612.0f));
}
