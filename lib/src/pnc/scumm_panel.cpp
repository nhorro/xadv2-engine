#include "engine/pnc/scumm_panel.hpp"

#include "engine/core/resource_cache.hpp"
#include "engine/core/strings.hpp"
#include "engine/core/text_encoding.hpp"
#include "engine/core/text_layout.hpp" // core::wrap_text
#include "engine/pnc/inventory.hpp"
#include "engine/pnc/speech_manager.hpp"

#include <SFML/Config.hpp>
#include <SFML/Graphics/CircleShape.hpp>
#include <SFML/Graphics/ConvexShape.hpp>
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
#include <cstdint>
#include <exception>
#include <functional>
#include <map>
#include <utility>
#include <vector>

namespace pac::pnc {

namespace {

sf::Color with_opacity(sf::Color color, float opacity) {
    color.a =
        static_cast<std::uint8_t>(std::clamp(static_cast<float>(color.a) * opacity, 0.0f, 255.0f));
    return color;
}

sf::FloatRect padded(sf::FloatRect rect, ScummPanelPadding pad) {
    rect.position.x += pad.left;
    rect.position.y += pad.top;
    rect.size.x -= pad.left + pad.right;
    rect.size.y -= pad.top + pad.bottom;
    if (rect.size.x < 0.0f) {
        rect.size.x = 0.0f;
    }
    if (rect.size.y < 0.0f) {
        rect.size.y = 0.0f;
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
    float x = rect.position.x + (rect.size.x - bounds.size.x) / 2.0f - bounds.position.x;
    if (align == "left") {
        x = rect.position.x + inset - bounds.position.x;
    } else if (align == "right") {
        x = rect.position.x + rect.size.x - bounds.size.x - inset - bounds.position.x;
    }

    // Vertical centering uses string-independent metrics, not this label's glyph
    // bounds: otherwise a word with a descender ("Agarrar") or none ("Mirar") would
    // sit at a different height than its neighbors. A fixed probe with both an
    // ascender and a descender gives every label the same baseline.
    float ref_top = bounds.position.y;
    float ref_height = bounds.size.y;
    if (const sf::Font* font = &text.getFont()) {
        sf::Text probe(*font, "Ag", text.getCharacterSize());
        probe.setStyle(text.getStyle());
        probe.setOutlineThickness(text.getOutlineThickness());
        const sf::FloatRect pb = probe.getLocalBounds();
        ref_top = pb.position.y;
        ref_height = pb.size.y;
    }
    text.setPosition({x, rect.position.y + (rect.size.y - ref_height) / 2.0f - ref_top});
}

void center_text(sf::Text& text, sf::FloatRect rect) {
    place_text(text, rect, "center");
}

// A small filled triangle used by the icon-inventory paging arrows.
void draw_v_arrow(sf::RenderTarget& target, sf::FloatRect rect, bool up, sf::Color color) {
    const float inset = rect.size.x * 0.2f;
    const float l = rect.position.x + inset;
    const float r = rect.position.x + rect.size.x - inset;
    const float t = rect.position.y + inset;
    const float b = rect.position.y + rect.size.y - inset;
    sf::ConvexShape tri;
    tri.setPointCount(3);
    if (up) {
        tri.setPoint(0, {(l + r) / 2.0f, t});
        tri.setPoint(1, {r, b});
        tri.setPoint(2, {l, b});
    } else {
        tri.setPoint(0, {l, t});
        tri.setPoint(1, {r, t});
        tri.setPoint(2, {(l + r) / 2.0f, b});
    }
    tri.setFillColor(color);
    target.draw(tri);
}

} // namespace

ScummPanel::ScummPanel(sf::FloatRect region, const sf::Font* font, ScummPanelTheme theme)
    : ScummPanel(default_scumm_panel_config(region),
                 {static_cast<unsigned>(std::ceil(region.position.x + region.size.x)),
                  static_cast<unsigned>(std::ceil(region.position.y + region.size.y))},
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
    evidence_font_ = load_font(config_.evidence_indicator.text.font);
    notebook_font_ = load_font(config_.notebook.text.font);
    system_button_font_ = load_font(config_.skin.system_button_text.font);

    // Optional font smoothing. The fonts live in the shared ResourceCache and are
    // returned const; smoothing is a render hint on the font object, so a const_cast
    // is safe here. Affects every user of the same font (intended: a global look).
    if (config_.font_smooth.has_value()) {
        const bool smooth = *config_.font_smooth;
        for (const sf::Font* f : {command_font_,
                                  verb_font_,
                                  inventory_font_,
                                  arrow_font_,
                                  settings_font_,
                                  evidence_font_,
                                  notebook_font_,
                                  system_button_font_,
                                  font_}) {
            if (f != nullptr) {
#if SFML_VERSION_MAJOR > 2 || (SFML_VERSION_MAJOR == 2 && SFML_VERSION_MINOR >= 6)
                const_cast<sf::Font*>(f)->setSmooth(smooth);
#else
                // The pinned Android GLES2 fork is API-compatible with SFML
                // 2.5, before Font::setSmooth was added. Its glyph atlas keeps
                // the default smooth filtering, which is the game's setting.
                (void) smooth;
#endif
            }
        }
    }
}

sf::FloatRect ScummPanel::bounds() const {
    return scale_rect(config_.layout.panel_rect);
}

bool ScummPanel::contains(sf::Vector2f p) const {
    return bounds().contains(p);
}

sf::FloatRect ScummPanel::scale_rect(sf::FloatRect design_rect) const {
    const float sx = static_cast<float>(runtime_size_.x) / config_.layout.design_size.x;
    const float sy = static_cast<float>(runtime_size_.y) / config_.layout.design_size.y;
    return {{design_rect.position.x * sx, design_rect.position.y * sy},
            {design_rect.size.x * sx, design_rect.size.y * sy}};
}

const sf::Font* ScummPanel::font_or_default(const sf::Font* configured) const {
    return configured ? configured : font_;
}

unsigned ScummPanel::scaled_text_size(unsigned design_size) const {
    const float sy = static_cast<float>(runtime_size_.y) / config_.layout.design_size.y;
    return std::max(1u, static_cast<unsigned>(std::lround(static_cast<float>(design_size) * sy)));
}

void ScummPanel::apply_text_style(sf::Text& text,
                                  const ScummTextStyle& style,
                                  sf::Color fill) const {
    text.setFillColor(fill);
    if (style.outline_thickness > 0.0f) {
        const float sy = static_cast<float>(runtime_size_.y) / config_.layout.design_size.y;
        text.setOutlineThickness(style.outline_thickness * sy);
        text.setOutlineColor(style.outline_color);
    }
}

sf::FloatRect ScummPanel::panel_child(sf::FloatRect rect) const {
    const sf::FloatRect panel = config_.layout.panel_rect;
    return scale_rect({{panel.position.x + rect.position.x, panel.position.y + rect.position.y},
                       {rect.size.x, rect.size.y}});
}

sf::FloatRect ScummPanel::body_child(sf::FloatRect rect) const {
    const sf::FloatRect panel = config_.layout.panel_rect;
    const sf::FloatRect body = config_.layout.body_rect;
    return scale_rect({{panel.position.x + body.position.x + rect.position.x,
                        panel.position.y + body.position.y + rect.position.y},
                       {rect.size.x, rect.size.y}});
}

sf::FloatRect ScummPanel::inventory_child(sf::FloatRect rect) const {
    const sf::FloatRect panel = config_.layout.panel_rect;
    const sf::FloatRect body = config_.layout.body_rect;
    const sf::FloatRect inv = config_.layout.inventory_panel.rect;
    return scale_rect({{panel.position.x + body.position.x + inv.position.x + rect.position.x,
                        panel.position.y + body.position.y + inv.position.y + rect.position.y},
                       {rect.size.x, rect.size.y}});
}

std::vector<ScummPanel::VerbCell> ScummPanel::verb_cells() const {
    std::vector<VerbCell> cells;
    const ScummGridLayout& grid = config_.layout.verb_panel;
    const sf::FloatRect area = padded(body_child(grid.rect), grid.padding);

    // TEXT style: a single horizontal row of equal-width clickable cells, one per
    // verb (no grid capacity, no boxes — the label is drawn plainly in draw()).
    if (config_.layout.verb_style == VerbPanelStyle::TEXT) {
        const std::size_t n = config_.content.verbs.size();
        if (n == 0) {
            return cells;
        }
        const float cell_w = area.size.x / static_cast<float>(n);
        for (std::size_t i = 0; i < n; ++i) {
            cells.push_back({config_.content.verbs[i],
                             {{area.position.x + static_cast<float>(i) * cell_w, area.position.y},
                              {cell_w, area.size.y}}});
        }
        return cells;
    }

    const float sx = static_cast<float>(runtime_size_.x) / config_.layout.design_size.x;
    const float sy = static_cast<float>(runtime_size_.y) / config_.layout.design_size.y;
    const float gap_x = grid.cell_gap.x * sx;
    const float gap_y = grid.cell_gap.y * sy;
    const float cell_w = (area.size.x - gap_x * static_cast<float>(std::max(0, grid.columns - 1))) /
                         static_cast<float>(grid.columns);
    const float cell_h = (area.size.y - gap_y * static_cast<float>(std::max(0, grid.rows - 1))) /
                         static_cast<float>(grid.rows);
    for (std::size_t i = 0; i < config_.content.verbs.size(); ++i) {
        if (i >= static_cast<std::size_t>(grid.rows * grid.columns)) {
            break;
        }
        const int col = static_cast<int>(i) % grid.columns;
        const int row = static_cast<int>(i) / grid.columns;
        cells.push_back({config_.content.verbs[i],
                         {{area.position.x + static_cast<float>(col) * (cell_w + gap_x),
                           area.position.y + static_cast<float>(row) * (cell_h + gap_y)},
                          {cell_w - 2.0f, cell_h - 2.0f}}});
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
    const float cell_w = (area.size.x - gap_x * static_cast<float>(std::max(0, grid.columns - 1))) /
                         static_cast<float>(grid.columns);
    const float cell_h = (area.size.y - gap_y * static_cast<float>(std::max(0, grid.rows - 1))) /
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
                         {{area.position.x + static_cast<float>(col) * (cell_w + gap_x),
                           area.position.y + static_cast<float>(row) * (cell_h + gap_y)},
                          {cell_w, cell_h}}});
    }
    return cells;
}

