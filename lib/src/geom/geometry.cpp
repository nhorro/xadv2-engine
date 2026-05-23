#include "engine/geom/geometry.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <limits>
#include <queue>
#include <utility>

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

namespace {

float orient(Point o, Point a, Point b) {
    return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}

// Strict crossing only: segments meeting at a shared endpoint (two graph edges
// touching at a corner node) are not crossings, so corner nodes stay connectable.
bool segments_properly_cross(Point a1, Point a2, Point b1, Point b2) {
    const float d1 = orient(b1, b2, a1);
    const float d2 = orient(b1, b2, a2);
    const float d3 = orient(a1, a2, b1);
    const float d4 = orient(a1, a2, b2);
    return ((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) && ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0));
}

bool segment_crosses_polygon(Point a, Point b, const Polygon& poly) {
    const std::size_t n = poly.size();
    if (n < 2) {
        return false;
    }
    for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
        if (segments_properly_cross(a, b, poly[j], poly[i])) {
            return true;
        }
    }
    return false;
}

// A chord is travelable when its midpoint stays in free space and it neither
// leaves the walkable area nor pierces an obstacle. Exact for convex obstacles.
bool chord_clear(Point a, Point b, const Polygon& walkable, const std::vector<Polygon>& obstacles) {
    const Point mid{(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f};
    if (!point_in_polygon(mid, walkable) || point_in_any_polygon(mid, obstacles)) {
        return false;
    }
    if (segment_crosses_polygon(a, b, walkable)) {
        return false;
    }
    for (const Polygon& ob : obstacles) {
        if (segment_crosses_polygon(a, b, ob)) {
            return false;
        }
    }
    return true;
}

// Routing nodes sit a few px off each corner, on the free-space side, so edges
// between them clear the boundary instead of grazing it (where the midpoint test
// is ambiguous). Trying the bisector both ways handles obstacle corners (push
// out) and concave walkable corners (push in) without tracking winding.
void push_corner_nodes(const Polygon& poly,
                       const Polygon& walkable,
                       const std::vector<Polygon>& obstacles,
                       std::vector<Point>& out) {
    const std::size_t n = poly.size();
    if (n < 3) {
        return;
    }
    constexpr float kInset = 4.0f;
    for (std::size_t i = 0; i < n; ++i) {
        const Point v = poly[i];
        const Point prev = poly[(i + n - 1) % n];
        const Point next = poly[(i + 1) % n];
        const Point d1{prev.x - v.x, prev.y - v.y};
        const Point d2{next.x - v.x, next.y - v.y};
        const float l1 = std::hypot(d1.x, d1.y);
        const float l2 = std::hypot(d2.x, d2.y);
        if (l1 < 1e-6f || l2 < 1e-6f) {
            continue;
        }
        Point bis{d1.x / l1 + d2.x / l2, d1.y / l1 + d2.y / l2};
        const float bl = std::hypot(bis.x, bis.y);
        if (bl < 1e-6f) {
            continue; // collinear corner: nothing to round
        }
        bis = {bis.x / bl * kInset, bis.y / bl * kInset};
        for (const float s : {1.0f, -1.0f}) {
            const Point cand{v.x + s * bis.x, v.y + s * bis.y};
            if (point_in_polygon(cand, walkable) && !point_in_any_polygon(cand, obstacles)) {
                out.push_back(cand);
            }
        }
    }
}

} // namespace

std::vector<Point>
find_path(Point start, Point dest, const Polygon& walkable, const std::vector<Polygon>& obstacles) {
    if (walkable.size() < 3) {
        return {dest}; // ungated: no walkable area to honor
    }
    const auto reachable_at = [&](Point p) {
        return point_in_polygon(p, walkable) && !point_in_any_polygon(p, obstacles);
    };
    // Clamp endpoints onto the walkable area so a click just outside it, or float
    // drift at the avatar's feet, still routes.
    const Point from = reachable_at(start) ? start : closest_point_in_polygon(start, walkable);
    const Point goal = reachable_at(dest) ? dest : closest_point_in_polygon(dest, walkable);

    if (chord_clear(from, goal, walkable, obstacles)) {
        return {goal};
    }

    std::vector<Point> nodes{from, goal};
    push_corner_nodes(walkable, walkable, obstacles, nodes);
    for (const Polygon& ob : obstacles) {
        push_corner_nodes(ob, walkable, obstacles, nodes);
    }

    const std::size_t n = nodes.size();
    std::vector<std::vector<std::pair<std::size_t, float>>> adjacency(n);
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = i + 1; j < n; ++j) {
            if (chord_clear(nodes[i], nodes[j], walkable, obstacles)) {
                const float c = distance(nodes[i], nodes[j]);
                adjacency[i].push_back({j, c});
                adjacency[j].push_back({i, c});
            }
        }
    }

    constexpr std::size_t kStart = 0;
    constexpr std::size_t kGoal = 1;
    std::vector<float> g(n, std::numeric_limits<float>::infinity());
    std::vector<std::size_t> prev(n, n);
    g[kStart] = 0.0f;

    struct Open {
        float f;
        std::size_t id;
        bool operator>(const Open& o) const { return f > o.f; }
    };
    std::priority_queue<Open, std::vector<Open>, std::greater<Open>> open;
    open.push({distance(from, goal), kStart});

    while (!open.empty()) {
        const Open top = open.top();
        open.pop();
        if (top.f > g[top.id] + distance(nodes[top.id], goal) + 1e-4f) {
            continue; // stale queue entry, superseded by a cheaper path
        }
        if (top.id == kGoal) {
            std::vector<Point> path;
            for (std::size_t at = kGoal; at != n; at = prev[at]) {
                path.push_back(nodes[at]);
                if (at == kStart) {
                    break;
                }
            }
            std::reverse(path.begin(), path.end());
            path.erase(path.begin()); // drop start: callers walk toward the waypoints
            return path;
        }
        for (const auto& [to, cost] : adjacency[top.id]) {
            const float tentative = g[top.id] + cost;
            if (tentative < g[to]) {
                g[to] = tentative;
                prev[to] = top.id;
                open.push({tentative + distance(nodes[to], goal), to});
            }
        }
    }

    // No corner route exists: walk straight until the boundary or an obstacle
    // stops us, so the caller still gets a reachable waypoint (never empty).
    constexpr float kStep = 4.0f;
    const float dist = distance(from, goal);
    const int steps = std::max(1, static_cast<int>(std::ceil(dist / kStep)));
    Point furthest = from;
    for (int i = 1; i <= steps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        const Point p{from.x + (goal.x - from.x) * t, from.y + (goal.y - from.y) * t};
        if (!reachable_at(p)) {
            break;
        }
        furthest = p;
    }
    return {furthest};
}

} // namespace pac::geom
