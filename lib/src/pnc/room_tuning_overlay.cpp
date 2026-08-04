#include "pnc/room_tuning_overlay.hpp"

#include <SFML/Graphics/Font.hpp>
#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/Graphics/Text.hpp>
#include <SFML/Window/Clipboard.hpp>
#include <SFML/Window/Keyboard.hpp>
#include <SFML/Window/Mouse.hpp>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <type_traits>
#include <utility>

namespace pac::pnc {

namespace {

const sf::Color kPanel{10, 18, 34, 248};
const sf::Color kControl{24, 38, 61};
const sf::Color kControlHover{38, 57, 86};
const sf::Color kBorder{71, 85, 105};
const sf::Color kText{226, 232, 240};
const sf::Color kMuted{148, 163, 184};
const sf::Color kAccent{56, 189, 248};

std::string decimal(float value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(std::abs(value) >= 100.0f ? 1 : 3) << value;
    std::string text = out.str();
    while (text.size() > 1 && text.back() == '0') {
        text.pop_back();
    }
    if (!text.empty() && text.back() == '.') {
        text.pop_back();
    }
    return text;
}

void draw_label(sf::RenderTarget& target,
                const sf::Font* font,
                const std::string& label,
                sf::Vector2f position,
                unsigned size = 13,
                sf::Color color = kText) {
    if (!font) {
        return;
    }
    sf::Text text(label, *font, size);
    text.setFillColor(color);
    text.setPosition(position);
    target.draw(text);
}

sf::FloatRect
row_cell(sf::FloatRect region, std::size_t index, std::size_t count, float top, float height) {
    constexpr float margin = 8.0f;
    constexpr float gap = 6.0f;
    const float available = region.width - margin * 2.0f - gap * static_cast<float>(count - 1);
    const float width = available / static_cast<float>(count);
    return {region.left + margin + static_cast<float>(index) * (width + gap),
            region.top + top,
            width,
            height};
}

std::string modulation_name(LightModulation::Type type) {
    switch (type) {
    case LightModulation::Type::NONE:
        return "none";
    case LightModulation::Type::SINE:
        return "sine";
    case LightModulation::Type::FLICKER:
        return "flicker";
    case LightModulation::Type::FAULTY:
        return "faulty";
    }
    return "none";
}

LightModulation::Type next_modulation(LightModulation::Type type) {
    switch (type) {
    case LightModulation::Type::NONE:
        return LightModulation::Type::SINE;
    case LightModulation::Type::SINE:
        return LightModulation::Type::FLICKER;
    case LightModulation::Type::FLICKER:
        return LightModulation::Type::FAULTY;
    case LightModulation::Type::FAULTY:
        return LightModulation::Type::NONE;
    }
    return LightModulation::Type::NONE;
}

std::size_t component_count(const gfx::ShaderValue& value) {
    return std::visit(
        [](const auto& item) -> std::size_t {
            using T = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<T, std::array<float, 2>>) {
                return 2;
            } else if constexpr (std::is_same_v<T, std::array<float, 3>>) {
                return 3;
            } else if constexpr (std::is_same_v<T, std::array<float, 4>>) {
                return 4;
            }
            return 1;
        },
        value);
}

bool is_bool(const gfx::ShaderValue& value) {
    return std::holds_alternative<bool>(value);
}

float component_value(const gfx::ShaderValue& value, std::size_t component) {
    return std::visit(
        [component](const auto& item) -> float {
            using T = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<T, bool>) {
                return item ? 1.0f : 0.0f;
            } else if constexpr (std::is_same_v<T, int> || std::is_same_v<T, float>) {
                return static_cast<float>(item);
            } else {
                return item[std::min(component, item.size() - 1)];
            }
        },
        value);
}

