#include "engine/pnc/debug_overlay.hpp"

#include "engine/geom/geometry.hpp"
#include "engine/pnc/avatar.hpp"
#include "engine/pnc/room.hpp"

#include <SFML/Graphics/CircleShape.hpp>
#include <SFML/Graphics/Color.hpp>
#include <SFML/Graphics/Font.hpp>
#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/Graphics/Text.hpp>
#include <SFML/Graphics/VertexArray.hpp>

#include <cmath>
#include <string>

namespace pac::pnc {

bool DebugOverlayFlags::toggle(sf::Keyboard::Key key) {
    switch (key) {
    case sf::Keyboard::F1:
        walkboxes = !walkboxes;
        return true;
    case sf::Keyboard::F2:
        hotspots = !hotspots;
        return true;
    case sf::Keyboard::F3:
        anchors = !anchors;
        return true;
    case sf::Keyboard::F4:
        hud = !hud;
        return true;
    default:
        return false;
    }
}

namespace {

const sf::Color kWalkable(40, 220, 40);
const sf::Color kObstacle(230, 60, 60);
const sf::Color kHotspot(40, 220, 220);
const sf::Color kApproach(245, 230, 60);
const sf::Color kAnchor(235, 70, 235);

void outline_polygon(sf::RenderTarget& target, const geom::Polygon& poly, sf::Color color) {
    if (poly.size() < 2) {
        return;
    }
    sf::VertexArray strip(sf::LineStrip, poly.size() + 1);
    for (std::size_t i = 0; i < poly.size(); ++i) {
        strip[i].position = poly[i];
        strip[i].color = color;
    }
    strip[poly.size()].position = poly[0]; // close the loop
    strip[poly.size()].color = color;
    target.draw(strip);
}

void world_label(sf::RenderTarget& target,
                 const sf::Font& font,
                 const std::string& s,
                 geom::Point at,
                 sf::Color color) {
    sf::Text text(s, font, 11);
    text.setFillColor(color);
    text.setPosition(std::round(at.x), std::round(at.y));
    target.draw(text);
}

void anchor_marker(sf::RenderTarget& target, geom::Point p, sf::Color color) {
    constexpr float kArm = 6.0f; // cross half-length in world units
    sf::VertexArray cross(sf::Lines, 4);
    cross[0] = sf::Vertex({p.x - kArm, p.y}, color);
    cross[1] = sf::Vertex({p.x + kArm, p.y}, color);
    cross[2] = sf::Vertex({p.x, p.y - kArm}, color);
    cross[3] = sf::Vertex({p.x, p.y + kArm}, color);
    target.draw(cross);
}

} // namespace

void DebugOverlay::draw_world(sf::RenderTarget& target,
                              const DebugOverlayFlags& flags,
                              const RoomData& room,
                              const Avatar* player,
                              const std::vector<const Avatar*>& npcs,
                              const sf::Font* font) const {
    if (flags.walkboxes) {
        outline_polygon(target, room.walkable, kWalkable);
        for (const geom::Polygon& obstacle : room.obstacles) {
            outline_polygon(target, obstacle, kObstacle);
        }
    }

    if (flags.hotspots) {
        for (const auto& [id, hs] : room.hotspots) {
            // An explicit area, else the polygon of a region this hotspot binds to.
            const geom::Polygon* area = nullptr;
            if (!hs.area.empty()) {
                area = &hs.area;
            } else if (hs.bind.starts_with("region:")) {
                const auto it = room.regions.find(hs.bind.substr(std::string("region:").size()));
                if (it != room.regions.end()) {
                    area = &it->second.area;
                }
            }
            if (area != nullptr && area->size() >= 2) {
                outline_polygon(target, *area, kHotspot);
                if (font != nullptr) {
                    const sf::FloatRect b = geom::polygon_bounds(*area);
                    world_label(target,
                                *font,
                                hs.name.empty() ? id : hs.name,
                                {b.left + 2.0f, b.top + 2.0f},
                                kHotspot);
                }
            }
            if (hs.approach) {
                sf::CircleShape dot(5.0f);
                dot.setOrigin(5.0f, 5.0f);
                dot.setPosition(*hs.approach);
                dot.setFillColor(sf::Color(kApproach.r, kApproach.g, kApproach.b, 150));
                dot.setOutlineThickness(1.0f);
                dot.setOutlineColor(kApproach);
                target.draw(dot);
            }
        }
    }

    if (flags.anchors) {
        const auto draw_anchor = [&](const Avatar& a) {
            const geom::Point p = a.position();
            anchor_marker(target, p, kAnchor);
            if (font != nullptr) {
                world_label(target,
                            *font,
                            "z=" + std::to_string(static_cast<int>(a.z())),
                            {p.x + 8.0f, p.y - 6.0f},
                            kAnchor);
            }
        };
        if (player != nullptr) {
            draw_anchor(*player);
        }
        for (const Avatar* npc : npcs) {
            if (npc != nullptr) {
                draw_anchor(*npc);
            }
        }
    }
}

void DebugOverlay::draw_hud(sf::RenderTarget& target,
                            const sf::Font* font,
                            const std::string& text) const {
    if (font == nullptr || text.empty()) {
        return;
    }
    sf::Text t(text, *font, 12);
    t.setFillColor(sf::Color(235, 235, 235));
    t.setPosition(10.0f, 10.0f);
    const sf::FloatRect b = t.getLocalBounds();
    sf::RectangleShape bg({b.width + 14.0f, b.height + 14.0f});
    bg.setPosition(6.0f, 6.0f);
    bg.setFillColor(sf::Color(0, 0, 0, 175));
    target.draw(bg);
    target.draw(t);
}

} // namespace pac::pnc
