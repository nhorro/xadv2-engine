#include "engine/pnc/command_builder.hpp"

namespace pac::pnc {

namespace {
CommandBuilder::State param1_state_for(ArgClass arg_class) {
    switch (arg_class) {
    case ArgClass::ROOM_OBJECT:
        return CommandBuilder::State::EXPECTING_PARAM1_ROOM_OBJECT;
    case ArgClass::INVENTORY_OBJECT:
        return CommandBuilder::State::EXPECTING_PARAM1_INVENTORY_OBJECT;
    case ArgClass::ANY_OBJECT:
        return CommandBuilder::State::EXPECTING_PARAM1_ANY_OBJECT;
    }
    return CommandBuilder::State::EXPECTING_PARAM1_ANY_OBJECT;
}
} // namespace

bool CommandBuilder::expecting_param1() const {
    return state_ == State::EXPECTING_PARAM1_ROOM_OBJECT ||
           state_ == State::EXPECTING_PARAM1_INVENTORY_OBJECT ||
           state_ == State::EXPECTING_PARAM1_ANY_OBJECT;
}

bool CommandBuilder::expecting_param2() const {
    return state_ == State::EXPECTING_PARAM2_ROOM_OBJECT ||
           state_ == State::EXPECTING_PARAM2_INVENTORY_OBJECT ||
           state_ == State::EXPECTING_PARAM2_ANY_OBJECT;
}

ArgClass CommandBuilder::expected_class() const {
    switch (state_) {
    case State::EXPECTING_PARAM1_ROOM_OBJECT:
    case State::EXPECTING_PARAM2_ROOM_OBJECT:
        return ArgClass::ROOM_OBJECT;
    case State::EXPECTING_PARAM1_INVENTORY_OBJECT:
    case State::EXPECTING_PARAM2_INVENTORY_OBJECT:
        return ArgClass::INVENTORY_OBJECT;
    default:
        return ArgClass::ANY_OBJECT;
    }
}

void CommandBuilder::select_verb(Verb verb) {
    verb_ = verb;
    param1_.reset();
    param2_.reset();
    state_ = param1_state_for(verb_param1_class(verb));
}

bool CommandBuilder::provide_object(const ObjectRef& object, bool affordance_ok, bool combinable) {
    if (!verb_ || (!expecting_param1() && !expecting_param2())) {
        return false;
    }
    if (!object.valid() || !kind_matches(object.kind, expected_class()) || !affordance_ok) {
        return false; // type/affordance mismatch: stay in the same state
    }

    if (expecting_param1()) {
        param1_ = object;
        // Decide whether a second operand is needed.
        bool wants_param2 = false;
        ArgClass param2_class = ArgClass::ANY_OBJECT;
        if (*verb_ == Verb::GIVE) {
            wants_param2 = true;
            param2_class = ArgClass::ROOM_OBJECT; // give <item> to <recipient>
        } else if (*verb_ == Verb::USE && object.kind == ObjectKind::INVENTORY_OBJECT &&
                   combinable) {
            wants_param2 = true;
            param2_class = ArgClass::ANY_OBJECT; // use <item> with <other>
        }
        if (wants_param2) {
            switch (param2_class) {
            case ArgClass::ROOM_OBJECT:
                state_ = State::EXPECTING_PARAM2_ROOM_OBJECT;
                break;
            case ArgClass::INVENTORY_OBJECT:
                state_ = State::EXPECTING_PARAM2_INVENTORY_OBJECT;
                break;
            case ArgClass::ANY_OBJECT:
                state_ = State::EXPECTING_PARAM2_ANY_OBJECT;
                break;
            }
        } else {
            state_ = State::COMMAND_READY;
        }
        return true;
    }

    // expecting_param2
    param2_ = object;
    state_ = State::COMMAND_READY;
    return true;
}

void CommandBuilder::cancel() {
    state_ = State::IDLE;
    verb_.reset();
    param1_.reset();
    param2_.reset();
}

std::optional<Command> CommandBuilder::take_ready() {
    if (state_ != State::COMMAND_READY || !verb_ || !param1_) {
        return std::nullopt;
    }
    Command command{*verb_, *param1_, param2_};
    state_ = State::COMMAND_EXECUTING;
    return command;
}

void CommandBuilder::finish_execution() {
    state_ = State::IDLE;
    verb_.reset();
    param1_.reset();
    param2_.reset();
}

} // namespace pac::pnc
