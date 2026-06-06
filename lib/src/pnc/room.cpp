#include "engine/pnc/room.hpp"

#include "core/load_error_yaml.hpp"
#include "engine/pnc/data_error.hpp"
#include "shader_yaml.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace pac::pnc {

const geom::Point* RoomData::point(const std::string& name) const {
    const auto it = points.find(name);
    return it != points.end() ? &it->second : nullptr;
}

const RoomConfig* RoomConfigs::find(const std::string& id) const {
    const auto it = by_id.find(id);
    return it != by_id.end() ? &it->second : nullptr;
}

bool RoomData::is_walkable(geom::Point p) const {
    if (!geom::point_in_polygon(p, walkable)) {
        return false;
    }
    for (const Obstacle& o : obstacles) {
        if (o.enabled && geom::point_in_polygon(p, o.area)) {
            return false;
        }
    }
    return true;
}

std::vector<geom::Polygon> RoomData::active_obstacles() const {
    std::vector<geom::Polygon> active;
    active.reserve(obstacles.size());
    for (const Obstacle& o : obstacles) {
        if (o.enabled) {
            active.push_back(o.area);
        }
    }
    return active;
}

float RoomData::avatar_scale_at(float y, float fallback) const {
    if (!perspective) {
        return fallback;
    }
    const Perspective& p = *perspective;
    const float span = p.bottom_y - p.top_y;
    if (std::abs(span) < 1e-3f) {
        return p.bottom_scale;
    }
    const float t = std::clamp((y - p.top_y) / span, 0.0f, 1.0f);
    return p.top_scale + t * (p.bottom_scale - p.top_scale);
}

namespace {

constexpr const char* kSource = "room-loader";

[[noreturn]] void
room_fail(const std::string& code, const std::string& msg, const YAML::Node& at = YAML::Node()) {
    pac::core::fail_at<DataError>(kSource, code, msg, at);
}

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

// Shaders on a drawable (`shader:` single + optional `shaders:` ordered list)
// share a parser with the cast loader's appearance shaders; see shader_yaml.hpp.
std::vector<gfx::ShaderEffect> parse_shaders(const YAML::Node& owner) {
    return detail::parse_shaders(owner, kSource, "room");
}

} // namespace

