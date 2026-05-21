#pragma once

#include "engine/geom/geometry.hpp"

#include <SFML/Graphics/Color.hpp>
#include <SFML/System/Vector2.hpp>

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace pac::pnc {

struct BackgroundLayer {
    std::string id;
    std::string image; // logical path, relative to the room file's directory
    float z = 0.0f;
    bool interactive = false;
};

struct RoomHotspot {
    std::string id;
    std::string name;                     // localized noun
    geom::Polygon area;                   // hit-test polygon (M3: required)
    std::optional<geom::Point> approach;  // resolved approach point, if any
    std::vector<std::string> affordances; // verbs the UI may offer
    std::string default_verb = "look_at";
    std::string bind; // "object:<id>" / "region:<id>" (unused in M3)
    bool enabled = true;
};

struct RoomAvatarPlacement {
    std::string id;
    std::string start; // point name
    std::string orientation = "down";
    bool player = false;
};

/// Trigger polygon for room exits / scripted events (fires on_zone_enter/exit).
struct Zone {
    std::string id;
    geom::Polygon polygon;
};

/// A changeable part of the background with named states (e.g. drawer shut/open),
/// swapped with set_region_state. A state image may be empty (draws nothing).
struct Region {
    std::string id;
    geom::Polygon area; // footprint (also a hit source)
    float z = 0.0f;
    std::map<std::string, std::string> states; // state id -> image logical path
    std::string initial;
};

/// An active sprite placed in the room (visual only; interactivity via a hotspot).
struct RoomObject {
    std::string id;
    std::string image;          // logical path (M4: static texture)
    geom::Point position{0, 0}; // world position of the top-left
    bool z_auto = true;         // auto: depth = position.y; else explicit z
    float z = 0.0f;
    bool visible = true;
};

/// Parsed static room definition (`rooms/<id>.yaml`). Headless and testable; the
/// behavior lives in `rooms/<id>.lua`.
struct RoomData {
    int version = 1;
    std::string id;
    sf::Vector2u size{0, 0};
    sf::Color background_color = sf::Color::Black;
    std::vector<BackgroundLayer> layers;
    geom::Polygon walkable;
    std::vector<geom::Polygon> obstacles;
    std::map<std::string, geom::Point> points;
    std::vector<Zone> zones;
    std::map<std::string, Region> regions;
    std::map<std::string, RoomObject> objects;
    std::map<std::string, RoomHotspot> hotspots;
    std::vector<RoomAvatarPlacement> avatars;

    const geom::Point* point(const std::string& name) const;
    bool is_walkable(geom::Point p) const;
};

/// Parse + validate room YAML. Throws DataError on malformed input. Hotspot
/// `approach` given as a point name is resolved against `points`.
RoomData parse_room(const std::string& yaml_text);

} // namespace pac::pnc
