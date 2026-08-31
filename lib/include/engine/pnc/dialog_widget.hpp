#pragma once

#include "engine/pnc/room_input_router.hpp"
#include "engine/pnc/room_ui_intent.hpp"
#include "engine/pnc/room_ui_state.hpp"
#include "engine/pnc/widget_presentation.hpp"

#include <SFML/Graphics/Color.hpp>
#include <SFML/Graphics/Rect.hpp>
#include <SFML/System/Vector2.hpp>

#include <functional>
#include <string>
#include <vector>

namespace sf {
class Font;
class RenderTarget;
}

namespace pac::pnc {

struct DialogPageLayout {
    struct Row {
        int option_index = 0;
        std::vector<std::string> lines;
        sf::FloatRect rect;
    };
    std::vector<Row> rows;
    int page_count = 1;
    int page_index = 0;
    bool has_prev = false;
    bool has_next = false;
    sf::FloatRect prev_arrow;
    sf::FloatRect next_arrow;
    sf::FloatRect box;
};

DialogPageLayout layout_dialog_options(const std::vector<std::string>& labels,
                                       int page_index,
                                       sf::FloatRect area,
                                       float line_height,
                                       float option_gap,
                                       float arrow_size,
                                       const std::function<float(const std::string&)>& measure);

struct DialogWidgetConfig {
    sf::Vector2f design_size{1280.0f, 720.0f};
    float min_width = 320.0f;
    float max_width = 820.0f;
    float max_height = 300.0f;
    WidgetPlacement placement{{0.5f, 1.0f}, WidgetAnchor::BOTTOM_CENTER, {0.0f, -24.0f}};
    WidgetTransition transition;
    float opacity = 1.0f;
    sf::FloatRect padding{20.0f, 14.0f, 20.0f, 14.0f}; // l, t, r, b
    float option_gap = 8.0f;
    std::string font;
    unsigned font_size = 19;
    sf::Color text{225, 209, 171};
    sf::Color hover_text{43, 183, 214};
    float text_outline_thickness = 0.0f;
    sf::Color text_outline{0, 0, 0, 210};
    sf::Color background{21, 22, 23, 210};
    sf::Color border{225, 209, 171, 150};
    float border_thickness = 1.0f;
};

[[nodiscard]] DialogWidgetConfig
parse_dialog_widget_config(const std::string& yaml_text, const std::string& logical_path = {});

/// Independent modal choice widget. It subscribes to room UI state, owns only
/// screen-space presentation/input, and emits generic intents. It has no room,
/// command, inventory, SCUMM-panel, or Lua dependency.
class DialogWidget final : public UiWidget {
public:
    DialogWidget(DialogWidgetConfig config,
                 sf::Vector2u runtime_size,
                 const sf::Font* font,
                 RoomUiIntentSink intent_sink);
    void connect(RoomUiStateStream& stream);

    [[nodiscard]] InputResult handle(const RoutedInput& input) override;
    [[nodiscard]] sf::FloatRect input_bounds() const override;
    [[nodiscard]] bool captures(sf::Vector2f point) const override;
    void update(float dt) override;
    void draw(sf::RenderTarget& target) const override;

    [[nodiscard]] DialogPageLayout current_layout() const;

private:
    void emit(RoomUiIntent intent);

    DialogWidgetConfig config_;
    sf::Vector2u runtime_size_;
    const sf::Font* font_ = nullptr;
    RoomUiIntentSink intent_sink_;
    RoomUiState state_;
    RoomUiStateStream::Subscription state_subscription_;
    sf::Vector2f cursor_{-1.0f, -1.0f};
    bool dialog_active_ = false;
    WidgetPresentation presentation_;
    mutable WidgetSurface surface_;
};

} // namespace pac::pnc