sf::FloatRect ScummPanel::inventory_area() const {
    return body_child(config_.layout.inventory_panel.rect);
}

ScummPanel::IconInventoryLayout ScummPanel::icon_inventory_layout() const {
    IconInventoryLayout out;
    const ScummGridLayout& grid = config_.layout.inventory_panel;
    const sf::FloatRect area = padded(inventory_area(), grid.padding);
    const float sx = static_cast<float>(runtime_size_.x) / config_.layout.design_size.x;
    const float sy = static_cast<float>(runtime_size_.y) / config_.layout.design_size.y;

    float slots_w = area.size.x;
    const ScummInventoryPagination& paging = config_.layout.inventory_pagination;
    if (paging.enabled) {
        // The arrows live in their own body-relative zone, so the whole inventory
        // rect stays available to the slots.
        const sf::FloatRect zone = config_.layout.inventory_pagination.rect;
        const auto in_zone = [&](sf::FloatRect r) {
            return body_child({{zone.position.x + r.position.x, zone.position.y + r.position.y},
                               {r.size.x, r.size.y}});
        };
        out.prev_arrow = in_zone(paging.previous);
        out.next_arrow = in_zone(paging.next);
    } else {
        // Reserve a right-hand gutter for the stacked up/down paging arrows; the
        // slots share the rest. The gutter is reserved whether or not paging is
        // currently needed, so slot positions stay stable as the inventory fills up.
        const float gutter = std::min(area.size.x * 0.16f, area.size.y * 0.6f);
        const float arrow_gap = gutter * 0.2f;
        slots_w = std::max(1.0f, area.size.x - gutter - arrow_gap);
        out.prev_arrow = {{area.position.x + area.size.x - gutter, area.position.y},
                          {gutter, area.size.y * 0.5f}};
        out.next_arrow = {
            {area.position.x + area.size.x - gutter, area.position.y + area.size.y * 0.5f},
            {gutter, area.size.y * 0.5f}};
    }

    const float gap_x = grid.cell_gap.x * sx;
    const float gap_y = grid.cell_gap.y * sy;
    const float cell_w = (slots_w - gap_x * static_cast<float>(std::max(0, grid.columns - 1))) /
                         static_cast<float>(grid.columns);
    const float cell_h = (area.size.y - gap_y * static_cast<float>(std::max(0, grid.rows - 1))) /
                         static_cast<float>(grid.rows);
    const int capacity = inventory_capacity();
    for (int i = 0; i < capacity; ++i) {
        const int col = i % grid.columns;
        const int row = i / grid.columns;
        out.slots.push_back({{area.position.x + static_cast<float>(col) * (cell_w + gap_x),
                              area.position.y + static_cast<float>(row) * (cell_h + gap_y)},
                             {cell_w, cell_h}});
    }
    return out;
}

