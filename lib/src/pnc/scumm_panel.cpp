#include "engine/pnc/scumm_panel.hpp"

#include "engine/core/resource_cache.hpp"
#include "engine/core/strings.hpp"
#include "engine/core/text_encoding.hpp"
#include "engine/pnc/inventory.hpp"

#include <SFML/Graphics/Font.hpp>
#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/Graphics/Sprite.hpp>
#include <SFML/Graphics/Text.hpp>
#include <SFML/Graphics/Texture.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <exception>
#include <map>
#include <utility>

namespace pac::pnc {

namespace {

sf::FloatRect padded(sf::FloatRect rect, ScummPanelPadding pad) {
    rect.left += pad.left;
    rect.top += pad.top;
    rect.width -= pad.left + pad.right;
    rect.height -= pad.top + pad.bottom;
    if (rect.width < 0.0f) {
        rect.width = 0.0f;
    }
    if (rect.height < 0.0f) {
        rect.height = 0.0f;
    }
    return rect;
}

bool has_variant(const std::map<std::string, std::string>& variants, const std::string& key) {
    return variants.find(key) != variants.end();
}

std::string first_existing(const std::map<std::string, std::string>& variants,
                           std::initializer_list<const char*> keys) {
    for (const char* key : keys) {
        const auto it = variants.find(key);
        if (it != variants.end() && !it->second.empty()) {
            return it->second;
        }
    }
    return {};
}

void place_text(sf::Text& text, sf::FloatRect rect, const std::string& align, float inset = 6.0f) {
    const sf::FloatRect bounds = text.getLocalBounds();
    float x = rect.left + (rect.width - bounds.width) / 2.0f - bounds.left;
    if (align == "left") {
        x = rect.left + inset - bounds.left;
    } else if (align == "right") {
        x = rect.left + rect.width - bounds.width - inset - bounds.left;
    }
    text.setPosition(x, rect.top + (rect.height - bounds.height) / 2.0f - bounds.top);
}

void center_text(sf::Text& text, sf::FloatRect rect) {
    place_text(text, rect, "center");
}

} // namespace

ScummPanel::ScummPanel(sf::FloatRect region, const sf::Font* font, ScummPanelTheme theme)
    : ScummPanel(default_scumm_panel_config(region),
                 {static_cast<unsigned>(std::ceil(region.left + region.width)),
                  static_cast<unsigned>(std::ceil(region.top + region.height))},
                 font,
                 nullptr,
                 theme) {}

ScummPanel::ScummPanel(ScummPanelConfig config,
                       sf::Vector2u runtime_size,
                       const sf::Font* font,
                       pac::core::ResourceCache* resources,
                       ScummPanelTheme theme)
    : config_(std::move(config)), runtime_size_(runtime_size), font_(font), resources_(resources),
      theme_(theme) {
    const auto load_font = [this](const std::string& logical) -> const sf::Font* {
        if (!logical.empty() && resources_) {
            return resources_->try_font(logical);
        }
        return nullptr;
    };
    command_font_ = load_font(config_.skin.command_text.font);
    verb_font_ = load_font(config_.skin.verb_text.font);
    inventory_font_ = load_font(config_.skin.inventory_text.font);
    arrow_font_ = load_font(config_.skin.arrows_draw.font);
    settings_font_ = load_font(config_.settings_button.panel.font);
}

bool ScummPanel::contains(sf::Vector2f p) const {
    return scale_rect(config_.layout.panel_rect).contains(p);
}

sf::FloatRect ScummPanel::scale_rect(sf::FloatRect design_rect) const {
    const float sx = static_cast<float>(runtime_size_.x) / config_.layout.design_size.x;
    const float sy = static_cast<float>(runtime_size_.y) / config_.layout.design_size.y;
    return {design_rect.left * sx,
            design_rect.top * sy,
            design_rect.width * sx,
            design_rect.height * sy};
}

const sf::Font* ScummPanel::font_or_default(const sf::Font* configured) const {
    return configured ? configured : font_;
}

unsigned ScummPanel::scaled_text_size(unsigned design_size) const {
    const float sy = static_cast<float>(runtime_size_.y) / config_.layout.design_size.y;
    return std::max(1u, static_cast<unsigned>(std::lround(static_cast<float>(design_size) * sy)));
}

sf::FloatRect ScummPanel::panel_child(sf::FloatRect rect) const {
    const sf::FloatRect panel = config_.layout.panel_rect;
    return scale_rect({panel.left + rect.left, panel.top + rect.top, rect.width, rect.height});
}

sf::FloatRect ScummPanel::body_child(sf::FloatRect rect) const {
    const sf::FloatRect panel = config_.layout.panel_rect;
    const sf::FloatRect body = config_.layout.body_rect;
    return scale_rect({panel.left + body.left + rect.left,
                       panel.top + body.top + rect.top,
                       rect.width,
                       rect.height});
}

sf::FloatRect ScummPanel::inventory_child(sf::FloatRect rect) const {
    const sf::FloatRect panel = config_.layout.panel_rect;
    const sf::FloatRect body = config_.layout.body_rect;
    const sf::FloatRect inv = config_.layout.inventory_panel.rect;
    return scale_rect({panel.left + body.left + inv.left + rect.left,
                       panel.top + body.top + inv.top + rect.top,
                       rect.width,
                       rect.height});
}

std::vector<ScummPanel::VerbCell> ScummPanel::verb_cells() const {
    std::vector<VerbCell> cells;
    const ScummGridLayout& grid = config_.layout.verb_panel;
    const sf::FloatRect area = padded(body_child(grid.rect), grid.padding);
    const float sx = static_cast<float>(runtime_size_.x) / config_.layout.design_size.x;
    const float sy = static_cast<float>(runtime_size_.y) / config_.layout.design_size.y;
    const float gap_x = grid.cell_gap.x * sx;
    const float gap_y = grid.cell_gap.y * sy;
    const float cell_w = (area.width - gap_x * static_cast<float>(std::max(0, grid.columns - 1))) /
                         static_cast<float>(grid.columns);
    const float cell_h = (area.height - gap_y * static_cast<float>(std::max(0, grid.rows - 1))) /
                         static_cast<float>(grid.rows);
    for (std::size_t i = 0; i < config_.content.verbs.size(); ++i) {
        if (i >= static_cast<std::size_t>(grid.rows * grid.columns)) {
            break;
        }
        const int col = static_cast<int>(i) % grid.columns;
        const int row = static_cast<int>(i) / grid.columns;
        cells.push_back({config_.content.verbs[i],
                         {area.left + static_cast<float>(col) * (cell_w + gap_x),
                          area.top + static_cast<float>(row) * (cell_h + gap_y),
                          cell_w - 2.0f,
                          cell_h - 2.0f}});
    }
    return cells;
}

int ScummPanel::inventory_capacity() const {
    return config_.layout.inventory_panel.rows * config_.layout.inventory_panel.columns;
}

int ScummPanel::inventory_page_count(const InventoryModel& inventory) const {
    const int capacity = inventory_capacity();
    if (capacity <= 0) {
        return 1;
    }
    const int count = static_cast<int>(inventory.list().size());
    return std::max(1, (count + capacity - 1) / capacity);
}

int ScummPanel::clamped_inventory_page(const InventoryModel& inventory, int page_index) const {
    const int pages = inventory_page_count(inventory);
    if (page_index < 0) {
        return 0;
    }
    if (page_index >= pages) {
        return pages - 1;
    }
    return page_index;
}

std::vector<ScummPanel::InventoryCell> ScummPanel::inventory_cells(const InventoryModel& inventory,
                                                                   int page_index) const {
    std::vector<InventoryCell> cells;
    const ScummGridLayout& grid = config_.layout.inventory_panel;
    const sf::FloatRect area = padded(inventory_area(), grid.padding);
    const float sx = static_cast<float>(runtime_size_.x) / config_.layout.design_size.x;
    const float sy = static_cast<float>(runtime_size_.y) / config_.layout.design_size.y;
    const float gap_x = grid.cell_gap.x * sx;
    const float gap_y = grid.cell_gap.y * sy;
    const float cell_w = (area.width - gap_x * static_cast<float>(std::max(0, grid.columns - 1))) /
                         static_cast<float>(grid.columns);
    const float cell_h = (area.height - gap_y * static_cast<float>(std::max(0, grid.rows - 1))) /
                         static_cast<float>(grid.rows);
    const int capacity = inventory_capacity();
    const int page = clamped_inventory_page(inventory, page_index);
    const int first = page * capacity;
    const int last = std::min(first + capacity, static_cast<int>(inventory.list().size()));
    for (int item_index = first; item_index < last; ++item_index) {
        const int local = item_index - first;
        const int col = local % grid.columns;
        const int row = local / grid.columns;
        cells.push_back({inventory.list()[static_cast<std::size_t>(item_index)],
                         {area.left + static_cast<float>(col) * (cell_w + gap_x),
                          area.top + static_cast<float>(row) * (cell_h + gap_y),
                          cell_w,
                          cell_h}});
    }
    return cells;
}

sf::FloatRect ScummPanel::inventory_area() const {
    return body_child(config_.layout.inventory_panel.rect);
}

sf::FloatRect ScummPanel::command_bar_area() const {
    return panel_child(config_.layout.command_bar_rect);
}

sf::FloatRect ScummPanel::arrow_previous_area() const {
    return inventory_child(config_.layout.inventory_arrows.previous_hitbox);
}

sf::FloatRect ScummPanel::arrow_next_area() const {
    return inventory_child(config_.layout.inventory_arrows.next_hitbox);
}

sf::FloatRect ScummPanel::settings_button_area() const {
    const ScummSettingsButtonConfig& button = config_.settings_button;
    if (!button.enabled) {
        return {};
    }

    const sf::FloatRect panel = config_.layout.panel_rect;
    const sf::Vector2f size = button.size;
    sf::Vector2f anchor_offset;
    switch (button.anchor) {
    case ScummPanelAnchor::TOP_LEFT:
        anchor_offset = {0.0f, 0.0f};
        break;
    case ScummPanelAnchor::TOP_RIGHT:
        anchor_offset = {size.x, 0.0f};
        break;
    case ScummPanelAnchor::BOTTOM_LEFT:
        anchor_offset = {0.0f, size.y};
        break;
    case ScummPanelAnchor::BOTTOM_RIGHT:
        anchor_offset = {size.x, size.y};
        break;
    case ScummPanelAnchor::CENTER:
        anchor_offset = {size.x / 2.0f, size.y / 2.0f};
        break;
    }

    const sf::Vector2f anchor_point{panel.left + button.position.x * panel.width,
                                    panel.top + button.position.y * panel.height};
    return scale_rect(
        {anchor_point.x - anchor_offset.x, anchor_point.y - anchor_offset.y, size.x, size.y});
}

PanelIntent ScummPanel::click(sf::Vector2f p,
                              const InventoryModel& inventory,
                              const CommandState& command_state) const {
    if (config_.settings_button.enabled && settings_button_area().contains(p)) {
        return {PanelIntent::Kind::OPEN_SETTINGS, Verb::LOOK_AT, {}, 0};
    }
    for (const VerbCell& cell : verb_cells()) {
        if (cell.rect.contains(p)) {
            return {PanelIntent::Kind::SELECT_VERB, cell.verb, {}, 0};
        }
    }
    const int page = clamped_inventory_page(inventory, command_state.inventory_page_index);
    if (config_.layout.inventory_arrows.mode != InventoryArrowMode::NONE) {
        if (arrow_previous_area().contains(p) && page > 0) {
            return {PanelIntent::Kind::CHANGE_INVENTORY_PAGE, Verb::LOOK_AT, {}, page - 1};
        }
        if (arrow_next_area().contains(p) && page < inventory_page_count(inventory) - 1) {
            return {PanelIntent::Kind::CHANGE_INVENTORY_PAGE, Verb::LOOK_AT, {}, page + 1};
        }
    }
    for (const InventoryCell& cell : inventory_cells(inventory, page)) {
        if (cell.rect.contains(p)) {
            return {PanelIntent::Kind::CLICK_INVENTORY, Verb::LOOK_AT, cell.item_id, 0};
        }
    }
    return {};
}

std::string ScummPanel::background_variant(const InventoryModel& inventory,
                                           const CommandState& command_state,
                                           sf::Vector2f cursor) const {
    const auto& variants = config_.skin.background_variants;
    if (variants.empty()) {
        return {};
    }
    const int page = clamped_inventory_page(inventory, command_state.inventory_page_index);
    const bool prev = page > 0;
    const bool next = page < inventory_page_count(inventory) - 1;
    const bool prev_hover = prev && arrow_previous_area().contains(cursor);
    const bool next_hover = next && arrow_next_area().contains(cursor);
    if (prev_hover && next && has_variant(variants, "inv_both_hover")) {
        return variants.at("inv_both_hover");
    }
    if (next_hover && prev && has_variant(variants, "inv_both_hover")) {
        return variants.at("inv_both_hover");
    }
    if (prev_hover) {
        return first_existing(variants,
                              {"inv_prev_hover", "inv_prev_visible", "inv_both_visible", "normal"});
    }
    if (next_hover) {
        return first_existing(variants,
                              {"inv_next_hover", "inv_next_visible", "inv_both_visible", "normal"});
    }
    if (prev && next) {
        return first_existing(variants, {"inv_both_visible", "normal"});
    }
    if (prev) {
        return first_existing(variants, {"inv_prev_visible", "normal"});
    }
    if (next) {
        return first_existing(variants, {"inv_next_visible", "normal"});
    }
    return first_existing(variants, {"inv_no_arrows", "normal"});
}

void ScummPanel::draw_background_image(sf::RenderTarget& target,
                                       const std::string& image,
                                       ScummPanelScaleMode mode) const {
    if (!resources_ || image.empty()) {
        return;
    }
    try {
        const sf::Texture& texture = resources_->texture(image);
        if (mode == ScummPanelScaleMode::NINE_SLICE) {
            draw_nine_slice(target, image);
            return;
        }
        const sf::FloatRect panel = scale_rect(config_.layout.panel_rect);
        const sf::Vector2u size = texture.getSize();
        sf::Sprite sprite(texture);
        if (mode == ScummPanelScaleMode::FIT) {
            const float scale = std::min(panel.width / static_cast<float>(size.x),
                                         panel.height / static_cast<float>(size.y));
            sprite.setScale(scale, scale);
            sprite.setPosition(
                panel.left + (panel.width - static_cast<float>(size.x) * scale) / 2.0f,
                panel.top + (panel.height - static_cast<float>(size.y) * scale) / 2.0f);
            target.draw(sprite);
        } else if (mode == ScummPanelScaleMode::TILE) {
            for (float y = panel.top; y < panel.top + panel.height;
                 y += static_cast<float>(size.y)) {
                for (float x = panel.left; x < panel.left + panel.width;
                     x += static_cast<float>(size.x)) {
                    sprite.setPosition(x, y);
                    target.draw(sprite);
                }
            }
        } else {
            sprite.setPosition(panel.left, panel.top);
            sprite.setScale(panel.width / static_cast<float>(size.x),
                            panel.height / static_cast<float>(size.y));
            target.draw(sprite);
        }
    } catch (const std::exception&) {
        // Fall back to the solid panel fill below.
    }
}

void ScummPanel::draw_nine_slice(sf::RenderTarget& target, const std::string& image) const {
    if (!resources_ || image.empty()) {
        return;
    }
    const sf::Texture& texture = resources_->texture(image);
    const sf::Vector2u tex_size = texture.getSize();
    const sf::FloatRect panel = scale_rect(config_.layout.panel_rect);
    const ScummPanelPadding margin = config_.layout.background.nine_slice;
    const float sx = static_cast<float>(runtime_size_.x) / config_.layout.design_size.x;
    const float sy = static_cast<float>(runtime_size_.y) / config_.layout.design_size.y;
    const std::array<float, 4> dx{panel.left,
                                  panel.left + margin.left * sx,
                                  panel.left + panel.width - margin.right * sx,
                                  panel.left + panel.width};
    const std::array<float, 4> dy{panel.top,
                                  panel.top + margin.top * sy,
                                  panel.top + panel.height - margin.bottom * sy,
                                  panel.top + panel.height};
    const std::array<int, 4> sxp{0,
                                 static_cast<int>(margin.left),
                                 static_cast<int>(tex_size.x - margin.right),
                                 static_cast<int>(tex_size.x)};
    const std::array<int, 4> syp{0,
                                 static_cast<int>(margin.top),
                                 static_cast<int>(tex_size.y - margin.bottom),
                                 static_cast<int>(tex_size.y)};
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            const int sw = sxp[col + 1] - sxp[col];
            const int sh = syp[row + 1] - syp[row];
            const float dw = dx[col + 1] - dx[col];
            const float dh = dy[row + 1] - dy[row];
            if (sw <= 0 || sh <= 0 || dw <= 0.0f || dh <= 0.0f) {
                continue;
            }
            sf::Sprite sprite(texture, sf::IntRect(sxp[col], syp[row], sw, sh));
            sprite.setPosition(dx[col], dy[row]);
            sprite.setScale(dw / static_cast<float>(sw), dh / static_cast<float>(sh));
            target.draw(sprite);
        }
    }
}

