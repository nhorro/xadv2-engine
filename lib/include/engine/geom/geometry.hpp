#pragma once

#include <SFML/Graphics/Rect.hpp>
#include <SFML/System/Vector2.hpp>

#include <optional>
#include <vector>

/// Foundational geometry. Depends only on SFML and is fully headless-testable
/// (no graphics context). Used by hit testing, zones, walkable checks, and
/// pathfinding. YAML/Lua binding of these types lives in the loaders that need it.
namespace pac::geom {

using Point = sf::Vector2f;
using Polygon = std::vector<Point>;

float distance(Point a, Point b);
float distance_squared(Point a, Point b);

/// Even-odd point-in-polygon test. A point on an edge is considered inside.
bool point_in_polygon(Point p, const Polygon& poly);

/// True when `p` is in any of the polygons.
bool point_in_any_polygon(Point p, const std::vector<Polygon>& polys);

/// Axis-aligned bounds of a polygon. Empty polygon yields a zero rect.
sf::FloatRect polygon_bounds(const Polygon& poly);

/// Intersection point of segments ab and cd, if they cross (including touching).
/// Returns nullopt for parallel/non-intersecting segments.
std::optional<Point> segment_intersection(Point a, Point b, Point c, Point d);

/// Closest point to `p` on the segment [a, b]. A degenerate segment (a == b)
/// returns `a`.
Point closest_point_on_segment(Point p, Point a, Point b);

/// The point of `poly` (interior or boundary) nearest to `p`: returns `p`
/// unchanged when it is already inside, otherwise the nearest point on the
/// polygon's boundary. A polygon with fewer than 3 vertices returns `p`. Used to
/// clamp a click outside the walkable area to the nearest reachable spot.
Point closest_point_in_polygon(Point p, const Polygon& poly);

/// The stable pathfinding seam (design 03 §Pathfinding): returns the waypoints an
/// avatar walks from `start` toward `dest`, gated by the `walkable` area minus
/// `obstacles`. The MVP body is a straight-line stand-in — it clamps an
/// out-of-bounds `dest` to the nearest reachable point and truncates the segment
/// at the walkable boundary, so the result is a single reachable waypoint. Grid A*
/// replaces the body later without changing the `[Point]` contract. An empty/
/// degenerate `walkable` is treated as ungated and yields `{dest}`.
std::vector<Point> find_path(Point start,
                             Point dest,
                             const Polygon& walkable,
                             const std::vector<Polygon>& obstacles = {});

} // namespace pac::geom
