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

} // namespace pac::geom