void ScummPanel::draw_backdrop(sf::RenderTarget& target,
                               const InventoryModel* inventory,
                               const CommandState* command_state,
                               sf::Vector2f cursor) const {
    const sf::FloatRect panel = scale_rect(config_.layout.panel_rect);
    sf::RectangleShape bg(sf::Vector2f(panel.width, panel.height));
    bg.setPosition(panel.left, panel.top);
    bg.setFillColor(config_.layout.background.color);
    target.draw(bg);

    std::string image = config_.layout.background.type == ScummPanelBackgroundType::SOLID
                            ? std::string()
                            : config_.layout.background.image;
    ScummPanelScaleMode mode = config_.layout.background.scale_mode;
    if (config_.layout.inventory_arrows.mode == InventoryArrowMode::BACKGROUND_VARIANTS) {
        if (inventory && command_state) {
            const std::string variant = background_variant(*inventory, *command_state, cursor);
            if (!variant.empty()) {
                image = variant;
                mode = ScummPanelScaleMode::STRETCH;
            }
        } else {
            const auto normal = config_.skin.background_variants.find("normal");
            if (normal != config_.skin.background_variants.end() && !normal->second.empty()) {
                image = normal->second;
                mode = ScummPanelScaleMode::STRETCH;
            }
        }
    }
    if (!image.empty()) {
        draw_background_image(target, image, mode);
    } else {
        // Preserve the old command strip when the panel is using engine-drawn
        // cells over a solid background.
        sf::RectangleShape strip(sf::Vector2f(command_bar_area().width, command_bar_area().height));
        strip.setPosition(command_bar_area().left, command_bar_area().top);
        strip.setFillColor(theme_.command_bar_bg);
        target.draw(strip);

        sf::RectangleShape rule(sf::Vector2f(panel.width, 2.0f));
        rule.setPosition(panel.left, command_bar_area().top + command_bar_area().height - 1.0f);
        rule.setFillColor(theme_.separator);
        target.draw(rule);
    }
}

