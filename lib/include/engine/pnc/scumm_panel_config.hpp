#pragma once

#include "engine/pnc/command.hpp"

#include <SFML/Graphics/Color.hpp>
#include <SFML/Graphics/Rect.hpp>
#include <SFML/System/Vector2.hpp>

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace pac::pnc {

struct ScummPanelPadding {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
};

enum class ScummPanelBackgroundType { SOLID, IMAGE, NINE_SLICE };
enum class ScummPanelScaleMode { STRETCH, FIT, TILE, NINE_SLICE };
enum class InventoryArrowMode { DRAW, BACKGROUND_VARIANTS, NONE };
enum class InventoryArrowPlacement { RIGHT, LEFT, BOTH };

struct ScummPanelBackground {
    ScummPanelBackgroundType type = ScummPanelBackgroundType::SOLID;
    sf::Color color{26, 24, 31};
    std::string image;
    ScummPanelScaleMode scale_mode = ScummPanelScaleMode::STRETCH;
    ScummPanelPadding nine_slice{12.0f, 12.0f, 12.0f, 12.0f};
};

struct ScummGridLayout {
    sf::FloatRect rect;
    int rows = 1;
    int columns = 1;
    ScummPanelPadding padding;
    sf::Vector2f cell_gap{0.0f, 0.0f};
};

struct InventoryArrowLayout {
    InventoryArrowMode mode = InventoryArrowMode::DRAW;
    InventoryArrowPlacement placement = InventoryArrowPlacement::RIGHT;
    sf::FloatRect previous_hitbox;
    sf::FloatRect next_hitbox;
};

struct ScummPanelLayout {
    sf::Vector2f design_size{1280.0f, 720.0f};
    sf::FloatRect panel_rect{0.0f, 612.0f, 1280.0f, 108.0f};
    ScummPanelPadding panel_padding{10.0f, 10.0f, 10.0f, 10.0f};
    ScummPanelBackground background;
    sf::FloatRect command_bar_rect{0.0f, 0.0f, 1280.0f, 32.0f};
    sf::FloatRect body_rect{10.0f, 42.0f, 1260.0f, 56.0f};
    float body_gap = 10.0f;
    ScummGridLayout verb_panel{{0.0f, 0.0f, 569.0f, 56.0f},
                               3,
                               3,
                               {0.0f, 0.0f, 0.0f, 0.0f},
                               {0.0f, 0.0f}};
    ScummGridLayout inventory_panel{{589.0f, 0.0f, 671.0f, 56.0f},
                                    2,
                                    4,
                                    {0.0f, 0.0f, 52.0f, 0.0f},
                                    {8.0f, 0.0f}};
    InventoryArrowLayout inventory_arrows{
        InventoryArrowMode::DRAW,
        InventoryArrowPlacement::RIGHT,
        {619.0f, 0.0f, 24.0f, 56.0f},
        {645.0f, 0.0f, 24.0f, 56.0f},
    };
};

struct ScummCommandTemplates {
    std::string idle;
    std::string verb_only = "{verb}";
    std::string verb_object = "{verb} {object}";
    std::string verb_object_target = "{verb} {object} {connector} {target}";
};

struct ScummPanelContent {
    std::vector<Verb> verbs;
    ScummCommandTemplates command_template;
};

struct ScummTextStyle {
    std::string font;
    unsigned size = 18;
    sf::Color color{210, 213, 226};
    sf::Color hover_color{248, 250, 255};
    sf::Color disabled_color{136, 136, 136};
    std::string align = "center";
};

struct ScummArrowDrawSkin {
    std::string previous_text = "<";
    std::string next_text = ">";
    std::string font;
    unsigned size = 22;
    sf::Color color{210, 213, 226};
    sf::Color hover_color{248, 250, 255};
    sf::Color disabled_color{136, 136, 136};
};

struct ScummPanelSkin {
    std::map<std::string, std::string> background_variants;
    ScummTextStyle command_text;
    ScummTextStyle verb_text;
    ScummTextStyle inventory_text;
    ScummArrowDrawSkin arrows_draw;
};

struct ScummPanelConfig {
    ScummPanelLayout layout;
    ScummPanelContent content;
    ScummPanelSkin skin;
};

[[nodiscard]] ScummPanelConfig default_scumm_panel_config(sf::FloatRect panel_rect);

/// Parse and validate `scumm_panel:` YAML. Relative asset paths are resolved
/// against `logical_path` when it is non-empty.
[[nodiscard]] ScummPanelConfig parse_scumm_panel_config(const std::string& yaml_text,
                                                        const std::string& logical_path = {});

} // namespace pac::pnc
