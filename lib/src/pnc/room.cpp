#include "engine/pnc/room.hpp"

#include "core/load_error_yaml.hpp"
#include "engine/pnc/data_error.hpp"
#include "shader_yaml.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cmath>
#include <set>
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

core::AmbienceRange parse_ambience_range(const YAML::Node& node,
                                         core::AmbienceRange fallback,
                                         float allowed_min,
                                         float allowed_max,
                                         const std::string& field,
                                         const std::string& room_id) {
    if (!node) {
        return fallback;
    }
    core::AmbienceRange result;
    if (node.IsScalar()) {
        result.min = result.max = node.as<float>();
    } else if (node.IsMap() && node["min"] && node["max"]) {
        result.min = node["min"].as<float>();
        result.max = node["max"].as<float>();
    } else {
        room_fail("room.ambience-range-invalid",
                  "room '" + room_id + "': ambience '" + field + "' must be a number or {min, max}",
                  node);
    }
    if (!std::isfinite(result.min) || !std::isfinite(result.max) || result.min < allowed_min ||
        result.max > allowed_max || result.min > result.max) {
        room_fail("room.ambience-range-invalid",
                  "room '" + room_id + "': ambience '" + field +
                      "' range is outside its allowed bounds or min is greater than max",
                  node);
    }
    return result;
}

std::array<float, 3>
parse_light_color(const YAML::Node& node, const std::string& field, const std::string& room_id) {
    if (!node.IsSequence() || node.size() != 3) {
        room_fail("room.light-color-invalid",
                  "room '" + room_id + "': '" + field + "' must be [r, g, b]",
                  node);
    }
    std::array<float, 3> color{};
    for (std::size_t i = 0; i < color.size(); ++i) {
        color[i] = node[i].as<float>();
        if (!std::isfinite(color[i]) || color[i] < 0.0f || color[i] > 1.0f) {
            room_fail("room.light-color-invalid",
                      "room '" + room_id + "': '" + field +
                          "' components must be finite numbers between 0 and 1",
                      node[i]);
        }
    }
    return color;
}

