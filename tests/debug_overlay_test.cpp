#include "engine/pnc/debug_overlay.hpp"

#include <doctest/doctest.h>

using namespace pac::pnc;

TEST_CASE("DebugOverlayFlags::toggle flips the F1-F4 layers") {
    DebugOverlayFlags f;
    CHECK_FALSE(f.any());

    CHECK(f.toggle(sf::Keyboard::F1));
    CHECK(f.walkboxes);
    CHECK(f.any());
    CHECK(f.toggle(sf::Keyboard::F1)); // same key toggles it back off
    CHECK_FALSE(f.walkboxes);
    CHECK_FALSE(f.any());

    CHECK(f.toggle(sf::Keyboard::F2));
    CHECK(f.hotspots);
    CHECK(f.toggle(sf::Keyboard::F3));
    CHECK(f.anchors);
    CHECK(f.toggle(sf::Keyboard::F4));
    CHECK(f.hud);
}

TEST_CASE("DebugOverlayFlags::toggle ignores keys it does not own") {
    DebugOverlayFlags f;
    CHECK_FALSE(f.toggle(sf::Keyboard::A));
    CHECK_FALSE(f.toggle(sf::Keyboard::Escape));
    CHECK_FALSE(f.toggle(sf::Keyboard::F5));
    CHECK_FALSE(f.any()); // nothing toggled
}
