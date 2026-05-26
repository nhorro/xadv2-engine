#pragma once

#include "engine/pnc/command.hpp"
#include "engine/pnc/command_builder.hpp"

#include <optional>
#include <string>

namespace pac::pnc {

enum class CommandHoverKind { NONE, VERB, OBJECT, WALKABLE };

/// UI-facing snapshot of the command system. The SCUMM panel renders this state;
/// it does not own the command-building state machine or derive command meaning.
struct CommandState {
    CommandBuilder::State builder_state = CommandBuilder::State::IDLE;
    std::optional<Verb> selected_verb;
    std::optional<ObjectRef> param1;
    std::optional<ObjectRef> param2;
    std::optional<std::string> selected_inventory_item_id;
    int inventory_page_index = 0;

    CommandHoverKind hover = CommandHoverKind::NONE;
    std::optional<Verb> hovered_verb;
    std::optional<ObjectRef> hovered_object;

    std::string preview_text;
};

struct VerbSelected {
    Verb verb{};
};

struct InventoryItemSelected {
    std::string item_id;
};

struct InventoryPageChanged {
    int page_index = 0;
};

struct HotspotHovered {
    std::string hotspot_id;
};

struct HotspotLeft {
    std::string hotspot_id;
};

struct HotspotClicked {
    std::string hotspot_id;
};

struct CommandStateChanged {
    CommandState state;
};

} // namespace pac::pnc
