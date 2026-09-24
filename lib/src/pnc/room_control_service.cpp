#include "room_control_service.hpp"

#include "engine/gfx/shader_effect.hpp"
#include "engine/pnc/room_scene.hpp"
#include "pnc/room_tuning_overlay.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <set>
#include <string>

namespace pac::pnc {

namespace {

using pac::core::ControlError;
using pac::core::ControlResult;
using pac::core::ControlValue;

ControlError invalid(std::string message) { return {-32602, std::move(message), std::nullopt}; }
ControlError unavailable() { return {-32020, "no room is currently loaded", std::nullopt}; }

const ControlValue* field(const ControlValue::Object& object, const char* name) {
    const auto found = object.find(name);
    return found == object.end() ? nullptr : &found->second;
}

std::optional<double> number(const ControlValue& value) {
    if (const auto* item = std::get_if<double>(&value.value)) return *item;
    if (const auto* item = std::get_if<std::int64_t>(&value.value)) return static_cast<double>(*item);
    return std::nullopt;
}

std::optional<double> number_field(const ControlValue::Object& object, const char* name) {
    const ControlValue* item = field(object, name);
    return item ? number(*item) : std::nullopt;
}

const std::string* string_field(const ControlValue::Object& object, const char* name) {
    const ControlValue* item = field(object, name);
    return item ? std::get_if<std::string>(&item->value) : nullptr;
}

const bool* bool_field(const ControlValue::Object& object, const char* name) {
    const ControlValue* item = field(object, name);
    return item ? std::get_if<bool>(&item->value) : nullptr;
}

template <std::size_t N>
std::optional<std::array<float, N>> float_array(const ControlValue& value) {
    const auto* array = value.array();
    if (!array || array->size() != N) return std::nullopt;
    std::array<float, N> result{};
    for (std::size_t i = 0; i < N; ++i) {
        const auto item = number((*array)[i]);
        if (!item || !std::isfinite(*item)) return std::nullopt;
        result[i] = static_cast<float>(*item);
    }
    return result;
}

ControlValue array_value(const std::array<float, 3>& values) {
    return ControlValue::Array{values[0], values[1], values[2]};
}

ControlValue color_value(sf::Color color) {
    constexpr double byte_scale = 1.0 / 255.0;
    return ControlValue::Array{color.r * byte_scale,
                               color.g * byte_scale,
                               color.b * byte_scale};
}

ControlValue point_value(geom::Point point) {
    return ControlValue::Object{{"x", point.x}, {"y", point.y}};
}

ControlValue target_value(const RoomTargetRef& target) {
    if (target.kind == RoomTargetRef::Kind::FIXED_POINT) {
        return point_value(target.point);
    }
    ControlValue::Object value{{"target", room_target_name(target)}};
    if (!target.anchor.empty()) value["anchor"] = target.anchor;
    if (target.offset.x != 0.0f || target.offset.y != 0.0f) {
        value["offset"] = point_value(target.offset);
    }
    return value;
}

std::optional<RoomTargetRef> named_target(std::string_view name) {
    RoomTargetRef target;
    if (name == "player") {
        target.kind = RoomTargetRef::Kind::PLAYER;
        return target;
    }
    const auto prefixed = [&](std::string_view prefix, RoomTargetRef::Kind kind) {
        if (!name.starts_with(prefix) || name.size() == prefix.size()) return false;
        target.kind = kind;
        target.id = std::string(name.substr(prefix.size()));
        return true;
    };
    if (prefixed("avatar:", RoomTargetRef::Kind::AVATAR) ||
        prefixed("npc:", RoomTargetRef::Kind::AVATAR) ||
        prefixed("object:", RoomTargetRef::Kind::OBJECT) ||
        prefixed("point:", RoomTargetRef::Kind::NAMED_POINT)) {
        return target;
    }
    return std::nullopt;
}

std::optional<RoomTargetRef> control_target(const ControlValue& value, std::string& error) {
    if (const auto* name = std::get_if<std::string>(&value.value)) {
        auto target = named_target(*name);
        if (!target) error = "unknown aim target reference: " + *name;
        return target;
    }
    const auto* object = value.object();
    if (!object) {
        error = "light aim_at must be {x, y}, a target string, or a target object";
        return std::nullopt;
    }
    if (field(*object, "x") || field(*object, "y")) {
        const auto x = number_field(*object, "x");
        const auto y = number_field(*object, "y");
        if (!x || !y || !std::isfinite(*x) || !std::isfinite(*y)) {
            error = "light aim_at requires finite numeric x and y";
            return std::nullopt;
        }
        RoomTargetRef target;
        target.kind = RoomTargetRef::Kind::FIXED_POINT;
        target.point = {static_cast<float>(*x), static_cast<float>(*y)};
        return target;
    }
    const std::string* name = string_field(*object, "target");
    auto target = name ? named_target(*name) : std::nullopt;
    if (!target) {
        error = "tracked light aim_at needs player, avatar:<id>, object:<id>, or point:<id>";
        return std::nullopt;
    }
    if (const ControlValue* anchor_value = field(*object, "anchor")) {
        const auto* anchor = std::get_if<std::string>(&anchor_value->value);
        if (!anchor || anchor->empty() || target->kind == RoomTargetRef::Kind::NAMED_POINT) {
            error = "light aim anchor must target the player, an avatar, or an object";
            return std::nullopt;
        }
        target->anchor = *anchor;
    }
    if (const ControlValue* offset_value = field(*object, "offset")) {
        const auto* offset = offset_value->object();
        const auto x = offset ? number_field(*offset, "x") : std::nullopt;
        const auto y = offset ? number_field(*offset, "y") : std::nullopt;
        if (!x || !y || !std::isfinite(*x) || !std::isfinite(*y)) {
            error = "light aim offset requires finite numeric x and y";
            return std::nullopt;
        }
        target->offset = {static_cast<float>(*x), static_cast<float>(*y)};
    }
    return target;
}

ControlValue shader_value(const gfx::ShaderValue& value) {
    return std::visit(
        [](const auto& item) -> ControlValue {
            using T = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<T, std::array<float, 2>> ||
                          std::is_same_v<T, std::array<float, 3>> ||
                          std::is_same_v<T, std::array<float, 4>>) {
                ControlValue::Array values;
                for (float component : item) values.emplace_back(component);
                return values;
            } else {
                return item;
            }
        },
        value);
}

ControlResult assign_shader_value(gfx::ShaderValue& target, const ControlValue& source) {
    return std::visit(
        [&](auto& current) -> ControlResult {
            using T = std::decay_t<decltype(current)>;
            if constexpr (std::is_same_v<T, bool>) {
                const auto* item = std::get_if<bool>(&source.value);
                if (!item) return invalid("shader bool parameter requires a boolean");
                current = *item;
            } else if constexpr (std::is_same_v<T, int>) {
                const auto* item = std::get_if<std::int64_t>(&source.value);
                if (!item) return invalid("shader int parameter requires an integer");
                current = static_cast<int>(*item);
            } else if constexpr (std::is_same_v<T, float>) {
                const auto item = number(source);
                if (!item || !std::isfinite(*item)) return invalid("shader float must be finite");
                current = static_cast<float>(*item);
            } else {
                const auto item = float_array<std::tuple_size_v<T>>(source);
                if (!item) return invalid("shader vector has the wrong shape");
                current = *item;
            }
            return ControlValue(true);
        },
        target);
}

ControlValue state_value(const RoomRuntime& room) {
    const RoomRenderState& state = room.render_state();
    ControlValue::Object result{{"room", room.data().id}};
    if (state.lighting) {
        ControlValue::Array lights;
        for (const RoomLight& light : state.lighting->lights) {
            lights.emplace_back(ControlValue::Object{
                {"id", light.id},
                {"type", light.type == RoomLight::Type::SPOT ? "spot" : "omni"},
                {"enabled", light.enabled},
                {"at", point_value(light.at)},
                {"attach", light.attach},
                {"radius", light.radius},
                {"height", light.height},
                {"color", array_value(light.color)},
                {"intensity", light.intensity},
                {"direction", light.direction},
                {"angle", light.angle},
                {"softness", light.softness},
                {"beam_width", light.beam_width},
                {"aim_at", light.aim_at ? target_value(*light.aim_at) : ControlValue(nullptr)}});
        }
        result["ambient"] = ControlValue::Object{
            {"color", array_value(state.lighting->ambient_color)},
            {"intensity", state.lighting->ambient_intensity}};
        result["lights"] = std::move(lights);
    }
    if (state.post_process) {
        ControlValue::Array effects;
        for (const gfx::ShaderEffect& effect : state.post_process->shaders) {
            ControlValue::Object params;
            for (const auto& param : effect.params) params[param.name] = shader_value(param.value);
            effects.emplace_back(ControlValue::Object{{"source", effect.source},
                                                      {"enabled", effect.enabled},
                                                      {"params", std::move(params)}});
        }
        result["post_process"] = ControlValue::Object{{"enabled", state.post_process->enabled},
                                                       {"effects", std::move(effects)}};
    }
    if (state.projected_shadow) {
        const ProjectedShadow& shadow = *state.projected_shadow;
        ControlValue::Object value{
            {"enabled", shadow.enabled},
            {"casters", shadow.casters == ProjectedShadow::Casters::ALL ? "all" : "player"},
            {"length", shadow.length},
            {"width", shadow.width},
            {"opacity", shadow.opacity},
            {"softness", shadow.softness},
            {"contact_shadow", shadow.contact_shadow},
            {"color", color_value(shadow.color)},
            {"z", shadow.z ? ControlValue(*shadow.z) : ControlValue(nullptr)}};
        if (!shadow.sources.empty()) {
            ControlValue::Array sources;
            for (const std::string& source : shadow.sources) {
                sources.emplace_back(source);
            }
            value["sources"] = std::move(sources);
        } else if (!shadow.source.empty()) {
            value["source"] = shadow.source;
        } else {
            value["light"] = point_value(shadow.light);
        }
        result["projected_shadow"] = std::move(value);
    }
    return result;
}

} // namespace