bool valid_light_attachment(const std::string& attach) {
    return attach == "player" ||
           (attach.starts_with("avatar:") && attach.size() > std::string("avatar:").size()) ||
           (attach.starts_with("object:") && attach.size() > std::string("object:").size());
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

    if (const YAML::Node ambience = root["ambience"]) {
        if (!ambience.IsMap()) {
            room_fail("room.ambience-not-map",
                      "room '" + room.id + "': 'ambience' must be a mapping",
                      ambience);
        }
        core::AmbienceDefinition definition;
        definition.transition = ambience["transition"] ? ambience["transition"].as<float>() : 2.5f;
        if (!std::isfinite(definition.transition) || definition.transition < 0.0f) {
            room_fail("room.ambience-transition-invalid",
                      "room '" + room.id + "': ambience 'transition' must be a non-negative number",
                      ambience["transition"]);
        }

        if (const YAML::Node base = ambience["base"]) {
            if (!base.IsMap() || !base["sound"]) {
                room_fail("room.ambience-base-invalid",
                          "room '" + room.id + "': ambience 'base' must be a mapping with a sound",
                          base);
            }
            definition.base.sound = base["sound"].as<std::string>();
            if (definition.base.sound.empty()) {
                room_fail("room.ambience-base-invalid",
                          "room '" + room.id + "': ambience base sound must not be empty",
                          base["sound"]);
            }
            definition.base.volume = parse_ambience_range(base["volume"],
                                                          {1.0f, 1.0f},
                                                          0.0f,
                                                          1.0f,
                                                          "base.volume",
                                                          room.id)
                                         .min;
        }

        std::set<std::string> ids;
        if (const YAML::Node random = ambience["random"]) {
            if (!random.IsSequence()) {
                room_fail("room.ambience-random-not-sequence",
                          "room '" + room.id + "': ambience 'random' must be a sequence",
                          random);
            }
            for (const YAML::Node& node : random) {
                if (!node.IsMap() || !node["id"]) {
                    room_fail("room.ambience-random-invalid",
                              "room '" + room.id + "': each random ambience layer needs an id",
                              node);
                }
                core::AmbienceRandomLayer layer;
                layer.id = node["id"].as<std::string>();
                if (layer.id.empty() || !ids.insert(layer.id).second) {
                    room_fail("room.ambience-random-id-invalid",
                              "room '" + room.id +
                                  "': random ambience layer ids must be non-empty and unique",
                              node["id"]);
                }
                if (const YAML::Node sounds = node["sounds"]) {
                    if (!sounds.IsSequence()) {
                        room_fail("room.ambience-sounds-invalid",
                                  "room '" + room.id + "': ambience layer '" + layer.id +
                                      "' sounds must be a sequence",
                                  sounds);
                    }
                    for (const YAML::Node& sound : sounds) {
                        layer.sounds.push_back(sound.as<std::string>());
                    }
                } else if (node["sound"]) {
                    layer.sounds.push_back(node["sound"].as<std::string>());
                }
                if (layer.sounds.empty() ||
                    std::any_of(layer.sounds.begin(),
                                layer.sounds.end(),
                                [](const std::string& sound) { return sound.empty(); })) {
                    room_fail("room.ambience-sounds-invalid",
                              "room '" + room.id + "': ambience layer '" + layer.id +
                                  "' needs at least one non-empty sound",
                              node);
                }
                layer.delay = parse_ambience_range(node["delay"],
                                                   {5.0f, 15.0f},
                                                   0.01f,
                                                   86400.0f,
                                                   "delay",
                                                   room.id);
                layer.volume = parse_ambience_range(node["volume"],
                                                    {1.0f, 1.0f},
                                                    0.0f,
                                                    1.0f,
                                                    "volume",
                                                    room.id);
                layer.pan =
                    parse_ambience_range(node["pan"], {-1.0f, 1.0f}, -1.0f, 1.0f, "pan", room.id);
                definition.random.push_back(std::move(layer));
            }
        }
        room.ambience = std::move(definition);
    }

    if (const YAML::Node post = root["post_process"]) {
        if (!post.IsMap()) {
            room_fail("room.post-process-not-map",
                      "room '" + room.id + "': 'post_process' must be a mapping",
                      post);
        }
        RoomPostProcess config;
        config.enabled = post["enabled"] ? post["enabled"].as<bool>() : true;
        config.shaders = parse_shaders(post);
        if (config.shaders.empty()) {
            room_fail("room.post-process-no-shaders",
                      "room '" + room.id + "': 'post_process' needs a 'shader' or 'shaders' entry",
                      post);
        }
        room.post_process = std::move(config);
    }

    if (const YAML::Node lighting = root["lighting"]) {
        if (!lighting.IsMap()) {
            room_fail("room.lighting-not-map",
                      "room '" + room.id + "': 'lighting' must be a mapping",
                      lighting);
        }

        const YAML::Node ambient = lighting["ambient"];
        const YAML::Node lights = lighting["lights"];
        const YAML::Node normal = lighting["normal_map"];
        const YAML::Node occluders = lighting["occluders"];
        if (ambient || lights || normal || occluders) {
            RoomLighting config;
            if (ambient) {
                if (!ambient.IsMap()) {
                    room_fail("room.ambient-light-not-map",
                              "room '" + room.id + "': 'lighting.ambient' must be a mapping",
                              ambient);
                }
                if (ambient["color"]) {
                    config.ambient_color =
                        parse_light_color(ambient["color"], "lighting.ambient.color", room.id);
                }
                if (ambient["intensity"]) {
                    config.ambient_intensity = ambient["intensity"].as<float>();
                }
            }
            if (!std::isfinite(config.ambient_intensity) || config.ambient_intensity < 0.0f ||
                config.ambient_intensity > 1.0f) {
                room_fail("room.ambient-light-intensity-invalid",
                          "room '" + room.id +
                              "': ambient light 'intensity' must be between 0 and 1",
                          ambient ? ambient["intensity"] : YAML::Node());
            }

            if (normal) {
                if (!normal.IsMap() || !normal["image"]) {
                    room_fail("room.normal-map-invalid",
                              "room '" + room.id +
                                  "': 'lighting.normal_map' needs an image",
                              normal);
                }
                config.normal_map = normal["image"].as<std::string>();
                if (normal["origin"]) {
                    config.normal_origin = parse_point(normal["origin"]);
                }
                config.normal_scale = normal["scale"] ? normal["scale"].as<float>() : 1.0f;
                config.normal_strength =
                    normal["strength"] ? normal["strength"].as<float>() : 1.0f;
                if (config.normal_map.empty() || !std::isfinite(config.normal_scale) ||
                    config.normal_scale <= 0.0f || !std::isfinite(config.normal_strength) ||
                    config.normal_strength < 0.0f || config.normal_strength > 2.0f) {
                    room_fail("room.normal-map-params-invalid",
                              "room '" + room.id +
                                  "': normal map needs non-empty image, scale > 0, and strength 0..2",
                              normal);
                }
            }

            if (lights) {
                if (!lights.IsSequence()) {
                    room_fail("room.lights-not-sequence",
                              "room '" + room.id + "': 'lighting.lights' must be a sequence",
                              lights);
                }
                std::set<std::string> ids;
                for (const YAML::Node& node : lights) {
                    if (!node.IsMap() || !node["id"] || !node["type"]) {
                        room_fail("room.light-invalid",
                                  "room '" + room.id +
                                      "': every dynamic light needs an 'id' and 'type'",
                                  node);
                    }
                    RoomLight light;
                    light.id = node["id"].as<std::string>();
                    if (light.id.empty() || !ids.insert(light.id).second) {
                        room_fail("room.light-id-invalid",
                                  "room '" + room.id +
                                      "': dynamic light ids must be non-empty and unique",
                                  node["id"]);
                    }

                    const std::string type = node["type"].as<std::string>();
                    if (type == "omni") {
                        light.type = RoomLight::Type::OMNI;
                    } else if (type == "spot") {
                        light.type = RoomLight::Type::SPOT;
                    } else {
                        room_fail("room.light-type-invalid",
                                  "room '" + room.id +
                                      "': dynamic light 'type' must be 'omni' or 'spot'",
                                  node["type"]);
                    }

                    const bool has_at = static_cast<bool>(node["at"]);
                    const bool has_attach = static_cast<bool>(node["attach"]);
                    if (has_at == has_attach) {
                        room_fail("room.light-position-invalid",
                                  "room '" + room.id + "': light '" + light.id +
                                      "' needs exactly one of 'at' or 'attach'",
                                  node);
                    }
                    if (has_at) {
                        light.at = parse_point(node["at"]);
                    } else {
                        light.attach = node["attach"].as<std::string>();
                        if (!valid_light_attachment(light.attach)) {
                            room_fail("room.light-attachment-invalid",
                                      "room '" + room.id + "': light '" + light.id +
                                          "' attach must be player, avatar:<id>, or object:<id>",
                                      node["attach"]);
                        }
                    }
                    if (node["offset"]) {
                        light.offset = parse_point(node["offset"]);
                    }

                    const YAML::Node radius = node["radius"] ? node["radius"] : node["range"];
                    if (!radius) {
                        room_fail("room.light-radius-missing",
                                  "room '" + room.id + "': light '" + light.id +
                                      "' needs 'radius' (or 'range')",
                                  node);
                    }
                    light.radius = radius.as<float>();
                    light.height =
                        node["height"] ? node["height"].as<float>() : light.radius * 0.5f;
                    light.intensity = node["intensity"] ? node["intensity"].as<float>() : 1.0f;
                    light.enabled = node["enabled"] ? node["enabled"].as<bool>() : true;
                    if (node["color"]) {
                        light.color =
                            parse_light_color(node["color"], "lighting.lights[].color", room.id);
                    }
                    if (!std::isfinite(light.radius) || light.radius <= 0.0f ||
                        !std::isfinite(light.height) || light.height <= 0.0f) {
                        room_fail("room.light-radius-invalid",
                                  "room '" + room.id + "': light '" + light.id +
                                      "' radius and height must be finite numbers greater than zero",
                                  radius);
                    }
                    if (!std::isfinite(light.intensity) || light.intensity < 0.0f ||
                        light.intensity > 4.0f) {
                        room_fail("room.light-intensity-invalid",
                                  "room '" + room.id + "': light '" + light.id +
                                      "' intensity must be between 0 and 4",
                                  node["intensity"]);
                    }

                    if (light.type == RoomLight::Type::SPOT) {
                        const bool follows_facing =
                            node["follow_facing"] && node["follow_facing"].as<bool>();
                        if (!node["direction"] && !follows_facing) {
                            room_fail("room.spotlight-direction-missing",
                                      "room '" + room.id + "': spotlight '" + light.id +
                                          "' needs 'direction' or 'follow_facing: true'",
                                      node);
                        }
                        light.direction = node["direction"] ? node["direction"].as<float>() : 0.0f;
                        light.angle = node["angle"] ? node["angle"].as<float>() : 45.0f;
                        light.softness = node["softness"] ? node["softness"].as<float>() : 8.0f;
                        light.follow_facing =
                            node["follow_facing"] ? node["follow_facing"].as<bool>() : false;
                        if (!std::isfinite(light.direction) || !std::isfinite(light.angle) ||
                            !std::isfinite(light.softness) || light.angle <= 0.0f ||
                            light.angle >= 180.0f || light.softness < 0.0f ||
                            light.softness >= light.angle * 0.5f) {
                            room_fail("room.spotlight-cone-invalid",
                                      "room '" + room.id + "': spotlight '" + light.id +
                                          "' needs 0 < angle < 180 and 0 <= softness < angle/2",
                                      node);
                        }
                        const bool avatar_attached =
                            light.attach == "player" || light.attach.starts_with("avatar:");
                        if (light.follow_facing && !avatar_attached) {
                            room_fail("room.spotlight-follow-facing-invalid",
                                      "room '" + room.id + "': spotlight '" + light.id +
                                          "' can follow facing only when attached to an avatar",
                                      node["follow_facing"]);
                        }
                    }

                    if (const YAML::Node modulation = node["modulation"]) {
                        if (!modulation.IsMap() || !modulation["type"]) {
                            room_fail("room.light-modulation-invalid",
                                      "room '" + room.id + "': light '" + light.id +
                                          "' modulation must be a mapping with a type",
                                      modulation);
                        }
                        const std::string mod_type = modulation["type"].as<std::string>();
                        if (mod_type == "none") {
                            light.modulation.type = LightModulation::Type::NONE;
                        } else if (mod_type == "sine") {
                            light.modulation.type = LightModulation::Type::SINE;
                        } else if (mod_type == "flicker") {
                            light.modulation.type = LightModulation::Type::FLICKER;
                        } else if (mod_type == "faulty") {
                            light.modulation.type = LightModulation::Type::FAULTY;
                        } else {
                            room_fail(
                                "room.light-modulation-type-invalid",
                                "room '" + room.id + "': light '" + light.id +
                                    "' modulation type must be none, sine, flicker, or faulty",
                                modulation["type"]);
                        }
                        light.modulation.amount =
                            modulation["amount"] ? modulation["amount"].as<float>() : 0.08f;
                        light.modulation.speed =
                            modulation["speed"] ? modulation["speed"].as<float>() : 6.0f;
                        light.modulation.seed =
                            modulation["seed"] ? modulation["seed"].as<float>() : 0.0f;
                        if (!std::isfinite(light.modulation.amount) ||
                            light.modulation.amount < 0.0f || light.modulation.amount > 1.0f ||
                            !std::isfinite(light.modulation.speed) ||
                            light.modulation.speed <= 0.0f ||
                            !std::isfinite(light.modulation.seed)) {
                            room_fail(
                                "room.light-modulation-params-invalid",
                                "room '" + room.id + "': light '" + light.id +
                                    "' modulation needs amount 0..1, speed > 0, and finite seed",
                                modulation);
                        }
                    }
                    config.lights.push_back(std::move(light));
                }
            }

            if (occluders) {
                if (!occluders.IsSequence()) {
                    room_fail("room.light-occluders-not-sequence",
                              "room '" + room.id +
                                  "': 'lighting.occluders' must be a sequence",
                              occluders);
                }
                std::set<std::string> ids;
                for (const YAML::Node& node : occluders) {
                    if (!node.IsMap() || !node["id"] || !node["area"]) {
                        room_fail("room.light-occluder-invalid",
                                  "room '" + room.id +
                                      "': each light occluder needs id and area",
                                  node);
                    }
                    LightOccluder occluder;
                    occluder.id = node["id"].as<std::string>();
                    occluder.area = parse_polygon(node["area"]);
                    occluder.enabled = node["enabled"] ? node["enabled"].as<bool>() : true;
                    if (occluder.id.empty() || !ids.insert(occluder.id).second ||
                        occluder.area.size() < 2) {
                        room_fail("room.light-occluder-params-invalid",
                                  "room '" + room.id +
                                      "': light occluder ids must be unique and areas need at least 2 points",
                                  node);
                    }
                    config.occluders.push_back(std::move(occluder));
                }
            }
            room.dynamic_lighting = std::move(config);
        }

        if (const YAML::Node projected = lighting["projected_shadows"]) {
            if (!projected.IsMap()) {
                room_fail("room.projected-shadows-not-map",
                          "room '" + room.id + "': 'lighting.projected_shadows' must be a mapping",
                          projected);
            }
            const bool has_light_point = static_cast<bool>(projected["light"]);
            const bool has_source = static_cast<bool>(projected["source"]);
            if (has_light_point == has_source) {
                room_fail("room.projected-shadows-light-missing",
                          "room '" + room.id +
                              "': projected shadows need exactly one of 'light: {x, y}' or "
                              "'source: <dynamic-light-id>'",
                          projected);
            }

            ProjectedShadow shadow;
            shadow.enabled = projected["enabled"] ? projected["enabled"].as<bool>() : true;
            if (has_light_point) {
                shadow.light = parse_point(projected["light"]);
            } else {
                shadow.source = projected["source"].as<std::string>();
                const bool known = room.dynamic_lighting &&
                                   std::any_of(room.dynamic_lighting->lights.begin(),
                                               room.dynamic_lighting->lights.end(),
                                               [&shadow](const RoomLight& light) {
                                                   return light.id == shadow.source;
                                               });
                if (shadow.source.empty() || !known) {
                    room_fail("room.projected-shadows-source-invalid",
                              "room '" + room.id + "': projected shadow source '" +
                                  shadow.source + "' is not a declared dynamic light",
                              projected["source"]);
                }
            }
            shadow.length = projected["length"] ? projected["length"].as<float>() : 0.45f;
            shadow.width = projected["width"] ? projected["width"].as<float>() : 0.75f;
            shadow.opacity = projected["opacity"] ? projected["opacity"].as<float>() : 0.18f;
            shadow.softness = projected["softness"] ? projected["softness"].as<float>() : 4.0f;
            shadow.contact_shadow =
                projected["contact_shadow"] ? projected["contact_shadow"].as<float>() : 0.55f;
            if (projected["z"]) {
                shadow.z = projected["z"].as<float>();
                if (!std::isfinite(*shadow.z)) {
                    room_fail("room.projected-shadows-z-invalid",
                              "room '" + room.id +
                                  "': projected shadow 'z' must be a finite number",
                              projected["z"]);
                }
            }

            if (const YAML::Node color = projected["color"]) {
                shadow.color = sf::Color(color["r"].as<unsigned>(),
                                         color["g"].as<unsigned>(),
                                         color["b"].as<unsigned>());
            }
            if (projected["casters"]) {
                const std::string casters = projected["casters"].as<std::string>();
                if (casters == "player") {
                    shadow.casters = ProjectedShadow::Casters::PLAYER;
                } else if (casters == "all") {
                    shadow.casters = ProjectedShadow::Casters::ALL;
                } else {
                    room_fail("room.projected-shadows-casters-invalid",
                              "room '" + room.id +
                                  "': projected shadow 'casters' must be 'player' or 'all'",
                              projected["casters"]);
                }
            }

            if (!(shadow.length > 0.0f)) {
                room_fail("room.projected-shadows-length-invalid",
                          "room '" + room.id + "': projected shadow 'length' must be > 0",
                          projected["length"]);
            }
            if (!(shadow.width > 0.0f)) {
                room_fail("room.projected-shadows-width-invalid",
                          "room '" + room.id + "': projected shadow 'width' must be > 0",
                          projected["width"]);
            }
            if (shadow.opacity < 0.0f || shadow.opacity > 1.0f) {
                room_fail("room.projected-shadows-opacity-invalid",
                          "room '" + room.id +
                              "': projected shadow 'opacity' must be between 0 and 1",
                          projected["opacity"]);
            }
            if (shadow.softness < 0.0f) {
                room_fail("room.projected-shadows-softness-invalid",
                          "room '" + room.id + "': projected shadow 'softness' must be >= 0",
                          projected["softness"]);
            }
            if (shadow.contact_shadow < 0.0f || shadow.contact_shadow > 1.0f) {
                room_fail("room.projected-shadows-contact-invalid",
                          "room '" + room.id +
                              "': projected shadow 'contact_shadow' must be between 0 and 1",
                          projected["contact_shadow"]);
            }
            room.projected_shadow = shadow;
        }
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
            layer.extend_bounds =
                ln["extend_bounds"] ? ln["extend_bounds"].as<bool>() : true;
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
            object.rotation = node["rotation"] ? node["rotation"].as<float>() : 0.0f;
            if (!std::isfinite(object.rotation)) {
                room_fail("room.object-rotation-invalid",
                          "room '" + room.id + "': object '" + object.id +
                              "' has a non-finite rotation",
                          node["rotation"]);
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
        placement.enter_from =
            av["enter_from"] ? av["enter_from"].as<std::string>() : std::string();
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
