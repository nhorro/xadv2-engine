#include "engine/pnc/direct_room_widget.hpp"

#include "engine/core/resource_cache.hpp"
#include "engine/core/strings.hpp"
#include "engine/core/text_encoding.hpp"
#include "engine/pnc/inventory.hpp"

#include <SFML/Graphics/CircleShape.hpp>
#include <SFML/Graphics/ConvexShape.hpp>
#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/Graphics/Sprite.hpp>
#include <SFML/Graphics/Text.hpp>

#include <algorithm>
#include <cmath>
#include <exception>
#include <utility>

namespace pac::pnc {

namespace {

float distance(sf::Vector2f a, sf::Vector2f b) {
    const float x = a.x - b.x;
    const float y = a.y - b.y;
    return std::sqrt(x * x + y * y);
}

void center_text(sf::Text& text, sf::FloatRect rect) {
    const sf::FloatRect bounds = text.getLocalBounds();
    text.setOrigin(bounds.left + bounds.width / 2.0f, bounds.top + bounds.height / 2.0f);
    text.setPosition(rect.left + rect.width / 2.0f, rect.top + rect.height / 2.0f);
}

sf::Color mix_color(sf::Color from, sf::Color to, float amount) {
    amount = std::clamp(amount, 0.0f, 1.0f);
    const auto channel = [amount](sf::Uint8 a, sf::Uint8 b) {
        return static_cast<sf::Uint8>(std::lround(
            static_cast<float>(a) + (static_cast<float>(b) - static_cast<float>(a)) * amount));
    };
    return {channel(from.r, to.r),
            channel(from.g, to.g),
            channel(from.b, to.b),
            channel(from.a, to.a)};
}

sf::FloatRect scale_from_center(sf::FloatRect rect, float factor) {
    const float width = rect.width * factor;
    const float height = rect.height * factor;
    return {rect.left + (rect.width - width) / 2.0f,
            rect.top + (rect.height - height) / 2.0f,
            width,
            height};
}

} // namespace

DirectRoomWidget::DirectRoomWidget(DirectRoomUiConfig config,
                                   sf::Vector2u virtual_resolution,
                                   const sf::Font* font,
                                   DirectRoomWidgetModel model,
                                   RoomUiIntentSink intent_sink)
    : config_(std::move(config)), virtual_resolution_(virtual_resolution), font_(font),
      model_(std::move(model)), intent_sink_(std::move(intent_sink)) {}

void DirectRoomWidget::connect(RoomUiStateStream& stream) {
    subscription_ = stream.subscribe([this](const RoomUiState& state) {
        state_ = state;
        const bool available =
            state_.mode == RoomInteractionMode::COMMAND && state_.widget_visible("direct");
        if (!available || !state_.inventory_open) {
            cancel_gesture();
        }
    });
}

sf::FloatRect DirectRoomWidget::scale(sf::FloatRect rect) const {
    const float sx = static_cast<float>(virtual_resolution_.x) / config_.design_size.x;
    const float sy = static_cast<float>(virtual_resolution_.y) / config_.design_size.y;
    return {rect.left * sx, rect.top * sy, rect.width * sx, rect.height * sy};
}

float DirectRoomWidget::scale_distance(float value) const {
    const float sx = static_cast<float>(virtual_resolution_.x) / config_.design_size.x;
    const float sy = static_cast<float>(virtual_resolution_.y) / config_.design_size.y;
    return value * std::min(sx, sy);
}

int DirectRoomWidget::inventory_capacity() const {
    return std::max(1, config_.inventory.rows * config_.inventory.columns);
}

int DirectRoomWidget::page_count() const {
    const int count = static_cast<int>(state_.inventory.list().size());
    return std::max(1, (count + inventory_capacity() - 1) / inventory_capacity());
}

int DirectRoomWidget::current_page() const {
    return std::clamp(state_.command.inventory_page_index, 0, page_count() - 1);
}

std::vector<sf::FloatRect> DirectRoomWidget::inventory_slots() const {
    std::vector<sf::FloatRect> result;
    const sf::FloatRect grid = scale(config_.inventory.grid);
    const float gap_x = scale_distance(config_.inventory.cell_gap.x);
    const float gap_y = scale_distance(config_.inventory.cell_gap.y);
    const float width = (grid.width - gap_x * static_cast<float>(config_.inventory.columns - 1)) /
                        static_cast<float>(config_.inventory.columns);
    const float height = (grid.height - gap_y * static_cast<float>(config_.inventory.rows - 1)) /
                         static_cast<float>(config_.inventory.rows);
    result.reserve(static_cast<std::size_t>(inventory_capacity()));
    for (int row = 0; row < config_.inventory.rows; ++row) {
        for (int column = 0; column < config_.inventory.columns; ++column) {
            result.push_back({grid.left + static_cast<float>(column) * (width + gap_x),
                              grid.top + static_cast<float>(row) * (height + gap_y),
                              width,
                              height});
        }
    }
    if (config_.inventory.compact_single_page && page_count() == 1 && config_.inventory.rows == 1) {
        const int visible =
            std::min(static_cast<int>(state_.inventory.list().size()), inventory_capacity());
        const float shift = static_cast<float>(inventory_capacity() - visible) * (width + gap_x);
        for (sf::FloatRect& slot : result) {
            slot.left += shift;
        }
    }
    return result;
}

sf::FloatRect DirectRoomWidget::inventory_panel_bounds() const {
    if (!config_.inventory.compact_single_page || page_count() > 1) {
        return scale(config_.inventory.panel);
    }
    const int visible =
        std::min(static_cast<int>(state_.inventory.list().size()), inventory_capacity());
    if (visible <= 0) {
        return {};
    }
    const std::vector<sf::FloatRect> slots = inventory_slots();
    const sf::FloatRect first = slots.front();
    const sf::FloatRect last = slots[static_cast<std::size_t>(visible - 1)];
    const float padding = scale_distance(config_.inventory.compact_padding);
    return {first.left - padding,
            first.top - padding,
            last.left + last.width - first.left + padding * 2.0f,
            first.height + padding * 2.0f};
}

std::optional<std::string> DirectRoomWidget::inventory_item_at(sf::Vector2f point) const {
    if (!state_.inventory_open || state_.mode != RoomInteractionMode::COMMAND ||
        !state_.widget_visible("direct")) {
        return std::nullopt;
    }
    const std::vector<std::string>& items = state_.inventory.list();
    const int first = current_page() * inventory_capacity();
    const std::vector<sf::FloatRect> slots = inventory_slots();
    for (std::size_t i = 0; i < slots.size(); ++i) {
        const int index = first + static_cast<int>(i);
        if (index < static_cast<int>(items.size()) && slots[i].contains(point)) {
            return items[static_cast<std::size_t>(index)];
        }
    }
    return std::nullopt;
}

InputResult DirectRoomWidget::handle(const RoutedInput& input) {
    const bool available =
        state_.mode == RoomInteractionMode::COMMAND && state_.widget_visible("direct");
    if (input.moved()) {
        cursor_ = input.position;
        if (state_.context_menu.open()) {
            return InputResult::CONSUMED;
        }
        if (pressed_item_ && !dragged_item_ &&
            distance(press_position_, cursor_) >=
                scale_distance(config_.interaction.drag_threshold)) {
            dragged_item_ = pressed_item_;
        }
        if (dragged_item_) {
            RoomUiIntent intent;
            intent.kind = RoomUiIntent::Kind::PREVIEW_INVENTORY_DROP;
            intent.id = *dragged_item_;
            intent.position = cursor_;
            emit(std::move(intent));
            return InputResult::CONSUMED;
        }
        const auto hovered_item = inventory_item_at(cursor_);
        const bool state_has_same_item =
            hovered_item && state_.command.hovered_object &&
            state_.command.hovered_object->kind == ObjectKind::INVENTORY_OBJECT &&
            state_.command.hovered_object->id == *hovered_item;
        if (hovered_item && !state_has_same_item) {
            RoomUiIntent intent;
            intent.kind = RoomUiIntent::Kind::HOVER_INVENTORY_ITEM;
            intent.id = *hovered_item;
            emit(std::move(intent));
        }
        if (captures(cursor_)) {
            if (!hovered_item && state_.command.hover != CommandHoverKind::NONE) {
                RoomUiIntent intent;
                intent.kind = RoomUiIntent::Kind::CLEAR_COMMAND_HOVER;
                emit(std::move(intent));
            }
            return InputResult::CONSUMED;
        }
        if (state_.command.hovered_object &&
            state_.command.hovered_object->kind == ObjectKind::INVENTORY_OBJECT) {
            RoomUiIntent intent;
            intent.kind = RoomUiIntent::Kind::CLEAR_COMMAND_HOVER;
            emit(std::move(intent));
        }
        return InputResult::PASS;
    }
    if (!available) {
        pressed_control_ = PressedControl::NONE;
        return InputResult::PASS;
    }

    if (state_.context_menu.open()) {
        if (input.secondary_release()) {
            RoomUiIntent intent;
            intent.kind = RoomUiIntent::Kind::DISMISS_CONTEXT_MENU;
            emit(std::move(intent));
        } else if (input.primary_release()) {
            RoomUiIntent intent;
            if (const auto action = context_action_at(input.position)) {
                intent.kind = RoomUiIntent::Kind::CHOOSE_CONTEXT_ACTION;
                intent.verb = *action;
            } else {
                intent.kind = RoomUiIntent::Kind::DISMISS_CONTEXT_MENU;
            }
            emit(std::move(intent));
        }
        return InputResult::CONSUMED;
    }

    if (input.kind == RoutedInputKind::PRIMARY_PRESSED) {
        if (state_.speech_active) {
            return captures(input.position) ? InputResult::CONSUMED : InputResult::PASS;
        }
        if (const auto item = inventory_item_at(input.position)) {
            pressed_item_ = *item;
            press_position_ = input.position;
            return InputResult::CONSUMED;
        }
        const sf::FloatRect bag = scale(config_.bag_button);
        const sf::FloatRect menu = scale(config_.menu_button);
        if (menu.contains(input.position)) {
            pressed_control_ = PressedControl::MENU;
            return InputResult::CONSUMED;
        }
        if (bag.contains(input.position)) {
            pressed_control_ = PressedControl::BAG;
            return InputResult::CONSUMED;
        }
        return captures(input.position) ? InputResult::CONSUMED : InputResult::PASS;
    }

    if (input.secondary_release()) {
        if (dragged_item_ || pressed_item_) {
            cancel_gesture();
            RoomUiIntent intent;
            intent.kind = RoomUiIntent::Kind::PREVIEW_INVENTORY_DROP;
            emit(std::move(intent));
            return InputResult::CONSUMED;
        }
        if (state_.inventory_open) {
            RoomUiIntent intent;
            intent.kind = RoomUiIntent::Kind::TOGGLE_INVENTORY;
            emit(std::move(intent));
            return InputResult::CONSUMED;
        }
        return InputResult::PASS;
    }

    if (!input.primary_release()) {
        return dragged_item_ || captures(input.position) ? InputResult::CONSUMED
                                                         : InputResult::PASS;
    }
    pressed_control_ = PressedControl::NONE;

    if (dragged_item_) {
        RoomUiIntent intent;
        intent.kind = RoomUiIntent::Kind::DROP_INVENTORY_ITEM;
        intent.id = *dragged_item_;
        intent.position = input.position;
        cancel_gesture();
        emit(std::move(intent));
        return InputResult::CONSUMED;
    }

    const sf::FloatRect bag = scale(config_.bag_button);
    const sf::FloatRect menu = scale(config_.menu_button);
    if (menu.contains(input.position)) {
        pressed_item_.reset();
        pressed_control_ = PressedControl::NONE;
        cursor_ = {-1.0f, -1.0f};
        RoomUiIntent intent;
        intent.kind = RoomUiIntent::Kind::OPEN_MENU;
        emit(std::move(intent));
        return InputResult::CONSUMED;
    }
    if (bag.contains(input.position)) {
        pressed_item_.reset();
        pressed_control_ = PressedControl::NONE;
        cursor_ = {-1.0f, -1.0f};
        if (state_.speech_active) {
            RoomUiIntent intent;
            intent.kind = RoomUiIntent::Kind::DISMISS_SPEECH;
            emit(std::move(intent));
        } else {
            RoomUiIntent intent;
            intent.kind = RoomUiIntent::Kind::TOGGLE_INVENTORY;
            emit(std::move(intent));
        }
        return InputResult::CONSUMED;
    }

    if (!state_.inventory_open) {
        pressed_item_.reset();
        pressed_control_ = PressedControl::NONE;
        return InputResult::PASS;
    }
    if (state_.speech_active) {
        pressed_item_.reset();
        RoomUiIntent intent;
        intent.kind = RoomUiIntent::Kind::DISMISS_SPEECH;
        emit(std::move(intent));
        return inventory_panel_bounds().contains(input.position) ? InputResult::CONSUMED
                                                                 : InputResult::PASS;
    }

    const sf::FloatRect previous = scale(config_.inventory.previous);
    const sf::FloatRect next = scale(config_.inventory.next);
    if (page_count() > 1 && previous.contains(input.position)) {
        pressed_item_.reset();
        if (current_page() > 0) {
            RoomUiIntent intent;
            intent.kind = RoomUiIntent::Kind::CHANGE_INVENTORY_PAGE;
            intent.index = current_page() - 1;
            emit(std::move(intent));
        }
        return InputResult::CONSUMED;
    }
    if (page_count() > 1 && next.contains(input.position)) {
        pressed_item_.reset();
        if (current_page() + 1 < page_count()) {
            RoomUiIntent intent;
            intent.kind = RoomUiIntent::Kind::CHANGE_INVENTORY_PAGE;
            intent.index = current_page() + 1;
            emit(std::move(intent));
        }
        return InputResult::CONSUMED;
    }

    const auto released_item = inventory_item_at(input.position);
    if (pressed_item_ && released_item && *released_item == *pressed_item_) {
        const std::string item_id = *pressed_item_;
        pressed_item_.reset();
        activate_item(item_id);
        return InputResult::CONSUMED;
    }
    pressed_item_.reset();
    pressed_control_ = PressedControl::NONE;
    return inventory_panel_bounds().contains(input.position) ? InputResult::CONSUMED
                                                             : InputResult::PASS;
}

void DirectRoomWidget::activate_item(const std::string& item_id) {
    RoomUiIntent intent;
    intent.kind = RoomUiIntent::Kind::INTERACT_INVENTORY_ITEM;
    intent.id = item_id;
    emit(std::move(intent));
}

void DirectRoomWidget::update(float dt) {
    // A short ease makes pointer entry feel intentional while remaining quick
    // enough for mouse use. It also gives touch a visible down-state through
    // `pressed_control_` without adding ornamental animation.
    const float amount = std::clamp(dt * 12.0f, 0.0f, 1.0f);
    const auto approach = [amount](float current, bool active) {
        const float target = active ? 1.0f : 0.0f;
        return current + (target - current) * amount;
    };
    const sf::FloatRect bag = scale(config_.bag_button);
    const sf::FloatRect menu = scale(config_.menu_button);
    bag_hover_ = approach(bag_hover_, bag.contains(cursor_));
    menu_hover_ = approach(menu_hover_, menu.contains(cursor_));
}

sf::FloatRect DirectRoomWidget::context_menu_bounds() const {
    if (!state_.context_menu.open()) {
        return {};
    }
    const sf::FloatRect cell =
        scale({0.0f, 0.0f, config_.context_menu.cell_size.x, config_.context_menu.cell_size.y});
    const float gap = scale_distance(config_.context_menu.gap);
    const float padding = scale_distance(config_.context_menu.padding);
    const float count = static_cast<float>(state_.context_menu.actions.size());
    const float width = padding * 2.0f + cell.width * count + gap * std::max(0.0f, count - 1.0f);
    const float height = padding * 2.0f + cell.height;
    const float pointer_gap = scale_distance(18.0f);
    float left = state_.context_menu.position.x - width / 2.0f;
    float top = state_.context_menu.position.y - height - pointer_gap;
    if (top < 0.0f) {
        top = state_.context_menu.position.y + pointer_gap;
    }
    left =
        std::clamp(left, 0.0f, std::max(0.0f, static_cast<float>(virtual_resolution_.x) - width));
    top = std::clamp(top, 0.0f, std::max(0.0f, static_cast<float>(virtual_resolution_.y) - height));
    return {left, top, width, height};
}

std::optional<Verb> DirectRoomWidget::context_action_at(sf::Vector2f point) const {
    const sf::FloatRect bounds = context_menu_bounds();
    if (!bounds.contains(point)) {
        return std::nullopt;
    }
    const sf::FloatRect cell =
        scale({0.0f, 0.0f, config_.context_menu.cell_size.x, config_.context_menu.cell_size.y});
    const float gap = scale_distance(config_.context_menu.gap);
    const float padding = scale_distance(config_.context_menu.padding);
    for (std::size_t i = 0; i < state_.context_menu.actions.size(); ++i) {
        const sf::FloatRect rect{bounds.left + padding + static_cast<float>(i) * (cell.width + gap),
                                 bounds.top + padding,
                                 cell.width,
                                 cell.height};
        if (rect.contains(point)) {
            return state_.context_menu.actions[i];
        }
    }
    return std::nullopt;
}

sf::FloatRect DirectRoomWidget::controls_bounds() const {
    const sf::FloatRect bag = scale(config_.bag_button);
    const sf::FloatRect menu = scale(config_.menu_button);
    const float left = std::min(bag.left, menu.left);
    const float top = std::min(bag.top, menu.top);
    const float right = std::max(bag.left + bag.width, menu.left + menu.width);
    const float bottom = std::max(bag.top + bag.height, menu.top + menu.height);
    return {left, top, right - left, bottom - top};
}

sf::FloatRect DirectRoomWidget::input_bounds() const {
    if (state_.context_menu.open()) {
        return {0.0f,
                0.0f,
                static_cast<float>(virtual_resolution_.x),
                static_cast<float>(virtual_resolution_.y)};
    }
    if (state_.inventory_open) {
        const sf::FloatRect controls = controls_bounds();
        const sf::FloatRect panel = inventory_panel_bounds();
        if (panel.width <= 0.0f || panel.height <= 0.0f) {
            return controls;
        }
        const float left = std::min(controls.left, panel.left);
        const float top = std::min(controls.top, panel.top);
        const float right = std::max(controls.left + controls.width, panel.left + panel.width);
        const float bottom = std::max(controls.top + controls.height, panel.top + panel.height);
        return {left, top, right - left, bottom - top};
    }
    return controls_bounds();
}

bool DirectRoomWidget::captures(sf::Vector2f point) const {
    if (state_.mode != RoomInteractionMode::COMMAND || !state_.widget_visible("direct")) {
        return false;
    }
    if (dragged_item_) {
        return true;
    }
    if (state_.context_menu.open()) {
        return true;
    }
    if (controls_bounds().contains(point)) {
        return true;
    }
    return state_.inventory_open && inventory_panel_bounds().contains(point);
}

bool DirectRoomWidget::contains_ui(sf::Vector2f point) const {
    if (state_.mode != RoomInteractionMode::COMMAND || !state_.widget_visible("direct")) {
        return false;
    }
    return controls_bounds().contains(point) ||
           (state_.inventory_open && inventory_panel_bounds().contains(point));
}

void DirectRoomWidget::cancel_gesture() {
    pressed_item_.reset();
    dragged_item_.reset();
    pressed_control_ = PressedControl::NONE;
}

void DirectRoomWidget::emit(RoomUiIntent intent) const {
    if (intent_sink_) {
        intent_sink_(intent);
    }
}

void DirectRoomWidget::draw_button(sf::RenderTarget& target,
                                   sf::FloatRect rect,
                                   float hover_amount,
                                   bool pressed) const {
    const float radius =
        std::min(rect.width, rect.height) * config_.style.button_radius_ratio;
    sf::CircleShape shape(radius, 48);
    shape.setOrigin(radius, radius);
    shape.setPosition(rect.left + rect.width / 2.0f, rect.top + rect.height / 2.0f);
    const float visual_scale = pressed ? 0.93f : 1.0f + 0.025f * hover_amount;
    shape.setScale(visual_scale, visual_scale);
    shape.setFillColor(
        mix_color(config_.style.button_background, config_.style.button_hover, hover_amount));
    shape.setOutlineColor(config_.style.button_border);
    shape.setOutlineThickness(
        config_.style.button_border.a == 0 ? 0.0f : scale_distance(config_.style.border_thickness));
    target.draw(shape);
}

void DirectRoomWidget::draw_bag_icon(sf::RenderTarget& target, sf::FloatRect rect) const {
    if (draw_atlas_icon(target, config_.icons.bag, rect)) {
        return;
    }
    sf::CircleShape handle(rect.width * 0.14f, 24);
    handle.setOrigin(rect.width * 0.14f, rect.width * 0.14f);
    handle.setScale(1.0f, 0.82f);
    handle.setPosition(rect.left + rect.width / 2.0f, rect.top + rect.height * 0.42f);
    handle.setFillColor(sf::Color::Transparent);
    handle.setOutlineColor(config_.style.text);
    handle.setOutlineThickness(std::max(1.0f, scale_distance(3.0f)));
    target.draw(handle);

    sf::ConvexShape body(6);
    body.setPoint(0, {rect.left + rect.width * 0.25f, rect.top + rect.height * 0.45f});
    body.setPoint(1, {rect.left + rect.width * 0.75f, rect.top + rect.height * 0.45f});
    body.setPoint(2, {rect.left + rect.width * 0.81f, rect.top + rect.height * 0.78f});
    body.setPoint(3, {rect.left + rect.width * 0.73f, rect.top + rect.height * 0.84f});
    body.setPoint(4, {rect.left + rect.width * 0.27f, rect.top + rect.height * 0.84f});
    body.setPoint(5, {rect.left + rect.width * 0.19f, rect.top + rect.height * 0.78f});
    body.setFillColor(config_.style.text);
    target.draw(body);
}

void DirectRoomWidget::draw_menu_icon(sf::RenderTarget& target, sf::FloatRect rect) const {
    if (draw_atlas_icon(target, config_.icons.menu, rect)) {
        return;
    }
    const float width = rect.width * 0.48f;
    const float height = std::max(2.0f, rect.height * 0.045f);
    for (int row = 0; row < 3; ++row) {
        sf::RectangleShape line({width, height});
        line.setPosition(rect.left + (rect.width - width) / 2.0f,
                         rect.top + rect.height * (0.34f + 0.16f * static_cast<float>(row)));
        line.setFillColor(config_.style.text);
        target.draw(line);
    }
}

void DirectRoomWidget::draw_control_label(sf::RenderTarget& target,
                                          sf::FloatRect rect,
                                          const std::string& key,
                                          float opacity) const {
    if (opacity <= 0.01f || key.empty() || !font_ || !model_.strings) {
        return;
    }
    const std::string label = model_.strings->ui_label(key);
    if (label.empty() || label.front() == '?') {
        return;
    }
    const auto alpha = [opacity](sf::Uint8 value) {
        return static_cast<sf::Uint8>(std::lround(
            static_cast<float>(value) * std::clamp(opacity, 0.0f, 1.0f)));
    };
    const unsigned size = std::max(14u, config_.style.text_size * 3u / 4u);
    sf::Text text(pac::core::utf8(label), *font_, size);
    text.setFillColor({config_.style.text.r,
                       config_.style.text.g,
                       config_.style.text.b,
                       alpha(config_.style.text.a)});
    text.setOutlineColor({0, 0, 0, alpha(225)});
    text.setOutlineThickness(scale_distance(1.0f));
    const sf::FloatRect bounds = text.getLocalBounds();
    const float padding_x = scale_distance(8.0f);
    const float padding_y = scale_distance(4.0f);
    const float width = bounds.width + padding_x * 2.0f;
    const float height = bounds.height + padding_y * 2.0f;
    const float left = std::clamp(rect.left + rect.width / 2.0f - width / 2.0f,
                                  0.0f,
                                  std::max(0.0f,
                                           static_cast<float>(virtual_resolution_.x) - width));
    const float top = std::max(0.0f, rect.top - height - scale_distance(8.0f));
    sf::RectangleShape plate({width, height});
    plate.setPosition(left, top);
    plate.setFillColor({9, 14, 17, alpha(205)});
    target.draw(plate);
    text.setPosition(left + padding_x - bounds.left, top + padding_y - bounds.top);
    target.draw(text);
}

bool DirectRoomWidget::draw_item_icon(sf::RenderTarget& target,
                                      const std::string& item_id,
                                      sf::FloatRect rect,
                                      sf::Uint8 opacity) const {
    const InventoryItem* item = state_.inventory.item(item_id);
    if (!item || !model_.resources) {
        return false;
    }
    try {
        const InventoryIconSheet& sheet = state_.inventory.icon_sheet();
        const sf::Texture* texture = nullptr;
        sf::IntRect source;
        if (sheet.production && item->icon_cell >= 0 && !sheet.sheet.empty()) {
            texture = &model_.resources->texture(sheet.sheet);
            const int columns = std::max(1, sheet.columns);
            const int rows = std::max(1, sheet.rows);
            const int width = static_cast<int>(texture->getSize().x) / columns;
            const int height = static_cast<int>(texture->getSize().y) / rows;
            source = {item->icon_cell % columns * width,
                      item->icon_cell / columns * height,
                      width,
                      height};
        } else if (!sheet.production && !item->icon.empty()) {
            texture = &model_.resources->texture(item->icon);
            source = {0,
                      0,
                      static_cast<int>(texture->getSize().x),
                      static_cast<int>(texture->getSize().y)};
        }
        if (!texture || source.width <= 0 || source.height <= 0) {
            return false;
        }
        const float fit = std::min(rect.width / static_cast<float>(source.width),
                                   rect.height / static_cast<float>(source.height));
        sf::Sprite sprite(*texture, source);
        sprite.setScale(fit, fit);
        sprite.setPosition(rect.left + (rect.width - static_cast<float>(source.width) * fit) / 2.0f,
                           rect.top +
                               (rect.height - static_cast<float>(source.height) * fit) / 2.0f);
        sprite.setColor({255, 255, 255, opacity});
        target.draw(sprite);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

void DirectRoomWidget::draw_inventory(sf::RenderTarget& target) const {
    const sf::FloatRect panel = inventory_panel_bounds();
    if (panel.width > 0.0f && panel.height > 0.0f) {
        sf::RectangleShape shadow({panel.width, panel.height});
        shadow.setPosition(panel.left + scale_distance(3.0f), panel.top + scale_distance(4.0f));
        shadow.setFillColor(
            {0, 0, 0, static_cast<sf::Uint8>(config_.style.panel_background.a / 2)});
        target.draw(shadow);

        sf::RectangleShape background({panel.width, panel.height});
        background.setPosition(panel.left, panel.top);
        background.setFillColor(config_.style.panel_background);
        background.setOutlineColor(config_.style.panel_border);
        background.setOutlineThickness(config_.style.panel_border.a == 0
                                           ? 0.0f
                                           : scale_distance(config_.style.border_thickness));
        target.draw(background);
    }

    const std::vector<std::string>& items = state_.inventory.list();
    const int first = current_page() * inventory_capacity();
    const std::vector<sf::FloatRect> slots = inventory_slots();
    for (std::size_t i = 0; i < slots.size(); ++i) {
        const sf::FloatRect slot = slots[i];
        const int index = first + static_cast<int>(i);
        const bool occupied = index < static_cast<int>(items.size());
        // A short, single-page inventory reads as a row of possessions rather
        // than a mostly empty developer grid. Paged inventories retain the
        // fixed capacity so their geometry does not jump between pages.
        if (!occupied && page_count() == 1) {
            continue;
        }
        const bool hovered = occupied && slot.contains(cursor_);
        if (hovered) {
            const float radius = std::min(slot.width, slot.height) * 0.44f;
            sf::CircleShape halo(radius, 48);
            halo.setOrigin(radius, radius);
            halo.setPosition(slot.left + slot.width / 2.0f, slot.top + slot.height / 2.0f);
            halo.setFillColor(config_.style.button_hover);
            target.draw(halo);
        }
        if (config_.style.slot_background.a > 0 || config_.style.slot_border.a > 0) {
            sf::RectangleShape box({slot.width, slot.height});
            box.setPosition(slot.left, slot.top);
            box.setFillColor(config_.style.slot_background);
            box.setOutlineColor(hovered ? config_.style.hover_border : config_.style.slot_border);
            box.setOutlineThickness(scale_distance(config_.style.border_thickness));
            target.draw(box);
        }
        if (!occupied) {
            continue;
        }
        const std::string& item_id = items[static_cast<std::size_t>(index)];
        const float inset = std::min(slot.width, slot.height) * 0.10f;
        const sf::FloatRect art{slot.left + inset,
                                slot.top + inset,
                                slot.width - 2.0f * inset,
                                slot.height - 2.0f * inset};
        if (!draw_item_icon(target, item_id, art) && font_) {
            const InventoryItem* item = state_.inventory.item(item_id);
            const std::string source = item ? item->name : item_id;
            const std::string name =
                model_.localized_name ? model_.localized_name(item_id, source) : source;
            const std::string glyph = name.empty() ? "?" : name.substr(0, 1);
            sf::Text fallback(pac::core::utf8(glyph), *font_, config_.style.text_size);
            fallback.setFillColor(config_.style.text);
            center_text(fallback, slot);
            target.draw(fallback);
        }
        if (hovered) {
            sf::RectangleShape underline({slot.width * 0.46f, scale_distance(2.0f)});
            underline.setPosition(slot.left + slot.width * 0.27f,
                                  slot.top + slot.height - scale_distance(2.0f));
            underline.setFillColor(config_.style.hover_border);
            target.draw(underline);
        }
        if (model_.has_notification && model_.has_notification(item_id)) {
            const float radius = std::clamp(std::min(slot.width, slot.height) * 0.08f, 5.0f, 9.0f);
            sf::CircleShape dot(radius, 24);
            dot.setOrigin(radius, radius);
            dot.setPosition(slot.left + slot.width - radius * 1.25f, slot.top + radius * 1.25f);
            dot.setFillColor(config_.style.notification);
            target.draw(dot);
        }
    }

    const bool has_previous = current_page() > 0;
    const bool has_next = current_page() + 1 < page_count();
    const auto arrow = [&](sf::FloatRect design_rect, int cell, const char* label, bool enabled) {
        const sf::FloatRect rect = scale(design_rect);
        draw_button(target, rect, enabled && rect.contains(cursor_) ? 1.0f : 0.0f, false);
        if (draw_atlas_icon(target, cell, rect, enabled ? 255 : 105)) {
            return;
        }
        if (font_) {
            sf::Text text(label, *font_, config_.style.text_size);
            text.setFillColor(enabled ? config_.style.text : config_.style.disabled_text);
            center_text(text, rect);
            target.draw(text);
        }
    };
    if (page_count() > 1) {
        arrow(config_.inventory.previous, config_.icons.previous, "<", has_previous);
        arrow(config_.inventory.next, config_.icons.next, ">", has_next);
    }
}

void DirectRoomWidget::draw_action_text(sf::RenderTarget& target) const {
    const std::string& text = state_.action_text;
    if (text.empty() || !font_) {
        return;
    }
    sf::FloatRect rect = scale(config_.action_text);
    sf::Text label(pac::core::utf8(text), *font_, config_.style.action_text_size);
    label.setFillColor(config_.style.text);
    label.setOutlineColor(config_.style.action_outline);
    label.setOutlineThickness(scale_distance(config_.style.action_outline_thickness));
    if (config_.action_follows_cursor && cursor_.x >= 0.0f && cursor_.y >= 0.0f) {
        const sf::Vector2f offset{scale_distance(config_.action_text_offset.x),
                                  scale_distance(config_.action_text_offset.y)};
        const sf::FloatRect bounds = label.getLocalBounds();
        rect.left =
            std::clamp(cursor_.x + offset.x,
                       0.0f,
                       std::max(0.0f, static_cast<float>(virtual_resolution_.x) - bounds.width));
        rect.top =
            std::clamp(cursor_.y + offset.y,
                       0.0f,
                       std::max(0.0f, static_cast<float>(virtual_resolution_.y) - bounds.height));
        rect.width = bounds.width;
        rect.height = bounds.height;
        label.setPosition(rect.left - bounds.left, rect.top - bounds.top);
    } else {
        center_text(label, rect);
    }
    if (config_.style.action_background.a > 0) {
        sf::RectangleShape background({rect.width, rect.height});
        background.setPosition(rect.left, rect.top);
        background.setFillColor(config_.style.action_background);
        target.draw(background);
    }
    target.draw(label);
}

bool DirectRoomWidget::draw_atlas_icon(sf::RenderTarget& target,
                                       int cell,
                                       sf::FloatRect rect,
                                       sf::Uint8 opacity) const {
    if (cell < 0 || config_.icons.sheet.empty() || !model_.resources) {
        return false;
    }
    try {
        const sf::Texture& texture = model_.resources->texture(config_.icons.sheet);
        const int columns = std::max(1, config_.icons.columns);
        const int rows = std::max(1, config_.icons.rows);
        const int width = static_cast<int>(texture.getSize().x) / columns;
        const int height = static_cast<int>(texture.getSize().y) / rows;
        if (width <= 0 || height <= 0 || cell >= columns * rows) {
            return false;
        }
        const sf::IntRect source{cell % columns * width, cell / columns * height, width, height};
        const float fit = std::min(rect.width / static_cast<float>(width),
                                   rect.height / static_cast<float>(height));
        sf::Sprite sprite(texture, source);
        sprite.setScale(fit, fit);
        sprite.setPosition(rect.left + (rect.width - static_cast<float>(width) * fit) / 2.0f,
                           rect.top + (rect.height - static_cast<float>(height) * fit) / 2.0f);
        sprite.setColor({255, 255, 255, opacity});
        target.draw(sprite);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

int DirectRoomWidget::action_icon_cell(Verb verb) const {
    switch (verb) {
    case Verb::LOOK_AT:
        return config_.icons.look_at;
    case Verb::TALK_TO:
        return config_.icons.talk_to;
    case Verb::PICK_UP:
        return config_.icons.pick_up;
    case Verb::USE:
        return config_.icons.use;
    case Verb::GIVE:
        return config_.icons.give;
    case Verb::OPEN:
        return config_.icons.open;
    case Verb::CLOSE:
        return config_.icons.close;
    case Verb::PUSH:
        return config_.icons.push;
    case Verb::PULL:
        return config_.icons.pull;
    }
    return -1;
}

void DirectRoomWidget::draw_action_icon(sf::RenderTarget& target,
                                        Verb verb,
                                        sf::FloatRect rect,
                                        sf::Color color) const {
    if (draw_atlas_icon(target, action_icon_cell(verb), rect)) {
        return;
    }
    const float stroke = std::max(1.0f, scale_distance(2.0f));
    if (verb == Verb::LOOK_AT) {
        sf::CircleShape eye(rect.width * 0.22f, 32);
        eye.setOrigin(rect.width * 0.22f, rect.width * 0.22f);
        eye.setScale(1.65f, 0.82f);
        eye.setPosition(rect.left + rect.width / 2.0f, rect.top + rect.height * 0.34f);
        eye.setFillColor(sf::Color::Transparent);
        eye.setOutlineColor(color);
        eye.setOutlineThickness(stroke);
        target.draw(eye);
        sf::CircleShape pupil(rect.width * 0.075f, 24);
        pupil.setOrigin(rect.width * 0.075f, rect.width * 0.075f);
        pupil.setPosition(rect.left + rect.width / 2.0f, rect.top + rect.height * 0.34f);
        pupil.setFillColor(color);
        target.draw(pupil);
        return;
    }
    if (verb == Verb::TALK_TO) {
        sf::ConvexShape mouth(6);
        mouth.setPoint(0, {rect.left + rect.width * 0.23f, rect.top + rect.height * 0.22f});
        mouth.setPoint(1, {rect.left + rect.width * 0.77f, rect.top + rect.height * 0.22f});
        mouth.setPoint(2, {rect.left + rect.width * 0.69f, rect.top + rect.height * 0.49f});
        mouth.setPoint(3, {rect.left + rect.width * 0.50f, rect.top + rect.height * 0.56f});
        mouth.setPoint(4, {rect.left + rect.width * 0.31f, rect.top + rect.height * 0.49f});
        mouth.setPoint(5, {rect.left + rect.width * 0.23f, rect.top + rect.height * 0.22f});
        mouth.setFillColor(color);
        target.draw(mouth);
        return;
    }
    // The direct UI deliberately groups open/close/push/pull/pick-up under one
    // hand family. The localized label below disambiguates actions that share it.
    sf::ConvexShape hand(7);
    hand.setPoint(0, {rect.left + rect.width * 0.34f, rect.top + rect.height * 0.53f});
    hand.setPoint(1, {rect.left + rect.width * 0.34f, rect.top + rect.height * 0.23f});
    hand.setPoint(2, {rect.left + rect.width * 0.45f, rect.top + rect.height * 0.23f});
    hand.setPoint(3, {rect.left + rect.width * 0.48f, rect.top + rect.height * 0.39f});
    hand.setPoint(4, {rect.left + rect.width * 0.72f, rect.top + rect.height * 0.39f});
    hand.setPoint(5, {rect.left + rect.width * 0.67f, rect.top + rect.height * 0.62f});
    hand.setPoint(6, {rect.left + rect.width * 0.43f, rect.top + rect.height * 0.65f});
    hand.setFillColor(color);
    target.draw(hand);
}

void DirectRoomWidget::draw_context_menu(sf::RenderTarget& target) const {
    if (!state_.context_menu.open()) {
        return;
    }
    const sf::FloatRect bounds = context_menu_bounds();
    if (config_.style.panel_background.a > 0 || config_.style.panel_border.a > 0) {
        sf::RectangleShape shadow({bounds.width, bounds.height});
        shadow.setPosition(bounds.left + scale_distance(3.0f), bounds.top + scale_distance(4.0f));
        shadow.setFillColor(
            {0, 0, 0, static_cast<sf::Uint8>(config_.style.panel_background.a / 2)});
        target.draw(shadow);
        sf::RectangleShape background({bounds.width, bounds.height});
        background.setPosition(bounds.left, bounds.top);
        background.setFillColor(config_.style.panel_background);
        background.setOutlineColor(config_.style.panel_border);
        background.setOutlineThickness(config_.style.panel_border.a == 0
                                           ? 0.0f
                                           : scale_distance(config_.style.border_thickness));
        target.draw(background);
    }

    const sf::FloatRect cell =
        scale({0.0f, 0.0f, config_.context_menu.cell_size.x, config_.context_menu.cell_size.y});
    const float gap = scale_distance(config_.context_menu.gap);
    const float padding = scale_distance(config_.context_menu.padding);
    for (std::size_t i = 0; i < state_.context_menu.actions.size(); ++i) {
        const Verb verb = state_.context_menu.actions[i];
        const sf::FloatRect item{bounds.left + padding + static_cast<float>(i) * (cell.width + gap),
                                 bounds.top + padding,
                                 cell.width,
                                 cell.height};
        const bool hovered = item.contains(cursor_);
        if (hovered) {
            const float radius = std::min(item.width, item.height) * 0.34f;
            sf::CircleShape halo(radius, 48);
            halo.setOrigin(radius, radius);
            halo.setPosition(item.left + item.width / 2.0f, item.top + item.height * 0.34f);
            halo.setFillColor(config_.style.button_hover);
            target.draw(halo);
        }
        const sf::FloatRect icon{item.left,
                                 item.top,
                                 item.width,
                                 item.height * (font_ ? 0.68f : 1.0f)};
        draw_action_icon(target,
                         verb,
                         icon,
                         hovered ? config_.style.hover_border : config_.style.text);
        if (font_ && model_.strings) {
            const std::string label = model_.strings->verb_label(std::string(verb_id(verb)));
            const unsigned size = std::max(10u, config_.style.text_size * 3u / 5u);
            sf::Text text(pac::core::utf8(label), *font_, size);
            text.setFillColor(hovered ? config_.style.hover_border : config_.style.text);
            center_text(
                text,
                {item.left, item.top + item.height * 0.64f, item.width, item.height * 0.32f});
            target.draw(text);
        }
    }
}

void DirectRoomWidget::draw(sf::RenderTarget& target) const {
    if (state_.mode != RoomInteractionMode::COMMAND || !state_.widget_visible("direct")) {
        return;
    }
    if (state_.inventory_open) {
        draw_inventory(target);
    }

    const sf::FloatRect bag = scale(config_.bag_button);
    const sf::FloatRect menu = scale(config_.menu_button);
    const bool bag_pressed = pressed_control_ == PressedControl::BAG;
    const bool menu_pressed = pressed_control_ == PressedControl::MENU;
    draw_button(target, bag, bag_hover_, bag_pressed);
    draw_bag_icon(target, scale_from_center(bag, bag_pressed ? 0.93f : 1.0f));
    draw_button(target, menu, menu_hover_, menu_pressed);
    draw_menu_icon(target, scale_from_center(menu, menu_pressed ? 0.93f : 1.0f));
    if (bag_hover_ >= menu_hover_) {
        draw_control_label(target, bag, config_.bag_label, bag_hover_);
    } else {
        draw_control_label(target, menu, config_.menu_label, menu_hover_);
    }
    draw_action_text(target);
    draw_context_menu(target);

    if (dragged_item_) {
        const float size = scale_distance(82.0f);
        const sf::FloatRect ghost{cursor_.x - size / 2.0f, cursor_.y - size / 2.0f, size, size};
        (void) draw_item_icon(target, *dragged_item_, ghost, 220);
    }
}

} // namespace pac::pnc