void set_component(gfx::ShaderValue& value, std::size_t component, float next) {
    std::visit(
        [component, next](auto& item) {
            using T = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<T, bool>) {
                item = next >= 0.5f;
            } else if constexpr (std::is_same_v<T, int>) {
                item = static_cast<int>(std::lround(next));
            } else if constexpr (std::is_same_v<T, float>) {
                item = next;
            } else {
                item[std::min(component, item.size() - 1)] = next;
            }
        },
        value);
}

std::pair<float, float> shader_range(const gfx::ShaderParam& param) {
    if (param.name.find("brightness") != std::string::npos ||
        param.name.find("offset") != std::string::npos ||
        param.name.find("hue") != std::string::npos) {
        return {-1.0f, 1.0f};
    }
    if (param.name.find("tint") != std::string::npos ||
        param.name.find("color") != std::string::npos ||
        param.name.find("colour") != std::string::npos ||
        param.name.find("contrast") != std::string::npos ||
        param.name.find("saturation") != std::string::npos ||
        param.name.find("strength") != std::string::npos) {
        return {0.0f, 2.0f};
    }
    const float current = std::abs(component_value(param.value, 0));
    const float extent = std::max(1.0f, std::ceil(current * 2.0f));
    return {-extent, extent};
}

std::string component_suffix(const gfx::ShaderParam& param, std::size_t component) {
    const bool color = param.name.find("tint") != std::string::npos ||
                       param.name.find("color") != std::string::npos ||
                       param.name.find("colour") != std::string::npos;
    static constexpr std::array<std::string_view, 4> xyzw{"x", "y", "z", "w"};
    static constexpr std::array<std::string_view, 4> rgba{"r", "g", "b", "a"};
    const auto& labels = color ? rgba : xyzw;
    return component_count(param.value) > 1 ? "." + std::string(labels[component]) : "";
}

YAML::Node point_node(geom::Point point) {
    YAML::Node node;
    node["x"] = point.x;
    node["y"] = point.y;
    return node;
}

template <std::size_t N>
YAML::Node array_node(const std::array<float, N>& values) {
    YAML::Node node(YAML::NodeType::Sequence);
    for (float value : values) {
        node.push_back(value);
    }
    return node;
}

YAML::Node shader_value_node(const gfx::ShaderValue& value) {
    return std::visit(
        [](const auto& item) -> YAML::Node {
            using T = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<T, std::array<float, 2>> ||
                          std::is_same_v<T, std::array<float, 3>> ||
                          std::is_same_v<T, std::array<float, 4>>) {
                return array_node(item);
            } else if constexpr (std::is_same_v<T, int>) {
                YAML::Node typed;
                typed["type"] = "int";
                typed["value"] = item;
                return typed;
            } else {
                return YAML::Node(item);
            }
        },
        value);
}

YAML::Node shader_node(const gfx::ShaderEffect& effect) {
    YAML::Node node;
    node["source"] = effect.source;
    if (!effect.enabled) {
        node["enabled"] = false;
    }
    if (!effect.controller.empty()) {
        node["controller"] = effect.controller;
    }
    if (!effect.params.empty()) {
        YAML::Node params;
        for (const gfx::ShaderParam& param : effect.params) {
            params[param.name] = shader_value_node(param.value);
        }
        node["params"] = params;
    }
    return node;
}

} // namespace

void RoomTuningOverlay::open(const RoomData& room,
                             sf::FloatRect panel_region,
                             const sf::Font* font) {
    active_ = true;
    compare_original_ = false;
    had_original_lighting_ = room.dynamic_lighting.has_value();
    original_lighting_ = room.dynamic_lighting.value_or(RoomLighting{});
    working_lighting_ = original_lighting_;
    original_post_process_ = room.post_process;
    working_post_process_ = room.post_process;
    projected_shadow_ = room.projected_shadow;
    region_ = panel_region;
    font_ = font;
    tab_ = Tab::AMBIENT;
    light_page_ = LightPage::CORE;
    selected_light_ = 0;
    selected_effect_ = 0;
    selected_param_ = 0;
    selected_component_ = 0;
    dragged_control_.reset();
    status_ = "F9/Esc closes · live working copy";
    rebuild_controls();
}

