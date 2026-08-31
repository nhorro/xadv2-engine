#pragma once

#include "engine/pnc/command.hpp"
#include "engine/pnc/command_state.hpp"
#include "engine/pnc/scumm_panel_config.hpp"

#include <SFML/Graphics/Color.hpp>
#include <SFML/Graphics/Rect.hpp>
#include <SFML/System/Vector2.hpp>

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace sf {
class Font;
class RenderTarget;
class Text;
} // namespace sf

namespace pac::core {
class ResourceCache;
class Strings;
} // namespace pac::core

namespace pac::pnc {

class InventoryModel;
struct InventoryItem;
struct InventoryIconSheet;

/// Look-and-feel of the SCUMM panel: colors, spacing, and type sizes (issue #77).
/// Purely presentational — the command model is independent of it. The defaults
/// are a warm, modern adventure-panel palette; a game may override them (e.g. from
/// a future manifest `panel_theme` block) by passing a customized struct.
struct ScummPanelTheme {
    // Palette.
    sf::Color panel_bg{26, 24, 31};        // main panel fill
    sf::Color command_bar_bg{34, 31, 40};  // strip behind the command preview
    sf::Color separator{122, 96, 56};      // thin accent line under the command bar
    sf::Color command_text{242, 224, 176}; // command preview (warm parchment)
    sf::Color verb_default{44, 43, 56};
    sf::Color verb_hover{74, 92, 138};        // cool highlight for a hovered cell
    sf::Color verb_selected{214, 170, 92};    // warm amber accent for the active verb
    sf::Color verb_outline{63, 66, 86};       // subtle cell border
    sf::Color verb_text{210, 213, 226};       // resting verb label
    sf::Color verb_text_active{30, 27, 20};   // label over the amber selected cell
    sf::Color verb_text_hover{248, 250, 255}; // label over a hovered cell
    sf::Color inventory_text{202, 208, 224};
    sf::Color inventory_hover_bg{74, 92, 138};

    // Spacing (virtual px).
    float command_bar_height = 32.0f;
    float pad = 10.0f;
    float inventory_split = 0.46f; // fraction of width for the verb grid (left)
    float inventory_row_height = 26.0f;
    // Type sizes (px).
    unsigned command_text_size = 20;
    unsigned verb_text_size = 16;
    unsigned inventory_text_size = 18;
};

/// What a click on the panel means (the panel itself is not the command system).
struct PanelIntent {
    enum class Kind {
        NONE,
        SELECT_VERB,
        CLICK_INVENTORY,
        CHANGE_INVENTORY_PAGE,
        OPEN_SETTINGS,
        OPEN_NOTEBOOK,
        OPEN_MENU,  ///< a systemic button asking for the in-room pause menu
        PUSH_SCENE, ///< a systemic button asking for an arbitrary scene (see `scene`)
    };
    Kind kind = Kind::NONE;
    Verb verb{};
    std::string item_id;
    int page_index = 0;
    std::string tab;   ///< valid when kind == OPEN_NOTEBOOK: initial tab hint for the scene
    std::string scene; ///< valid when kind == PUSH_SCENE: the scene id to push
};

/// Evidence progress readout shown by the optional evidence indicator (issue #172).
/// The room view reads the counts from the panel config's state keys and passes them
/// in; the panel only renders "{label} {collected}/{total}".
struct EvidenceProgress {
    int collected = 0;
    int total = 0;
};

/// Runtime query for the small "new content" mark drawn over inventory items.
/// The owning room keeps the flag in persistent state; the panel only asks
/// whether the item currently needs the mark.
using InventoryNotificationQuery = std::function<bool(const std::string&)>;
/// Runtime localization seam for YAML-authored inventory names. Keeping it at
/// draw time means a language change does not require reparsing the inventory.
using InventoryNameQuery =
    std::function<std::string(const std::string& item_id, const std::string& source_name)>;

/// The bottom SCUMM panel: a command bar, a verb grid, and a text inventory list.
/// It translates clicks into intents; the room view runs them through the command
/// builder/dispatcher. Lives in virtual coordinates.
class ScummPanel {
public:
    ScummPanel(sf::FloatRect region, const sf::Font* font, ScummPanelTheme theme = {});
    ScummPanel(ScummPanelConfig config,
               sf::Vector2u runtime_size,
               const sf::Font* font,
               pac::core::ResourceCache* resources,
               ScummPanelTheme theme = {});

