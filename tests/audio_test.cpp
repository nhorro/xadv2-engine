#include "engine/core/audio.hpp"

#include <doctest/doctest.h>

#include <cmath>

TEST_CASE("music crossfade uses a clamped equal-power envelope") {
    using pac::core::detail::music_crossfade_gains;

    CHECK(music_crossfade_gains(-1.0f).outgoing == doctest::Approx(1.0f));
    CHECK(music_crossfade_gains(-1.0f).incoming == doctest::Approx(0.0f));

    const auto middle = music_crossfade_gains(0.5f);
    const float equal_power = std::sqrt(0.5f);
    CHECK(middle.outgoing == doctest::Approx(equal_power));
    CHECK(middle.incoming == doctest::Approx(equal_power));
    CHECK(middle.outgoing * middle.outgoing + middle.incoming * middle.incoming ==
          doctest::Approx(1.0f));

    const auto finished = music_crossfade_gains(2.0f);
    CHECK(finished.outgoing == 0.0f);
    CHECK(finished.incoming == doctest::Approx(1.0f));
    CHECK(finished.outgoing >= 0.0f);
    CHECK(finished.incoming <= 1.0f);

    const auto invalid = music_crossfade_gains(std::nanf(""));
    CHECK(std::isfinite(invalid.outgoing));
    CHECK(std::isfinite(invalid.incoming));
    CHECK(invalid.outgoing >= 0.0f);
    CHECK(invalid.incoming >= 0.0f);
}

TEST_CASE("sound fade gain falls smoothly to silence") {
    using pac::core::detail::sound_fade_gain;

    CHECK(sound_fade_gain(-1.0f) == doctest::Approx(1.0f));
    CHECK(sound_fade_gain(0.0f) == doctest::Approx(1.0f));
    CHECK(sound_fade_gain(0.5f) == doctest::Approx(std::sqrt(0.5f)));
    CHECK(sound_fade_gain(1.0f) == doctest::Approx(0.0f));
    CHECK(sound_fade_gain(2.0f) == doctest::Approx(0.0f));
    CHECK(sound_fade_gain(std::nanf("")) == doctest::Approx(1.0f));
}
