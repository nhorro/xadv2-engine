#include "engine/pnc/scumm_panel_config.hpp"

#include "core/load_error_yaml.hpp"
#include "engine/core/resource_source.hpp"
#include "engine/pnc/data_error.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <sstream>

namespace pac::pnc {

namespace {

constexpr const char* kSource = "scumm-panel-loader";

constexpr std::array<Verb, 9> kDefaultVerbGrid{Verb::LOOK_AT,
                                               Verb::OPEN,
                                               Verb::PUSH,
                                               Verb::TALK_TO,
                                               Verb::CLOSE,
                                               Verb::PULL,
                                               Verb::PICK_UP,
                                               Verb::USE,
                                               Verb::GIVE};

[[noreturn]] void
panel_fail(const std::string& code, const std::string& msg, const YAML::Node& at = YAML::Node()) {
    pac::core::fail_at<DataError>(kSource, code, msg, at);
}

float number_at(const YAML::Node& node, std::size_t i, const std::string& field) {
    try {
        return node[i].as<float>();
    } catch (const YAML::Exception&) {
        panel_fail("scumm-panel.number-invalid", field + " must contain numbers", node);
    }
}

sf::FloatRect rect_from(const YAML::Node& node, const std::string& field) {
    if (!node || !node.IsSequence() || node.size() != 4) {
        panel_fail("scumm-panel.rect-invalid", field + " must be [x, y, width, height]", node);
    }
    sf::FloatRect rect{number_at(node, 0, field),
                       number_at(node, 1, field),
                       number_at(node, 2, field),
                       number_at(node, 3, field)};
    if (rect.width <= 0.0f || rect.height <= 0.0f) {
        panel_fail("scumm-panel.rect-size-invalid",
                   field + " width and height must be positive",
                   node);
    }
    return rect;
}

ScummPanelPadding
padding_from(const YAML::Node& node, const std::string& field, ScummPanelPadding fallback = {}) {
    if (!node) {
        return fallback;
    }
    if (!node.IsSequence() || node.size() != 4) {
        panel_fail("scumm-panel.padding-invalid",
                   field + " must be [left, top, right, bottom]",
                   node);
    }
    return {number_at(node, 0, field),
            number_at(node, 1, field),
            number_at(node, 2, field),
            number_at(node, 3, field)};
}

sf::Vector2f vec2_from(const YAML::Node& node, const std::string& field) {
    if (!node || !node.IsSequence() || node.size() != 2) {
        panel_fail("scumm-panel.vec2-invalid", field + " must be [x, y]", node);
    }
    const sf::Vector2f v{number_at(node, 0, field), number_at(node, 1, field)};
    if (v.x <= 0.0f || v.y <= 0.0f) {
        panel_fail("scumm-panel.design-size-invalid", field + " values must be positive", node);
    }
    return v;
}

sf::Vector2f normalized_vec2_from(const YAML::Node& node, const std::string& field) {
    if (!node || !node.IsSequence() || node.size() != 2) {
        panel_fail("scumm-panel.normalized-position-invalid", field + " must be [x, y]", node);
    }
    const sf::Vector2f v{number_at(node, 0, field), number_at(node, 1, field)};
    if (v.x < 0.0f || v.y < 0.0f || v.x > 1.0f || v.y > 1.0f) {
        panel_fail("scumm-panel.normalized-position-invalid",
                   field + " values must be between 0 and 1",
                   node);
    }
    return v;
}

sf::Vector2f gap_from(const YAML::Node& node, const std::string& field, sf::Vector2f fallback) {
    if (!node) {
        return fallback;
    }
    if (!node.IsSequence() || node.size() != 2) {
        panel_fail("scumm-panel.cell-gap-invalid", field + " must be [x, y]", node);
    }
    return {number_at(node, 0, field), number_at(node, 1, field)};
}

int positive_int(const YAML::Node& node, const std::string& field) {
    if (!node) {
        panel_fail("scumm-panel.int-missing", field + " is required", node);
    }
    int value = 0;
    try {
        value = node.as<int>();
    } catch (const YAML::Exception&) {
        panel_fail("scumm-panel.int-invalid", field + " must be a positive integer", node);
    }
    if (value <= 0) {
        panel_fail("scumm-panel.int-invalid", field + " must be a positive integer", node);
    }
    return value;
}

sf::Color parse_color(const std::string& text, const YAML::Node& at) {
    if (text.size() != 7 || text[0] != '#') {
        panel_fail("scumm-panel.color-invalid", "color must use #RRGGBB form", at);
    }
    const auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') {
            return c - '0';
        }
        if (c >= 'a' && c <= 'f') {
            return c - 'a' + 10;
        }
        if (c >= 'A' && c <= 'F') {
            return c - 'A' + 10;
        }
        return -1;
    };
    std::array<int, 6> n{};
    for (std::size_t i = 0; i < n.size(); ++i) {
        n[i] = hex(text[i + 1]);
        if (n[i] < 0) {
            panel_fail("scumm-panel.color-invalid", "color must use #RRGGBB form", at);
        }
    }
    return sf::Color(static_cast<sf::Uint8>(n[0] * 16 + n[1]),
                     static_cast<sf::Uint8>(n[2] * 16 + n[3]),
                     static_cast<sf::Uint8>(n[4] * 16 + n[5]));
}