void RoomTuningOverlay::close() {
    active_ = false;
    dragged_control_.reset();
    controls_.clear();
}

const RoomLighting* RoomTuningOverlay::effective_lighting(const RoomData& room) const {
    if (!active_) {
        return room.dynamic_lighting ? &*room.dynamic_lighting : nullptr;
    }
    if (compare_original_) {
        return had_original_lighting_ ? &original_lighting_ : nullptr;
    }
    return &working_lighting_;
}

const RoomPostProcess* RoomTuningOverlay::effective_post_process(const RoomData& room) const {
    if (!active_) {
        return room.post_process ? &*room.post_process : nullptr;
    }
    const auto& selected = compare_original_ ? original_post_process_ : working_post_process_;
    return selected ? &*selected : nullptr;
}

void RoomTuningOverlay::reset() {
    working_lighting_ = original_lighting_;
    working_post_process_ = original_post_process_;
    compare_original_ = false;
    clamp_selection();
    status_ = "Working values reset to room YAML";
}

RoomLight* RoomTuningOverlay::selected_light() {
    clamp_selection();
    return working_lighting_.lights.empty() ? nullptr : &working_lighting_.lights[selected_light_];
}

gfx::ShaderEffect* RoomTuningOverlay::selected_effect() {
    clamp_selection();
    if (!working_post_process_ || working_post_process_->shaders.empty()) {
        return nullptr;
    }
    return &working_post_process_->shaders[selected_effect_];
}

gfx::ShaderParam* RoomTuningOverlay::selected_param() {
    gfx::ShaderEffect* effect = selected_effect();
    if (!effect || effect->params.empty()) {
        return nullptr;
    }
    selected_param_ = std::min(selected_param_, effect->params.size() - 1);
    selected_component_ =
        std::min(selected_component_, component_count(effect->params[selected_param_].value) - 1);
    return &effect->params[selected_param_];
}

void RoomTuningOverlay::clamp_selection() {
    if (!working_lighting_.lights.empty()) {
        selected_light_ = std::min(selected_light_, working_lighting_.lights.size() - 1);
    } else {
        selected_light_ = 0;
    }
    if (working_post_process_ && !working_post_process_->shaders.empty()) {
        selected_effect_ = std::min(selected_effect_, working_post_process_->shaders.size() - 1);
        auto& params = working_post_process_->shaders[selected_effect_].params;
        selected_param_ = params.empty() ? 0 : std::min(selected_param_, params.size() - 1);
    } else {
        selected_effect_ = 0;
        selected_param_ = 0;
    }
}

void RoomTuningOverlay::add_button(sf::FloatRect rect,
                                   std::string label,
                                   std::function<void()> action) {
    Control control;
    control.type = ControlType::BUTTON;
    control.rect = rect;
    control.label = std::move(label);
    control.action = [action = std::move(action)](float) {
        if (action) {
            action();
        }
    };
    controls_.push_back(std::move(control));
}

void RoomTuningOverlay::add_slider(sf::FloatRect rect,
                                   std::string label,
                                   float value,
                                   float minimum,
                                   float maximum,
                                   float step,
                                   std::function<void(float)> action) {
    Control control;
    control.type = ControlType::SLIDER;
    control.rect = rect;
    control.label = std::move(label);
    control.value = value;
    control.minimum = minimum;
    control.maximum = maximum;
    control.step = step;
    control.action = std::move(action);
    controls_.push_back(std::move(control));
}

