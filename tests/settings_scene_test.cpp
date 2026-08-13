#include "engine/pnc/settings_scene.hpp"

#include <doctest/doctest.h>

using pac::pnc::wrap_choice_index;

TEST_CASE("settings language choices wrap at both ends") {
    CHECK(wrap_choice_index(0, +1, 2) == 1);
    CHECK(wrap_choice_index(1, +1, 2) == 0);
    CHECK(wrap_choice_index(0, -1, 2) == 1);
    CHECK(wrap_choice_index(1, -1, 2) == 0);

    CHECK(wrap_choice_index(3, +1, 4) == 0);
    CHECK(wrap_choice_index(0, -1, 4) == 3);
    CHECK(wrap_choice_index(2, 0, 4) == 2);
}
