#include "engine/pnc/direct_action.hpp"

#include <algorithm>
#include <string>

namespace pac::pnc {

namespace {

bool affords(const CommandOperandInfo& info, Verb verb) {
    if (!info.found) {
        return false;
    }
    if (verb == Verb::LOOK_AT) {
        return true;
    }
    const std::string id(verb_id(verb));
    return std::find(info.affordances.begin(), info.affordances.end(), id) !=
           info.affordances.end();
}

} // namespace

pac::core::CursorKind direct_cursor_kind(Verb verb, bool exit) {
    if (exit) {
        return pac::core::CursorKind::EXIT;
    }
    switch (verb) {
    case Verb::LOOK_AT:
        return pac::core::CursorKind::LOOK;
    case Verb::TALK_TO:
        return pac::core::CursorKind::TALK;
    case Verb::PICK_UP:
    case Verb::OPEN:
    case Verb::CLOSE:
    case Verb::PUSH:
    case Verb::PULL:
        return pac::core::CursorKind::INTERACT;
    case Verb::USE:
    case Verb::GIVE:
        return pac::core::CursorKind::DEFAULT;
    }
    return pac::core::CursorKind::DEFAULT;
}

std::optional<Command> DirectActionComposer::examine(const ObjectRef& object) const {
    if (!object.valid() || !host_.resolve_direct_operand(object).found) {
        return std::nullopt;
    }
    return Command{Verb::LOOK_AT, object, std::nullopt};
}

std::optional<Command> DirectActionComposer::interact(const ObjectRef& object) const {
    if (!object.valid()) {
        return std::nullopt;
    }
    const CommandOperandInfo info = host_.resolve_direct_operand(object);
    const Verb verb = info.default_verb;
    if (!affords(info, verb) || !kind_matches(object.kind, verb_param1_class(verb))) {
        return std::nullopt;
    }
    // GIVE and combinable inventory USE are two-operand gestures. They must be
    // expressed by a drag rather than leaving an invisible sentence half-built.
    if (verb == Verb::GIVE ||
        (verb == Verb::USE && object.kind == ObjectKind::INVENTORY_OBJECT && info.combinable)) {
        return std::nullopt;
    }
    return Command{verb, object, std::nullopt};
}

std::optional<Command> DirectActionComposer::perform(const ObjectRef& object, Verb verb) const {
    if (!object.valid()) {
        return std::nullopt;
    }
    const CommandOperandInfo info = host_.resolve_direct_operand(object);
    if (!affords(info, verb) || !kind_matches(object.kind, verb_param1_class(verb))) {
        return std::nullopt;
    }
    // Two-operand actions are intentionally represented only by item dragging.
    if (verb == Verb::GIVE ||
        (verb == Verb::USE && object.kind == ObjectKind::INVENTORY_OBJECT && info.combinable)) {
        return std::nullopt;
    }
    return Command{verb, object, std::nullopt};
}

std::optional<Command> DirectActionComposer::drop_inventory(const ObjectRef& source,
                                                            const ObjectRef& target) const {
    if (source.kind != ObjectKind::INVENTORY_OBJECT || !source.valid() || !target.valid() ||
        source == target) {
        return std::nullopt;
    }

    const CommandOperandInfo source_info = host_.resolve_direct_operand(source);
    const CommandOperandInfo target_info = host_.resolve_direct_operand(target);
    if (!source_info.found || !target_info.found) {
        return std::nullopt;
    }

    if (target.kind == ObjectKind::ROOM_OBJECT && host_.direct_target_is_npc(target) &&
        affords(source_info, Verb::GIVE)) {
        return Command{Verb::GIVE, source, target};
    }

    const bool valid_use_source = affords(source_info, Verb::USE) && source_info.combinable;
    if (!valid_use_source || !affords(target_info, Verb::USE)) {
        return std::nullopt;
    }
    return Command{Verb::USE, source, target};
}

} // namespace pac::pnc
