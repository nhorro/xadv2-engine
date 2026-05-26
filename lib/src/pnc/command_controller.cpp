#include "engine/pnc/command_controller.hpp"

#include <algorithm>

namespace pac::pnc {

namespace {

bool affordance_ok(const CommandOperandInfo& info, Verb verb) {
    if (verb == Verb::LOOK_AT) {
        return true;
    }
    const std::string id(verb_id(verb));
    return std::find(info.affordances.begin(), info.affordances.end(), id) !=
           info.affordances.end();
}

} // namespace

CommandController::CommandController(const CommandControllerHost& host) : host_(host) {
    refresh_state();
}

void CommandController::reset() {
    builder_.cancel();
    hover_ = {};
    inventory_page_ = 0;
    refresh_state();
}

void CommandController::cancel() {
    builder_.cancel();
    refresh_state();
}

void CommandController::finish_execution() {
    builder_.finish_execution();
    refresh_state();
}

void CommandController::on_verb_selected(const VerbSelected& event) {
    builder_.select_verb(event.verb);
    refresh_state();
}

std::optional<Command>
CommandController::on_inventory_item_selected(const InventoryItemSelected& event) {
    return select_object({ObjectKind::INVENTORY_OBJECT, event.item_id});
}

void CommandController::on_inventory_page_changed(const InventoryPageChanged& event) {
    inventory_page_ = event.page_index;
    refresh_state();
}

void CommandController::on_verb_hovered(Verb verb) {
    hover_.kind = HoverKind::VERB;
    hover_.verb = verb;
    hover_.object = {};
    refresh_state();
}

void CommandController::on_inventory_item_hovered(const std::string& item_id) {
    hover_.kind = HoverKind::OBJECT;
    hover_.object = {ObjectKind::INVENTORY_OBJECT, item_id};
    refresh_state();
}

void CommandController::on_hotspot_hovered(const HotspotHovered& event) {
    hover_.kind = HoverKind::OBJECT;
    hover_.object = {ObjectKind::ROOM_OBJECT, event.hotspot_id};
    refresh_state();
}

void CommandController::on_hotspot_left(const HotspotLeft& event) {
    if (hover_.kind == HoverKind::OBJECT && hover_.object.kind == ObjectKind::ROOM_OBJECT &&
        hover_.object.id == event.hotspot_id) {
        clear_hover();
    }
}

void CommandController::on_walkable_hovered() {
    hover_.kind = HoverKind::WALKABLE;
    hover_.object = {};
    refresh_state();
}

void CommandController::clear_hover() {
    hover_ = {};
    refresh_state();
}

std::optional<Command> CommandController::on_hotspot_clicked(const HotspotClicked& event) {
    return select_object({ObjectKind::ROOM_OBJECT, event.hotspot_id});
}

std::optional<Command> CommandController::select_object(const ObjectRef& object) {
    const CommandOperandInfo info = host_.resolve_command_operand(object);
    if (builder_.state() == CommandBuilder::State::IDLE) {
        builder_.select_verb(info.default_verb);
    }
    const std::optional<Verb> verb = builder_.verb();
    if (!verb) {
        refresh_state();
        return std::nullopt;
    }

    builder_.provide_object(object, affordance_ok(info, *verb), info.combinable);
    return take_ready_command();
}

std::optional<Command> CommandController::take_ready_command() {
    if (builder_.state() != CommandBuilder::State::COMMAND_READY) {
        refresh_state();
        return std::nullopt;
    }
    std::optional<Command> command = builder_.take_ready();
    refresh_state();
    return command;
}

std::string CommandController::command_preview() const {
    const std::optional<Verb> verb = builder_.verb();
    if (!verb) {
        return {};
    }
    std::string text = host_.command_verb_label(*verb);
    if (builder_.param1()) {
        text += " " + host_.resolve_command_operand(*builder_.param1()).name;
        if (*verb == Verb::USE || *verb == Verb::GIVE) {
            text += " " + host_.command_connector_label(*verb);
        }
    }
    if (builder_.param2()) {
        text += " " + host_.resolve_command_operand(*builder_.param2()).name;
    }
    return text;
}

bool CommandController::would_accept_hovered(const ObjectRef& object,
                                             const CommandOperandInfo& info) const {
    const std::optional<Verb> verb = builder_.verb();
    return verb && builder_.would_accept(object, affordance_ok(info, *verb));
}

std::string CommandController::preview_text() const {
    if (builder_.state() == CommandBuilder::State::IDLE) {
        switch (hover_.kind) {
        case HoverKind::VERB:
            return host_.command_verb_label(hover_.verb);
        case HoverKind::OBJECT:
            return host_.resolve_command_operand(hover_.object).name;
        case HoverKind::WALKABLE:
            return host_.command_walk_label();
        case HoverKind::NONE:
            break;
        }
        return {};
    }

    std::string base = command_preview();
    if (hover_.kind == HoverKind::OBJECT) {
        const CommandOperandInfo info = host_.resolve_command_operand(hover_.object);
        if (would_accept_hovered(hover_.object, info)) {
            base += " " + info.name;
        }
    }
    return base;
}

void CommandController::refresh_state() {
    state_.builder_state = builder_.state();
    state_.selected_verb = builder_.verb();
    state_.param1 = builder_.param1();
    state_.param2 = builder_.param2();
    state_.selected_inventory_item_id.reset();
    if (state_.param1 && state_.param1->kind == ObjectKind::INVENTORY_OBJECT) {
        state_.selected_inventory_item_id = state_.param1->id;
    }
    state_.inventory_page_index = inventory_page_;
    switch (hover_.kind) {
    case HoverKind::VERB:
        state_.hover = CommandHoverKind::VERB;
        break;
    case HoverKind::OBJECT:
        state_.hover = CommandHoverKind::OBJECT;
        break;
    case HoverKind::WALKABLE:
        state_.hover = CommandHoverKind::WALKABLE;
        break;
    case HoverKind::NONE:
        state_.hover = CommandHoverKind::NONE;
        break;
    }
    state_.hovered_verb.reset();
    state_.hovered_object.reset();
    if (hover_.kind == HoverKind::VERB) {
        state_.hovered_verb = hover_.verb;
    } else if (hover_.kind == HoverKind::OBJECT) {
        state_.hovered_object = hover_.object;
    }
    state_.preview_text = preview_text();
}

} // namespace pac::pnc
