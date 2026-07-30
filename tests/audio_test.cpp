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

    CHECK(music_crossfade_gains(2.0f).outgoing == doctest::Approx(0.0f).epsilon(0.00001));
    CHECK(music_crossfade_gains(2.0f).incoming == doctest::Approx(1.0f));
}
