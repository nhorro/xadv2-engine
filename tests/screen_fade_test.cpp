#include "engine/core/screen_fade.hpp"

#include <doctest/doctest.h>

using namespace pac::core;

TEST_CASE("ScreenFade starts clear and idle") {
    ScreenFade f;
    CHECK(f.clear());
    CHECK_FALSE(f.opaque());
    CHECK_FALSE(f.active());
    CHECK(f.alpha255() == 0);
}

TEST_CASE("fade_out ramps to black over the duration") {
    ScreenFade f;
    f.fade_out(1.0f);
    CHECK(f.active());
    CHECK_FALSE(f.opaque());

    f.update(0.5f);
    CHECK(f.alpha() == doctest::Approx(0.5f));
    CHECK(f.active());
    CHECK_FALSE(f.opaque());

    f.update(0.5f);
    CHECK(f.alpha() == doctest::Approx(1.0f));
    CHECK(f.opaque());
    CHECK_FALSE(f.active());
    CHECK(f.alpha255() == 255);

    // Idle: further updates do nothing.
    f.update(1.0f);
    CHECK(f.alpha() == doctest::Approx(1.0f));
}

TEST_CASE("fade_in ramps from black back to clear") {
    ScreenFade f;
    f.fade_out(1.0f);
    f.update(1.0f); // now black
    REQUIRE(f.opaque());

    f.fade_in(1.0f);
    f.update(0.25f);
    CHECK(f.alpha() == doctest::Approx(0.75f));
    f.update(1.0f);
    CHECK(f.clear());
    CHECK(f.alpha255() == 0);
}

TEST_CASE("zero-duration fade is instant") {
    ScreenFade f;
    f.fade_out(0.0f);
    CHECK(f.opaque());
    CHECK_FALSE(f.active());
}

TEST_CASE("skip jumps to the current target") {
    ScreenFade f;
    f.fade_out(2.0f);
    f.update(0.2f);
    CHECK_FALSE(f.opaque());
    f.skip();
    CHECK(f.opaque());
    CHECK_FALSE(f.active());
}
