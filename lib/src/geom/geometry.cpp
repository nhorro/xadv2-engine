#include "engine/geom/geometry.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace pac::geom {

float distance_squared(Point a, Point b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return dx * dx + dy * dy;
}

float distance(Point a, Point b) {
    return std::sqrt(distance_squared(a, b));
}

bool point_in_polygon(Point p, const Polygon& poly) {
    const std::size_t n = poly.size();
    if (n < 3) {
        return false;
    }
    bool inside = false;
    for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
        const Point& a = poly[i];
        const Point& b = poly[j];
        const bool straddles = (a.y > p.y) != (b.y > p.y);
        if (straddles) {
            const float x_cross = (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x;
            if (p.x < x_cross) {
                inside = !inside;
            }
        }
    }
    return inside;
}

bool point_in_any_polygon(Point p, const std::vector<Polygon>& polys) {
    for (const Polygon& poly : polys) {
        if (point_in_polygon(p, poly)) {
            return true;
        }
    }
    return false;
}

sf::FloatRect polygon_bounds(const Polygon& poly) {
    if (poly.empty()) {
        return {};
    }
    float min_x = std::numeric_limits<float>::max();
    float min_y = std::numeric_limits<float>::max();
    float max_x = std::numeric_limits<float>::lowest();
    float max_y = std::numeric_limits<float>::lowest();
    for (const Point& p : poly) {
        min_x = std::min(min_x, p.x);
        min_y = std::min(min_y, p.y);
        max_x = std::max(max_x, p.x);
        max_y = std::max(max_y, p.y);
    }
    return {min_x, min_y, max_x - min_x, max_y - min_y};
}

std::optional<Point> segment_intersection(Point a, Point b, Point c, Point d) {
    const float r_x = b.x - a.x;
    const float r_y = b.y - a.y;
    const float s_x = d.x - c.x;
    const float s_y = d.y - c.y;
    const float denom = r_x * s_y - r_y * s_x;
    if (std::fabs(denom) < 1e-9f) {
        return std::nullopt; // parallel or collinear
    }
    const float t = ((c.x - a.x) * s_y - (c.y - a.y) * s_x) / denom;
    const float u = ((c.x - a.x) * r_y - (c.y - a.y) * r_x) / denom;
    if (t < 0.0f || t > 1.0f || u < 0.0f || u > 1.0f) {
        return std::nullopt;
    }
    return Point{a.x + t * r_x, a.y + t * r_y};
}

Point closest_point_on_segment(Point p, Point a, Point b) {
    const float ab_x = b.x - a.x;
    const float ab_y = b.y - a.y;
    const float len2 = ab_x * ab_x + ab_y * ab_y;
    if (len2 <= 1e-12f) {
        return a; // degenerate segment
    }
    float t = ((p.x - a.x) * ab_x + (p.y - a.y) * ab_y) / len2;
    t = std::clamp(t, 0.0f, 1.0f);
    return {a.x + t * ab_x, a.y + t * ab_y};
}

Point closest_point_in_polygon(Point p, const Polygon& poly) {
    if (poly.size() < 3 || point_in_polygon(p, poly)) {
        return p;
    }
    Point best = poly[0];
    float best_d2 = std::numeric_limits<float>::max();
    for (std::size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const Point c = closest_point_on_segment(p, poly[j], poly[i]);
        const float d2 = distance_squared(p, c);
        if (d2 < best_d2) {
            best_d2 = d2;
            best = c;
        }
    }
    return best;
}

std::vector<Point>
find_path(Point start, Point dest, const Polygon& walkable, const std::vector<Polygon>& obstacles) {
    if (walkable.size() < 3) {
        return {dest}; // ungated: no walkable polygon to honor
    }
    const auto reachable_at = [&](Point p) {
        return point_in_polygon(p, walkable) && !point_in_any_polygon(p, obstacles);
    };
    // Clamp an out-of-bounds destination to the nearest reachable spot.
    const Point goal = reachable_at(dest) ? dest : closest_point_in_polygon(dest, walkable);
    // Sample start->goal and stop at the last reachable sample, so a straight walk
    // refuses to cross the boundary or an obstacle. ~4px sampling matches the
    // avatar's per-step granularity.
    constexpr float kStep = 4.0f;
    const float dist = distance(start, goal);
    const int steps = std::max(1, static_cast<int>(std::ceil(dist / kStep)));
    Point furthest = reachable_at(start) ? start : closest_point_in_polygon(start, walkable);
    for (int i = 1; i <= steps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        const Point p{start.x + (goal.x - start.x) * t, start.y + (goal.y - start.y) * t};
        if (!reachable_at(p)) {
            break;
        }
        furthest = p;
    }
    return {furthest};
}

} // namespace pac::geom
