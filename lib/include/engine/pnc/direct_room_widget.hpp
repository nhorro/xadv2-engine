#pragma once

#include "engine/pnc/direct_room_ui_config.hpp"
#include "engine/pnc/room_input_router.hpp"
#include "engine/pnc/room_ui_intent.hpp"
#include "engine/pnc/room_ui_state.hpp"
#include "engine/pnc/scumm_panel.hpp" // inventory name/notification query aliases

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
class ResourceCache;
class Strings;
} // namespace pac::core

namespace pac::pnc {

struct DirectRoomWidgetModel {
    const pac::core::Strings* strings = nullptr;
    pac::core::ResourceCache* resources = nullptr;
    InventoryNotificationQuery has_notification;
    InventoryNameQuery localized_name;
};

/// Room-only direct composer UI: persistent bag/menu buttons, a compact paged
/// inventory, item activation, drag capture, alternate-action menu, and
/// transient action text. It emits semantic intents and never dispatches Lua.
class DirectRoomWidget final : public UiWidget {
public:
    DirectRoomWidget(DirectRoomUiConfig config,
                     sf::Vector2u virtual_resolution,
                     const sf::Font* font,
                     DirectRoomWidgetModel model,
                     RoomUiIntentSink intent_sink);

    void connect(RoomUiStateStream& stream);
    [[nodiscard]] InputResult handle(const RoutedInput& input) override;
    [[nodiscard]] sf::FloatRect input_bounds() const override;
    [[nodiscard]] bool captures(sf::Vector2f point) const override;
    void update(float dt) override;
    void draw(sf::RenderTarget& target) const override;

    [[nodiscard]] std::optional<std::string> inventory_item_at(sf::Vector2f point) const;
    [[nodiscard]] bool dragging() const { return dragged_item_.has_value(); }
    /// True over persistent controls or the open inventory panel. Unlike
    /// `captures`, this deliberately ignores an active drag, so RoomScene can
    /// still hit-test the world below the dragged item.
    [[nodiscard]] bool contains_ui(sf::Vector2f point) const;
    [[nodiscard]] sf::FloatRect controls_bounds() const;
    [[nodiscard]] sf::FloatRect context_menu_bounds() const;
    [[nodiscard]] std::optional<Verb> context_action_at(sf::Vector2f point) const;
    void cancel_gesture();

private:
    [[nodiscard]] sf::FloatRect scale(sf::FloatRect rect) const;
    [[nodiscard]] float scale_distance(float value) const;
    [[nodiscard]] std::vector<sf::FloatRect> inventory_slots() const;
    [[nodiscard]] sf::FloatRect inventory_panel_bounds() const;
    [[nodiscard]] int inventory_capacity() const;
    [[nodiscard]] int page_count() const;
    [[nodiscard]] int current_page() const;
    void activate_item(const std::string& item_id);
    void emit(RoomUiIntent intent) const;
    void draw_button(sf::RenderTarget& target,
                     sf::FloatRect rect,
                     float hover_amount,
                     bool pressed) const;
    void draw_bag_icon(sf::RenderTarget& target, sf::FloatRect rect) const;
    void draw_menu_icon(sf::RenderTarget& target, sf::FloatRect rect) const;
    void draw_control_label(sf::RenderTarget& target,
                            sf::FloatRect rect,
                            const std::string& key,
                            float opacity) const;
    void draw_inventory(sf::RenderTarget& target) const;
    bool draw_item_icon(sf::RenderTarget& target,
                        const std::string& item_id,
                        sf::FloatRect rect,
                        sf::Uint8 opacity = 255) const;
    void draw_action_text(sf::RenderTarget& target) const;
    void draw_context_menu(sf::RenderTarget& target) const;
    void draw_action_icon(sf::RenderTarget& target,
                          Verb verb,
                          sf::FloatRect rect,
                          sf::Color color) const;
    [[nodiscard]] bool draw_atlas_icon(sf::RenderTarget& target,
                                       int cell,
                                       sf::FloatRect rect,
                                       sf::Uint8 opacity = 255) const;
    [[nodiscard]] int action_icon_cell(Verb verb) const;

    DirectRoomUiConfig config_;
    sf::Vector2u virtual_resolution_;
    const sf::Font* font_ = nullptr;
    DirectRoomWidgetModel model_;
    RoomUiIntentSink intent_sink_;
    RoomUiState state_;
    RoomUiStateStream::Subscription subscription_;
    sf::Vector2f cursor_{-1.0f, -1.0f};
    std::optional<std::string> pressed_item_;
    sf::Vector2f press_position_{};
    std::optional<std::string> dragged_item_;
    enum class PressedControl { NONE, BAG, MENU };
    PressedControl pressed_control_ = PressedControl::NONE;
    float bag_hover_ = 0.0f;
    float menu_hover_ = 0.0f;
};

} // namespace pac::pnc
