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

TEST_CASE("closest_point_on_segment") {
    // Projection lands inside the segment.
    const Point m = closest_point_on_segment({5, 5}, {0, 0}, {10, 0});
    CHECK(m.x == doctest::Approx(5.0f));
    CHECK(m.y == doctest::Approx(0.0f));
    // Projection beyond an endpoint clamps to that endpoint.
    const Point e = closest_point_on_segment({-3, 4}, {0, 0}, {10, 0});
    CHECK(e.x == doctest::Approx(0.0f));
    CHECK(e.y == doctest::Approx(0.0f));
    // Degenerate segment returns the point itself.
    const Point d = closest_point_on_segment({7, 7}, {2, 2}, {2, 2});
    CHECK(d.x == doctest::Approx(2.0f));
    CHECK(d.y == doctest::Approx(2.0f));
}

TEST_CASE("closest_point_in_polygon clamps a click to the walkable area") {
    const Polygon square = {{0, 0}, {10, 0}, {10, 10}, {0, 10}};
    // Already inside: returned unchanged.
    const Point in = closest_point_in_polygon({5, 5}, square);
    CHECK(in.x == doctest::Approx(5.0f));
    CHECK(in.y == doctest::Approx(5.0f));
    // To the right: clamps onto the right edge.
    const Point right = closest_point_in_polygon({20, 5}, square);
    CHECK(right.x == doctest::Approx(10.0f));
    CHECK(right.y == doctest::Approx(5.0f));
    // Above a corner: clamps onto the nearest vertex.
    const Point corner = closest_point_in_polygon({-4, -3}, square);
    CHECK(corner.x == doctest::Approx(0.0f));
    CHECK(corner.y == doctest::Approx(0.0f));
    // Degenerate polygon returns the point unchanged.
    const Point degen = closest_point_in_polygon({3, 3}, Polygon{{0, 0}, {1, 1}});
    CHECK(degen.x == doctest::Approx(3.0f));
    CHECK(degen.y == doctest::Approx(3.0f));
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

TEST_CASE("find_path is a straight-line walk gated by the walkable area") {
    const Polygon room{{0, 0}, {100, 0}, {100, 100}, {0, 100}};

    // Clear straight line: the single waypoint is the destination itself.
    {
        const auto path = find_path({10, 10}, {90, 90}, room);
        REQUIRE(path.size() == 1);
        CHECK(path.back().x == doctest::Approx(90.0f));
        CHECK(path.back().y == doctest::Approx(90.0f));
    }

    // Destination outside the walkable area: clamped onto the boundary, and the
    // straight walk reaches that clamped point.
    {
        const auto path = find_path({50, 50}, {150, 50}, room);
        REQUIRE(path.size() == 1);
        // Stops just inside the clamped boundary (the sampler is ~one step
        // conservative at the very edge), never beyond it.
        CHECK(path.back().x <= 100.0f);
        CHECK(path.back().x >= 96.0f);
        CHECK(path.back().y == doctest::Approx(50.0f));
    }

    // An obstacle across the path truncates the walk before it: the waypoint is
    // short of the destination (does not enter the obstacle).
    {
        const std::vector<Polygon> obstacles{{{40, 0}, {60, 0}, {60, 100}, {40, 100}}};
        const auto path = find_path({10, 50}, {90, 50}, room, obstacles);
        REQUIRE(path.size() == 1);
        CHECK(path.back().x < 40.0f);
    }

    // No walkable polygon: ungated, returns the destination unchanged.
    {
        const auto path = find_path({0, 0}, {500, 500}, Polygon{});
        REQUIRE(path.size() == 1);
        CHECK(path.back().x == doctest::Approx(500.0f));
    }
}