RoomControlService::RoomControlService(RoomScene& scene) : scene_(scene) {}

std::vector<pac::core::ControlMethod> RoomControlService::methods() const {
    return {{"render.get", "Return current room lighting and grading."},
            {"render.set_ambient", "Set live ambient color or intensity."},
            {"render.set_light", "Set fields on one live room light."},
            {"render.set_shadow", "Set live projected-shadow fields."},
            {"render.set_grade", "Set grading enablement or shader parameters."},
            {"render.set_scene", "Set a complete live lighting-scene snapshot."},
            {"render.reset", "Restore rendering values authored in room YAML."},
            {"render.export_yaml", "Return the current lighting/post-process YAML."}};
}

ControlResult RoomControlService::invoke(std::string_view method, const ControlValue& params) {
    if (!scene_.room_) return unavailable();
    RoomRuntime& room = *scene_.room_;
    if (method == "render.get") return state_value(room);
    if (method == "render.reset") {
        room.reset_render_state();
        return state_value(room);
    }
    if (method == "render.export_yaml") {
        RoomRenderState copy = room.render_state();
        RoomTuningOverlay serializer;
        serializer.open(copy, room.authored_render_state(), {}, nullptr);
        const std::string yaml = serializer.yaml();
        serializer.close();
        return ControlValue::Object{{"yaml", yaml}};
    }

    const auto* object = params.object();
    if (!object) return invalid("params must be an object");

    if (method == "render.set_scene") {
        const RoomRenderState before = room.render_state();
        const auto apply = [&](std::string_view nested_method,
                               const ControlValue& nested_params) -> std::optional<ControlError> {
            ControlResult result = invoke(nested_method, nested_params);
            if (const auto* error = std::get_if<ControlError>(&result)) return *error;
            return std::nullopt;
        };
        const auto rollback = [&](ControlError error) -> ControlResult {
            room.render_state() = before;
            return error;
        };
        const auto malformed = [&](std::string message) -> ControlResult {
            return rollback(invalid(std::move(message)));
        };

        if (const ControlValue* ambient = field(*object, "ambient")) {
            if (!ambient->is_object()) return malformed("scene ambient must be an object");
            if (const auto error = apply("render.set_ambient", *ambient)) return rollback(*error);
        }
        if (const ControlValue* raw_lights = field(*object, "lights")) {
            const auto* lights = raw_lights->array();
            if (!lights) return malformed("scene lights must be an array");
            for (const ControlValue& light : *lights) {
                if (!light.is_object()) return malformed("each scene light must be an object");
                if (const auto error = apply("render.set_light", light)) return rollback(*error);
            }
        }
        if (const ControlValue* shadow = field(*object, "projected_shadow")) {
            if (!shadow->is_object()) return malformed("scene projected_shadow must be an object");
            if (const auto error = apply("render.set_shadow", *shadow)) return rollback(*error);
        }
        if (const ControlValue* raw_post = field(*object, "post_process")) {
            const auto* post = raw_post->object();
            if (!post) return malformed("scene post_process must be an object");
            ControlValue::Object top_level;
            if (const ControlValue* enabled = field(*post, "enabled")) {
                top_level["enabled"] = *enabled;
            }
            if (!top_level.empty()) {
                if (const auto error = apply("render.set_grade", top_level)) return rollback(*error);
            }
            if (const ControlValue* raw_effects = field(*post, "effects")) {
                const auto* effects = raw_effects->array();
                if (!effects) return malformed("scene post-process effects must be an array");
                for (std::size_t index = 0; index < effects->size(); ++index) {
                    const auto* effect = (*effects)[index].object();
                    if (!effect) return malformed("each scene post-process effect must be an object");
                    ControlValue::Object update{{"effect", static_cast<std::int64_t>(index)}};
                    if (const ControlValue* enabled = field(*effect, "enabled")) {
                        update["effect_enabled"] = *enabled;
                    }
                    if (const ControlValue* values = field(*effect, "params")) {
                        update["params"] = *values;
                    }
                    if (const auto error = apply("render.set_grade", update)) return rollback(*error);
                }
            }
        }
        return state_value(room);
    }

    if (method == "render.set_ambient") {
        if (!room.render_state().lighting) return unavailable();
        RoomLighting& lighting = *room.render_state().lighting;
        if (const auto intensity = number_field(*object, "intensity")) {
            if (!std::isfinite(*intensity)) return invalid("ambient intensity must be finite");
            lighting.ambient_intensity = std::clamp(static_cast<float>(*intensity), 0.0f, 4.0f);
        }
        if (const ControlValue* color = field(*object, "color")) {
            const auto value = float_array<3>(*color);
            if (!value) return invalid("ambient color must be a three-number array");
            lighting.ambient_color = *value;
        }
        return state_value(room);
    }

    if (method == "render.set_light") {
        const std::string* id = string_field(*object, "id");
        if (!id || !room.render_state().lighting) return invalid("set_light requires a valid 'id'");
        auto found = std::find_if(room.render_state().lighting->lights.begin(),
                                  room.render_state().lighting->lights.end(),
                                  [&](const RoomLight& light) { return light.id == *id; });
        if (found == room.render_state().lighting->lights.end())
            return ControlError{-32021, "room light not found: " + *id, std::nullopt};
        if (const bool* enabled = bool_field(*object, "enabled")) found->enabled = *enabled;
        if (const auto intensity = number_field(*object, "intensity")) {
            const float transition = static_cast<float>(number_field(*object, "transition").value_or(0));
            room.set_light_intensity(*id, static_cast<float>(*intensity), transition);
        }
        const auto set_number = [&](const char* name, float& target, float minimum, float maximum) {
            if (const auto value = number_field(*object, name))
                target = std::clamp(static_cast<float>(*value), minimum, maximum);
        };
        set_number("radius", found->radius, 1.0f, 100000.0f);
        set_number("height", found->height, 0.0f, 100000.0f);
        if (const auto direction = number_field(*object, "direction")) {
            found->direction = std::clamp(static_cast<float>(*direction), -3600.0f, 3600.0f);
            found->aim_at.reset();
        }
        set_number("angle", found->angle, 0.1f, 360.0f);
        set_number("softness", found->softness, 0.0f, 180.0f);
        set_number("beam_width", found->beam_width, 0.0f, 100000.0f);
        if (const ControlValue* color = field(*object, "color")) {
            const auto value = float_array<3>(*color);
            if (!value) return invalid("light color must be a three-number array");
            found->color = *value;
        }
        if (const ControlValue* at = field(*object, "at")) {
            const auto* position = at->object();
            if (!position) return invalid("light at must be an object with x and y");
            const auto x = number_field(*position, "x");
            const auto y = number_field(*position, "y");
            if (!x || !y) return invalid("light at requires numeric x and y");
            found->at = {static_cast<float>(*x), static_cast<float>(*y)};
        }
        if (const ControlValue* aim_at = field(*object, "aim_at")) {
            if (std::holds_alternative<std::nullptr_t>(aim_at->value)) {
                found->aim_at.reset();
            } else {
                if (found->type != RoomLight::Type::SPOT) {
                    return invalid("only spotlights can use aim_at");
                }
                std::string error;
                const auto target = control_target(*aim_at, error);
                if (!target) return invalid(error);
                found->aim_at = *target;
            }
        }
        return state_value(room);
    }

    if (method == "render.set_shadow") {
        if (!room.render_state().projected_shadow)
            return ControlError{-32022, "room has no projected shadow", std::nullopt};
        ProjectedShadow& shadow = *room.render_state().projected_shadow;
        if (const bool* enabled = bool_field(*object, "enabled")) shadow.enabled = *enabled;
        if (const std::string* casters = string_field(*object, "casters")) {
            if (*casters == "player") {
                shadow.casters = ProjectedShadow::Casters::PLAYER;
            } else if (*casters == "all") {
                shadow.casters = ProjectedShadow::Casters::ALL;
            } else {
                return invalid("shadow casters must be 'player' or 'all'");
            }
        }
        if (const std::string* source = string_field(*object, "source")) {
            const bool known =
                room.render_state().lighting &&
                std::any_of(room.render_state().lighting->lights.begin(),
                            room.render_state().lighting->lights.end(),
                            [&](const RoomLight& light) { return light.id == *source; });
            if (source->empty() || !known) {
                return invalid("shadow source must be a declared light id");
            }
            shadow.source = *source;
            shadow.sources.clear();
        }
        if (const ControlValue* raw_light = field(*object, "light")) {
            const auto* light = raw_light->object();
            if (!light) return invalid("shadow light must be an object with x and y");
            const auto x = number_field(*light, "x");
            const auto y = number_field(*light, "y");
            if (!x || !y || !std::isfinite(*x) || !std::isfinite(*y)) {
                return invalid("shadow light requires finite numeric x and y");
            }
            shadow.light = {static_cast<float>(*x), static_cast<float>(*y)};
            shadow.source.clear();
            shadow.sources.clear();
        }
        if (const ControlValue* raw_sources = field(*object, "sources")) {
            const auto* values = raw_sources->array();
            if (!values || values->empty()) {
                return invalid("shadow sources must be a non-empty array");
            }
            std::vector<std::string> sources;
            std::set<std::string> seen;
            for (const ControlValue& value : *values) {
                const auto* source = std::get_if<std::string>(&value.value);
                const bool known =
                    source && room.render_state().lighting &&
                    std::any_of(room.render_state().lighting->lights.begin(),
                                room.render_state().lighting->lights.end(),
                                [&](const RoomLight& light) { return light.id == *source; });
                if (!source || source->empty() || !known || !seen.insert(*source).second) {
                    return invalid("shadow sources must contain unique declared light ids");
                }
                sources.push_back(*source);
            }
            shadow.source.clear();
            shadow.sources = std::move(sources);
        }
        const auto set_number = [&](const char* name, float& target, float minimum, float maximum) {
            if (const auto value = number_field(*object, name))
                target = std::clamp(static_cast<float>(*value), minimum, maximum);
        };
        set_number("length", shadow.length, 0.0f, 10.0f);
        set_number("width", shadow.width, 0.0f, 10.0f);
        set_number("opacity", shadow.opacity, 0.0f, 1.0f);
        set_number("softness", shadow.softness, 0.0f, 100.0f);
        set_number("contact_shadow", shadow.contact_shadow, 0.0f, 1.0f);
        if (const ControlValue* color = field(*object, "color")) {
            const auto value = float_array<3>(*color);
            if (!value) return invalid("shadow color must be a three-number array");
            const auto channel = [](float component) {
                return static_cast<std::uint8_t>(
                    std::lround(std::clamp(component, 0.0f, 1.0f) * 255.0f));
            };
            shadow.color = sf::Color(channel((*value)[0]),
                                     channel((*value)[1]),
                                     channel((*value)[2]));
        }
        if (const ControlValue* z = field(*object, "z")) {
            if (std::holds_alternative<std::nullptr_t>(z->value)) {
                shadow.z.reset();
            } else {
                const auto value = number(*z);
                if (!value || !std::isfinite(*value)) {
                    return invalid("shadow z must be a finite number or null");
                }
                shadow.z = static_cast<float>(*value);
            }
        }
        return state_value(room);
    }

    if (method == "render.set_grade") {
        if (!room.render_state().post_process)
            return ControlError{-32023, "room has no post-process chain", std::nullopt};
        RoomPostProcess& post = *room.render_state().post_process;
        if (const bool* enabled = bool_field(*object, "enabled")) post.enabled = *enabled;
        const bool addresses_effect = field(*object, "effect") || field(*object, "effect_enabled") ||
                                      field(*object, "params");
        if (!addresses_effect) return state_value(room);
        std::size_t effect_index = 0;
        if (const auto raw = number_field(*object, "effect")) {
            if (*raw < 0 || std::floor(*raw) != *raw) return invalid("effect must be an index");
            effect_index = static_cast<std::size_t>(*raw);
        }
        if (effect_index >= post.shaders.size()) return invalid("grade effect index is out of range");
        gfx::ShaderEffect& effect = post.shaders[effect_index];
        if (const bool* enabled = bool_field(*object, "effect_enabled")) effect.enabled = *enabled;
        if (const ControlValue* raw_params = field(*object, "params")) {
            const auto* values = raw_params->object();
            if (!values) return invalid("grade params must be an object");
            for (const auto& [name, source] : *values) {
                auto param = std::find_if(effect.params.begin(),
                                          effect.params.end(),
                                          [&](const gfx::ShaderParam& item) { return item.name == name; });
                if (param == effect.params.end()) return invalid("unknown shader parameter: " + name);
                ControlResult assigned = assign_shader_value(param->value, source);
                if (std::holds_alternative<ControlError>(assigned)) return assigned;
            }
        }
        return state_value(room);
    }

    return ControlError{-32601, "unknown room method: " + std::string(method), std::nullopt};
}

} // namespace pac::pnc
