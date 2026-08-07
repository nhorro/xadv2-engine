#include "engine/core/cursor.hpp"

#include <doctest/doctest.h>

TEST_CASE("cursor blink moves smoothly between dark and light tones") {
    using pac::core::cursor_blink_progress;

    CHECK(cursor_blink_progress(0.0f, 0.35f) == doctest::Approx(0.0f));
    CHECK(cursor_blink_progress(0.175f, 0.35f) == doctest::Approx(0.5f));
    CHECK(cursor_blink_progress(0.35f, 0.35f) == doctest::Approx(1.0f));
    CHECK(cursor_blink_progress(0.525f, 0.35f) == doctest::Approx(0.5f));
    CHECK(cursor_blink_progress(0.70f, 0.35f) == doctest::Approx(0.0f));
    CHECK(cursor_blink_progress(1.0f, 0.0f) == doctest::Approx(0.0f));
}

TEST_CASE("cursor appearance requests reset completely each frame") {
    pac::core::CursorState cursor;
    CHECK(cursor.requested == pac::core::CursorKind::DEFAULT);
    CHECK_FALSE(cursor.inverted);
    CHECK_FALSE(cursor.hidden);

    cursor.want(pac::core::CursorKind::INTERACT);
    cursor.want_inverted();
    cursor.want_hidden();
    CHECK(cursor.requested == pac::core::CursorKind::INTERACT);
    CHECK(cursor.inverted);
    CHECK(cursor.hidden);

    cursor.reset();
    CHECK(cursor.requested == pac::core::CursorKind::DEFAULT);
    CHECK_FALSE(cursor.inverted);
    CHECK_FALSE(cursor.hidden);
}
