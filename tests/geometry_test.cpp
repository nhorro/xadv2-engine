#include "engine/geom/geometry.hpp"

#include <doctest/doctest.h>

#include <vector>

using namespace pac::geom;

TEST_CASE("distance and distance_squared") {
    CHECK(distance({0, 0}, {3, 4}) == doctest::Approx(5.0f));
    CHECK(distance_squared({0, 0}, {3, 4}) == doctest::Approx(25.0f));
}

TEST_CASE("point_in_polygon for a square") {
    const Polygon square = {{0, 0}, {10, 0}, {10, 10}, {0, 10}};
    CHECK(point_in_polygon({5, 5}, square));
    CHECK_FALSE(point_in_polygon({15, 5}, square));
    CHECK_FALSE(point_in_polygon({-1, 5}, square));
    CHECK_FALSE(point_in_polygon({5, 5}, Polygon{})); // degenerate
}

TEST_CASE("point_in_any_polygon (walkable obstacle check)") {
    const Polygon a = {{0, 0}, {2, 0}, {2, 2}, {0, 2}};
    const Polygon b = {{5, 5}, {7, 5}, {7, 7}, {5, 7}};
    const std::vector<Polygon> polys = {a, b};
    CHECK(point_in_any_polygon({1, 1}, polys));
    CHECK(point_in_any_polygon({6, 6}, polys));
    CHECK_FALSE(point_in_any_polygon({3, 3}, polys));
}

TEST_CASE("polygon_bounds") {
    const Polygon p = {{1, 2}, {5, 2}, {5, 8}, {1, 8}};
    const sf::FloatRect b = polygon_bounds(p);
    CHECK(b.left == doctest::Approx(1.0f));
    CHECK(b.top == doctest::Approx(2.0f));
    CHECK(b.width == doctest::Approx(4.0f));
    CHECK(b.height == doctest::Approx(6.0f));
}

TEST_CASE("segment_intersection") {
    const auto cross = segment_intersection({0, 0}, {10, 10}, {0, 10}, {10, 0});
    REQUIRE(cross.has_value());
    CHECK(cross->x == doctest::Approx(5.0f));
    CHECK(cross->y == doctest::Approx(5.0f));

    // Non-parallel but the crossing lies beyond the segments.
    CHECK_FALSE(segment_intersection({0, 0}, {1, 0}, {2, 2}, {2, -2}).has_value());
    // Parallel.
    CHECK_FALSE(segment_intersection({0, 0}, {10, 0}, {0, 1}, {10, 1}).has_value());
}