void RoomTuningOverlay::rebuild_controls() {
    controls_.clear();
    if (!active_) {
        return;
    }
    clamp_selection();

    const float x = region_.left + 8.0f;
    const float y = region_.top + 5.0f;
    const float h = 23.0f;
    const auto top_button = [&](float left, float width, std::string label, auto action) {
        add_button({region_.left + left, y, width, h}, std::move(label), std::move(action));
    };
    top_button(8.0f, 88.0f, tab_ == Tab::AMBIENT ? "[ Ambient ]" : "Ambient", [this] {
        tab_ = Tab::AMBIENT;
    });
    top_button(102.0f, 88.0f, tab_ == Tab::LIGHTS ? "[ Lights ]" : "Lights", [this] {
        tab_ = Tab::LIGHTS;
    });
    top_button(196.0f, 88.0f, tab_ == Tab::GRADING ? "[ Grading ]" : "Grading", [this] {
        tab_ = Tab::GRADING;
    });
    top_button(region_.width - 286.0f,
               90.0f,
               compare_original_ ? "View: YAML" : "View: Live",
               [this] { compare_original_ = !compare_original_; });
    top_button(region_.width - 190.0f, 72.0f, "Reset", [this] { reset(); });
    top_button(region_.width - 112.0f, 104.0f, "Copy YAML", [this] { copy_yaml(); });

    if (tab_ == Tab::AMBIENT) {
        const std::array<std::string, 4> labels{"Ambient R", "Ambient G", "Ambient B", "Intensity"};
        for (std::size_t i = 0; i < 3; ++i) {
            add_slider(row_cell(region_, i, 4, 43.0f, 48.0f),
                       labels[i],
                       working_lighting_.ambient_color[i],
                       0.0f,
                       1.0f,
                       0.01f,
                       [this, i](float value) { working_lighting_.ambient_color[i] = value; });
        }
        add_slider(row_cell(region_, 3, 4, 43.0f, 48.0f),
                   labels[3],
                   working_lighting_.ambient_intensity,
                   0.0f,
                   1.0f,
                   0.01f,
                   [this](float value) { working_lighting_.ambient_intensity = value; });
        return;
    }

    if (tab_ == Tab::LIGHTS) {
        RoomLight* light = selected_light();
        if (!light) {
            add_button({x, region_.top + 45.0f, 280.0f, 28.0f},
                       "No authored lights (ambient is still editable)",
                       {});
            return;
        }
        add_button({x, region_.top + 34.0f, 30.0f, 21.0f}, "<", [this] {
            selected_light_ =
                selected_light_ == 0 ? working_lighting_.lights.size() - 1 : selected_light_ - 1;
        });
        add_button({x + 36.0f, region_.top + 34.0f, 210.0f, 21.0f},
                   std::to_string(selected_light_ + 1) + "/" +
                       std::to_string(working_lighting_.lights.size()) + "  " + light->id,
                   {});
        add_button({x + 252.0f, region_.top + 34.0f, 30.0f, 21.0f}, ">", [this] {
            selected_light_ = (selected_light_ + 1) % working_lighting_.lights.size();
        });
        add_button({x + 294.0f, region_.top + 34.0f, 72.0f, 21.0f},
                   light_page_ == LightPage::CORE ? "[ Core ]" : "Core",
                   [this] { light_page_ = LightPage::CORE; });
        add_button({x + 372.0f, region_.top + 34.0f, 78.0f, 21.0f},
                   light_page_ == LightPage::SHAPE ? "[ Shape ]" : "Shape",
                   [this] { light_page_ = LightPage::SHAPE; });
        add_button({x + 456.0f, region_.top + 34.0f, 104.0f, 21.0f},
                   light_page_ == LightPage::MODULATION ? "[ Modulation ]" : "Modulation",
                   [this] { light_page_ = LightPage::MODULATION; });

        if (light_page_ == LightPage::CORE) {
            add_button(row_cell(region_, 0, 5, 62.0f, 38.0f),
                       light->enabled ? "[x] Enabled" : "[ ] Enabled",
                       [light] { light->enabled = !light->enabled; });
            add_slider(row_cell(region_, 1, 5, 62.0f, 38.0f),
                       "Intensity",
                       light->intensity,
                       0.0f,
                       4.0f,
                       0.01f,
                       [light](float value) { light->intensity = value; });
            for (std::size_t i = 0; i < 3; ++i) {
                static constexpr std::array<const char*, 3> names{"Color R", "Color G", "Color B"};
                add_slider(row_cell(region_, i + 2, 5, 62.0f, 38.0f),
                           names[i],
                           light->color[i],
                           0.0f,
                           1.0f,
                           0.01f,
                           [light, i](float value) { light->color[i] = value; });
            }
        } else if (light_page_ == LightPage::SHAPE) {
            const std::size_t count = light->type == RoomLight::Type::SPOT ? 5 : 2;
            add_slider(row_cell(region_, 0, count, 62.0f, 38.0f),
                       light->type == RoomLight::Type::SPOT ? "Range" : "Radius",
                       light->radius,
                       1.0f,
                       2000.0f,
                       1.0f,
                       [light](float value) { light->radius = value; });
            add_slider(row_cell(region_, 1, count, 62.0f, 38.0f),
                       "Height",
                       light->height,
                       1.0f,
                       1000.0f,
                       1.0f,
                       [light](float value) { light->height = value; });
            if (light->type == RoomLight::Type::SPOT) {
                add_slider(row_cell(region_, 2, count, 62.0f, 38.0f),
                           "Direction",
                           light->direction,
                           -180.0f,
                           180.0f,
                           1.0f,
                           [light](float value) { light->direction = value; });
                add_slider(row_cell(region_, 3, count, 62.0f, 38.0f),
                           "Angle",
                           light->angle,
                           1.0f,
                           179.0f,
                           1.0f,
                           [light](float value) {
                               light->angle = value;
                               light->softness = std::min(light->softness, value * 0.5f - 0.01f);
                           });
                add_slider(row_cell(region_, 4, count, 62.0f, 38.0f),
                           "Softness",
                           light->softness,
                           0.0f,
                           std::max(0.01f, light->angle * 0.5f - 0.01f),
                           0.1f,
                           [light](float value) { light->softness = value; });
            }
        } else {
            add_button(
                row_cell(region_, 0, 4, 62.0f, 38.0f),
                "Type: " + modulation_name(light->modulation.type),
                [light] { light->modulation.type = next_modulation(light->modulation.type); });
            add_slider(row_cell(region_, 1, 4, 62.0f, 38.0f),
                       "Amount",
                       light->modulation.amount,
                       0.0f,
                       1.0f,
                       0.01f,
                       [light](float value) { light->modulation.amount = value; });
            add_slider(row_cell(region_, 2, 4, 62.0f, 38.0f),
                       "Speed",
                       light->modulation.speed,
                       0.01f,
                       20.0f,
                       0.01f,
                       [light](float value) { light->modulation.speed = value; });
            add_slider(row_cell(region_, 3, 4, 62.0f, 38.0f),
                       "Seed",
                       light->modulation.seed,
                       -100.0f,
                       100.0f,
                       0.1f,
                       [light](float value) { light->modulation.seed = value; });
        }
        return;
    }

    if (!working_post_process_ || working_post_process_->shaders.empty()) {
        add_button({x, region_.top + 45.0f, 300.0f, 28.0f},
                   "No post-process shader in this room",
                   {});
        return;
    }

    gfx::ShaderEffect* effect = selected_effect();
    add_button({x, region_.top + 34.0f, 124.0f, 21.0f},
               working_post_process_->enabled ? "[x] Post process" : "[ ] Post process",
               [this] { working_post_process_->enabled = !working_post_process_->enabled; });
    add_button({x + 132.0f, region_.top + 34.0f, 30.0f, 21.0f}, "<", [this] {
        selected_effect_ = selected_effect_ == 0 ? working_post_process_->shaders.size() - 1
                                                 : selected_effect_ - 1;
        selected_param_ = selected_component_ = 0;
    });
    add_button({x + 168.0f, region_.top + 34.0f, 250.0f, 21.0f},
               std::to_string(selected_effect_ + 1) + "/" +
                   std::to_string(working_post_process_->shaders.size()) + "  " + effect->source,
               {});
    add_button({x + 424.0f, region_.top + 34.0f, 30.0f, 21.0f}, ">", [this] {
        selected_effect_ = (selected_effect_ + 1) % working_post_process_->shaders.size();
        selected_param_ = selected_component_ = 0;
    });
    add_button({x + 462.0f, region_.top + 34.0f, 108.0f, 21.0f},
               effect->enabled ? "[x] Pass" : "[ ] Pass",
               [effect] { effect->enabled = !effect->enabled; });

    gfx::ShaderParam* param = selected_param();
    if (!param) {
        add_button({x, region_.top + 64.0f, 260.0f, 30.0f}, "Shader has no authored params", {});
        return;
    }
    add_button({x, region_.top + 64.0f, 30.0f, 32.0f}, "<", [this, effect] {
        selected_param_ = selected_param_ == 0 ? effect->params.size() - 1 : selected_param_ - 1;
        selected_component_ = 0;
    });
    add_button({x + 36.0f, region_.top + 64.0f, 205.0f, 32.0f},
               std::to_string(selected_param_ + 1) + "/" + std::to_string(effect->params.size()) +
                   "  " + param->name,
               {});
    add_button({x + 247.0f, region_.top + 64.0f, 30.0f, 32.0f}, ">", [this, effect] {
        selected_param_ = (selected_param_ + 1) % effect->params.size();
        selected_component_ = 0;
    });

    if (is_bool(param->value)) {
        add_button({x + 285.0f, region_.top + 64.0f, 190.0f, 32.0f},
                   component_value(param->value, 0) > 0.5f ? "[x] true" : "[ ] false",
                   [param] {
                       set_component(param->value,
                                     0,
                                     component_value(param->value, 0) > 0.5f ? 0.0f : 1.0f);
                   });
        return;
    }
    const std::size_t count = component_count(param->value);
    if (count > 1) {
        add_button({x + 285.0f, region_.top + 64.0f, 30.0f, 32.0f}, "<", [this, count] {
            selected_component_ = selected_component_ == 0 ? count - 1 : selected_component_ - 1;
        });
        add_button({x + 321.0f, region_.top + 64.0f, 78.0f, 32.0f},
                   "component " + std::to_string(selected_component_ + 1),
                   {});
        add_button({x + 405.0f, region_.top + 64.0f, 30.0f, 32.0f}, ">", [this, count] {
            selected_component_ = (selected_component_ + 1) % count;
        });
    }
    const float slider_left = count > 1 ? x + 443.0f : x + 285.0f;
    const auto [minimum, maximum] = shader_range(*param);
    add_slider(
        {slider_left,
         region_.top + 64.0f,
         region_.left + region_.width - 8.0f - slider_left,
         32.0f},
        param->name + component_suffix(*param, selected_component_),
        component_value(param->value, selected_component_),
        minimum,
        maximum,
        std::holds_alternative<int>(param->value) ? 1.0f : 0.01f,
        [param, this](float value) { set_component(param->value, selected_component_, value); });
}

