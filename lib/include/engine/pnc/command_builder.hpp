#pragma once

#include "engine/pnc/command.hpp"

#include <optional>

namespace pac::pnc {

/// The SCUMM command-building state machine (design 04). UI-independent: the
/// SCUMM panel (or tests, or scripts) drive it through these inputs, and it
/// produces a Command. Operand validity (type + affordance) is decided by the
/// caller and passed in, so the builder stays free of room/inventory data.
class CommandBuilder {
public:
    enum class State {
        IDLE,
        EXPECTING_PARAM1_ROOM_OBJECT,
        EXPECTING_PARAM1_INVENTORY_OBJECT,
        EXPECTING_PARAM1_ANY_OBJECT,
        EXPECTING_PARAM2_ROOM_OBJECT,
        EXPECTING_PARAM2_INVENTORY_OBJECT,
        EXPECTING_PARAM2_ANY_OBJECT,
        COMMAND_READY,
        COMMAND_EXECUTING,
    };

    [[nodiscard]] State state() const { return state_; }
    [[nodiscard]] std::optional<Verb> verb() const { return verb_; }
    [[nodiscard]] const std::optional<ObjectRef>& param1() const { return param1_; }
    [[nodiscard]] const std::optional<ObjectRef>& param2() const { return param2_; }

    /// Select (or replace) the verb; clears operands and moves to the verb's
    /// first expected-parameter state.
    void select_verb(Verb verb);

    /// Offer an operand. `affordance_ok` is the caller's verb/affordance check;
    /// `combinable` matters only for an inventory item under `use`. Returns true
    /// if the operand was accepted (state advanced).
    bool provide_object(const ObjectRef& object, bool affordance_ok, bool combinable = false);

    void cancel();

    /// When COMMAND_READY, return the assembled command and move to
    /// COMMAND_EXECUTING. Otherwise returns nullopt.
    [[nodiscard]] std::optional<Command> take_ready();

    /// Return to IDLE after a dispatched command finishes.
    void finish_execution();

private:
    [[nodiscard]] ArgClass expected_class() const;
    [[nodiscard]] bool expecting_param1() const;
    [[nodiscard]] bool expecting_param2() const;

    State state_ = State::IDLE;
    std::optional<Verb> verb_;
    std::optional<ObjectRef> param1_;
    std::optional<ObjectRef> param2_;
};

} // namespace pac::pnc