RoomData parse_room(const std::string& yaml_text, const std::string& expected_id) {
    YAML::Node root;
    try {
        root = YAML::Load(yaml_text);
    } catch (const YAML::Exception& e) {
        room_fail("room.invalid-yaml", std::string("invalid YAML: ") + e.what());
    }
    if (!root || !root.IsMap()) {
        room_fail("room.root-not-map", "root must be a mapping");
    }

    RoomData room;
    room.version = root["version"] ? root["version"].as<int>() : 1;
    if (!root["id"]) {
        room_fail("room.id-missing", "'id' is required", root);
    }
    room.id = root["id"].as<std::string>();
    if (!expected_id.empty() && room.id != expected_id) {
        room_fail("room.id-mismatch",
                  "'id: " + room.id + "' does not match the room filename '" + expected_id +
                      "' (rooms are loaded by id from rooms/<id>.yaml)",
                  root["id"]);
    }

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
                room_fail("room.layer-image-missing",
                          "room '" + room.id + "': a background layer is missing 'image'",
                          ln);
            }
            layer.image = ln["image"].as<std::string>();
            layer.z = ln["z"] ? ln["z"].as<float>() : 0.0f;
            layer.interactive = ln["interactive"] ? ln["interactive"].as<bool>() : false;
            layer.visible = ln["visible"] ? ln["visible"].as<bool>() : true;
            if (const YAML::Node origin = ln["origin"]) {
                layer.origin = parse_point(origin);
            }
            layer.scale = ln["scale"] ? ln["scale"].as<float>() : 1.0f;
            if (!(layer.scale > 0.0f)) {
                room_fail("room.layer-scale-invalid",
                          "room '" + room.id + "': layer '" + layer.id +
                              "' has a non-positive scale (must be > 0)",
                          ln["scale"]);
            }
            layer.shaders = parse_shaders(ln);
            room.layers.push_back(std::move(layer));
        }
    }

    if (const YAML::Node walk = root["walkable"]) {
        room.walkable = parse_polygon(walk);
    }
    for (const YAML::Node& ob : root["obstacles"] ? root["obstacles"] : YAML::Node()) {
        Obstacle obs;
        // Two forms: a bare polygon (legacy: a sequence of points) or a mapping
        // `{ id, area, enabled? }` (named/toggleable). A map with `area` is the
        // named form; anything else is parsed as a bare polygon.
        if (ob.IsMap() && ob["area"]) {
            obs.id = ob["id"] ? ob["id"].as<std::string>() : std::string();
            obs.area = parse_polygon(ob["area"]);
            obs.enabled = ob["enabled"] ? ob["enabled"].as<bool>() : true;
        } else {
            obs.area = parse_polygon(ob);
        }
        room.obstacles.push_back(std::move(obs));
    }

    if (const YAML::Node points = root["points"]) {
        for (const auto& kv : points) {
            room.points[kv.first.as<std::string>()] = parse_point(kv.second);
        }
    }

    for (const YAML::Node& zn : root["zones"] ? root["zones"] : YAML::Node()) {
        Zone zone;
        if (!zn["id"] || !zn["polygon"]) {
            room_fail("room.zone-incomplete",
                      "room '" + room.id + "': a zone needs 'id' and 'polygon'",
                      zn);
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
            if (node["over"]) {
                region.over = node["over"].as<std::string>();
            }
            if (node["baseline"]) {
                region.baseline = node["baseline"].as<float>();
            }
            if (const YAML::Node states = node["states"]) {
                for (const auto& sv : states) {
                    region.states[sv.first.as<std::string>()] = sv.second.as<std::string>();
                }
            }
            if (region.states.empty()) {
                room_fail("room.region-no-states",
                          "room '" + room.id + "': region '" + region.id + "' has no states",
                          node);
            }
            region.initial =
                node["initial"] ? node["initial"].as<std::string>() : region.states.begin()->first;
            if (!region.states.contains(region.initial)) {
                room_fail("room.region-initial-not-in-states",
                          "room '" + room.id + "': region '" + region.id + "' has initial state '" +
                              region.initial + "' which is not one of its declared states",
                          node["initial"]);
            }
            region.shaders = parse_shaders(node);
            room.regions.emplace(region.id, std::move(region));
        }
    }

    if (const YAML::Node objects = root["objects"]) {
        for (const auto& kv : objects) {
            RoomObject object;
            object.id = kv.first.as<std::string>();
            const YAML::Node node = kv.second;
            // `sprite` is the documented key (06-data-formats.md); `image` is
            // accepted as a deprecated alias. One of them is required.
            const YAML::Node sprite_node = node["sprite"] ? node["sprite"] : node["image"];
            if (!sprite_node) {
                room_fail("room.object-sprite-missing",
                          "room '" + room.id + "': object '" + object.id + "' is missing 'sprite'",
                          node);
            }
            object.sprite = sprite_node.as<std::string>();
            if (node["sequence"]) {
                object.sequence = node["sequence"].as<std::string>();
            }
            if (const YAML::Node pos = node["position"]) {
                object.position = parse_point(pos);
            }
            if (node["z"] && node["z"].IsScalar() && node["z"].as<std::string>() != "auto") {
                object.z_auto = false;
                object.z = node["z"].as<float>();
            }
            if (node["baseline"]) {
                object.baseline = node["baseline"].as<float>();
            }
            object.scale = node["scale"] ? node["scale"].as<float>() : 1.0f;
            if (!(object.scale > 0.0f)) {
                room_fail("room.object-scale-invalid",
                          "room '" + room.id + "': object '" + object.id +
                              "' has a non-positive scale (must be > 0)",
                          node["scale"]);
            }
            object.visible = node["visible"] ? node["visible"].as<bool>() : true;
            object.shaders = parse_shaders(node);
            room.objects.emplace(object.id, std::move(object));
        }
    }

    if (const YAML::Node wbs = root["walkbehinds"]) {
        for (const auto& kv : wbs) {
            WalkBehind wb;
            wb.id = kv.first.as<std::string>();
            const YAML::Node node = kv.second;
            if (!node["layer"] || !node["area"] || !node["baseline"]) {
                room_fail("room.walkbehind-incomplete",
                          "room '" + room.id + "': walkbehind '" + wb.id +
                              "' needs 'layer', 'area', and 'baseline'",
                          node);
            }
            wb.layer = node["layer"].as<std::string>();
            const bool layer_exists =
                std::any_of(room.layers.begin(), room.layers.end(), [&](const BackgroundLayer& l) {
                    return l.id == wb.layer;
                });
            if (!layer_exists) {
                room_fail("room.walkbehind-unknown-layer",
                          "room '" + room.id + "': walkbehind '" + wb.id +
                              "' references unknown layer '" + wb.layer + "'",
                          node["layer"]);
            }
            wb.area = parse_polygon(node["area"]);
            wb.baseline = node["baseline"].as<float>();
            room.walkbehinds.push_back(std::move(wb));
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
                room_fail("room.hotspot-no-area-or-bind",
                          "room '" + room.id + "': hotspot '" + hs.id +
                              "' needs an 'area' or a 'bind'",
                          node);
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
            if (hs.default_verb != "look_at" &&
                std::find(hs.affordances.begin(), hs.affordances.end(), hs.default_verb) ==
                    hs.affordances.end()) {
                room_fail("room.default-verb-not-in-affordances",
                          "room '" + room.id + "': hotspot '" + hs.id + "' has default_verb '" +
                              hs.default_verb +
                              "' which is neither 'look_at' nor one of its affordances",
                          node["default_verb"]);
            }
            hs.enabled = node["enabled"] ? node["enabled"].as<bool>() : true;
            // Default true: walk to the approach point and act on arrival (SCUMM
            // convention). Authors set `requires_approach: false` for the rare
            // act-from-a-distance interaction (e.g. shouting at a parrot in a tree).
            hs.requires_approach =
                node["requires_approach"] ? node["requires_approach"].as<bool>() : true;
            room.hotspots.emplace(hs.id, std::move(hs));
        }
    }

    for (const YAML::Node& av : root["avatars"] ? root["avatars"] : YAML::Node()) {
        RoomAvatarPlacement placement;
        if (!av["id"]) {
            room_fail("room.avatar-id-missing",
                      "room '" + room.id + "': an avatar entry is missing 'id'",
                      av);
        }
        placement.id = av["id"].as<std::string>();
        placement.start = av["start"] ? av["start"].as<std::string>() : std::string();
        placement.orientation = av["orientation"] ? av["orientation"].as<std::string>() : "down";
        placement.player = av["player"] ? av["player"].as<bool>() : false;
        room.avatars.push_back(std::move(placement));
    }

    if (const YAML::Node persp = root["perspective"]) {
        const YAML::Node top = persp["top"];
        const YAML::Node bottom = persp["bottom"];
        if (!top || !bottom) {
            room_fail("room.perspective-incomplete",
                      "room '" + room.id + "': perspective needs 'top' and 'bottom'",
                      persp);
        }
        Perspective p;
        p.top_y = top["y"].as<float>();
        p.top_scale = top["scale"].as<float>();
        p.bottom_y = bottom["y"].as<float>();
        p.bottom_scale = bottom["scale"].as<float>();
        if (!(p.top_scale > 0.0f) || !(p.bottom_scale > 0.0f)) {
            room_fail("room.perspective-scale-invalid",
                      "room '" + room.id + "': perspective scales must be > 0",
                      persp);
        }
        room.perspective = p;
    }

    // Declarative configurations (#185): presence (managed NPCs / objects /
    // obstacles) per symbolic config id. Parsed last so references resolve against
    // the already-parsed points / objects / obstacles. Behavior beats live in Lua.
    if (const YAML::Node cfgs = root["configs"]) {
        if (!cfgs.IsMap()) {
            room_fail("room.configs-not-map",
                      "room '" + room.id + "': 'configs' must be a mapping",
                      cfgs);
        }
        if (!cfgs["start"]) {
            room_fail("room.config-start-missing",
                      "room '" + room.id + "': 'configs' needs a 'start' config id",
                      cfgs);
        }
        RoomConfigs rc;
        rc.start = cfgs["start"].as<std::string>();
        for (const auto& kv : cfgs) {
            const std::string key = kv.first.as<std::string>();
            if (key == "start") {
                continue; // reserved key, not a config
            }
            const YAML::Node& body = kv.second;
            RoomConfig c;
            c.id = key;
            if (const YAML::Node present = body["present"]) {
                if (!present.IsMap()) {
                    room_fail("room.config-present-not-map",
                              "room '" + room.id + "': config '" + key +
                                  "' has a non-mapping 'present'",
                              present);
                }
                for (const auto& nkv : present["npcs"] ? present["npcs"] : YAML::Node()) {
                    const std::string npc_id = nkv.first.as<std::string>();
                    RoomConfig::Npc npc;
                    if (!nkv.second["at"]) {
                        room_fail("room.config-npc-at-missing",
                                  "room '" + room.id + "': config '" + key + "' npc '" + npc_id +
                                      "' needs an 'at' point",
                                  nkv.second);
                    }
                    npc.at = nkv.second["at"].as<std::string>();
                    if (!room.point(npc.at)) {
                        room_fail("room.config-npc-point-unknown",
                                  "room '" + room.id + "': config '" + key + "' npc '" + npc_id +
                                      "' at unknown point '" + npc.at + "'",
                                  nkv.second);
                    }
                    npc.facing =
                        nkv.second["facing"] ? nkv.second["facing"].as<std::string>() : "down";
                    c.npcs[npc_id] = std::move(npc);
                }
                for (const YAML::Node& o : present["objects"] ? present["objects"] : YAML::Node()) {
                    const std::string oid = o.as<std::string>();
                    if (room.objects.find(oid) == room.objects.end()) {
                        room_fail("room.config-object-unknown",
                                  "room '" + room.id + "': config '" + key +
                                      "' lists unknown object '" + oid + "'",
                                  o);
                    }
                    c.objects.push_back(oid);
                }
                for (const YAML::Node& o :
                     present["obstacles"] ? present["obstacles"] : YAML::Node()) {
                    const std::string oid = o.as<std::string>();
                    const bool found =
                        std::any_of(room.obstacles.begin(),
                                    room.obstacles.end(),
                                    [&](const Obstacle& ob) { return ob.id == oid; });
                    if (!found) {
                        room_fail("room.config-obstacle-unknown",
                                  "room '" + room.id + "': config '" + key +
                                      "' lists unknown obstacle '" + oid +
                                      "' (only obstacles with an 'id' can be config-managed)",
                                  o);
                    }
                    c.obstacles.push_back(oid);
                }
            }
            rc.order.push_back(key);
            rc.by_id[key] = std::move(c);
        }
        if (!rc.find(rc.start)) {
            room_fail("room.config-start-unknown",
                      "room '" + room.id + "': start config '" + rc.start + "' is not defined",
                      cfgs["start"]);
        }
        // Managed sets = union of every id any config controls, so reconciling one
        // config turns the others' actors off. Dedup via a sorted unique pass.
        auto union_of = [&](auto picker) {
            std::vector<std::string> all;
            for (const auto& [id, c] : rc.by_id) {
                picker(c, all);
            }
            std::sort(all.begin(), all.end());
            all.erase(std::unique(all.begin(), all.end()), all.end());
            return all;
        };
        rc.managed_npcs = union_of([](const RoomConfig& c, std::vector<std::string>& out) {
            for (const auto& [id, npc] : c.npcs) {
                out.push_back(id);
            }
        });
        rc.managed_objects = union_of([](const RoomConfig& c, std::vector<std::string>& out) {
            out.insert(out.end(), c.objects.begin(), c.objects.end());
        });
        rc.managed_obstacles = union_of([](const RoomConfig& c, std::vector<std::string>& out) {
            out.insert(out.end(), c.obstacles.begin(), c.obstacles.end());
        });
        room.configs = std::move(rc);
    }

    return room;
}

} // namespace pac::pnc