void RoomTuningOverlay::activate(Control& control, float mouse_x) {
    if (!control.action) {
        return;
    }
    if (control.type == ControlType::BUTTON) {
        control.action(0.0f);
        return;
    }
    const float t = std::clamp((mouse_x - control.rect.left) / control.rect.width, 0.0f, 1.0f);
    float value = control.minimum + (control.maximum - control.minimum) * t;
    if (control.step > 0.0f) {
        value = std::round(value / control.step) * control.step;
    }
    value = std::clamp(value, control.minimum, control.maximum);
    control.action(value);
}

bool RoomTuningOverlay::handle_event(const sf::Event& event) {
    if (!active_) {
        return false;
    }
    if (event.type == sf::Event::KeyPressed) {
        if (event.key.code == sf::Keyboard::Escape) {
            close();
            return true;
        }
        if (event.key.control && event.key.code == sf::Keyboard::C) {
            copy_yaml();
            return true;
        }
    }
    if (event.type == sf::Event::MouseMoved) {
        pointer_ = {static_cast<float>(event.mouseMove.x), static_cast<float>(event.mouseMove.y)};
        if (dragged_control_) {
            rebuild_controls();
            if (*dragged_control_ < controls_.size()) {
                activate(controls_[*dragged_control_], pointer_.x);
            }
        }
        return true;
    }
    if (event.type == sf::Event::MouseButtonPressed &&
        event.mouseButton.button == sf::Mouse::Left) {
        pointer_ = {static_cast<float>(event.mouseButton.x),
                    static_cast<float>(event.mouseButton.y)};
        rebuild_controls();
        for (std::size_t i = controls_.size(); i-- > 0;) {
            if (!controls_[i].rect.contains(pointer_)) {
                continue;
            }
            if (controls_[i].type == ControlType::SLIDER) {
                dragged_control_ = i;
            }
            activate(controls_[i], pointer_.x);
            break;
        }
        return true;
    }
    if (event.type == sf::Event::MouseButtonReleased &&
        event.mouseButton.button == sf::Mouse::Left) {
        dragged_control_.reset();
        return true;
    }
    return true;
}

