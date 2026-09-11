#pragma once

#include "engine/core/cursor.hpp"
#include "engine/pnc/command.hpp"
#include "engine/pnc/command_controller.hpp"

#include <optional>

namespace pac::pnc {

[[nodiscard]] pac::core::CursorKind direct_cursor_kind(Verb verb, bool exit = false);

/// Read-only operand queries needed by the direct (tap/drag) command composer.
/// It deliberately produces the same Command values as the classic verb grid;
/// approach walking, validation, Lua dispatch, and recording remain downstream.
class DirectActionHost {
public:
    virtual ~DirectActionHost() = default;

    [[nodiscard]] virtual CommandOperandInfo
    resolve_direct_operand(const ObjectRef& object) const = 0;
    [[nodiscard]] virtual bool direct_target_is_npc(const ObjectRef& object) const = 0;
};

/// Maps the direct-room vocabulary (examine, interact, and inventory drop) onto
/// the engine's canonical verb commands without retaining any pointer/UI state.
class DirectActionComposer {
public:
    explicit DirectActionComposer(const DirectActionHost& host) : host_(host) {}

    [[nodiscard]] std::optional<Command> examine(const ObjectRef& object) const;
    [[nodiscard]] std::optional<Command> interact(const ObjectRef& object) const;
    [[nodiscard]] std::optional<Command> perform(const ObjectRef& object, Verb verb) const;
    [[nodiscard]] std::optional<Command> drop_inventory(const ObjectRef& source,
                                                        const ObjectRef& target) const;

private:
    const DirectActionHost& host_;
};

} // namespace pac::pnc