    [[nodiscard]] const ScummPanelConfig& config() const { return config_; }

    [[nodiscard]] sf::FloatRect bounds() const;
    [[nodiscard]] bool contains(sf::Vector2f virtual_point) const;
    [[nodiscard]] PanelIntent click(sf::Vector2f virtual_point,
                                    const InventoryModel& inventory,
                                    const CommandState& command_state) const;

    /// `cursor` is the pointer position (panel/virtual coords) used to highlight
    /// the hovered verb cell and inventory row; pass an off-panel point for none.
    void draw(sf::RenderTarget& target,
              const pac::core::Strings& strings,
              const InventoryModel& inventory,
              const CommandState& command_state,
              sf::Vector2f cursor,
              EvidenceProgress evidence = {},
              InventoryNotificationQuery has_notification = {},
              InventoryNameQuery localized_name = {}) const;

private:
    struct VerbCell {
        Verb verb;
        sf::FloatRect rect;
    };
    struct InventoryCell {
        std::string item_id;
        sf::FloatRect rect;
    };
    struct NotebookCell {
        std::string label_key;
        std::string tab;
        sf::FloatRect rect;
    };
    /// Geometry of the icon-style inventory: a fixed `rows*columns` slot grid plus
    /// the vertical paging arrows. The arrows sit in a right-hand gutter carved out
    /// of the inventory rect, or — when `layout.inventory_pagination` is enabled —
    /// in their own body-relative zone, leaving the whole rect to the slots.
    struct IconInventoryLayout {
        std::vector<sf::FloatRect> slots; ///< capacity slot rects (incl. empty ones)
        sf::FloatRect prev_arrow;         ///< up arrow (previous page)
        sf::FloatRect next_arrow;         ///< down arrow (next page)
    };
    struct SystemButtonCell {
        const ScummSystemButton* button = nullptr;
        sf::FloatRect rect;
    };
    [[nodiscard]] std::vector<VerbCell> verb_cells() const;
    [[nodiscard]] std::vector<SystemButtonCell> system_button_cells() const;
    [[nodiscard]] std::vector<InventoryCell> inventory_cells(const InventoryModel& inventory,
                                                             int page_index) const;
    [[nodiscard]] IconInventoryLayout icon_inventory_layout() const;
    /// Body-relative notebook access zones stacked vertically inside
    /// `notebook.rect` (one row per entry, right of a leading icon).
    [[nodiscard]] std::vector<NotebookCell> notebook_cells() const;
    /// Panel fill + command-bar strip + separator rule.
    void draw_backdrop(sf::RenderTarget& target,
                       const InventoryModel* inventory = nullptr,
                       const CommandState* command_state = nullptr,
                       sf::Vector2f cursor = {-1.0f, -1.0f}) const;
    [[nodiscard]] bool draw_background_image(sf::RenderTarget& target,
                                             const std::string& image,
                                             ScummPanelScaleMode mode) const;
    [[nodiscard]] bool
    draw_nine_slice(sf::RenderTarget& target, const std::string& image) const;
    void draw_inventory_arrows(sf::RenderTarget& target,
                               const InventoryModel& inventory,
                               const CommandState& command_state,
                               sf::Vector2f cursor) const;
    void draw_settings_button(sf::RenderTarget& target,
                              const pac::core::Strings& strings,
                              sf::Vector2f cursor) const;
    /// Framed systemic buttons ("Opciones", "Menú", …): a skinned box with an
    /// optional leading icon and a localized label.
    void draw_system_buttons(sf::RenderTarget& target,
                             const pac::core::Strings& strings,
                             sf::Vector2f cursor) const;
    /// Draw one skinned control box. `selected` wins over `hovered`; a disabled box
    /// ignores both.
    void draw_button_box(sf::RenderTarget& target,
                         const ScummButtonSkin& skin,
                         sf::FloatRect rect,
                         bool hovered,
                         bool selected,
                         bool enabled = true) const;
    /// Icon-style inventory: fixed slot frames with the held item's icon (or a
    /// drawn placeholder when the item has none), plus vertical paging arrows
    /// (same look as the dialog arrows) shown when more than one page exists.
    void draw_inventory_icons(sf::RenderTarget& target,
                              const InventoryModel& inventory,
                              const CommandState& command_state,
                              sf::Vector2f cursor,
                              const InventoryNotificationQuery& has_notification,
                              const InventoryNameQuery& localized_name) const;
    /// A compact amber circle with an exclamation mark, anchored to a slot/row's
    /// upper-right corner. It remains legible with either icon or text inventory.
    void draw_inventory_notification(sf::RenderTarget& target, sf::FloatRect rect) const;
    void draw_evidence_indicator(sf::RenderTarget& target,
                                 const pac::core::Strings& strings,
                                 EvidenceProgress evidence) const;
    void draw_notebook(sf::RenderTarget& target,
                       const pac::core::Strings& strings,
                       sf::Vector2f cursor) const;
    /// Draw `image` scaled into `rect`. `src` selects a sub-rectangle of the
    /// texture (for sprite-sheet cells); a zero-size `src` means the whole texture.
    void draw_image_in_rect(sf::RenderTarget& target,
                            const std::string& image,
                            sf::FloatRect rect,
                            sf::IntRect src = {}) const;
    /// Resolve and draw an inventory item's icon into `dest`: a production sheet
    /// cell, or the development individual image. Returns false (draw nothing) when
    /// the item has no usable icon, so the caller can fall back to a placeholder.
    [[nodiscard]] bool draw_item_icon(sf::RenderTarget& target,
                                      const InventoryItem& item,
                                      const InventoryIconSheet& sheet,
                                      sf::FloatRect dest) const;
    [[nodiscard]] sf::FloatRect inventory_area() const;
    [[nodiscard]] sf::FloatRect command_bar_area() const;
    [[nodiscard]] sf::FloatRect arrow_previous_area() const;
    [[nodiscard]] sf::FloatRect arrow_next_area() const;
    [[nodiscard]] sf::FloatRect settings_button_area() const;
    [[nodiscard]] const sf::Font* system_button_font() const;
    [[nodiscard]] sf::FloatRect evidence_indicator_area() const;
    [[nodiscard]] sf::FloatRect notebook_area() const;
    [[nodiscard]] sf::FloatRect scale_rect(sf::FloatRect design_rect) const;
    [[nodiscard]] sf::FloatRect panel_child(sf::FloatRect rect) const;
    [[nodiscard]] sf::FloatRect body_child(sf::FloatRect rect) const;
    [[nodiscard]] sf::FloatRect inventory_child(sf::FloatRect rect) const;
    [[nodiscard]] const sf::Font* font_or_default(const sf::Font* configured) const;
    [[nodiscard]] unsigned scaled_text_size(unsigned design_size) const;
    /// Apply a text style's fill plus its (optional, panel-scaled) outline.
    void apply_text_style(sf::Text& text, const ScummTextStyle& style, sf::Color fill) const;
    [[nodiscard]] int inventory_capacity() const;
    [[nodiscard]] int inventory_page_count(const InventoryModel& inventory) const;
    [[nodiscard]] int clamped_inventory_page(const InventoryModel& inventory, int page_index) const;
    [[nodiscard]] std::string background_variant(const InventoryModel& inventory,
                                                 const CommandState& command_state,
                                                 sf::Vector2f cursor) const;

    ScummPanelConfig config_;
    sf::Vector2u runtime_size_{1280, 720};
    const sf::Font* font_;
    pac::core::ResourceCache* resources_ = nullptr;
    const sf::Font* command_font_ = nullptr;
    const sf::Font* verb_font_ = nullptr;
    const sf::Font* inventory_font_ = nullptr;
    const sf::Font* arrow_font_ = nullptr;
    const sf::Font* settings_font_ = nullptr;
    const sf::Font* evidence_font_ = nullptr;
    const sf::Font* notebook_font_ = nullptr;
    const sf::Font* system_button_font_ = nullptr;
    ScummPanelTheme theme_;
};

} // namespace pac::pnc