void ScummPanel::draw_image_in_rect(sf::RenderTarget& target,
                                    const std::string& image,
                                    sf::FloatRect rect) const {
    if (!resources_ || image.empty()) {
        return;
    }
    try {
        const sf::Texture& texture = resources_->texture(image);
        const sf::Vector2u size = texture.getSize();
        sf::Sprite sprite(texture);
        sprite.setPosition(rect.left, rect.top);
        sprite.setScale(rect.width / static_cast<float>(size.x),
                        rect.height / static_cast<float>(size.y));
        target.draw(sprite);
    } catch (const std::exception&) {
        // Optional button art falls back to no image.
    }
}

void ScummPanel::draw_inventory_arrows(sf::RenderTarget& target,
                                       const InventoryModel& inventory,
                                       const CommandState& command_state,
                                       sf::Vector2f cursor) const {
    const sf::Font* arrow_font = font_or_default(arrow_font_);
    if (!arrow_font || config_.layout.inventory_arrows.mode != InventoryArrowMode::DRAW) {
        return;
    }
    const int page = clamped_inventory_page(inventory, command_state.inventory_page_index);
    const bool prev = page > 0;
    const bool next = page < inventory_page_count(inventory) - 1;
    const auto draw_arrow = [&](sf::FloatRect rect, const std::string& label, bool enabled) {
        const bool hot = enabled && rect.contains(cursor);
        sf::Text text(pac::core::utf8(label),
                      *arrow_font,
                      scaled_text_size(config_.skin.arrows_draw.size));
        text.setFillColor(
            enabled ? (hot ? config_.skin.arrows_draw.hover_color : config_.skin.arrows_draw.color)
                    : config_.skin.arrows_draw.disabled_color);
        center_text(text, rect);
        target.draw(text);
    };
    draw_arrow(arrow_previous_area(), config_.skin.arrows_draw.previous_text, prev);
    draw_arrow(arrow_next_area(), config_.skin.arrows_draw.next_text, next);
}

