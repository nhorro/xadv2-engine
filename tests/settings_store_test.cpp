#include "engine/core/settings.hpp"
#include "engine/core/settings_store.hpp"

#include <doctest/doctest.h>

using namespace pac::core;

TEST_CASE("parse_settings_into overlays only the keys present (defaults < user)") {
    Settings s; // manifest-default stand-ins
    s.audio.music_volume = 0.8f;
    s.audio.sfx_volume = 0.8f;
    s.fullscreen = false;
    s.window_width = 1280;
    s.window_height = 720;
    s.language = "es";

    // Only music + fullscreen present: everything else keeps its prior value.
    const bool ok = parse_settings_into("audio: { music_volume: 0.25 }\n"
                                        "display: { fullscreen: true }\n",
                                        s);
    CHECK(ok);
    CHECK(s.audio.music_volume == doctest::Approx(0.25f));
    CHECK(s.audio.sfx_volume == doctest::Approx(0.8f)); // untouched
    CHECK(s.fullscreen == true);
    CHECK(s.window_width == 1280u); // untouched
    CHECK(s.window_height == 720u); // untouched
    CHECK(s.language == "es");      // untouched
}

TEST_CASE("serialize_settings round-trips through parse_settings_into") {
    Settings a;
    a.audio.music_volume = 0.3f;
    a.audio.sfx_volume = 0.6f;
    a.fullscreen = true;
    a.window_width = 1920;
    a.window_height = 1080;
    a.language = "en";

    Settings b; // fresh defaults
    REQUIRE(parse_settings_into(serialize_settings(a), b));
    CHECK(b.audio.music_volume == doctest::Approx(0.3f));
    CHECK(b.audio.sfx_volume == doctest::Approx(0.6f));
    CHECK(b.fullscreen == true);
    CHECK(b.window_width == 1920u);
    CHECK(b.window_height == 1080u);
    CHECK(b.language == "en");
}

TEST_CASE("parse_settings_into clamps out-of-range values") {
    Settings s;
    REQUIRE(parse_settings_into("audio: { music_volume: 5.0, sfx_volume: -1.0 }\n"
                                "display: { width: 10, height: 10 }\n",
                                s));
    CHECK(s.audio.music_volume == doctest::Approx(1.0f));
    CHECK(s.audio.sfx_volume == doctest::Approx(0.0f));
    CHECK(s.window_width >= 320u);
    CHECK(s.window_height >= 240u);
}

TEST_CASE("parse_settings_into rejects non-mapping text without mutating") {
    Settings s;
    s.audio.music_volume = 0.42f;
    CHECK_FALSE(parse_settings_into("- just\n- a\n- list\n", s));
    CHECK_FALSE(parse_settings_into("", s));
    CHECK(s.audio.music_volume == doctest::Approx(0.42f)); // unchanged
}

TEST_CASE("serialize_settings omits an empty language") {
    Settings s; // language defaults to empty
    const std::string text = serialize_settings(s);
    CHECK(text.find("language") == std::string::npos);
}
