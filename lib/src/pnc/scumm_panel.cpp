#include "engine/pnc/scumm_panel.hpp"

#include "engine/core/strings.hpp"
#include "engine/pnc/inventory.hpp"

#include <SFML/Graphics/Font.hpp>
#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/Graphics/Text.hpp>

#include <array>
#include <cstddef>

namespace pac::pnc {

namespace {

// Verb grid: 3 columns x 3 rows.
constexpr int kCols = 3;
constexpr int kRows = 3;
constexpr std::array<Verb, 9> kVerbGrid{Verb::LOOK_AT,
                                        Verb::OPEN,
                                        Verb::PUSH,
                                        Verb::TALK_TO,
                                        Verb::CLOSE,
                                        Verb::PULL,
                                        Verb::PICK_UP,
                                        Verb::USE,
                                        Verb::GIVE};
constexpr float kCommandBarHeight = 30.0f;
constexpr float kPad = 8.0f;
constexpr float kInventorySplit = 0.46f; // verbs left, inventory right

} // namespace

ScummPanel::ScummPanel(sf::FloatRect region, const sf::Font* font) : region_(region), font_(font) {}

bool ScummPanel::contains(sf::Vector2f p) const {
    return region_.contains(p);
}

std::vector<ScummPanel::VerbCell> ScummPanel::verb_cells() const {
    std::vector<VerbCell> cells;
    const float grid_x = region_.left + kPad;
    const float grid_y = region_.top + kCommandBarHeight + kPad;
    const float grid_w = region_.width * kInventorySplit - 2.0f * kPad;
    const float grid_h = region_.height - kCommandBarHeight - 2.0f * kPad;
    const float cell_w = grid_w / kCols;
    const float cell_h = grid_h / kRows;
    for (std::size_t i = 0; i < kVerbGrid.size(); ++i) {
        const int col = static_cast<int>(i) % kCols;
        const int row = static_cast<int>(i) / kCols;
        cells.push_back(
            {kVerbGrid[i],
             {grid_x + col * cell_w, grid_y + row * cell_h, cell_w - 2.0f, cell_h - 2.0f}});
    }
    return cells;
}

sf::FloatRect ScummPanel::inventory_area() const {
    const float x = region_.left + region_.width * kInventorySplit + kPad;
    const float y = region_.top + kCommandBarHeight + kPad;
    return {x,
            y,
            region_.left + region_.width - kPad - x,
            region_.height - kCommandBarHeight - 2.0f * kPad};
}

PanelIntent ScummPanel::click(sf::Vector2f p, const InventoryModel& inventory) const {
    for (const VerbCell& cell : verb_cells()) {
        if (cell.rect.contains(p)) {
            return {PanelIntent::Kind::SELECT_VERB, cell.verb, {}};
        }
    }
    const sf::FloatRect inv = inventory_area();
    if (inv.contains(p)) {
        const float row_h = 26.0f;
        const auto index = static_cast<std::size_t>((p.y - inv.top) / row_h);
        if (index < inventory.list().size()) {
            return {PanelIntent::Kind::CLICK_INVENTORY, Verb::LOOK_AT, inventory.list()[index]};
        }
    }
    return {};
}

void ScummPanel::draw(sf::RenderTarget& target,
                      const pac::core::Strings& strings,
                      const InventoryModel& inventory,
                      const std::string& command_preview,
                      std::optional<Verb> selected_verb) const {
    sf::RectangleShape bg(sf::Vector2f(region_.width, region_.height));
    bg.setPosition(region_.left, region_.top);
    bg.setFillColor(sf::Color(18, 20, 30));
    target.draw(bg);

    if (!font_) {
        return;
    }

    // Command bar.
    sf::Text bar(command_preview, *font_, 20);
    bar.setFillColor(sf::Color(255, 240, 180));
    bar.setPosition(region_.left + kPad, region_.top + 4.0f);
    target.draw(bar);

    // Verb grid.
    for (const VerbCell& cell : verb_cells()) {
        const bool selected = selected_verb && *selected_verb == cell.verb;
        sf::RectangleShape box(sf::Vector2f(cell.rect.width, cell.rect.height));
        box.setPosition(cell.rect.left, cell.rect.top);
        box.setFillColor(selected ? sf::Color(70, 90, 140) : sf::Color(34, 38, 54));
        box.setOutlineThickness(1.0f);
        box.setOutlineColor(sf::Color(70, 78, 104));
        target.draw(box);

        sf::Text label(strings.verb_label(std::string(verb_id(cell.verb))), *font_, 16);
        label.setFillColor(sf::Color(220, 224, 235));
        const sf::FloatRect b = label.getLocalBounds();
        label.setPosition(cell.rect.left + (cell.rect.width - b.width) / 2.0f - b.left,
                          cell.rect.top + (cell.rect.height - b.height) / 2.0f - b.top);
        target.draw(label);
    }

    // Inventory list (text).
    const sf::FloatRect inv = inventory_area();
    const float row_h = 26.0f;
    float y = inv.top;
    for (const std::string& id : inventory.list()) {
        const InventoryItem* item = inventory.item(id);
        sf::Text text(item ? item->name : id, *font_, 18);
        text.setFillColor(sf::Color(210, 215, 230));
        text.setPosition(inv.left, y);
        target.draw(text);
        y += row_h;
    }
}

} // namespace pac::pnc