std::string
resolve_asset(const std::string& base_dir, const YAML::Node& node, const std::string& field) {
    if (!node) {
        return {};
    }
    const std::string value = node.as<std::string>();
    if (value.empty()) {
        return {};
    }
    const std::string joined = pac::core::logical_join(base_dir, value);
    if (!pac::core::is_valid_logical_path(joined)) {
        panel_fail("scumm-panel.asset-path-invalid",
                   field + " is not a valid logical path after resolving relative to the YAML file",
                   node);
    }
    return joined;
}

ScummPanelBackgroundType background_type(const std::string& value, const YAML::Node& node) {
    if (value == "solid") {
        return ScummPanelBackgroundType::SOLID;
    }
    if (value == "image") {
        return ScummPanelBackgroundType::IMAGE;
    }
    if (value == "nine_slice") {
        return ScummPanelBackgroundType::NINE_SLICE;
    }
    panel_fail("scumm-panel.background-type-invalid",
               "background.type must be solid, image, or nine_slice",
               node);
}

ScummPanelScaleMode scale_mode(const std::string& value, const YAML::Node& node) {
    if (value == "stretch") {
        return ScummPanelScaleMode::STRETCH;
    }
    if (value == "fit") {
        return ScummPanelScaleMode::FIT;
    }
    if (value == "tile") {
        return ScummPanelScaleMode::TILE;
    }
    if (value == "nine_slice") {
        return ScummPanelScaleMode::NINE_SLICE;
    }
    panel_fail("scumm-panel.scale-mode-invalid",
               "background.scale_mode must be stretch, fit, tile, or nine_slice",
               node);
}

InventoryArrowMode arrow_mode(const std::string& value, const YAML::Node& node) {
    if (value == "draw") {
        return InventoryArrowMode::DRAW;
    }
    if (value == "background_variants") {
        return InventoryArrowMode::BACKGROUND_VARIANTS;
    }
    if (value == "none") {
        return InventoryArrowMode::NONE;
    }
    panel_fail("scumm-panel.arrow-mode-invalid",
               "inventory_arrows.mode must be draw, background_variants, or none",
               node);
}

InventoryArrowPlacement arrow_placement(const std::string& value, const YAML::Node& node) {
    if (value == "right") {
        return InventoryArrowPlacement::RIGHT;
    }
    if (value == "left") {
        return InventoryArrowPlacement::LEFT;
    }
    if (value == "both") {
        return InventoryArrowPlacement::BOTH;
    }
    panel_fail("scumm-panel.arrow-placement-invalid",
               "inventory_arrows.placement must be right, left, or both",
               node);
}