void RoomTuningOverlay::draw(sf::RenderTarget& target) {
    if (!active_) {
        return;
    }
    rebuild_controls();

    sf::RectangleShape panel({region_.width, region_.height});
    panel.setPosition(region_.left, region_.top);
    panel.setFillColor(kPanel);
    panel.setOutlineColor(kBorder);
    panel.setOutlineThickness(1.0f);
    target.draw(panel);

    for (const Control& control : controls_) {
        const bool hovered = control.rect.contains(pointer_);
        sf::RectangleShape box({control.rect.width, control.rect.height});
        box.setPosition(control.rect.left, control.rect.top);
        box.setFillColor(hovered ? kControlHover : kControl);
        box.setOutlineColor(kBorder);
        box.setOutlineThickness(1.0f);
        target.draw(box);

        if (control.type == ControlType::SLIDER) {
            const float t =
                std::clamp((control.value - control.minimum) / (control.maximum - control.minimum),
                           0.0f,
                           1.0f);
            sf::RectangleShape fill({control.rect.width * t, 5.0f});
            fill.setPosition(control.rect.left, control.rect.top + control.rect.height - 5.0f);
            fill.setFillColor(kAccent);
            target.draw(fill);
            draw_label(target,
                       font_,
                       control.label + "  " + decimal(control.value),
                       {control.rect.left + 6.0f, control.rect.top + 4.0f},
                       12);
        } else {
            draw_label(target,
                       font_,
                       control.label,
                       {control.rect.left + 6.0f, control.rect.top + 3.0f},
                       12,
                       control.label.starts_with("[") ? sf::Color::White : kText);
        }
    }

    if (!status_.empty()) {
        draw_label(target, font_, status_, {region_.left + 300.0f, region_.top + 8.0f}, 11, kMuted);
    }
}

