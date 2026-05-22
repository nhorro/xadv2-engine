#include "engine/pnc/camera.hpp"

#include <doctest/doctest.h>

using namespace pac::pnc;

TEST_CASE("a room no larger than the viewport stays centered") {
    Camera cam({1280.0f, 612.0f}, {1280u, 612u});
    CHECK(cam.center().x == doctest::Approx(640.0f));
    CHECK(cam.center().y == doctest::Approx(306.0f));
    cam.follow({0.0f, 0.0f}); // cannot scroll
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

TEST_CASE("with no follow bounds, follow maps the room linearly onto the scroll") {
    // Default follow bounds == whole room: mid-room target -> mid scroll.
    Camera cam({1280.0f, 612.0f}, {3000u, 612u});
    cam.follow({1500.0f, 306.0f});
    CHECK(cam.center().x == doctest::Approx(1500.0f)); // 50% of [640, 2360]
    cam.follow({0.0f, 306.0f});
    CHECK(cam.center().x == doctest::Approx(640.0f)); // clamped to the left edge
    cam.follow({3000.0f, 306.0f});
    CHECK(cam.center().x == doctest::Approx(2360.0f)); // clamped to the right edge
}

TEST_CASE("follow bounds map the reachable span onto the whole background") {
    // A wide room whose walkable area the player can only traverse in
    // x:[500,2500], y:[700,760]. The extremes of reach must show the extremes of
    // the background so the whole image is covered (issue #28).
    Camera cam({1280.0f, 612.0f}, {3000u, 1000u});
    cam.set_follow_bounds({500.0f, 700.0f, 2000.0f, 60.0f}); // [500,2500] x [700,760]

    cam.snap_to({500.0f, 700.0f}); // leftmost/topmost reachable point
    CHECK(cam.top_left().x == doctest::Approx(0.0f)); // shows the left edge
    CHECK(cam.top_left().y == doctest::Approx(0.0f)); // shows the top edge

    cam.follow({2500.0f, 760.0f}); // rightmost/bottommost reachable point
    CHECK(cam.top_left().x == doctest::Approx(3000.0f - 1280.0f)); // shows the right edge
    CHECK(cam.top_left().y == doctest::Approx(1000.0f - 612.0f));  // shows the bottom edge

    // Midpoint of reach -> midpoint of scroll.
    cam.follow({1500.0f, 730.0f});
    CHECK(cam.center().x == doctest::Approx(1500.0f));
    CHECK(cam.center().y == doctest::Approx(500.0f));
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
