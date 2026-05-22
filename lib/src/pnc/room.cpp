#include "engine/pnc/room.hpp"

#include "engine/pnc/data_error.hpp"

#include <yaml-cpp/yaml.h>

#include <utility>

namespace pac::pnc {

const geom::Point* RoomData::point(const std::string& name) const {
    const auto it = points.find(name);
    return it != points.end() ? &it->second : nullptr;
}

bool RoomData::is_walkable(geom::Point p) const {
    return geom::point_in_polygon(p, walkable) && !geom::point_in_any_polygon(p, obstacles);
}

namespace {

geom::Point parse_point(const YAML::Node& node) {
    return {node["x"].as<float>(), node["y"].as<float>()};
}

geom::Polygon parse_polygon(const YAML::Node& node) {
    geom::Polygon poly;
    for (const YAML::Node& p : node) {
        poly.push_back(parse_point(p));
    }
    return poly;
}

} // namespace

RoomData parse_room(const std::string& yaml_text) {
    YAML::Node root;
    try {
        root = YAML::Load(yaml_text);
    } catch (const YAML::Exception& e) {
        throw DataError(std::string("room: invalid YAML: ") + e.what());
    }
    if (!root || !root.IsMap()) {
        throw DataError("room: root must be a mapping");
    }

    RoomData room;
    room.version = root["version"] ? root["version"].as<int>() : 1;
    if (!root["id"]) {
        throw DataError("room: 'id' is required");
    }
    room.id = root["id"].as<std::string>();

    const YAML::Node size = root["size"];
    if (!size || !size["width"] || !size["height"]) {
        throw DataError("room '" + room.id + "': 'size.width/height' is required");
    }
    room.size = {size["width"].as<unsigned>(), size["height"].as<unsigned>()};

    if (const YAML::Node bg = root["background"]) {
        if (const YAML::Node color = bg["color"]) {
            room.background_color = sf::Color(color["r"].as<unsigned>(),
                                              color["g"].as<unsigned>(),
                                              color["b"].as<unsigned>(),
                                              color["a"] ? color["a"].as<unsigned>() : 255);
        }
        for (const YAML::Node& ln : bg["layers"] ? bg["layers"] : YAML::Node()) {
            BackgroundLayer layer;
            layer.id = ln["id"] ? ln["id"].as<std::string>() : std::string();
            if (!ln["image"]) {
                throw DataError("room '" + room.id + "': a background layer is missing 'image'");
            }
            layer.image = ln["image"].as<std::string>();
            layer.z = ln["z"] ? ln["z"].as<float>() : 0.0f;
            layer.interactive = ln["interactive"] ? ln["interactive"].as<bool>() : false;
            if (const YAML::Node origin = ln["origin"]) {
                layer.origin = parse_point(origin);
            }
            room.layers.push_back(std::move(layer));
        }
    }

    if (const YAML::Node walk = root["walkable"]) {
        room.walkable = parse_polygon(walk);
    }
    for (const YAML::Node& ob : root["obstacles"] ? root["obstacles"] : YAML::Node()) {
        room.obstacles.push_back(parse_polygon(ob));
    }

    if (const YAML::Node points = root["points"]) {
        for (const auto& kv : points) {
            room.points[kv.first.as<std::string>()] = parse_point(kv.second);
        }
    }

    for (const YAML::Node& zn : root["zones"] ? root["zones"] : YAML::Node()) {
        Zone zone;
        if (!zn["id"] || !zn["polygon"]) {
            throw DataError("room '" + room.id + "': a zone needs 'id' and 'polygon'");
        }
        zone.id = zn["id"].as<std::string>();
        zone.polygon = parse_polygon(zn["polygon"]);
        room.zones.push_back(std::move(zone));
    }

    if (const YAML::Node regions = root["regions"]) {
        for (const auto& kv : regions) {
            Region region;
            region.id = kv.first.as<std::string>();
            const YAML::Node node = kv.second;
            if (const YAML::Node area = node["area"]) {
                region.area = parse_polygon(area);
            }
            region.z = node["z"] ? node["z"].as<float>() : 0.0f;
            if (const YAML::Node states = node["states"]) {
                for (const auto& sv : states) {
                    region.states[sv.first.as<std::string>()] = sv.second.as<std::string>();
                }
            }
            if (region.states.empty()) {
                throw DataError("room '" + room.id + "': region '" + region.id + "' has no states");
            }
            region.initial =
                node["initial"] ? node["initial"].as<std::string>() : region.states.begin()->first;
            room.regions.emplace(region.id, std::move(region));
        }
    }

    if (const YAML::Node objects = root["objects"]) {
        for (const auto& kv : objects) {
            RoomObject object;
            object.id = kv.first.as<std::string>();
            const YAML::Node node = kv.second;
            object.image = node["image"] ? node["image"].as<std::string>() : std::string();
            if (const YAML::Node pos = node["position"]) {
                object.position = parse_point(pos);
            }
            if (node["z"] && node["z"].IsScalar() && node["z"].as<std::string>() != "auto") {
                object.z_auto = false;
                object.z = node["z"].as<float>();
            }
            object.visible = node["visible"] ? node["visible"].as<bool>() : true;
            room.objects.emplace(object.id, std::move(object));
        }
    }

    if (const YAML::Node hotspots = root["hotspots"]) {
        for (const auto& kv : hotspots) {
            RoomHotspot hs;
            hs.id = kv.first.as<std::string>();
            const YAML::Node node = kv.second;
            hs.name = node["name"] ? node["name"].as<std::string>() : hs.id;
            if (const YAML::Node area = node["area"]) {
                hs.area = parse_polygon(area);
            }
            if (node["bind"]) {
                hs.bind = node["bind"].as<std::string>();
            }
            if (hs.area.empty() && hs.bind.empty()) {
                throw DataError("room '" + room.id + "': hotspot '" + hs.id +
                                "' needs an 'area' or a 'bind'");
            }
            if (const YAML::Node approach = node["approach"]) {
                if (approach.IsScalar()) {
                    const std::string name = approach.as<std::string>();
                    if (const geom::Point* p = room.point(name)) {
                        hs.approach = *p;
                    }
                } else {
                    hs.approach = parse_point(approach);
                }
            }
            for (const YAML::Node& a : node["affordances"] ? node["affordances"] : YAML::Node()) {
                hs.affordances.push_back(a.as<std::string>());
            }
            hs.default_verb =
                node["default_verb"] ? node["default_verb"].as<std::string>() : "look_at";
            hs.enabled = node["enabled"] ? node["enabled"].as<bool>() : true;
            room.hotspots.emplace(hs.id, std::move(hs));
        }
    }

    for (const YAML::Node& av : root["avatars"] ? root["avatars"] : YAML::Node()) {
        RoomAvatarPlacement placement;
        if (!av["id"]) {
            throw DataError("room '" + room.id + "': an avatar entry is missing 'id'");
        }
        placement.id = av["id"].as<std::string>();
        placement.start = av["start"] ? av["start"].as<std::string>() : std::string();
        placement.orientation = av["orientation"] ? av["orientation"].as<std::string>() : "down";
        placement.player = av["player"] ? av["player"].as<bool>() : false;
        room.avatars.push_back(std::move(placement));
    }

    return room;
}

} // namespace pac::pnc