void RoomTuningOverlay::copy_yaml() {
    const std::string text = yaml();
    sf::Clipboard::setString(sf::String::fromUtf8(text.begin(), text.end()));
    status_ = "Copied lighting + post_process YAML";
}

std::string RoomTuningOverlay::yaml() const {
    YAML::Node root;
    YAML::Node lighting;
    YAML::Node ambient;
    ambient["color"] = array_node(working_lighting_.ambient_color);
    ambient["intensity"] = working_lighting_.ambient_intensity;
    lighting["ambient"] = ambient;

    if (!working_lighting_.normal_map.empty()) {
        YAML::Node normal;
        normal["image"] = working_lighting_.normal_map;
        normal["origin"] = point_node(working_lighting_.normal_origin);
        normal["scale"] = working_lighting_.normal_scale;
        normal["strength"] = working_lighting_.normal_strength;
        lighting["normal_map"] = normal;
    }

    if (!working_lighting_.lights.empty()) {
        YAML::Node lights(YAML::NodeType::Sequence);
        for (const RoomLight& light : working_lighting_.lights) {
            YAML::Node node;
            node["id"] = light.id;
            node["type"] = light.type == RoomLight::Type::SPOT ? "spot" : "omni";
            if (light.attach.empty()) {
                node["at"] = point_node(light.at);
            } else {
                node["attach"] = light.attach;
                if (light.offset.x != 0.0f || light.offset.y != 0.0f) {
                    node["offset"] = point_node(light.offset);
                }
            }
            node[light.type == RoomLight::Type::SPOT ? "range" : "radius"] = light.radius;
            node["height"] = light.height;
            node["color"] = array_node(light.color);
            node["intensity"] = light.intensity;
            if (!light.enabled) {
                node["enabled"] = false;
            }
            if (light.type == RoomLight::Type::SPOT) {
                node["direction"] = light.direction;
                node["follow_facing"] = light.follow_facing;
                node["angle"] = light.angle;
                node["softness"] = light.softness;
            }
            if (light.modulation.type != LightModulation::Type::NONE) {
                YAML::Node modulation;
                modulation["type"] = modulation_name(light.modulation.type);
                modulation["amount"] = light.modulation.amount;
                modulation["speed"] = light.modulation.speed;
                modulation["seed"] = light.modulation.seed;
                node["modulation"] = modulation;
            }
            lights.push_back(node);
        }
        lighting["lights"] = lights;
    }

    if (!working_lighting_.occluders.empty()) {
        YAML::Node occluders(YAML::NodeType::Sequence);
        for (const LightOccluder& occluder : working_lighting_.occluders) {
            YAML::Node node;
            node["id"] = occluder.id;
            YAML::Node area(YAML::NodeType::Sequence);
            for (geom::Point point : occluder.area) {
                area.push_back(point_node(point));
            }
            node["area"] = area;
            if (!occluder.enabled) {
                node["enabled"] = false;
            }
            occluders.push_back(node);
        }
        lighting["occluders"] = occluders;
    }

    if (projected_shadow_) {
        const ProjectedShadow& shadow = *projected_shadow_;
        YAML::Node node;
        if (!shadow.enabled) {
            node["enabled"] = false;
        }
        if (shadow.source.empty()) {
            node["light"] = point_node(shadow.light);
        } else {
            node["source"] = shadow.source;
        }
        node["casters"] = shadow.casters == ProjectedShadow::Casters::ALL ? "all" : "player";
        node["length"] = shadow.length;
        node["width"] = shadow.width;
        node["opacity"] = shadow.opacity;
        node["softness"] = shadow.softness;
        node["contact_shadow"] = shadow.contact_shadow;
        if (shadow.z) {
            node["z"] = *shadow.z;
        }
        YAML::Node color;
        color["r"] = static_cast<unsigned>(shadow.color.r);
        color["g"] = static_cast<unsigned>(shadow.color.g);
        color["b"] = static_cast<unsigned>(shadow.color.b);
        node["color"] = color;
        lighting["projected_shadows"] = node;
    }
    root["lighting"] = lighting;

    if (working_post_process_) {
        YAML::Node post;
        if (!working_post_process_->enabled) {
            post["enabled"] = false;
        }
        if (working_post_process_->shaders.size() == 1) {
            post["shader"] = shader_node(working_post_process_->shaders.front());
        } else if (!working_post_process_->shaders.empty()) {
            YAML::Node shaders(YAML::NodeType::Sequence);
            for (const gfx::ShaderEffect& effect : working_post_process_->shaders) {
                shaders.push_back(shader_node(effect));
            }
            post["shaders"] = shaders;
        }
        root["post_process"] = post;
    }

    YAML::Emitter out;
    out.SetIndent(2);
    out << root;
    return std::string(out.c_str()) + "\n";
}

} // namespace pac::pnc
