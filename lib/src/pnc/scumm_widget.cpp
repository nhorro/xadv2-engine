#include "engine/pnc/scumm_widget.hpp"

#include "engine/core/strings.hpp"
#include "engine/pnc/inventory.hpp"

#include <utility>

namespace pac::pnc {

ScummWidget::ScummWidget(ScummPanel panel,
                         ScummWidgetModel model,
                         RoomUiIntentSink intent_sink,
                         WidgetTransition transition)
    : panel_(std::move(panel)), model_(std::move(model)), intent_sink_(std::move(intent_sink)),
      presentation_(true, transition) {
    presentation_.set_bounds(panel_.bounds());
}

void ScummWidget::connect(RoomUiStateStream& stream) {
    state_subscription_ = stream.subscribe([this](const RoomUiState& state) {
        state_ = state;
        const bool should_show =
            state.mode == RoomInteractionMode::COMMAND && state.widget_visible("scumm");
        should_show ? presentation_.show() : presentation_.hide();
    });
}

InputResult ScummWidget::handle(const RoutedInput& input) {
    if (input.moved()) {
        cursor_ = input.position;
    }
    if (!captures(input.position)) {
        return InputResult::PASS;
    }

    if (input.moved()) {
        if (state_.mode == RoomInteractionMode::COMMAND) {
            const PanelIntent intent =
                panel_.click(input.position, state_.inventory, state_.command);
            if (intent.kind == PanelIntent::Kind::SELECT_VERB) {
                RoomUiIntent out;
                out.kind = RoomUiIntent::Kind::HOVER_VERB;
                out.verb = intent.verb;
                emit(std::move(out));
            } else if (intent.kind == PanelIntent::Kind::CLICK_INVENTORY) {
                RoomUiIntent out;
                out.kind = RoomUiIntent::Kind::HOVER_INVENTORY_ITEM;
                out.id = intent.item_id;
                emit(std::move(out));
            } else {
                RoomUiIntent out;
                out.kind = RoomUiIntent::Kind::CLEAR_COMMAND_HOVER;
                emit(std::move(out));
            }
        } else {
            RoomUiIntent out;
            out.kind = RoomUiIntent::Kind::CLEAR_COMMAND_HOVER;
            emit(std::move(out));
        }
        return InputResult::CONSUMED;
    }

    if (input.secondary_release()) {
        if (state_.mode == RoomInteractionMode::COMMAND) {
            RoomUiIntent out;
            out.kind = RoomUiIntent::Kind::CANCEL_COMMAND;
            emit(std::move(out));
        }
        return InputResult::CONSUMED;
    }
    if (!input.primary_release()) {
        return InputResult::CONSUMED;
    }

    if (state_.mode != RoomInteractionMode::COMMAND) {
        return InputResult::CONSUMED;
    }

    const PanelIntent intent = panel_.click(input.position, state_.inventory, state_.command);
    const bool systemic = intent.kind == PanelIntent::Kind::OPEN_SETTINGS ||
                          intent.kind == PanelIntent::Kind::OPEN_MENU ||
                          intent.kind == PanelIntent::Kind::PUSH_SCENE;
    if (state_.speech_active && !systemic) {
        RoomUiIntent out;
        out.kind = RoomUiIntent::Kind::DISMISS_SPEECH;
        emit(std::move(out));
    } else {
        emit_panel_intent(intent);
    }
    return InputResult::CONSUMED;
}

sf::FloatRect ScummWidget::input_bounds() const {
    return presentation_.bounds();
}

bool ScummWidget::captures(sf::Vector2f point) const {
    return presentation_.captures_input() && input_bounds().contains(point);
}

void ScummWidget::update(float dt) {
    presentation_.update(dt);
}

void ScummWidget::draw(sf::RenderTarget& target) const {
    if (!presentation_.rendered() || !model_.strings) {
        return;
    }
    surface_.draw(target, presentation_, panel_.bounds(), [this](sf::RenderTarget& surface) {
        panel_.draw(surface,
                    *model_.strings,
                    state_.inventory,
                    state_.command,
                    cursor_,
                    model_.evidence ? model_.evidence() : EvidenceProgress{},
                    model_.has_notification,
                    model_.localized_name);
    });
}

void ScummWidget::emit_panel_intent(const PanelIntent& intent) {
    RoomUiIntent out;
    switch (intent.kind) {
    case PanelIntent::Kind::NONE:
        return;
    case PanelIntent::Kind::SELECT_VERB:
        out.kind = RoomUiIntent::Kind::SELECT_VERB;
        out.verb = intent.verb;
        break;
    case PanelIntent::Kind::CLICK_INVENTORY:
        out.kind = RoomUiIntent::Kind::SELECT_INVENTORY_ITEM;
        out.id = intent.item_id;
        break;
    case PanelIntent::Kind::CHANGE_INVENTORY_PAGE:
        out.kind = RoomUiIntent::Kind::CHANGE_INVENTORY_PAGE;
        out.index = intent.page_index;
        break;
    case PanelIntent::Kind::OPEN_SETTINGS:
        out.kind = RoomUiIntent::Kind::OPEN_SETTINGS;
        break;
    case PanelIntent::Kind::OPEN_MENU:
        out.kind = RoomUiIntent::Kind::OPEN_MENU;
        break;
    case PanelIntent::Kind::PUSH_SCENE:
        out.kind = RoomUiIntent::Kind::PUSH_SCENE;
        out.scene = intent.scene;
        break;
    case PanelIntent::Kind::OPEN_NOTEBOOK:
        out.kind = RoomUiIntent::Kind::OPEN_NOTEBOOK;
        out.id = intent.tab;
        out.scene = panel_.config().notebook.scene;
        out.state_key = panel_.config().notebook.tab_state;
        break;
    }
    emit(std::move(out));
}

void ScummWidget::emit(RoomUiIntent intent) {
    if (intent_sink_) {
        intent_sink_(intent);
    }
}

} // namespace pac::pnc
