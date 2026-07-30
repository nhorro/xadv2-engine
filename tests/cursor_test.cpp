#include "engine/core/cursor.hpp"

#include <doctest/doctest.h>

TEST_CASE("cursor appearance requests reset completely each frame") {
    pac::core::CursorState cursor;
    CHECK(cursor.requested == pac::core::CursorKind::DEFAULT);
    CHECK_FALSE(cursor.inverted);

    cursor.want(pac::core::CursorKind::INTERACT);
    cursor.want_inverted();
    CHECK(cursor.requested == pac::core::CursorKind::INTERACT);
    CHECK(cursor.inverted);

    cursor.reset();
    CHECK(cursor.requested == pac::core::CursorKind::DEFAULT);
    CHECK_FALSE(cursor.inverted);
}