void ScummPanel::draw_settings_button(sf::RenderTarget& target,
                                      const pac::core::Strings& strings,
                                      sf::Vector2f cursor) const {
    if (!config_.settings_button.enabled) {
        return;
    }
    const sf::FloatRect rect = settings_button_area();
    const bool hot = rect.contains(cursor);
    const ScummSettingsButtonConfig& button = config_.settings_button;

    if (button.render_mode == ScummButtonRenderMode::IMAGE) {
        const std::string& image =
            hot && !button.image.hovered.empty() ? button.image.hovered : button.image.normal;
        draw_image_in_rect(target, image, rect);
        return;
    }

    sf::RectangleShape box(sf::Vector2f(rect.width, rect.height));
    box.setPosition(rect.left, rect.top);
    box.setFillColor(hot ? button.panel.hovered_background_color : button.panel.background_color);
    box.setOutlineThickness(1.0f);
    box.setOutlineColor(button.panel.outline_color);
    target.draw(box);

    const sf::Font* settings_font = font_or_default(settings_font_);
    if (!settings_font) {
        return;
    }
    sf::Text label(pac::core::utf8(strings.ui_label(button.panel.label_key)),
                   *settings_font,
                   scaled_text_size(button.panel.font_size));
    label.setFillColor(hot ? button.panel.hovered_color : button.panel.normal_color);
    center_text(label, rect);
    target.draw(label);
}

