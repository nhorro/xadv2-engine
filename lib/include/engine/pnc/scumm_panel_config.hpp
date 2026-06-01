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
enum class ScummPanelAnchor { TOP_LEFT, TOP_RIGHT, BOTTOM_LEFT, BOTTOM_RIGHT, CENTER };
enum class ScummButtonRenderMode { PANEL, IMAGE };
// Verb row presentation: BUTTONS = the framed grid (default, themummy); TEXT = plain
// horizontal pixel-text labels with no boxes (issue #172).
enum class VerbPanelStyle { BUTTONS, TEXT };
// Inventory presentation: TEXT = the localized-name list (default); ICONS = fixed
// rows*columns slots showing each held item's icon, no paging (issue #172).
enum class InventoryStyle { TEXT, ICONS };

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
    // Presentation styles (issue #172). Defaults preserve the classic layout so the
    // engine default config (themummy) is unchanged; the IU panel opts into TEXT/ICONS.
    VerbPanelStyle verb_style = VerbPanelStyle::BUTTONS;
    InventoryStyle inventory_style = InventoryStyle::TEXT;
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
    // Optional text outline for legibility over busy backgrounds. 0 = none
    // (default, classic look). Thickness is in design pixels and scales with the
    // panel; the outline color defaults to black.
    float outline_thickness = 0.0f;
    sf::Color outline_color{0, 0, 0};
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

struct ScummSettingsButtonPanelSkin {
    std::string label_key = "settings_button";
    std::string font;
    unsigned font_size = 14;
    sf::Color normal_color{210, 213, 226};
    sf::Color hovered_color{248, 250, 255};
    sf::Color background_color{44, 43, 56};
    sf::Color hovered_background_color{74, 92, 138};
    sf::Color outline_color{63, 66, 86};
};

struct ScummSettingsButtonImageSkin {
    std::string normal;
    std::string hovered;
};

struct ScummSettingsButtonConfig {
    bool enabled = false;
    sf::Vector2f position{0.97f, 0.15f}; // normalized panel coordinates
    sf::Vector2f size{32.0f, 32.0f};     // design pixels
    ScummPanelAnchor anchor = ScummPanelAnchor::CENTER;
    ScummButtonRenderMode render_mode = ScummButtonRenderMode::PANEL;
    ScummSettingsButtonPanelSkin panel;
    ScummSettingsButtonImageSkin image;
};

// Evidence progress readout (issue #172): "[icon] {label} x/n". The counts come from
// two engine state keys the game/notebook writes via set_state (decoupled from the
// game-specific notebook); unset keys read as 0. Rect is body-relative (like the verb
// and inventory panels). Disabled by default.
struct ScummEvidenceIndicator {
    bool enabled = false;
    sf::FloatRect rect; // relative to body_rect
    std::string icon;   // optional image; a placeholder is drawn when empty
    std::string label_key = "evidencias";
    std::string collected_state; // get_state key for the collected count
    std::string total_state;     // get_state key for the total count
    ScummTextStyle text;
};

// One clickable notebook access zone (e.g. "Cuaderno" / "Hipótesis"); both open
// `ScummNotebookConfig::scene`, passing `tab` via the `tab_state` key.
struct ScummNotebookEntry {
    std::string label_key;
    std::string tab; // initial tab hint written to tab_state before opening the scene
};

// Notebook access in the panel (issue #172). Body-relative rect; disabled by default.
struct ScummNotebookConfig {
    bool enabled = false;
    sf::FloatRect rect;    // relative to body_rect
    std::string scene;     // manifest scene id to push (e.g. "notebook")
    std::string tab_state; // set_state key written with the clicked entry's tab
    std::string icon;      // optional image; a placeholder is drawn when empty
    std::vector<ScummNotebookEntry> entries;
    ScummTextStyle text;
};

struct ScummPanelConfig {
    ScummPanelLayout layout;
    ScummPanelContent content;
    ScummPanelSkin skin;
    ScummSettingsButtonConfig settings_button;
    ScummEvidenceIndicator evidence_indicator;
    ScummNotebookConfig notebook;
    // Optional font smoothing (linear filtering) for the panel fonts. Unset =
    // leave SFML's default; set `smooth: false` for crisp pixel text. Applied to
    // the shared font objects, so it affects all users of those fonts.
    std::optional<bool> font_smooth;
};

[[nodiscard]] ScummPanelConfig default_scumm_panel_config(sf::FloatRect panel_rect);

/// Parse and validate `scumm_panel:` YAML. Relative asset paths are resolved
/// against `logical_path` when it is non-empty.
[[nodiscard]] ScummPanelConfig parse_scumm_panel_config(const std::string& yaml_text,
                                                        const std::string& logical_path = {});

} // namespace pac::pnc