std::vector<ScummPanel::SystemButtonCell> ScummPanel::system_button_cells() const {
    std::vector<SystemButtonCell> cells;
    cells.reserve(config_.system_buttons.size());
    for (const ScummSystemButton& button : config_.system_buttons) {
        cells.push_back({&button, body_child(button.rect)});
    }
    return cells;
}

const sf::Font* ScummPanel::system_button_font() const {
    return font_or_default(system_button_font_);
}

void ScummPanel::draw_button_box(sf::RenderTarget& target,
                                 const ScummButtonSkin& skin,
                                 sf::FloatRect rect,
                                 bool hovered,
                                 bool selected,
                                 bool enabled) const {
    sf::Color fill = skin.background;
    sf::Color border = skin.border;
    if (!enabled) {
        fill = skin.disabled_background;
        border = skin.disabled_border;
    } else if (selected) {
        fill = skin.selected_background;
        border = skin.selected_border;
    } else if (hovered) {
        fill = skin.hover_background;
        border = skin.hover_border;
    }

    // The border grows inward so a thick highlight never bleeds into the
    // neighbouring cell (the grid packs cells edge to edge).
    const float sy = static_cast<float>(runtime_size_.y) / config_.layout.design_size.y;
    const float thickness = skin.border_thickness * sy;
    sf::RectangleShape box(sf::Vector2f(std::max(0.0f, rect.size.x - 2.0f * thickness),
                                        std::max(0.0f, rect.size.y - 2.0f * thickness)));
    box.setPosition({rect.position.x + thickness, rect.position.y + thickness});
    box.setFillColor(fill);
    box.setOutlineThickness(thickness);
    box.setOutlineColor(border);
    target.draw(box);
}

void ScummPanel::draw_system_buttons(sf::RenderTarget& target,
                                     const pac::core::Strings& strings,
                                     sf::Vector2f cursor) const {
    const sf::Font* font = system_button_font();
    const ScummTextStyle& style = config_.skin.system_button_text;
    for (const SystemButtonCell& cell : system_button_cells()) {
        const ScummSystemButton& button = *cell.button;
        const bool hot = cell.rect.contains(cursor);

        if (button.render_mode == ScummButtonRenderMode::IMAGE) {
            const std::string& image =
                hot && !button.image.hovered.empty() ? button.image.hovered : button.image.normal;
            draw_image_in_rect(target, image, cell.rect);
            continue;
        }

        draw_button_box(target, config_.skin.system_button, cell.rect, hot, /*selected=*/false);

        // An optional icon takes a compact gutter on the left; keeping it smaller
        // than the button height leaves enough room for labels such as "Opciones".
        // Without an icon the label owns the whole box.
        sf::FloatRect label_rect = cell.rect;
        if (!button.icon.empty()) {
            const float icon_w = std::min(cell.rect.size.y, cell.rect.size.x * 0.32f);
            const float inset = icon_w * 0.2f;
            draw_image_in_rect(target,
                               button.icon,
                               {{cell.rect.position.x + inset, cell.rect.position.y + inset},
                                {icon_w - 2.0f * inset, icon_w - 2.0f * inset}});
            label_rect.position.x += icon_w;
            label_rect.size.x -= icon_w;
        }
        if (!font || button.label_key.empty()) {
            continue;
        }
        sf::Text label(*font,
                       pac::core::utf8(strings.ui_label(button.label_key)),
                       scaled_text_size(style.size));
        apply_text_style(label, style, hot ? style.hover_color : style.color);
        place_text(label, label_rect, style.align);
        target.draw(label);
    }
}

sf::FloatRect ScummPanel::evidence_indicator_area() const {
    return body_child(config_.evidence_indicator.rect);
}

sf::FloatRect ScummPanel::notebook_area() const {
    return body_child(config_.notebook.rect);
}