ScummPanelAnchor anchor_from(const std::string& value, const YAML::Node& node) {
    if (value == "top_left") {
        return ScummPanelAnchor::TOP_LEFT;
    }
    if (value == "top_right") {
        return ScummPanelAnchor::TOP_RIGHT;
    }
    if (value == "bottom_left") {
        return ScummPanelAnchor::BOTTOM_LEFT;
    }
    if (value == "bottom_right") {
        return ScummPanelAnchor::BOTTOM_RIGHT;
    }
    if (value == "center") {
        return ScummPanelAnchor::CENTER;
    }
    panel_fail("scumm-panel.anchor-invalid",
               "settings_button.anchor must be center, top_left, top_right, bottom_left, "
               "or bottom_right",
               node);
}

ScummButtonRenderMode button_render_mode(const std::string& value, const YAML::Node& node) {
    if (value == "panel") {
        return ScummButtonRenderMode::PANEL;
    }
    if (value == "image") {
        return ScummButtonRenderMode::IMAGE;
    }
    panel_fail("scumm-panel.button-render-mode-invalid",
               "settings_button.render_mode must be panel or image",
               node);
}

VerbPanelStyle verb_panel_style(const std::string& value, const YAML::Node& node) {
    if (value == "buttons") {
        return VerbPanelStyle::BUTTONS;
    }
    if (value == "text") {
        return VerbPanelStyle::TEXT;
    }
    panel_fail("scumm-panel.verb-style-invalid",
               "layout.verb_panel.style must be buttons or text",
               node);
}

InventoryStyle inventory_style_from(const std::string& value, const YAML::Node& node) {
    if (value == "text") {
        return InventoryStyle::TEXT;
    }
    if (value == "icons") {
        return InventoryStyle::ICONS;
    }
    panel_fail("scumm-panel.inventory-style-invalid",
               "layout.inventory_panel.style must be text or icons",
               node);
}

ScummPanelBackground parse_background(const YAML::Node& node,
                                      const std::string& base_dir,
                                      ScummPanelBackground fallback) {
    if (!node) {
        return fallback;
    }
    ScummPanelBackground bg = fallback;
    if (node["type"]) {
        bg.type = background_type(node["type"].as<std::string>(), node["type"]);
    }
    if (node["color"]) {
        bg.color = parse_color(node["color"].as<std::string>(), node["color"]);
    }
    if (node["image"]) {
        bg.image = resolve_asset(base_dir, node["image"], "layout.panel.background.image");
    }
    if (node["scale_mode"]) {
        bg.scale_mode = scale_mode(node["scale_mode"].as<std::string>(), node["scale_mode"]);
    }
    if (bg.type == ScummPanelBackgroundType::NINE_SLICE) {
        bg.scale_mode = ScummPanelScaleMode::NINE_SLICE;
    }
    if (const YAML::Node ns = node["nine_slice"]) {
        bg.nine_slice = padding_from(ns["margins"],
                                     "layout.panel.background.nine_slice.margins",
                                     bg.nine_slice);
    }
    if (bg.type != ScummPanelBackgroundType::SOLID && bg.image.empty()) {
        panel_fail("scumm-panel.background-image-missing",
                   "image and nine_slice backgrounds require layout.panel.background.image",
                   node);
    }
    return bg;
}

ScummGridLayout
parse_grid(const YAML::Node& node, const std::string& field, ScummGridLayout fallback) {
    if (!node) {
        return fallback;
    }
    ScummGridLayout grid = fallback;
    grid.rect = rect_from(node["rect"], field + ".rect");
    grid.rows = positive_int(node["rows"], field + ".rows");
    grid.columns = positive_int(node["columns"], field + ".columns");
    grid.padding = padding_from(node["padding"], field + ".padding", grid.padding);
    grid.cell_gap = gap_from(node["cell_gap"], field + ".cell_gap", grid.cell_gap);
    return grid;
}