void ScummPanel::draw(sf::RenderTarget& target,
                      const pac::core::Strings& strings,
                      const InventoryModel& inventory,
                      const CommandState& command_state,
                      sf::Vector2f cursor) const {
    draw_backdrop(target, &inventory, &command_state, cursor);

    if (const sf::Font* command_font = font_or_default(command_font_)) {
        sf::Text bar(pac::core::utf8(command_state.preview_text),
                     *command_font,
                     scaled_text_size(config_.skin.command_text.size));
        bar.setFillColor(config_.skin.command_text.color);
        place_text(bar, command_bar_area(), config_.skin.command_text.align);
        target.draw(bar);
    }

    const sf::Font* verb_font = font_or_default(verb_font_);
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

        if (verb_font) {
            sf::Text label(pac::core::utf8(strings.verb_label(std::string(verb_id(cell.verb)))),
                           *verb_font,
                           scaled_text_size(config_.skin.verb_text.size));
            label.setFillColor(selected ? theme_.verb_text_active
                                        : (hot ? config_.skin.verb_text.hover_color
                                               : config_.skin.verb_text.color));
            place_text(label, cell.rect, config_.skin.verb_text.align);
            target.draw(label);
        }
    }

    const int inactive_start = static_cast<int>(config_.content.verbs.size());
    const int verb_capacity = config_.layout.verb_panel.rows * config_.layout.verb_panel.columns;
    if (inactive_start < verb_capacity) {
        const ScummGridLayout& grid = config_.layout.verb_panel;
        const sf::FloatRect area = padded(body_child(grid.rect), grid.padding);
        const float sx = static_cast<float>(runtime_size_.x) / config_.layout.design_size.x;
        const float sy = static_cast<float>(runtime_size_.y) / config_.layout.design_size.y;
        const float gap_x = grid.cell_gap.x * sx;
        const float gap_y = grid.cell_gap.y * sy;
        const float cell_w =
            (area.width - gap_x * static_cast<float>(std::max(0, grid.columns - 1))) /
            static_cast<float>(grid.columns);
        const float cell_h =
            (area.height - gap_y * static_cast<float>(std::max(0, grid.rows - 1))) /
            static_cast<float>(grid.rows);
        sf::Color inactive = theme_.verb_default;
        inactive.a = 96;
        for (int i = inactive_start; i < verb_capacity; ++i) {
            const int col = i % grid.columns;
            const int row = i / grid.columns;
            const sf::FloatRect rect{area.left + static_cast<float>(col) * (cell_w + gap_x),
                                     area.top + static_cast<float>(row) * (cell_h + gap_y),
                                     cell_w - 2.0f,
                                     cell_h - 2.0f};
            sf::RectangleShape box(sf::Vector2f(rect.width, rect.height));
            box.setPosition(rect.left, rect.top);
            box.setFillColor(inactive);
            box.setOutlineThickness(1.0f);
            box.setOutlineColor(config_.skin.verb_text.disabled_color);
            target.draw(box);
        }
    }

    const sf::Font* inventory_font = font_or_default(inventory_font_);
    const int page = clamped_inventory_page(inventory, command_state.inventory_page_index);
    for (const InventoryCell& cell : inventory_cells(inventory, page)) {
        const bool hot = cell.rect.contains(cursor);
        if (hot) {
            sf::RectangleShape hl(sf::Vector2f(cell.rect.width, cell.rect.height - 2.0f));
            hl.setPosition(cell.rect.left, cell.rect.top);
            hl.setFillColor(theme_.inventory_hover_bg);
            target.draw(hl);
        }
        const InventoryItem* item = inventory.item(cell.item_id);
        if (inventory_font) {
            sf::Text text(pac::core::utf8(item ? item->name : cell.item_id),
                          *inventory_font,
                          scaled_text_size(config_.skin.inventory_text.size));
            text.setFillColor(hot ? config_.skin.inventory_text.hover_color
                                  : config_.skin.inventory_text.color);
            place_text(text, cell.rect, config_.skin.inventory_text.align);
            target.draw(text);
        }
    }
    draw_inventory_arrows(target, inventory, command_state, cursor);
    draw_settings_button(target, strings, cursor);
}

sf::FloatRect ScummPanel::options_area() const {
    const sf::FloatRect panel = scale_rect(config_.layout.panel_rect);
    const float pad = theme_.pad;
    const sf::FloatRect command = command_bar_area();
    return {panel.left + pad,
            command.top + command.height + pad,
            panel.width - 2.0f * pad,
            panel.height - command.height - 2.0f * pad};
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
                              const pac::core::Strings& strings,
                              const std::vector<std::string>& options,
                              sf::Vector2f cursor) const {
    draw_backdrop(target);

    const sf::Font* option_font = font_or_default(inventory_font_);
    if (!option_font) {
        return;
    }

    const sf::FloatRect area = options_area();
    const float row = option_row_height(options.size());
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

        sf::Text label(pac::core::utf8(options[i]),
                       *option_font,
                       scaled_text_size(theme_.option_text_size));
        label.setFillColor(hot ? theme_.verb_text_hover : theme_.verb_text);
        const sf::FloatRect b = label.getLocalBounds();
        label.setPosition(area.left + theme_.pad, y + (row - b.height) / 2.0f - b.top - 1.0f);
        target.draw(label);
    }
    draw_settings_button(target, strings, cursor);
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
