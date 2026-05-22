#pragma once

#include "engine/pnc/command.hpp"

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
    ScummPanel(sf::FloatRect region, const sf::Font* font);

    [[nodiscard]] bool contains(sf::Vector2f virtual_point) const;
    [[nodiscard]] PanelIntent click(sf::Vector2f virtual_point,
                                    const InventoryModel& inventory) const;

    void draw(sf::RenderTarget& target,
              const pac::core::Strings& strings,
              const InventoryModel& inventory,
              const std::string& command_preview,
              std::optional<Verb> selected_verb) const;

    /// Draw dialog options in place of the verb/inventory layout. Used while
    /// the room view is in ViewState::DIALOG.
    void draw_options(sf::RenderTarget& target, const std::vector<std::string>& options) const;

    /// Map a click in the panel to a dialog option index, or -1 on miss.
    [[nodiscard]] int click_option(sf::Vector2f virtual_point, std::size_t option_count) const;

private:
    struct VerbCell {
        Verb verb;
        sf::FloatRect rect;
    };
    [[nodiscard]] std::vector<VerbCell> verb_cells() const;
    [[nodiscard]] sf::FloatRect inventory_area() const;
    [[nodiscard]] sf::FloatRect options_area() const;
    [[nodiscard]] float option_row_height(std::size_t option_count) const;

    sf::FloatRect region_;
    const sf::Font* font_;
};

} // namespace pac::pnc