std::optional<Verb> verb_from_label(const std::string& value) {
    if (const auto verb = verb_from_id(value)) {
        return verb;
    }
    std::string v;
    v.reserve(value.size());
    for (const char c : value) {
        if (c == ' ' || c == '-') {
            v.push_back('_');
        } else {
            v.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
    }
    return verb_from_id(v);
}

std::vector<Verb> parse_verbs(const YAML::Node& node, std::vector<Verb> fallback) {
    if (!node) {
        return fallback;
    }
    if (!node.IsSequence()) {
        panel_fail("scumm-panel.verbs-invalid", "content.verbs must be a sequence", node);
    }
    std::vector<Verb> verbs;
    for (const YAML::Node& vn : node) {
        std::string id;
        if (vn.IsMap()) {
            if (!vn["id"]) {
                panel_fail("scumm-panel.verb-id-missing",
                           "content.verbs entries must be verb ids or mappings with an id",
                           vn);
            }
            id = vn["id"].as<std::string>();
        } else {
            id = vn.as<std::string>();
        }
        const std::optional<Verb> verb = verb_from_label(id);
        if (!verb) {
            panel_fail("scumm-panel.verb-unknown", "unknown verb '" + id + "'", vn);
        }
        verbs.push_back(*verb);
    }
    return verbs;
}

ScummTextStyle parse_text_style(const YAML::Node& node,
                                const std::string& base_dir,
                                const std::string& field,
                                ScummTextStyle fallback) {
    if (!node) {
        return fallback;
    }
    ScummTextStyle style = fallback;
    style.font = resolve_asset(base_dir, node["font"], field + ".font");
    if (node["size"]) {
        style.size = static_cast<unsigned>(positive_int(node["size"], field + ".size"));
    }
    if (node["color"]) {
        style.color = parse_color(node["color"].as<std::string>(), node["color"]);
    }
    if (node["hover_color"]) {
        style.hover_color = parse_color(node["hover_color"].as<std::string>(), node["hover_color"]);
    }
    if (node["disabled_color"]) {
        style.disabled_color =
            parse_color(node["disabled_color"].as<std::string>(), node["disabled_color"]);
    }
    if (node["align"]) {
        style.align = node["align"].as<std::string>();
    }
    if (node["outline_thickness"]) {
        style.outline_thickness = node["outline_thickness"].as<float>();
    }
    if (node["outline_color"]) {
        style.outline_color =
            parse_color(node["outline_color"].as<std::string>(), node["outline_color"]);
    }
    return style;
}

ScummSettingsButtonConfig parse_settings_button(const YAML::Node& node,
                                                const std::string& base_dir,
                                                ScummSettingsButtonConfig fallback) {
    if (!node) {
        return fallback;
    }
    if (!node.IsMap()) {
        panel_fail("scumm-panel.settings-button-invalid",
                   "scumm_panel.settings_button must be a mapping",
                   node);
    }

    ScummSettingsButtonConfig button = fallback;
    if (node["enabled"]) {
        button.enabled = node["enabled"].as<bool>();
    }
    if (node["position"]) {
        button.position = normalized_vec2_from(node["position"], "settings_button.position");
    }
    if (node["size"]) {
        button.size = vec2_from(node["size"], "settings_button.size");
    }
    if (node["anchor"]) {
        button.anchor = anchor_from(node["anchor"].as<std::string>(), node["anchor"]);
    }
    if (node["render_mode"]) {
        button.render_mode =
            button_render_mode(node["render_mode"].as<std::string>(), node["render_mode"]);
    }

    if (const YAML::Node panel = node["panel"]) {
        if (panel["label_key"]) {
            button.panel.label_key = panel["label_key"].as<std::string>();
        }
        button.panel.font = resolve_asset(base_dir, panel["font"], "settings_button.panel.font");
        if (panel["font_size"]) {
            button.panel.font_size = static_cast<unsigned>(
                positive_int(panel["font_size"], "settings_button.panel.font_size"));
        }
        if (panel["normal_color"]) {
            button.panel.normal_color =
                parse_color(panel["normal_color"].as<std::string>(), panel["normal_color"]);
        }
        if (panel["hovered_color"]) {
            button.panel.hovered_color =
                parse_color(panel["hovered_color"].as<std::string>(), panel["hovered_color"]);
        }
        if (panel["background_color"]) {
            button.panel.background_color =
                parse_color(panel["background_color"].as<std::string>(), panel["background_color"]);
        }
        if (panel["hovered_background_color"]) {
            button.panel.hovered_background_color =
                parse_color(panel["hovered_background_color"].as<std::string>(),
                            panel["hovered_background_color"]);
        }
        if (panel["outline_color"]) {
            button.panel.outline_color =
                parse_color(panel["outline_color"].as<std::string>(), panel["outline_color"]);
        }
    }

    if (const YAML::Node image = node["image"]) {
        button.image.normal =
            resolve_asset(base_dir, image["normal"], "settings_button.image.normal");
        button.image.hovered =
            resolve_asset(base_dir, image["hovered"], "settings_button.image.hovered");
    }

    return button;
}

ScummEvidenceIndicator parse_evidence_indicator(const YAML::Node& node,
                                                const std::string& base_dir,
                                                ScummEvidenceIndicator fallback) {
    if (!node) {
        return fallback;
    }
    if (!node.IsMap()) {
        panel_fail("scumm-panel.evidence-indicator-invalid",
                   "scumm_panel.evidence_indicator must be a mapping",
                   node);
    }
    ScummEvidenceIndicator ev = fallback;
    if (node["enabled"]) {
        ev.enabled = node["enabled"].as<bool>();
    }
    ev.rect = rect_from(node["rect"], "evidence_indicator.rect");
    ev.icon = resolve_asset(base_dir, node["icon"], "evidence_indicator.icon");
    if (node["label_key"]) {
        ev.label_key = node["label_key"].as<std::string>();
    }
    if (node["collected_state"]) {
        ev.collected_state = node["collected_state"].as<std::string>();
    }
    if (node["total_state"]) {
        ev.total_state = node["total_state"].as<std::string>();
    }
    ev.text = parse_text_style(node["text"], base_dir, "evidence_indicator.text", ev.text);
    return ev;
}

ScummNotebookConfig
parse_notebook(const YAML::Node& node, const std::string& base_dir, ScummNotebookConfig fallback) {
    if (!node) {
        return fallback;
    }
    if (!node.IsMap()) {
        panel_fail("scumm-panel.notebook-invalid", "scumm_panel.notebook must be a mapping", node);
    }
    ScummNotebookConfig nb = fallback;
    if (node["enabled"]) {
        nb.enabled = node["enabled"].as<bool>();
    }
    nb.rect = rect_from(node["rect"], "notebook.rect");
    if (node["scene"]) {
        nb.scene = node["scene"].as<std::string>();
    }
    if (node["tab_state"]) {
        nb.tab_state = node["tab_state"].as<std::string>();
    }
    nb.icon = resolve_asset(base_dir, node["icon"], "notebook.icon");
    nb.text = parse_text_style(node["text"], base_dir, "notebook.text", nb.text);
    if (const YAML::Node entries = node["entries"]) {
        if (!entries.IsSequence()) {
            panel_fail("scumm-panel.notebook-entries-invalid",
                       "notebook.entries must be a sequence",
                       entries);
        }
        nb.entries.clear();
        for (const YAML::Node& en : entries) {
            if (!en.IsMap() || !en["label_key"]) {
                panel_fail("scumm-panel.notebook-entry-invalid",
                           "notebook.entries items must be mappings with a label_key",
                           en);
            }
            ScummNotebookEntry entry;
            entry.label_key = en["label_key"].as<std::string>();
            if (en["tab"]) {
                entry.tab = en["tab"].as<std::string>();
            }
            nb.entries.push_back(std::move(entry));
        }
    }
    return nb;
}

void validate_config(const ScummPanelConfig& cfg, const YAML::Node& root) {
    // The grid-capacity bound only constrains the BUTTONS grid; TEXT lays verbs out
    // horizontally and is sized by the rect, not rows*columns.
    if (cfg.layout.verb_style == VerbPanelStyle::BUTTONS) {
        const int verb_capacity = cfg.layout.verb_panel.rows * cfg.layout.verb_panel.columns;
        if (static_cast<int>(cfg.content.verbs.size()) > verb_capacity) {
            panel_fail("scumm-panel.too-many-verbs",
                       "content.verbs length must be <= verb_panel.rows * verb_panel.columns",
                       root["content"] ? root["content"]["verbs"] : root);
        }
    }
    // ICONS inventory has no paging, so the arrow-mode requirements below don't apply.
    if (cfg.layout.inventory_style == InventoryStyle::TEXT &&
        cfg.layout.inventory_arrows.mode == InventoryArrowMode::DRAW) {
        if (cfg.skin.arrows_draw.previous_text.empty() || cfg.skin.arrows_draw.next_text.empty()) {
            panel_fail("scumm-panel.arrow-draw-invalid",
                       "draw arrow mode requires previous_text and next_text",
                       root);
        }
    }
    if (cfg.layout.inventory_style == InventoryStyle::TEXT &&
        cfg.layout.inventory_arrows.mode == InventoryArrowMode::BACKGROUND_VARIANTS) {
        const bool has_normal =
            cfg.skin.background_variants.find("normal") != cfg.skin.background_variants.end();
        const bool has_layout_image =
            cfg.layout.background.type != ScummPanelBackgroundType::SOLID &&
            !cfg.layout.background.image.empty();
        if (!has_normal && !has_layout_image) {
            panel_fail(
                "scumm-panel.background-variants-invalid",
                "background_variants arrow mode requires skin.panel.background_variants.normal "
                "or layout.panel.background.image",
                root);
        }
    }
    if (cfg.settings_button.enabled) {
        if (cfg.settings_button.render_mode == ScummButtonRenderMode::PANEL &&
            cfg.settings_button.panel.label_key.empty()) {
            panel_fail("scumm-panel.settings-button-label-missing",
                       "panel-rendered settings buttons require panel.label_key",
                       root["settings_button"] ? root["settings_button"] : root);
        }
        if (cfg.settings_button.render_mode == ScummButtonRenderMode::IMAGE &&
            cfg.settings_button.image.normal.empty()) {
            panel_fail("scumm-panel.settings-button-image-missing",
                       "image-rendered settings buttons require image.normal",
                       root["settings_button"] ? root["settings_button"] : root);
        }
    }
    if (cfg.notebook.enabled) {
        if (cfg.notebook.scene.empty()) {
            panel_fail("scumm-panel.notebook-scene-missing",
                       "an enabled notebook requires a scene id",
                       root["notebook"] ? root["notebook"] : root);
        }
        if (cfg.notebook.entries.empty()) {
            panel_fail("scumm-panel.notebook-entries-missing",
                       "an enabled notebook requires at least one entry",
                       root["notebook"] ? root["notebook"] : root);
        }
    }
}

} // namespace

