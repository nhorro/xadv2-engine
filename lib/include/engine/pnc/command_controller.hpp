#pragma once

#include "engine/pnc/command.hpp"
#include "engine/pnc/command_builder.hpp"
#include "engine/pnc/command_state.hpp"

#include <optional>
#include <string>
#include <vector>

namespace pac::pnc {

struct CommandOperandInfo {
    bool found = false;
    std::string name;
    std::vector<std::string> affordances;
    bool combinable = false;
    Verb default_verb = Verb::LOOK_AT;
};

class CommandControllerHost {
public:
    virtual ~CommandControllerHost() = default;

    [[nodiscard]] virtual CommandOperandInfo
    resolve_command_operand(const ObjectRef& object) const = 0;
    [[nodiscard]] virtual std::string command_verb_label(Verb verb) const = 0;
    [[nodiscard]] virtual std::string command_connector_label(Verb verb) const = 0;
    [[nodiscard]] virtual std::string command_walk_label() const = 0;
};

/// Mediates room input, SCUMM-panel intent, and the command builder. It owns the
/// authoritative command state and exposes a renderable snapshot for UI views.
class CommandController {
public:
    explicit CommandController(const CommandControllerHost& host);

    [[nodiscard]] const CommandState& state() const { return state_; }

    void reset();
    void cancel();
    void finish_execution();

    void on_verb_selected(const VerbSelected& event);
    [[nodiscard]] std::optional<Command>
    on_inventory_item_selected(const InventoryItemSelected& event);
    void on_inventory_page_changed(const InventoryPageChanged& event);

    void on_verb_hovered(Verb verb);
    void on_inventory_item_hovered(const std::string& item_id);
    void on_hotspot_hovered(const HotspotHovered& event);
    void on_hotspot_left(const HotspotLeft& event);
    void on_walkable_hovered();
    void clear_hover();

    [[nodiscard]] std::optional<Command> on_hotspot_clicked(const HotspotClicked& event);

private:
    enum class HoverKind { NONE, VERB, OBJECT, WALKABLE };

    struct HoverState {
        HoverKind kind = HoverKind::NONE;
        Verb verb = Verb::LOOK_AT;
        ObjectRef object;
    };

    [[nodiscard]] std::optional<Command> select_object(const ObjectRef& object);
    [[nodiscard]] std::optional<Command> take_ready_command();
    [[nodiscard]] std::string command_preview() const;
    [[nodiscard]] std::string preview_text() const;
    [[nodiscard]] bool would_accept_hovered(const ObjectRef& object,
                                            const CommandOperandInfo& info) const;
    void refresh_state();

    const CommandControllerHost& host_;
    CommandBuilder builder_;
    HoverState hover_;
    int inventory_page_ = 0;
    CommandState state_;
};

} // namespace pac::pnc
