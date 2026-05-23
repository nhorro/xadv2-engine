#include "engine/pnc/scumm_panel.hpp"

#include "engine/core/strings.hpp"
#include "engine/core/text_encoding.hpp"
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

// Dialog-options layout: rows fill the panel below the command bar, clamped
// so they stay readable even when only one option is shown.
constexpr float kOptionRowMin = 22.0f;
constexpr float kOptionRowMax = 40.0f;

// Shared button palette. Hover matches the TitleScreen menu so every clickable
// element highlights the same way; the active verb is brighter than hover so a
// selection still reads distinctly from a passing cursor.
const sf::Color kBtnDefault(34, 38, 54);
const sf::Color kBtnHover(70, 90, 140);
const sf::Color kBtnSelected(92, 120, 182);
const sf::Color kBtnOutline(70, 78, 104);
const sf::Color kTextDefault(220, 224, 235);
const sf::Color kTextInv(210, 215, 230);

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
                      std::optional<Verb> selected_verb,
                      sf::Vector2f cursor) const {
    sf::RectangleShape bg(sf::Vector2f(region_.width, region_.height));
    bg.setPosition(region_.left, region_.top);
    bg.setFillColor(sf::Color(18, 20, 30));
    target.draw(bg);

    if (!font_) {
        return;
    }

    // Command bar.
    sf::Text bar(pac::core::utf8(command_preview), *font_, 20);
    bar.setFillColor(sf::Color(255, 240, 180));
    bar.setPosition(region_.left + kPad, region_.top + 4.0f);
    target.draw(bar);

    // Verb grid. Selected wins over hover so the active verb stays distinct.
    for (const VerbCell& cell : verb_cells()) {
        const bool selected = selected_verb && *selected_verb == cell.verb;
        const bool hot = cell.rect.contains(cursor);
        sf::RectangleShape box(sf::Vector2f(cell.rect.width, cell.rect.height));
        box.setPosition(cell.rect.left, cell.rect.top);
        box.setFillColor(selected ? kBtnSelected : (hot ? kBtnHover : kBtnDefault));
        box.setOutlineThickness(1.0f);
        box.setOutlineColor(kBtnOutline);
        target.draw(box);

        sf::Text label(pac::core::utf8(strings.verb_label(std::string(verb_id(cell.verb)))),
                       *font_,
                       16);
        label.setFillColor((selected || hot) ? sf::Color::White : kTextDefault);
        const sf::FloatRect b = label.getLocalBounds();
        label.setPosition(cell.rect.left + (cell.rect.width - b.width) / 2.0f - b.left,
                          cell.rect.top + (cell.rect.height - b.height) / 2.0f - b.top);
        target.draw(label);
    }

    // Inventory list (text rows; the hovered row gets a highlight + white text).
    // Hovered row is computed the same way click() maps a click, so they agree.
    const sf::FloatRect inv = inventory_area();
    const float row_h = 26.0f;
    int hot_row = -1;
    if (inv.contains(cursor)) {
        const auto idx = static_cast<std::size_t>((cursor.y - inv.top) / row_h);
        if (idx < inventory.list().size()) {
            hot_row = static_cast<int>(idx);
        }
    }
    float y = inv.top;
    int row = 0;
    for (const std::string& id : inventory.list()) {
        const bool hot = (row == hot_row);
        if (hot) {
            sf::RectangleShape hl(sf::Vector2f(inv.width, row_h - 2.0f));
            hl.setPosition(inv.left, y);
            hl.setFillColor(kBtnHover);
            target.draw(hl);
        }
        const InventoryItem* item = inventory.item(id);
        sf::Text text(pac::core::utf8(item ? item->name : id), *font_, 18);
        text.setFillColor(hot ? sf::Color::White : kTextInv);
        text.setPosition(inv.left, y);
        target.draw(text);
        y += row_h;
        ++row;
    }
}

sf::FloatRect ScummPanel::options_area() const {
    return {region_.left + kPad,
            region_.top + kCommandBarHeight + kPad,
            region_.width - 2.0f * kPad,
            region_.height - kCommandBarHeight - 2.0f * kPad};
}

float ScummPanel::option_row_height(std::size_t option_count) const {
    if (option_count == 0) {
        return kOptionRowMax;
    }
    const float available = options_area().height;
    const float h = available / static_cast<float>(option_count);
    if (h < kOptionRowMin) {
        return kOptionRowMin;
    }
    if (h > kOptionRowMax) {
        return kOptionRowMax;
    }
    return h;
}

void ScummPanel::draw_options(sf::RenderTarget& target,
                              const std::vector<std::string>& options,
                              sf::Vector2f cursor) const {
    sf::RectangleShape bg(sf::Vector2f(region_.width, region_.height));
    bg.setPosition(region_.left, region_.top);
    bg.setFillColor(sf::Color(18, 20, 30));
    target.draw(bg);

    if (!font_) {
        return;
    }

    const sf::FloatRect area = options_area();
    const float row = option_row_height(options.size());
    // Hovered row via the same mapping click_option uses, so they agree.
    const int hot_idx = click_option(cursor, options.size());
    for (std::size_t i = 0; i < options.size(); ++i) {
        const float y = area.top + static_cast<float>(i) * row;
        const bool hot = (static_cast<int>(i) == hot_idx);
        sf::RectangleShape box(sf::Vector2f(area.width, row - 2.0f));
        box.setPosition(area.left, y);
        box.setFillColor(hot ? kBtnHover : kBtnDefault);
        box.setOutlineThickness(1.0f);
        box.setOutlineColor(kBtnOutline);
        target.draw(box);

        sf::Text label(pac::core::utf8(options[i]), *font_, 18);
        label.setFillColor(hot ? sf::Color::White : kTextDefault);
        const sf::FloatRect b = label.getLocalBounds();
        label.setPosition(area.left + kPad, y + (row - b.height) / 2.0f - b.top - 1.0f);
        target.draw(label);
    }
}

int ScummPanel::click_option(sf::Vector2f p, std::size_t option_count) const {
    if (option_count == 0) {
        return -1;
    }
    const sf::FloatRect area = options_area();
    if (!area.contains(p)) {
        return -1;
    }
    const float row = option_row_height(option_count);
    const auto idx = static_cast<std::size_t>((p.y - area.top) / row);
    if (idx >= option_count) {
        return -1;
    }
    return static_cast<int>(idx);
}

} // namespace pac::pnc
