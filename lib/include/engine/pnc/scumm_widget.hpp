#pragma once

#include "engine/pnc/room_input_router.hpp"
#include "engine/pnc/room_ui_intent.hpp"
#include "engine/pnc/room_ui_state.hpp"
#include "engine/pnc/scumm_panel.hpp"
#include "engine/pnc/widget_presentation.hpp"

#include <functional>
#include <string>
#include <vector>

namespace pac::core {
class Strings;
}

namespace pac::pnc {

struct ScummWidgetModel {
    const pac::core::Strings* strings = nullptr;
    std::function<EvidenceProgress()> evidence;
    InventoryNotificationQuery has_notification;
    InventoryNameQuery localized_name;
};

/// Input/render adapter around the existing ScummPanel. It owns panel hit
/// testing, captures its full configured rectangle, and emits generic room UI
/// intents instead of executing gameplay or scene actions itself.
class ScummWidget final : public UiWidget {
public:
    ScummWidget(ScummPanel panel,
                ScummWidgetModel model,
                RoomUiIntentSink intent_sink,
                WidgetTransition transition = {});
    void connect(RoomUiStateStream& stream);

    [[nodiscard]] InputResult handle(const RoutedInput& input) override;
    [[nodiscard]] sf::FloatRect input_bounds() const override;
    [[nodiscard]] bool captures(sf::Vector2f point) const override;
    void update(float dt) override;
    void draw(sf::RenderTarget& target) const override;

    void set_visible(bool visible) { visible ? presentation_.show() : presentation_.hide(); }
    void set_position(sf::Vector2f position) { presentation_.set_position(position); }
    void set_size(sf::Vector2f size) { presentation_.set_size(size); }
    void set_translation(sf::Vector2f translation) { presentation_.set_translation(translation); }
    void set_opacity(float opacity) { presentation_.set_opacity(opacity); }
    void set_input_enabled(bool enabled) { presentation_.set_input_enabled(enabled); }
    [[nodiscard]] bool visible() const { return presentation_.rendered(); }
    [[nodiscard]] WidgetVisibility visibility() const { return presentation_.visibility(); }

private:
    void emit_panel_intent(const PanelIntent& intent);
    void emit(RoomUiIntent intent);

    ScummPanel panel_;
    ScummWidgetModel model_;
    RoomUiIntentSink intent_sink_;
    RoomUiState state_;
    RoomUiStateStream::Subscription state_subscription_;
    sf::Vector2f cursor_{-1.0f, -1.0f};
    WidgetPresentation presentation_;
    mutable WidgetSurface surface_;
};

} // namespace pac::pnc
