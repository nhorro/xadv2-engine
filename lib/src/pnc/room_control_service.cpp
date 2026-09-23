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

ControlValue point_value(geom::Point point) {
    return ControlValue::Object{{"x", point.x}, {"y", point.y}};
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
                {"softness", light.softness}});
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
            {"contact_shadow", shadow.contact_shadow}};
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
        set_number("direction", found->direction, -3600.0f, 3600.0f);
        set_number("angle", found->angle, 0.1f, 360.0f);
        set_number("softness", found->softness, 0.0f, 180.0f);
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
        return state_value(room);
    }

    if (method == "render.set_shadow") {
        if (!room.render_state().projected_shadow)
            return ControlError{-32022, "room has no projected shadow", std::nullopt};
        ProjectedShadow& shadow = *room.render_state().projected_shadow;
        if (const bool* enabled = bool_field(*object, "enabled")) shadow.enabled = *enabled;
        if (const std::string* source = string_field(*object, "source")) {
            shadow.source = *source;
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
        set_number("contact_shadow", shadow.contact_shadow, 0.0f, 4.0f);
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