ScummPanelConfig default_scumm_panel_config(sf::FloatRect panel_rect) {
    ScummPanelConfig cfg;
    cfg.layout.design_size = {panel_rect.left + panel_rect.width,
                              panel_rect.top + panel_rect.height};
    cfg.layout.panel_rect = panel_rect;
    cfg.layout.command_bar_rect = {0.0f, 0.0f, panel_rect.width, 32.0f};
    cfg.layout.body_rect = {10.0f, 42.0f, panel_rect.width - 20.0f, panel_rect.height - 52.0f};
    cfg.layout.verb_panel.rect = {0.0f,
                                  0.0f,
                                  panel_rect.width * 0.46f - 20.0f,
                                  cfg.layout.body_rect.height};
    cfg.layout.inventory_panel.rect = {panel_rect.width * 0.46f,
                                       0.0f,
                                       panel_rect.width * 0.54f - 10.0f,
                                       cfg.layout.body_rect.height};
    cfg.layout.inventory_arrows.previous_hitbox = {cfg.layout.inventory_panel.rect.width - 52.0f,
                                                   0.0f,
                                                   24.0f,
                                                   cfg.layout.inventory_panel.rect.height};
    cfg.layout.inventory_arrows.next_hitbox = {cfg.layout.inventory_panel.rect.width - 26.0f,
                                               0.0f,
                                               24.0f,
                                               cfg.layout.inventory_panel.rect.height};
    cfg.content.verbs.assign(kDefaultVerbGrid.begin(), kDefaultVerbGrid.end());
    cfg.skin.command_text.size = 20;
    cfg.skin.command_text.color = sf::Color(242, 224, 176);
    cfg.skin.verb_text.size = 16;
    cfg.skin.inventory_text.size = 18;
    return cfg;
}

