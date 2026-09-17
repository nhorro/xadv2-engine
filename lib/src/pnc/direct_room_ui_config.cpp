#include "engine/pnc/direct_room_ui_config.hpp"

#include "core/load_error_yaml.hpp"
#include "engine/pnc/data_error.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <string>

namespace pac::pnc {

namespace {

constexpr const char* kSource = "direct-room-ui-loader";

[[noreturn]] void
fail(const std::string& code, const std::string& message, const YAML::Node& node = {}) {
    pac::core::fail_at<DataError>(kSource, code, message, node);
}

float number(const YAML::Node& node, std::size_t index, const std::string& field) {
    try {
        const float value = node[index].as<float>();
        if (!std::isfinite(value)) {
            fail("direct-room-ui.number-invalid", field + " must contain finite numbers", node);
        }
        return value;
    } catch (const YAML::Exception&) {
        fail("direct-room-ui.number-invalid", field + " must contain numbers", node);
    }
}

sf::FloatRect rect(const YAML::Node& node, const std::string& field, sf::FloatRect fallback) {
    if (!node) {
        return fallback;
    }
    if (!node.IsSequence() || node.size() != 4) {
        fail("direct-room-ui.rect-invalid", field + " must be [x, y, width, height]", node);
    }
    sf::FloatRect value{number(node, 0, field),
                        number(node, 1, field),
                        number(node, 2, field),
                        number(node, 3, field)};
    if (value.width <= 0.0f || value.height <= 0.0f) {
        fail("direct-room-ui.rect-size-invalid",
             field + " width and height must be positive",
             node);
    }
    return value;
}

sf::Vector2f size(const YAML::Node& node, const std::string& field, sf::Vector2f fallback) {
    if (!node) {
        return fallback;
    }
    if (!node.IsSequence() || node.size() != 2) {
        fail("direct-room-ui.size-invalid", field + " must be [width, height]", node);
    }
    sf::Vector2f value{number(node, 0, field), number(node, 1, field)};
    if (value.x <= 0.0f || value.y <= 0.0f) {
        fail("direct-room-ui.size-positive", field + " values must be positive", node);
    }
    return value;
}

sf::Vector2f spacing(const YAML::Node& node, const std::string& field, sf::Vector2f fallback) {
    if (!node) {
        return fallback;
    }
    if (!node.IsSequence() || node.size() != 2) {
        fail("direct-room-ui.spacing-invalid", field + " must be [x, y]", node);
    }
    sf::Vector2f value{number(node, 0, field), number(node, 1, field)};
    if (value.x < 0.0f || value.y < 0.0f) {
        fail("direct-room-ui.spacing-negative", field + " values must not be negative", node);
    }
    return value;
}

std::uint8_t hex_byte(const std::string& value, std::size_t at, const YAML::Node& node) {
    try {
        return static_cast<std::uint8_t>(std::stoul(value.substr(at, 2), nullptr, 16));
    } catch (const std::exception&) {
        fail("direct-room-ui.color-invalid", "colors must use #RRGGBB or #RRGGBBAA", node);
    }
}

sf::Color color(const YAML::Node& node, const std::string& field, sf::Color fallback) {
    if (!node) {
        return fallback;
    }
    const std::string value = node.as<std::string>();
    if ((value.size() != 7 && value.size() != 9) || value.front() != '#') {
        fail("direct-room-ui.color-invalid", field + " must use #RRGGBB or #RRGGBBAA", node);
    }
    for (std::size_t i = 1; i < value.size(); ++i) {
        if (!std::isxdigit(static_cast<unsigned char>(value[i]))) {
            fail("direct-room-ui.color-invalid", field + " must use hexadecimal digits", node);
        }
    }
    return {hex_byte(value, 1, node),
            hex_byte(value, 3, node),
            hex_byte(value, 5, node),
            value.size() == 9 ? hex_byte(value, 7, node) : static_cast<std::uint8_t>(255)};
}

float positive(const YAML::Node& node, const std::string& field, float fallback) {
    if (!node) {
        return fallback;
    }
    const float value = node.as<float>();
    if (!std::isfinite(value) || value <= 0.0f) {
        fail("direct-room-ui.positive-invalid", field + " must be a positive number", node);
    }
    return value;
}

bool boolean(const YAML::Node& node, const std::string& field, bool fallback) {
    if (!node) {
        return fallback;
    }
    try {
        return node.as<bool>();
    } catch (const YAML::Exception&) {
        fail("direct-room-ui.boolean-invalid", field + " must be true or false", node);
    }
}

int integer(const YAML::Node& node, const std::string& field, int fallback) {
    if (!node) {
        return fallback;
    }
    try {
        return node.as<int>();
    } catch (const YAML::Exception&) {
        fail("direct-room-ui.integer-invalid", field + " must be an integer", node);
    }
}

} // namespace

DirectRoomUiConfig parse_direct_room_ui_config(const std::string& yaml_text) {
    YAML::Node document;
    try {
        document = YAML::Load(yaml_text);
    } catch (const YAML::Exception& e) {
        fail("direct-room-ui.invalid-yaml", std::string("invalid YAML: ") + e.what());
    }
    const YAML::Node root = document["direct_room_ui"];
    if (!root || !root.IsMap()) {
        fail("direct-room-ui.root-missing", "expected a 'direct_room_ui' mapping", document);
    }

    DirectRoomUiConfig config;
    config.design_size = size(root["design_size"], "design_size", config.design_size);

    if (const YAML::Node interaction = root["interaction"]) {
        config.interaction.drag_threshold = positive(interaction["drag_threshold"],
                                                     "interaction.drag_threshold",
                                                     config.interaction.drag_threshold);
        config.interaction.long_press_seconds = positive(interaction["long_press_seconds"],
                                                         "interaction.long_press_seconds",
                                                         config.interaction.long_press_seconds);
    }

    config.bag_button = rect(root["bag_button"], "bag_button", config.bag_button);
    config.menu_button = rect(root["menu_button"], "menu_button", config.menu_button);
    config.action_text = rect(root["action_text"], "action_text", config.action_text);
    config.action_text_offset =
        spacing(root["action_text_offset"], "action_text_offset", config.action_text_offset);
    config.action_follows_cursor = boolean(root["action_follows_cursor"],
                                           "action_follows_cursor",
                                           config.action_follows_cursor);
    config.bag_label = root["bag_label"] ? root["bag_label"].as<std::string>() : std::string();
    config.menu_label =
        root["menu_label"] ? root["menu_label"].as<std::string>() : std::string();

    if (const YAML::Node context = root["context_menu"]) {
        config.context_menu.cell_size =
            size(context["cell_size"], "context_menu.cell_size", config.context_menu.cell_size);
        config.context_menu.gap =
            positive(context["gap"], "context_menu.gap", config.context_menu.gap);
        config.context_menu.padding =
            positive(context["padding"], "context_menu.padding", config.context_menu.padding);
    }

    if (const YAML::Node inventory = root["inventory"]) {
        config.inventory.panel =
            rect(inventory["panel"], "inventory.panel", config.inventory.panel);
        config.inventory.grid = rect(inventory["grid"], "inventory.grid", config.inventory.grid);
        config.inventory.previous =
            rect(inventory["previous"], "inventory.previous", config.inventory.previous);
        config.inventory.next = rect(inventory["next"], "inventory.next", config.inventory.next);
        config.inventory.rows =
            inventory["rows"] ? inventory["rows"].as<int>() : config.inventory.rows;
        config.inventory.columns =
            inventory["columns"] ? inventory["columns"].as<int>() : config.inventory.columns;
        config.inventory.cell_gap =
            spacing(inventory["cell_gap"], "inventory.cell_gap", config.inventory.cell_gap);
        config.inventory.compact_single_page = boolean(inventory["compact_single_page"],
                                                       "inventory.compact_single_page",
                                                       config.inventory.compact_single_page);
        config.inventory.compact_padding = positive(inventory["compact_padding"],
                                                    "inventory.compact_padding",
                                                    config.inventory.compact_padding);
        if (config.inventory.rows <= 0 || config.inventory.columns <= 0) {
            fail("direct-room-ui.grid-invalid",
                 "inventory rows and columns must be positive",
                 inventory);
        }
    }

    if (const YAML::Node icons = root["icons"]) {
        if (!icons.IsMap()) {
            fail("direct-room-ui.icons-invalid", "icons must be a mapping", icons);
        }
        config.icons.sheet = icons["sheet"] ? icons["sheet"].as<std::string>() : config.icons.sheet;
        config.icons.columns = integer(icons["columns"], "icons.columns", config.icons.columns);
        config.icons.rows = integer(icons["rows"], "icons.rows", config.icons.rows);
        config.icons.bag = integer(icons["bag"], "icons.bag", config.icons.bag);
        config.icons.menu = integer(icons["menu"], "icons.menu", config.icons.menu);
        config.icons.previous = integer(icons["previous"], "icons.previous", config.icons.previous);
        config.icons.next = integer(icons["next"], "icons.next", config.icons.next);
        config.icons.look_at = integer(icons["look_at"], "icons.look_at", config.icons.look_at);
        config.icons.talk_to = integer(icons["talk_to"], "icons.talk_to", config.icons.talk_to);
        config.icons.pick_up = integer(icons["pick_up"], "icons.pick_up", config.icons.pick_up);
        config.icons.open = integer(icons["open"], "icons.open", config.icons.open);
        config.icons.close = integer(icons["close"], "icons.close", config.icons.close);
        config.icons.push = integer(icons["push"], "icons.push", config.icons.push);
        config.icons.pull = integer(icons["pull"], "icons.pull", config.icons.pull);
        config.icons.use = integer(icons["use"], "icons.use", config.icons.use);
        config.icons.give = integer(icons["give"], "icons.give", config.icons.give);
        if (config.icons.columns <= 0 || config.icons.rows <= 0) {
            fail("direct-room-ui.icon-grid-invalid",
                 "icon atlas rows and columns must be positive",
                 icons);
        }
        const int limit = config.icons.columns * config.icons.rows;
        const int cells[] = {config.icons.bag,
                             config.icons.menu,
                             config.icons.previous,
                             config.icons.next,
                             config.icons.look_at,
                             config.icons.talk_to,
                             config.icons.pick_up,
                             config.icons.open,
                             config.icons.close,
                             config.icons.push,
                             config.icons.pull,
                             config.icons.use,
                             config.icons.give};
        for (const int cell : cells) {
            if (cell < -1 || cell >= limit) {
                fail("direct-room-ui.icon-cell-invalid",
                     "icon cells must be -1 or within the configured atlas grid",
                     icons);
            }
        }
    }

    if (const YAML::Node style = root["style"]) {
        config.style.panel_background = color(style["panel_background"],
                                              "style.panel_background",
                                              config.style.panel_background);
        config.style.panel_border =
            color(style["panel_border"], "style.panel_border", config.style.panel_border);
        config.style.slot_background =
            color(style["slot_background"], "style.slot_background", config.style.slot_background);
        config.style.slot_border =
            color(style["slot_border"], "style.slot_border", config.style.slot_border);
        config.style.hover_border =
            color(style["hover_border"], "style.hover_border", config.style.hover_border);
        config.style.button_background = color(style["button_background"],
                                               "style.button_background",
                                               config.style.button_background);
        config.style.button_border =
            color(style["button_border"], "style.button_border", config.style.button_border);
        config.style.button_hover =
            color(style["button_hover"], "style.button_hover", config.style.button_hover);
        config.style.text = color(style["text"], "style.text", config.style.text);
        config.style.disabled_text =
            color(style["disabled_text"], "style.disabled_text", config.style.disabled_text);
        config.style.action_background = color(style["action_background"],
                                               "style.action_background",
                                               config.style.action_background);
        config.style.action_outline =
            color(style["action_outline"], "style.action_outline", config.style.action_outline);
        config.style.notification =
            color(style["notification"], "style.notification", config.style.notification);
        config.style.button_radius_ratio = positive(style["button_radius_ratio"],
                                                    "style.button_radius_ratio",
                                                    config.style.button_radius_ratio);
        config.style.border_thickness = positive(style["border_thickness"],
                                                 "style.border_thickness",
                                                 config.style.border_thickness);
        config.style.text_size =
            style["text_size"] ? style["text_size"].as<unsigned>() : config.style.text_size;
        config.style.action_text_size = style["action_text_size"]
                                            ? style["action_text_size"].as<unsigned>()
                                            : config.style.action_text_size;
        config.style.action_outline_thickness = positive(style["action_outline_thickness"],
                                                         "style.action_outline_thickness",
                                                         config.style.action_outline_thickness);
        if (config.style.button_radius_ratio > 0.5f) {
            fail("direct-room-ui.button-radius-invalid",
                 "style.button_radius_ratio must not exceed 0.5",
                 style["button_radius_ratio"]);
        }
        if (config.style.text_size == 0 || config.style.action_text_size == 0) {
            fail("direct-room-ui.text-size-invalid", "text sizes must be positive", style);
        }
    }
    return config;
}

} // namespace pac::pnc
