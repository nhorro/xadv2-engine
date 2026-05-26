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

// Verb grid: 3 columns x 3 rows. This is command-model structure, not theme.
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

} // namespace

ScummPanel::ScummPanel(sf::FloatRect region, const sf::Font* font, ScummPanelTheme theme)
    : region_(region), font_(font), theme_(theme) {}

bool ScummPanel::contains(sf::Vector2f p) const {
    return region_.contains(p);
}

std::vector<ScummPanel::VerbCell> ScummPanel::verb_cells() const {
    std::vector<VerbCell> cells;
    const float pad = theme_.pad;
    const float bar = theme_.command_bar_height;
    const float grid_x = region_.left + pad;
    const float grid_y = region_.top + bar + pad;
    const float grid_w = region_.width * theme_.inventory_split - 2.0f * pad;
    const float grid_h = region_.height - bar - 2.0f * pad;
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
    const float pad = theme_.pad;
    const float bar = theme_.command_bar_height;
    const float x = region_.left + region_.width * theme_.inventory_split + pad;
    const float y = region_.top + bar + pad;
    return {x, y, region_.left + region_.width - pad - x, region_.height - bar - 2.0f * pad};
}

PanelIntent ScummPanel::click(sf::Vector2f p, const InventoryModel& inventory) const {
    for (const VerbCell& cell : verb_cells()) {
        if (cell.rect.contains(p)) {
            return {PanelIntent::Kind::SELECT_VERB, cell.verb, {}};
        }
    }
    const sf::FloatRect inv = inventory_area();
    if (inv.contains(p)) {
        const float row_h = theme_.inventory_row_height;
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
                      const CommandState& command_state,
                      sf::Vector2f cursor) const {
    draw_backdrop(target);

    if (!font_) {
        return;
    }

    // Command bar text, vertically centered in the bar strip.
    sf::Text bar(pac::core::utf8(command_state.preview_text), *font_, theme_.command_text_size);
    bar.setFillColor(theme_.command_text);
    const sf::FloatRect bb = bar.getLocalBounds();
    bar.setPosition(region_.left + theme_.pad,
                    region_.top + (theme_.command_bar_height - bb.height) / 2.0f - bb.top);
    target.draw(bar);

    // Verb grid. Selected wins over hover so the active verb stays distinct.
    for (const VerbCell& cell : verb_cells()) {
        const bool selected =
            command_state.selected_verb && *command_state.selected_verb == cell.verb;
        const bool hot = cell.rect.contains(cursor);
        sf::RectangleShape box(sf::Vector2f(cell.rect.width, cell.rect.height));
        box.setPosition(cell.rect.left, cell.rect.top);
        box.setFillColor(selected ? theme_.verb_selected
                                  : (hot ? theme_.verb_hover : theme_.verb_default));
        box.setOutlineThickness(1.0f);
        box.setOutlineColor(theme_.verb_outline);
        target.draw(box);

        sf::Text label(pac::core::utf8(strings.verb_label(std::string(verb_id(cell.verb)))),
                       *font_,
                       theme_.verb_text_size);
        label.setFillColor(selected ? theme_.verb_text_active
                                    : (hot ? theme_.verb_text_hover : theme_.verb_text));
        const sf::FloatRect b = label.getLocalBounds();
        label.setPosition(cell.rect.left + (cell.rect.width - b.width) / 2.0f - b.left,
                          cell.rect.top + (cell.rect.height - b.height) / 2.0f - b.top);
        target.draw(label);
    }

    // Inventory list (text rows; the hovered row gets a highlight + brighter text).
    // Hovered row is computed the same way click() maps a click, so they agree.
    const sf::FloatRect inv = inventory_area();
    const float row_h = theme_.inventory_row_height;
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
            hl.setFillColor(theme_.inventory_hover_bg);
            target.draw(hl);
        }
        const InventoryItem* item = inventory.item(id);
        sf::Text text(pac::core::utf8(item ? item->name : id), *font_, theme_.inventory_text_size);
        text.setFillColor(hot ? theme_.verb_text_hover : theme_.inventory_text);
        text.setPosition(inv.left + 2.0f, y);
        target.draw(text);
        y += row_h;
        ++row;
    }
}

void ScummPanel::draw_backdrop(sf::RenderTarget& target) const {
    sf::RectangleShape bg(sf::Vector2f(region_.width, region_.height));
    bg.setPosition(region_.left, region_.top);
    bg.setFillColor(theme_.panel_bg);
    target.draw(bg);

    // Command-bar strip + a thin accent separator beneath it, so the command
    // preview reads as its own band above the verb/inventory area.
    sf::RectangleShape strip(sf::Vector2f(region_.width, theme_.command_bar_height));
    strip.setPosition(region_.left, region_.top);
    strip.setFillColor(theme_.command_bar_bg);
    target.draw(strip);

    sf::RectangleShape rule(sf::Vector2f(region_.width, 2.0f));
    rule.setPosition(region_.left, region_.top + theme_.command_bar_height - 1.0f);
    rule.setFillColor(theme_.separator);
    target.draw(rule);
}

sf::FloatRect ScummPanel::options_area() const {
    const float pad = theme_.pad;
    const float bar = theme_.command_bar_height;
    return {region_.left + pad,
            region_.top + bar + pad,
            region_.width - 2.0f * pad,
            region_.height - bar - 2.0f * pad};
}

float ScummPanel::option_row_height(std::size_t option_count) const {
    if (option_count == 0) {
        return theme_.option_row_max;
    }
    const float available = options_area().height;
    const float h = available / static_cast<float>(option_count);
    if (h < theme_.option_row_min) {
        return theme_.option_row_min;
    }
    if (h > theme_.option_row_max) {
        return theme_.option_row_max;
    }
    return h;
}

void ScummPanel::draw_options(sf::RenderTarget& target,
                              const std::vector<std::string>& options,
                              sf::Vector2f cursor) const {
    draw_backdrop(target);

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
        box.setFillColor(hot ? theme_.verb_hover : theme_.verb_default);
        box.setOutlineThickness(1.0f);
        box.setOutlineColor(theme_.verb_outline);
        target.draw(box);

        sf::Text label(pac::core::utf8(options[i]), *font_, theme_.option_text_size);
        label.setFillColor(hot ? theme_.verb_text_hover : theme_.verb_text);
        const sf::FloatRect b = label.getLocalBounds();
        label.setPosition(area.left + theme_.pad, y + (row - b.height) / 2.0f - b.top - 1.0f);
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