std::vector<ScummPanel::NotebookCell> ScummPanel::notebook_cells() const {
    std::vector<NotebookCell> cells;
    const ScummNotebookConfig& nb = config_.notebook;
    if (nb.entries.empty()) {
        return cells;
    }
    sf::FloatRect area = notebook_area();
    // A leading icon (image or placeholder) reserves a square gutter on the left;
    // the entries stack vertically (one row each) to its right.
    const float icon_w = area.size.y;
    const float gap = icon_w * 0.18f;
    area.position.x += icon_w + gap;
    area.size.x -= icon_w + gap;
    const float cell_h = area.size.y / static_cast<float>(nb.entries.size());
    for (std::size_t i = 0; i < nb.entries.size(); ++i) {
        cells.push_back({nb.entries[i].label_key,
                         nb.entries[i].tab,
                         {{area.position.x, area.position.y + static_cast<float>(i) * cell_h},
                          {area.size.x, cell_h}}});
    }
    return cells;
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

    const sf::Vector2f anchor_point{panel.position.x + button.position.x * panel.size.x,
                                    panel.position.y + button.position.y * panel.size.y};
    return scale_rect(
        {{anchor_point.x - anchor_offset.x, anchor_point.y - anchor_offset.y}, {size.x, size.y}});
}

PanelIntent ScummPanel::click(sf::Vector2f p,
                              const InventoryModel& inventory,
                              const CommandState& command_state) const {
    if (config_.settings_button.enabled && settings_button_area().contains(p)) {
        return {PanelIntent::Kind::OPEN_SETTINGS, Verb::LOOK_AT, {}, 0, {}, {}};
    }
    for (const SystemButtonCell& cell : system_button_cells()) {
        if (!cell.rect.contains(p)) {
            continue;
        }
        switch (cell.button->action) {
        case ScummSystemAction::OPEN_SETTINGS:
            return {PanelIntent::Kind::OPEN_SETTINGS, Verb::LOOK_AT, {}, 0, {}, {}};
        case ScummSystemAction::OPEN_MENU:
            return {PanelIntent::Kind::OPEN_MENU, Verb::LOOK_AT, {}, 0, {}, {}};
        case ScummSystemAction::PUSH_SCENE:
            return {PanelIntent::Kind::PUSH_SCENE, Verb::LOOK_AT, {}, 0, {}, cell.button->scene};
        }
    }
    if (config_.notebook.enabled) {
        for (const NotebookCell& cell : notebook_cells()) {
            if (cell.rect.contains(p)) {
                return {PanelIntent::Kind::OPEN_NOTEBOOK, Verb::LOOK_AT, {}, 0, cell.tab, {}};
            }
        }
    }
    for (const VerbCell& cell : verb_cells()) {
        if (cell.rect.contains(p)) {
            return {PanelIntent::Kind::SELECT_VERB, cell.verb, {}, 0, {}, {}};
        }
    }
    const int page = clamped_inventory_page(inventory, command_state.inventory_page_index);
    const int pages = inventory_page_count(inventory);

    if (config_.layout.inventory_style == InventoryStyle::ICONS) {
        const IconInventoryLayout layout = icon_inventory_layout();
        if (pages > 1) {
            if (layout.prev_arrow.contains(p) && page > 0) {
                return {PanelIntent::Kind::CHANGE_INVENTORY_PAGE,
                        Verb::LOOK_AT,
                        {},
                        page - 1,
                        {},
                        {}};
            }
            if (layout.next_arrow.contains(p) && page < pages - 1) {
                return {PanelIntent::Kind::CHANGE_INVENTORY_PAGE,
                        Verb::LOOK_AT,
                        {},
                        page + 1,
                        {},
                        {}};
            }
        }
        const int capacity = inventory_capacity();
        const int first = page * capacity;
        for (std::size_t i = 0; i < layout.slots.size(); ++i) {
            const int item_index = first + static_cast<int>(i);
            if (item_index >= static_cast<int>(inventory.list().size())) {
                break;
            }
            if (layout.slots[i].contains(p)) {
                return {PanelIntent::Kind::CLICK_INVENTORY,
                        Verb::LOOK_AT,
                        inventory.list()[static_cast<std::size_t>(item_index)],
                        0,
                        {},
                        {}};
            }
        }
        return {};
    }

    if (config_.layout.inventory_arrows.mode != InventoryArrowMode::NONE) {
        if (arrow_previous_area().contains(p) && page > 0) {
            return {PanelIntent::Kind::CHANGE_INVENTORY_PAGE, Verb::LOOK_AT, {}, page - 1, {}, {}};
        }
        if (arrow_next_area().contains(p) && page < pages - 1) {
            return {PanelIntent::Kind::CHANGE_INVENTORY_PAGE, Verb::LOOK_AT, {}, page + 1, {}, {}};
        }
    }
    for (const InventoryCell& cell : inventory_cells(inventory, page)) {
        if (cell.rect.contains(p)) {
            return {PanelIntent::Kind::CLICK_INVENTORY, Verb::LOOK_AT, cell.item_id, 0, {}, {}};
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

bool ScummPanel::draw_background_image(sf::RenderTarget& target,
                                       const std::string& image,
                                       ScummPanelScaleMode mode) const {
    if (!resources_ || image.empty()) {
        return false;
    }
    try {
        const sf::Texture& texture = resources_->texture(image);
        if (mode == ScummPanelScaleMode::NINE_SLICE) {
            return draw_nine_slice(target, image);
        }
        const sf::FloatRect panel = scale_rect(config_.layout.panel_rect);
        const sf::Vector2u size = texture.getSize();
        sf::Sprite sprite(texture);
        sprite.setColor(with_opacity(sf::Color::White, config_.layout.background.opacity));
        if (mode == ScummPanelScaleMode::FIT) {
            const float scale = std::min(panel.size.x / static_cast<float>(size.x),
                                         panel.size.y / static_cast<float>(size.y));
            sprite.setScale({scale, scale});
            sprite.setPosition(
                {panel.position.x + (panel.size.x - static_cast<float>(size.x) * scale) / 2.0f,
                 panel.position.y + (panel.size.y - static_cast<float>(size.y) * scale) / 2.0f});
            target.draw(sprite);
        } else if (mode == ScummPanelScaleMode::TILE) {
            for (float y = panel.position.y; y < panel.position.y + panel.size.y;
                 y += static_cast<float>(size.y)) {
                for (float x = panel.position.x; x < panel.position.x + panel.size.x;
                     x += static_cast<float>(size.x)) {
                    sprite.setPosition({x, y});
                    target.draw(sprite);
                }
            }
        } else {
            sprite.setPosition({panel.position.x, panel.position.y});
            sprite.setScale({panel.size.x / static_cast<float>(size.x),
                             panel.size.y / static_cast<float>(size.y)});
            target.draw(sprite);
        }
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool ScummPanel::draw_nine_slice(sf::RenderTarget& target, const std::string& image) const {
    if (!resources_ || image.empty()) {
        return false;
    }
    const sf::Texture* texture = nullptr;
    try {
        texture = &resources_->texture(image);
    } catch (const std::exception&) {
        return false;
    }
    const sf::Vector2u tex_size = texture->getSize();
    const sf::FloatRect panel = scale_rect(config_.layout.panel_rect);
    const ScummPanelPadding margin = config_.layout.background.nine_slice;
    const float sx = static_cast<float>(runtime_size_.x) / config_.layout.design_size.x;
    const float sy = static_cast<float>(runtime_size_.y) / config_.layout.design_size.y;
    const std::array<float, 4> dx{panel.position.x,
                                  panel.position.x + margin.left * sx,
                                  panel.position.x + panel.size.x - margin.right * sx,
                                  panel.position.x + panel.size.x};
    const std::array<float, 4> dy{panel.position.y,
                                  panel.position.y + margin.top * sy,
                                  panel.position.y + panel.size.y - margin.bottom * sy,
                                  panel.position.y + panel.size.y};
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
            sf::Sprite sprite(*texture, sf::IntRect({sxp[col], syp[row]}, {sw, sh}));
            sprite.setColor(with_opacity(sf::Color::White, config_.layout.background.opacity));
            sprite.setPosition({dx[col], dy[row]});
            sprite.setScale({dw / static_cast<float>(sw), dh / static_cast<float>(sh)});
            target.draw(sprite);
        }
    }
    return true;
}

void ScummPanel::draw_backdrop(sf::RenderTarget& target,
                               const InventoryModel* inventory,
                               const CommandState* command_state,
                               sf::Vector2f cursor) const {
    const sf::FloatRect panel = scale_rect(config_.layout.panel_rect);

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
    const bool image_drawn = !image.empty() && draw_background_image(target, image, mode);
    const ScummCommandBarSkin& bar = config_.skin.command_bar;
    const sf::FloatRect area = command_bar_area();
    if (!image_drawn) {
        const sf::Color background =
            with_opacity(config_.layout.background.color, config_.layout.background.opacity);
        const auto draw_background_rect = [&target, background](sf::FloatRect rect) {
            if (rect.size.x <= 0.0f || rect.size.y <= 0.0f)
                return;
            sf::RectangleShape shape({rect.size.x, rect.size.y});
            shape.setPosition({rect.position.x, rect.position.y});
            shape.setFillColor(background);
            target.draw(shape);
        };
        const float left = std::max(panel.position.x, area.position.x);
        const float top = std::max(panel.position.y, area.position.y);
        const float right =
            std::min(panel.position.x + panel.size.x, area.position.x + area.size.x);
        const float bottom =
            std::min(panel.position.y + panel.size.y, area.position.y + area.size.y);
        if (left >= right || top >= bottom) {
            draw_background_rect(panel);
        } else {
            draw_background_rect(
                {{panel.position.x, panel.position.y}, {panel.size.x, top - panel.position.y}});
            draw_background_rect({{panel.position.x, bottom},
                                  {panel.size.x, panel.position.y + panel.size.y - bottom}});
            draw_background_rect(
                {{panel.position.x, top}, {left - panel.position.x, bottom - top}});
            draw_background_rect(
                {{right, top}, {panel.position.x + panel.size.x - right, bottom - top}});
        }

        // The command bar replaces (rather than overlays) the panel material in
        // its rectangle, so its configured alpha is applied exactly once.
        sf::RectangleShape strip({area.size.x, area.size.y});
        strip.setPosition({area.position.x, area.position.y});
        strip.setFillColor(with_opacity(bar.background, config_.layout.background.opacity));
        target.draw(strip);
    }

    // Image skins already contain their authored command strip and dividers.
    if (!image_drawn && bar.separator_thickness > 0.0f) {
        const float sy = static_cast<float>(runtime_size_.y) / config_.layout.design_size.y;
        const float thickness = bar.separator_thickness * sy;
        sf::RectangleShape rule(sf::Vector2f(panel.size.x, thickness));
        rule.setPosition({panel.position.x, area.position.y + area.size.y - thickness});
        rule.setFillColor(with_opacity(bar.separator, config_.layout.background.opacity));
        target.draw(rule);
    }
}

void ScummPanel::draw_image_in_rect(sf::RenderTarget& target,
                                    const std::string& image,
                                    sf::FloatRect rect,
                                    sf::IntRect src) const {
    if (!resources_ || image.empty()) {
        return;
    }
    try {
        const sf::Texture& texture = resources_->texture(image);
        sf::Sprite sprite(texture);
        float src_w = static_cast<float>(texture.getSize().x);
        float src_h = static_cast<float>(texture.getSize().y);
        if (src.size.x > 0 && src.size.y > 0) {
            sprite.setTextureRect(src);
            src_w = static_cast<float>(src.size.x);
            src_h = static_cast<float>(src.size.y);
        }
        sprite.setPosition({rect.position.x, rect.position.y});
        sprite.setScale({rect.size.x / src_w, rect.size.y / src_h});
        target.draw(sprite);
    } catch (const std::exception&) {
        // Optional button art falls back to no image.
    }
}

bool ScummPanel::draw_item_icon(sf::RenderTarget& target,
                                const InventoryItem& item,
                                const InventoryIconSheet& sheet,
                                sf::FloatRect dest) const {
    if (!resources_) {
        return false;
    }
    try {
        if (sheet.production && item.icon_cell >= 0 && !sheet.sheet.empty()) {
            const sf::Texture& tex = resources_->texture(sheet.sheet);
            const int cols = std::max(1, sheet.columns);
            const int rows = std::max(1, sheet.rows);
            const int cw = static_cast<int>(tex.getSize().x) / cols;
            const int ch = static_cast<int>(tex.getSize().y) / rows;
            const int col = item.icon_cell % cols;
            const int row = item.icon_cell / cols;
            draw_image_in_rect(target,
                               sheet.sheet,
                               dest,
                               sf::IntRect({col * cw, row * ch}, {cw, ch}));
            return true;
        }
        if (!sheet.production && !item.icon.empty()) {
            draw_image_in_rect(target, item.icon, dest);
            return true;
        }
    } catch (const std::exception&) {
        // Missing/invalid art falls back to the placeholder glyph.
    }
    return false;
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
        sf::Text text(*arrow_font,
                      pac::core::utf8(label),
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

    sf::RectangleShape box(sf::Vector2f(rect.size.x, rect.size.y));
    box.setPosition({rect.position.x, rect.position.y});
    box.setFillColor(hot ? button.panel.hovered_background_color : button.panel.background_color);
    box.setOutlineThickness(1.0f);
    box.setOutlineColor(button.panel.outline_color);
    target.draw(box);

    const sf::Font* settings_font = font_or_default(settings_font_);
    if (!settings_font) {
        return;
    }
    sf::Text label(*settings_font,
                   pac::core::utf8(strings.ui_label(button.panel.label_key)),
                   scaled_text_size(button.panel.font_size));
    label.setFillColor(hot ? button.panel.hovered_color : button.panel.normal_color);
    center_text(label, rect);
    target.draw(label);
}

void ScummPanel::draw(sf::RenderTarget& target,
                      const pac::core::Strings& strings,
                      const InventoryModel& inventory,
                      const CommandState& command_state,
                      sf::Vector2f cursor,
                      EvidenceProgress evidence,
                      InventoryNotificationQuery has_notification,
                      InventoryNameQuery localized_name) const {
    draw_backdrop(target, &inventory, &command_state, cursor);

    if (const sf::Font* command_font = font_or_default(command_font_)) {
        sf::Text bar(*command_font,
                     pac::core::utf8(command_state.preview_text),
                     scaled_text_size(config_.skin.command_text.size));
        apply_text_style(bar, config_.skin.command_text, config_.skin.command_text.color);
        place_text(bar, command_bar_area(), config_.skin.command_text.align);
        target.draw(bar);
    }

    const sf::Font* verb_font = font_or_default(verb_font_);
    if (config_.layout.verb_style == VerbPanelStyle::TEXT) {
        // Plain horizontal labels, no boxes; the selected verb takes the amber
        // accent color so it reads as active without a frame.
        if (verb_font) {
            for (const VerbCell& cell : verb_cells()) {
                const bool selected =
                    command_state.selected_verb && *command_state.selected_verb == cell.verb;
                const bool hot = cell.rect.contains(cursor);
                sf::Text label(
                    *verb_font,
                    pac::core::utf8(strings.verb_panel_label(std::string(verb_id(cell.verb)))),
                    scaled_text_size(config_.skin.verb_text.size));
                apply_text_style(label,
                                 config_.skin.verb_text,
                                 selected ? theme_.verb_selected
                                          : (hot ? config_.skin.verb_text.hover_color
                                                 : config_.skin.verb_text.color));
                place_text(label, cell.rect, config_.skin.verb_text.align);
                target.draw(label);
            }
        }
    } else {
        for (const VerbCell& cell : verb_cells()) {
            const bool selected =
                command_state.selected_verb && *command_state.selected_verb == cell.verb;
            const bool hot = cell.rect.contains(cursor);
            draw_button_box(target, config_.skin.verb_button, cell.rect, hot, selected);

            if (verb_font) {
                sf::Text label(
                    *verb_font,
                    pac::core::utf8(strings.verb_panel_label(std::string(verb_id(cell.verb)))),
                    scaled_text_size(config_.skin.verb_text.size));
                apply_text_style(label,
                                 config_.skin.verb_text,
                                 selected ? config_.skin.verb_text.selected_color
                                          : (hot ? config_.skin.verb_text.hover_color
                                                 : config_.skin.verb_text.color));
                place_text(label, cell.rect, config_.skin.verb_text.align);
                target.draw(label);
            }
        }

        const int inactive_start = static_cast<int>(config_.content.verbs.size());
        const int verb_capacity =
            config_.layout.verb_panel.rows * config_.layout.verb_panel.columns;
        if (inactive_start < verb_capacity) {
            const ScummGridLayout& grid = config_.layout.verb_panel;
            const sf::FloatRect area = padded(body_child(grid.rect), grid.padding);
            const float sx = static_cast<float>(runtime_size_.x) / config_.layout.design_size.x;
            const float sy = static_cast<float>(runtime_size_.y) / config_.layout.design_size.y;
            const float gap_x = grid.cell_gap.x * sx;
            const float gap_y = grid.cell_gap.y * sy;
            const float cell_w =
                (area.size.x - gap_x * static_cast<float>(std::max(0, grid.columns - 1))) /
                static_cast<float>(grid.columns);
            const float cell_h =
                (area.size.y - gap_y * static_cast<float>(std::max(0, grid.rows - 1))) /
                static_cast<float>(grid.rows);
            for (int i = inactive_start; i < verb_capacity; ++i) {
                const int col = i % grid.columns;
                const int row = i / grid.columns;
                const sf::FloatRect rect{
                    {area.position.x + static_cast<float>(col) * (cell_w + gap_x),
                     area.position.y + static_cast<float>(row) * (cell_h + gap_y)},
                    {cell_w - 2.0f, cell_h - 2.0f}};
                draw_button_box(target,
                                config_.skin.verb_button,
                                rect,
                                /*hovered=*/false,
                                /*selected=*/false,
                                /*enabled=*/false);
            }
        }
    }

    if (config_.layout.inventory_style == InventoryStyle::ICONS) {
        draw_inventory_icons(target,
                             inventory,
                             command_state,
                             cursor,
                             has_notification,
                             localized_name);
    } else {
        const sf::Font* inventory_font = font_or_default(inventory_font_);
        const int page = clamped_inventory_page(inventory, command_state.inventory_page_index);
        for (const InventoryCell& cell : inventory_cells(inventory, page)) {
            const bool hot = cell.rect.contains(cursor);
            if (hot) {
                sf::RectangleShape hl(sf::Vector2f(cell.rect.size.x, cell.rect.size.y - 2.0f));
                hl.setPosition({cell.rect.position.x, cell.rect.position.y});
                hl.setFillColor(theme_.inventory_hover_bg);
                target.draw(hl);
            }
            const InventoryItem* item = inventory.item(cell.item_id);
            if (inventory_font) {
                const std::string source = item ? item->name : cell.item_id;
                const std::string name =
                    localized_name ? localized_name(cell.item_id, source) : source;
                sf::Text text(*inventory_font,
                              pac::core::utf8(name),
                              scaled_text_size(config_.skin.inventory_text.size));
                apply_text_style(text,
                                 config_.skin.inventory_text,
                                 hot ? config_.skin.inventory_text.hover_color
                                     : config_.skin.inventory_text.color);
                place_text(text, cell.rect, config_.skin.inventory_text.align);
                target.draw(text);
            }
            if (has_notification && has_notification(cell.item_id)) {
                draw_inventory_notification(target, cell.rect);
            }
        }
        draw_inventory_arrows(target, inventory, command_state, cursor);
    }

    draw_evidence_indicator(target, strings, evidence);
    draw_notebook(target, strings, cursor);
    draw_settings_button(target, strings, cursor);
    draw_system_buttons(target, strings, cursor);
}

void ScummPanel::draw_inventory_icons(sf::RenderTarget& target,
                                      const InventoryModel& inventory,
                                      const CommandState& command_state,
                                      sf::Vector2f cursor,
                                      const InventoryNotificationQuery& has_notification,
                                      const InventoryNameQuery& localized_name) const {
    const IconInventoryLayout layout = icon_inventory_layout();
    const std::vector<std::string>& items = inventory.list();
    const sf::Font* inventory_font = font_or_default(inventory_font_);
    const int capacity = inventory_capacity();
    const int page = clamped_inventory_page(inventory, command_state.inventory_page_index);
    const int first = page * capacity;

    for (std::size_t i = 0; i < layout.slots.size(); ++i) {
        const sf::FloatRect rect = layout.slots[i];
        const int item_index = first + static_cast<int>(i);
        const bool has_item = item_index < static_cast<int>(items.size());
        const bool hot = has_item && rect.contains(cursor);
        // The selected item keeps its accent while the command is being composed —
        // including after a page flip, since selection is tracked by item id.
        const bool selected = has_item && command_state.selected_inventory_item_id &&
                              *command_state.selected_inventory_item_id ==
                                  items[static_cast<std::size_t>(item_index)];

        draw_button_box(target, config_.skin.inventory_slot, rect, hot, selected);

        if (!has_item) {
            continue;
        }
        const std::string& item_id = items[static_cast<std::size_t>(item_index)];
        const InventoryItem* item = inventory.item(item_id);
        // Inset the art a little inside the frame.
        const float inset = std::min(rect.size.x, rect.size.y) * 0.12f;
        const sf::FloatRect art{{rect.position.x + inset, rect.position.y + inset},
                                {rect.size.x - 2.0f * inset, rect.size.y - 2.0f * inset}};
        const bool drew = item && draw_item_icon(target, *item, inventory.icon_sheet(), art);
        if (!drew && inventory_font) {
            // Placeholder: the item name's first glyph, centered.
            const std::string source = item ? item->name : item_id;
            const std::string label = localized_name ? localized_name(item_id, source) : source;
            const std::string glyph = label.empty() ? std::string("?") : label.substr(0, 1);
            sf::Text text(*inventory_font,
                          pac::core::utf8(glyph),
                          scaled_text_size(config_.skin.inventory_text.size));
            apply_text_style(text, config_.skin.inventory_text, config_.skin.inventory_text.color);
            center_text(text, rect);
            target.draw(text);
        }
        if (has_notification && has_notification(item_id)) {
            draw_inventory_notification(target, rect);
        }
    }

    // Keep both paging controls visible even when the inventory fits on one page.
    // Besides making the layout stable, the disabled glyphs explain the otherwise
    // empty pagination column before the player has collected enough items.
    const int pages = inventory_page_count(inventory);
    const bool prev = page > 0;
    const bool next = page < pages - 1;
    const sf::Color disabled = config_.skin.inventory_text.disabled_color;
    const auto arrow_color = [&](sf::FloatRect rect, bool enabled) {
        if (!enabled) {
            return disabled;
        }
        return rect.contains(cursor) ? config_.skin.inventory_text.hover_color
                                     : config_.skin.inventory_text.color;
    };
    draw_v_arrow(target, layout.prev_arrow, true, arrow_color(layout.prev_arrow, prev));
    draw_v_arrow(target, layout.next_arrow, false, arrow_color(layout.next_arrow, next));
}

void ScummPanel::draw_inventory_notification(sf::RenderTarget& target, sf::FloatRect rect) const {
    const float radius = std::clamp(std::min(rect.size.x, rect.size.y) * 0.115f, 7.0f, 11.0f);
    const sf::Vector2f center{rect.position.x + rect.size.x - radius * 0.85f,
                              rect.position.y + radius * 0.85f};
    sf::CircleShape badge(radius);
    badge.setOrigin({radius, radius});
    badge.setPosition(center);
    badge.setFillColor(sf::Color(245, 193, 72, 255));
    badge.setOutlineColor(sf::Color(34, 27, 18, 245));
    badge.setOutlineThickness(std::max(1.0f, radius * 0.12f));
    target.draw(badge);

    const sf::Font* font = font_or_default(inventory_font_);
    if (!font) {
        return;
    }
    sf::Text mark(*font,
                  "!",
                  std::max(10u, scaled_text_size(static_cast<unsigned>(radius * 1.55f))));
    mark.setFillColor(sf::Color(35, 27, 17, 255));
    center_text(mark, {{center.x - radius, center.y - radius}, {radius * 2.0f, radius * 2.0f}});
    // Cormorant's exclamation sits optically a little high inside its glyph box.
    mark.move({0.0f, radius * 0.03f});
    target.draw(mark);
}

void ScummPanel::draw_evidence_indicator(sf::RenderTarget& target,
                                         const pac::core::Strings& strings,
                                         EvidenceProgress evidence) const {
    const ScummEvidenceIndicator& ev = config_.evidence_indicator;
    if (!ev.enabled) {
        return;
    }
    sf::FloatRect area = evidence_indicator_area();
    // Leading icon (image, else a small placeholder square), left of the text.
    const float icon_w = area.size.y;
    const sf::FloatRect icon_rect{{area.position.x, area.position.y}, {icon_w, area.size.y}};
    if (!ev.icon.empty()) {
        draw_image_in_rect(target, ev.icon, icon_rect);
    } else {
        const float inset = icon_w * 0.18f;
        sf::RectangleShape glyph(sf::Vector2f(icon_w - 2.0f * inset, area.size.y - 2.0f * inset));
        glyph.setPosition({icon_rect.position.x + inset, icon_rect.position.y + inset});
        glyph.setFillColor(ev.text.color);
        target.draw(glyph);
    }
    area.position.x += icon_w + icon_w * 0.25f;
    area.size.x -= icon_w + icon_w * 0.25f;

    const sf::Font* font = font_or_default(evidence_font_);
    if (!font) {
        return;
    }
    // Stack the label on top and the "x/y" count below it.
    const sf::FloatRect label_rect{{area.position.x, area.position.y},
                                   {area.size.x, area.size.y * 0.5f}};
    const sf::FloatRect count_rect{{area.position.x, area.position.y + area.size.y * 0.5f},
                                   {area.size.x, area.size.y * 0.5f}};
    sf::Text label(*font,
                   pac::core::utf8(strings.ui_label(ev.label_key)),
                   scaled_text_size(ev.text.size));
    apply_text_style(label, ev.text, ev.text.color);
    place_text(label, label_rect, ev.text.align);
    target.draw(label);

    const std::string count =
        std::to_string(evidence.collected) + "/" + std::to_string(evidence.total);
    sf::Text count_text(*font, pac::core::utf8(count), scaled_text_size(ev.text.size));
    apply_text_style(count_text, ev.text, ev.text.color);
    place_text(count_text, count_rect, ev.text.align);
    target.draw(count_text);
}

void ScummPanel::draw_notebook(sf::RenderTarget& target,
                               const pac::core::Strings& strings,
                               sf::Vector2f cursor) const {
    const ScummNotebookConfig& nb = config_.notebook;
    if (!nb.enabled) {
        return;
    }
    // Leading icon (image, else a drawn placeholder square), left of the entries.
    const sf::FloatRect area = notebook_area();
    const sf::FloatRect icon_rect{{area.position.x, area.position.y}, {area.size.y, area.size.y}};
    if (!nb.icon.empty()) {
        draw_image_in_rect(target, nb.icon, icon_rect);
    } else {
        const float inset = icon_rect.size.y * 0.16f;
        sf::RectangleShape glyph(
            sf::Vector2f(icon_rect.size.x - 2.0f * inset, icon_rect.size.y - 2.0f * inset));
        glyph.setPosition({icon_rect.position.x + inset, icon_rect.position.y + inset});
        glyph.setFillColor(sf::Color::Transparent);
        glyph.setOutlineThickness(2.0f);
        glyph.setOutlineColor(nb.text.color);
        target.draw(glyph);
        // A spine line to read as a little book/notebook.
        sf::RectangleShape spine(sf::Vector2f(2.0f, icon_rect.size.y - 2.0f * inset));
        spine.setPosition(
            {icon_rect.position.x + icon_rect.size.x * 0.5f - 1.0f, icon_rect.position.y + inset});
        spine.setFillColor(nb.text.color);
        target.draw(spine);
    }
    const sf::Font* font = font_or_default(notebook_font_);
    if (!font) {
        return;
    }
    for (const NotebookCell& cell : notebook_cells()) {
        const bool hot = cell.rect.contains(cursor);
        sf::Text text(*font,
                      pac::core::utf8(strings.ui_label(cell.label_key)),
                      scaled_text_size(nb.text.size));
        apply_text_style(text, nb.text, hot ? nb.text.hover_color : nb.text.color);
        place_text(text, cell.rect, nb.text.align);
        target.draw(text);
    }
}

} // namespace pac::pnc