ScummPanelConfig parse_scumm_panel_config(const std::string& yaml_text,
                                          const std::string& logical_path) {
    YAML::Node root;
    try {
        root = YAML::Load(yaml_text);
    } catch (const YAML::Exception& e) {
        panel_fail("scumm-panel.invalid-yaml", std::string("invalid YAML: ") + e.what());
    }
    const YAML::Node panel = root["scumm_panel"];
    if (!panel || !panel.IsMap()) {
        panel_fail("scumm-panel.root-missing", "root must contain a scumm_panel mapping", root);
    }

    const std::string base_dir =
        logical_path.empty() ? std::string() : pac::core::logical_dir(logical_path);

    ScummPanelConfig cfg;
    cfg.content.verbs.assign(kDefaultVerbGrid.begin(), kDefaultVerbGrid.end());
    cfg.layout.design_size = vec2_from(panel["design_size"], "scumm_panel.design_size");
    if (panel["smooth"]) {
        cfg.font_smooth = panel["smooth"].as<bool>();
    }

    const YAML::Node layout = panel["layout"];
    if (!layout || !layout.IsMap()) {
        panel_fail("scumm-panel.layout-missing", "scumm_panel.layout is required", panel);
    }
    const YAML::Node panel_layout = layout["panel"];
    if (!panel_layout || !panel_layout.IsMap()) {
        panel_fail("scumm-panel.panel-missing", "layout.panel is required", layout);
    }
    cfg.layout.panel_rect = rect_from(panel_layout["rect"], "layout.panel.rect");
    cfg.layout.panel_padding =
        padding_from(panel_layout["padding"], "layout.panel.padding", cfg.layout.panel_padding);
    cfg.layout.background =
        parse_background(panel_layout["background"], base_dir, cfg.layout.background);

    cfg.layout.command_bar_rect =
        rect_from(layout["command_bar"]["rect"], "layout.command_bar.rect");
    if (layout["command_bar"]["text_align"]) {
        cfg.skin.command_text.align = layout["command_bar"]["text_align"].as<std::string>();
    }
    cfg.layout.body_rect = rect_from(layout["body"]["rect"], "layout.body.rect");
    if (layout["body"]["gap"]) {
        cfg.layout.body_gap = layout["body"]["gap"].as<float>();
    }
    cfg.layout.verb_panel =
        parse_grid(layout["verb_panel"], "layout.verb_panel", cfg.layout.verb_panel);
    if (layout["verb_panel"] && layout["verb_panel"]["style"]) {
        cfg.layout.verb_style = verb_panel_style(layout["verb_panel"]["style"].as<std::string>(),
                                                 layout["verb_panel"]["style"]);
    }
    cfg.layout.inventory_panel =
        parse_grid(layout["inventory_panel"], "layout.inventory_panel", cfg.layout.inventory_panel);
    if (layout["inventory_panel"] && layout["inventory_panel"]["style"]) {
        cfg.layout.inventory_style =
            inventory_style_from(layout["inventory_panel"]["style"].as<std::string>(),
                                 layout["inventory_panel"]["style"]);
    }

    if (const YAML::Node arrows = layout["inventory_arrows"]) {
        if (arrows["mode"]) {
            cfg.layout.inventory_arrows.mode =
                arrow_mode(arrows["mode"].as<std::string>(), arrows["mode"]);
        }
        if (arrows["placement"]) {
            cfg.layout.inventory_arrows.placement =
                arrow_placement(arrows["placement"].as<std::string>(), arrows["placement"]);
        }
        if (cfg.layout.inventory_arrows.mode != InventoryArrowMode::NONE) {
            cfg.layout.inventory_arrows.previous_hitbox =
                rect_from(arrows["previous"]["hitbox"], "layout.inventory_arrows.previous.hitbox");
            cfg.layout.inventory_arrows.next_hitbox =
                rect_from(arrows["next"]["hitbox"], "layout.inventory_arrows.next.hitbox");
        }
    }

    if (const YAML::Node content = panel["content"]) {
        cfg.content.verbs = parse_verbs(content["verbs"], cfg.content.verbs);
        if (const YAML::Node templates = content["command_template"]) {
            if (templates["idle"]) {
                cfg.content.command_template.idle = templates["idle"].as<std::string>();
            }
            if (templates["verb_only"]) {
                cfg.content.command_template.verb_only = templates["verb_only"].as<std::string>();
            }
            if (templates["verb_object"]) {
                cfg.content.command_template.verb_object =
                    templates["verb_object"].as<std::string>();
            }
            if (templates["verb_object_target"]) {
                cfg.content.command_template.verb_object_target =
                    templates["verb_object_target"].as<std::string>();
            }
        }
    }

    if (const YAML::Node skin = panel["skin"]) {
        // Guard the intermediate node: yaml-cpp throws InvalidNode if we index
        // skin["panel"]["background_variants"] when skin has no "panel" child.
        if (const YAML::Node skin_panel = skin["panel"]) {
            if (const YAML::Node variants = skin_panel["background_variants"]) {
                for (const auto& kv : variants) {
                    const std::string key = kv.first.as<std::string>();
                    cfg.skin.background_variants[key] =
                        resolve_asset(base_dir, kv.second, "skin.panel.background_variants." + key);
                }
            }
        }
        cfg.skin.command_text = parse_text_style(skin["command_text"],
                                                 base_dir,
                                                 "skin.command_text",
                                                 cfg.skin.command_text);
        cfg.skin.verb_text =
            parse_text_style(skin["verb_text"], base_dir, "skin.verb_text", cfg.skin.verb_text);
        cfg.skin.inventory_text = parse_text_style(skin["inventory_text"],
                                                   base_dir,
                                                   "skin.inventory_text",
                                                   cfg.skin.inventory_text);
        const YAML::Node skin_arrows = skin["arrows"];
        if (const YAML::Node arrows = skin_arrows ? skin_arrows["draw"] : YAML::Node()) {
            cfg.skin.arrows_draw.font =
                resolve_asset(base_dir, arrows["font"], "skin.arrows.draw.font");
            if (arrows["previous_text"]) {
                cfg.skin.arrows_draw.previous_text = arrows["previous_text"].as<std::string>();
            }
            if (arrows["next_text"]) {
                cfg.skin.arrows_draw.next_text = arrows["next_text"].as<std::string>();
            }
            if (arrows["size"]) {
                cfg.skin.arrows_draw.size =
                    static_cast<unsigned>(positive_int(arrows["size"], "skin.arrows.draw.size"));
            }
            if (arrows["color"]) {
                cfg.skin.arrows_draw.color =
                    parse_color(arrows["color"].as<std::string>(), arrows["color"]);
            }
            if (arrows["hover_color"]) {
                cfg.skin.arrows_draw.hover_color =
                    parse_color(arrows["hover_color"].as<std::string>(), arrows["hover_color"]);
            }
            if (arrows["disabled_color"]) {
                cfg.skin.arrows_draw.disabled_color =
                    parse_color(arrows["disabled_color"].as<std::string>(),
                                arrows["disabled_color"]);
            }
        }
    }

    cfg.settings_button =
        parse_settings_button(panel["settings_button"], base_dir, cfg.settings_button);
    cfg.evidence_indicator =
        parse_evidence_indicator(panel["evidence_indicator"], base_dir, cfg.evidence_indicator);
    cfg.notebook = parse_notebook(panel["notebook"], base_dir, cfg.notebook);

    validate_config(cfg, panel);
    return cfg;
}

} // namespace pac::pnc
