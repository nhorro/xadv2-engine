#pragma once

#include "engine/pnc/command.hpp"

#include <SFML/Graphics/Color.hpp>
#include <SFML/Graphics/Rect.hpp>
#include <SFML/System/Vector2.hpp>

#include <optional>
#include <string>
#include <vector>

namespace sf {
class Font;
class RenderTarget;
} // namespace sf

namespace pac::core {
class Strings;
}

namespace pac::pnc {

class InventoryModel;

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
    float option_row_min = 24.0f;
    float option_row_max = 42.0f;

    // Type sizes (px).
    unsigned command_text_size = 20;
    unsigned verb_text_size = 16;
    unsigned inventory_text_size = 18;
    unsigned option_text_size = 18;
};

/// What a click on the panel means (the panel itself is not the command system).
struct PanelIntent {
    enum class Kind { NONE, SELECT_VERB, CLICK_INVENTORY };
    Kind kind = Kind::NONE;
    Verb verb{};
    std::string item_id;
};

/// The bottom SCUMM panel: a command bar, a verb grid, and a text inventory list.
/// It translates clicks into intents; the room view runs them through the command
/// builder/dispatcher. Lives in virtual coordinates.
class ScummPanel {
public:
    ScummPanel(sf::FloatRect region, const sf::Font* font, ScummPanelTheme theme = {});

    [[nodiscard]] bool contains(sf::Vector2f virtual_point) const;
    [[nodiscard]] PanelIntent click(sf::Vector2f virtual_point,
                                    const InventoryModel& inventory) const;

    /// `cursor` is the pointer position (panel/virtual coords) used to highlight
    /// the hovered verb cell and inventory row; pass an off-panel point for none.
    void draw(sf::RenderTarget& target,
              const pac::core::Strings& strings,
              const InventoryModel& inventory,
              const std::string& command_preview,
              std::optional<Verb> selected_verb,
              sf::Vector2f cursor) const;

    /// Draw dialog options in place of the verb/inventory layout. Used while
    /// the room view is in ViewState::DIALOG. `cursor` highlights the hovered row.
    void draw_options(sf::RenderTarget& target,
                      const std::vector<std::string>& options,
                      sf::Vector2f cursor) const;

    /// Map a click in the panel to a dialog option index, or -1 on miss.
    [[nodiscard]] int click_option(sf::Vector2f virtual_point, std::size_t option_count) const;

private:
    struct VerbCell {
        Verb verb;
        sf::FloatRect rect;
    };
    [[nodiscard]] std::vector<VerbCell> verb_cells() const;
    /// Panel fill + command-bar strip + separator rule (shared by both draw paths).
    void draw_backdrop(sf::RenderTarget& target) const;
    [[nodiscard]] sf::FloatRect inventory_area() const;
    [[nodiscard]] sf::FloatRect options_area() const;
    [[nodiscard]] float option_row_height(std::size_t option_count) const;

    sf::FloatRect region_;
    const sf::Font* font_;
    ScummPanelTheme theme_;
};

} // namespace pac::pnc
