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
    // A wide room whose walkable area the player traverses in x:[500,2500],
    // y:[400,600]. The strip is wide/tall enough that the on-screen clamp does
    // not engage, so the extremes of reach still show the extremes of the
    // background and the whole image is covered (issue #28).
    Camera cam({1280.0f, 612.0f}, {3000u, 1000u});
    cam.set_follow_bounds({500.0f, 400.0f, 2000.0f, 200.0f}); // [500,2500] x [400,600]

    cam.snap_to({500.0f, 400.0f});                    // leftmost/topmost reachable point
    CHECK(cam.top_left().x == doctest::Approx(0.0f)); // shows the left edge
    CHECK(cam.top_left().y == doctest::Approx(0.0f)); // shows the top edge

    cam.follow({2500.0f, 600.0f}); // rightmost/bottommost reachable point
    CHECK(cam.top_left().x == doctest::Approx(3000.0f - 1280.0f)); // shows the right edge
    CHECK(cam.top_left().y == doctest::Approx(1000.0f - 612.0f));  // shows the bottom edge

    // Midpoint of reach -> midpoint of scroll.
    cam.follow({1500.0f, 500.0f});
    CHECK(cam.center().x == doctest::Approx(1500.0f));
    CHECK(cam.center().y == doctest::Approx(500.0f));
}

TEST_CASE("the on-screen clamp keeps the player visible in a thin-slice room") {
    // The pathological case behind the fix: a tall room whose walkable area is a
    // 60px slice low in the room (y:[700,760]). The raw proportional reveal would
    // show the room top while the player sits at world-y 700 — i.e. off the bottom
    // of the 612px viewport. The clamp must keep the pivot on screen, within the
    // default 15% margin band [91.8, 520.2].
    Camera cam({1280.0f, 612.0f}, {3000u, 1000u});
    cam.set_follow_bounds({500.0f, 700.0f, 2000.0f, 60.0f}); // [500,2500] x [700,760]

    const float margin = 0.15f * 612.0f;   // 91.8
    const float lo_band = margin;          // nearest the top edge
    const float hi_band = 612.0f - margin; // nearest the bottom edge
    for (float y : {700.0f, 715.0f, 730.0f, 745.0f, 760.0f}) {
        cam.snap_to({1500.0f, y});
        const float screen_y = y - cam.top_left().y; // player pivot in viewport space
        CHECK(screen_y >= doctest::Approx(lo_band));
        CHECK(screen_y <= doctest::Approx(hi_band));
    }

    // The top of the slice (previously off-screen) now sits exactly at the bottom
    // margin instead of below the viewport.
    cam.snap_to({1500.0f, 700.0f});
    CHECK((700.0f - cam.top_left().y) == doctest::Approx(hi_band));
}

TEST_CASE("follow margin 0 keeps the player just on screen, edge allowed") {
    // With margin 0 the player may reach the very viewport edge but never beyond:
    // at the top of the thin slice the pivot lands exactly on the bottom edge.
    Camera cam({1280.0f, 612.0f}, {3000u, 1000u});
    cam.set_follow_bounds({500.0f, 700.0f, 2000.0f, 60.0f});
    cam.set_follow_margin(0.0f);

    cam.snap_to({1500.0f, 700.0f});
    CHECK((700.0f - cam.top_left().y) == doctest::Approx(612.0f)); // bottom edge, still visible
    CHECK(cam.top_left().y == doctest::Approx(88.0f));             // not the buggy 0
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

TEST_CASE("a full-height viewport scrolls a larger room on both axes") {
    Camera cam({1280.0f, 720.0f}, {1915u, 821u});

    cam.snap_to({0.0f, 0.0f});
    CHECK(cam.top_left().x == doctest::Approx(0.0f));
    CHECK(cam.top_left().y == doctest::Approx(0.0f));
    CHECK(cam.view_rect().width == doctest::Approx(1280.0f));
    CHECK(cam.view_rect().height == doctest::Approx(720.0f));

    cam.snap_to({1915.0f, 821.0f});
    CHECK(cam.top_left().x == doctest::Approx(635.0f));
    CHECK(cam.top_left().y == doctest::Approx(101.0f));
}

TEST_CASE("camera accepts a non-widescreen runtime viewport") {
    Camera cam({900.0f, 1200.0f}, {1600u, 1800u});

    cam.snap_to({1600.0f, 1800.0f});
    const sf::FloatRect view = cam.view_rect();
    CHECK(view.left == doctest::Approx(700.0f));
    CHECK(view.top == doctest::Approx(600.0f));
    CHECK(view.width == doctest::Approx(900.0f));
    CHECK(view.height == doctest::Approx(1200.0f));
}

TEST_CASE("camera_look_at snaps the center (clamped) and suspends follow") {
    Camera cam({1280.0f, 612.0f}, {3000u, 1000u});
    CHECK(cam.following());
    cam.look_at({1500.0f, 500.0f});
    CHECK_FALSE(cam.following());
    CHECK(cam.center().x == doctest::Approx(1500.0f));
    CHECK(cam.center().y == doctest::Approx(500.0f));

    // While suspended, follow() still computes a center if called, but the scene
    // gates on following(); the override target stays put until follow resumes.
    cam.look_at({0.0f, 0.0f}); // clamps to the top-left edge
    CHECK(cam.center().x == doctest::Approx(640.0f));
    CHECK(cam.center().y == doctest::Approx(306.0f));
}

TEST_CASE("camera_go_to tweens to the target over its duration and suspends follow") {
    Camera cam({1280.0f, 612.0f}, {3000u, 1000u});
    cam.set_center({640.0f, 306.0f}); // deterministic start (direct, not the reveal map)
    cam.go_to({2360.0f, 694.0f}, 1.0f);
    CHECK_FALSE(cam.following());
    CHECK(cam.tweening());

    cam.update(0.5f); // halfway: smoothstep(0.5) = 0.5, so exactly the midpoint
    CHECK(cam.center().x == doctest::Approx(1500.0f));
    CHECK(cam.center().y == doctest::Approx(500.0f));
    CHECK(cam.tweening());

    cam.update(0.5f); // reaches the target and the tween ends
    CHECK_FALSE(cam.tweening());
    CHECK(cam.center().x == doctest::Approx(2360.0f));
    CHECK(cam.center().y == doctest::Approx(694.0f));
}

TEST_CASE("a non-positive go_to duration snaps immediately") {
    Camera cam({1280.0f, 612.0f}, {3000u, 1000u});
    cam.go_to({1500.0f, 500.0f}, 0.0f);
    CHECK_FALSE(cam.tweening());
    CHECK(cam.center().x == doctest::Approx(1500.0f));
}

TEST_CASE("camera_follow_player resumes follow and cancels a tween") {
    Camera cam({1280.0f, 612.0f}, {3000u, 1000u});
    cam.go_to({2360.0f, 694.0f}, 1.0f);
    cam.update(0.25f);
    cam.follow_player();
    CHECK(cam.following());
    CHECK_FALSE(cam.tweening());
    cam.update(1.0f); // tween was cancelled: no further movement from update
    cam.follow({1500.0f, 500.0f});
    CHECK(cam.center().x == doctest::Approx(1500.0f)); // ordinary follow again
}
