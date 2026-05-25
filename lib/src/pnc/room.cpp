#include "engine/pnc/room.hpp"

#include "core/load_error_yaml.hpp"
#include "engine/pnc/data_error.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace pac::pnc {

const geom::Point* RoomData::point(const std::string& name) const {
    const auto it = points.find(name);
    return it != points.end() ? &it->second : nullptr;
}

bool RoomData::is_walkable(geom::Point p) const {
    return geom::point_in_polygon(p, walkable) && !geom::point_in_any_polygon(p, obstacles);
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

// A `vecN` shader param from a 2/3/4-element number sequence.
gfx::ShaderValue parse_shader_vec(const YAML::Node& seq) {
    switch (seq.size()) {
    case 2:
        return std::array<float, 2>{seq[0].as<float>(), seq[1].as<float>()};
    case 3:
        return std::array<float, 3>{seq[0].as<float>(), seq[1].as<float>(), seq[2].as<float>()};
    case 4:
        return std::array<float, 4>{seq[0].as<float>(),
                                    seq[1].as<float>(),
                                    seq[2].as<float>(),
                                    seq[3].as<float>()};
    default:
        room_fail("room.shader-param-vec-size",
                  "a shader vector param must have 2, 3, or 4 numbers",
                  seq);
    }
}

// Resolve a shader param value (design 06 §Shaders). A sequence infers vecN; an
// explicit {type, value} mapping overrides; a bare scalar infers bool from
// true/false and otherwise float (an integer literal becomes a float — declare
// {type: int} for a true int uniform).
gfx::ShaderValue parse_shader_value(const YAML::Node& node) {
    if (node.IsSequence()) {
        return parse_shader_vec(node);
    }
    if (node.IsMap()) {
        if (!node["type"] || !node["value"]) {
            room_fail("room.shader-param-form",
                      "a shader param mapping needs 'type' and 'value'",
                      node);
        }
        const std::string type = node["type"].as<std::string>();
        const YAML::Node value = node["value"];
        if (type == "bool") {
            return value.as<bool>();
        }
        if (type == "int") {
            return value.as<int>();
        }
        if (type == "float") {
            return value.as<float>();
        }
        if (type == "vec2" || type == "vec3" || type == "vec4") {
            return parse_shader_vec(value);
        }
        room_fail("room.shader-param-type",
                  "unknown shader param type '" + type + "' (want bool/int/float/vec2/vec3/vec4)",
                  node["type"]);
    }
    const std::string scalar = node.Scalar();
    if (scalar == "true") {
        return true;
    }
    if (scalar == "false") {
        return false;
    }
    return node.as<float>();
}

// A single shader effect: a bare string is shorthand for `{source: <string>}`.
gfx::ShaderEffect parse_one_shader(const YAML::Node& node) {
    gfx::ShaderEffect fx;
    if (node.IsScalar()) {
        fx.source = node.as<std::string>();
        return fx;
    }
    if (!node["source"]) {
        room_fail("room.shader-source-missing", "a shader needs a 'source'", node);
    }
    fx.source = node["source"].as<std::string>();
    if (node["controller"]) {
        fx.controller = node["controller"].as<std::string>();
    }
    if (node["enabled"]) {
        fx.enabled = node["enabled"].as<bool>();
    }
    if (const YAML::Node params = node["params"]) {
        for (const auto& kv : params) {
            gfx::ShaderParam p;
            p.name = kv.first.as<std::string>();
            p.value = parse_shader_value(kv.second);
            fx.params.push_back(std::move(p));
        }
    }
    return fx;
}

// Shaders on a drawable: `shader` (one, shorthand or mapping) and/or `shaders`
// (an ordered list). Both may appear; `shader` comes first.
std::vector<gfx::ShaderEffect> parse_shaders(const YAML::Node& owner) {
    std::vector<gfx::ShaderEffect> out;
    if (const YAML::Node one = owner["shader"]) {
        out.push_back(parse_one_shader(one));
    }
    if (const YAML::Node list = owner["shaders"]) {
        for (const YAML::Node& e : list) {
            out.push_back(parse_one_shader(e));
        }
    }
    return out;
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
            object.image = node["image"] ? node["image"].as<std::string>() : std::string();
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

    return room;
}

} // namespace pac::pnc
