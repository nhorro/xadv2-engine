#include "engine/pnc/command_controller.hpp"

#include <doctest/doctest.h>

#include <map>
#include <string>
#include <utility>
#include <vector>

using namespace pac::pnc;
using State = CommandBuilder::State;

namespace {

struct FakeHost : CommandControllerHost {
    std::map<std::string, CommandOperandInfo> room;
    std::map<std::string, CommandOperandInfo> inventory;

    CommandOperandInfo resolve_command_operand(const ObjectRef& object) const override {
        const auto& source = object.kind == ObjectKind::INVENTORY_OBJECT ? inventory : room;
        const auto it = source.find(object.id);
        if (it != source.end()) {
            return it->second;
        }
        CommandOperandInfo fallback;
        fallback.name = object.id;
        return fallback;
    }

    std::string command_verb_label(Verb verb) const override {
        switch (verb) {
        case Verb::LOOK_AT:
            return "Look at";
        case Verb::TALK_TO:
            return "Talk to";
        case Verb::PICK_UP:
            return "Pick up";
        case Verb::USE:
            return "Use";
        case Verb::GIVE:
            return "Give";
        case Verb::OPEN:
            return "Open";
        case Verb::CLOSE:
            return "Close";
        case Verb::PUSH:
            return "Push";
        case Verb::PULL:
            return "Pull";
        }
        return "?";
    }

    std::string command_connector_label(Verb verb) const override {
        return verb == Verb::GIVE ? "to" : "with";
    }

    std::string command_walk_label() const override { return "Walk to"; }
};

CommandOperandInfo operand(std::string name,
                           std::vector<std::string> affordances,
                           Verb default_verb = Verb::LOOK_AT,
                           bool combinable = false) {
    CommandOperandInfo info;
    info.found = true;
    info.name = std::move(name);
    info.affordances = std::move(affordances);
    info.default_verb = default_verb;
    info.combinable = combinable;
    return info;
}

FakeHost make_host() {
    FakeHost host;
    host.room["door"] = operand("door", {"look_at", "open", "use"}, Verb::OPEN);
    host.room["stan"] = operand("Stan", {"look_at", "talk_to"}, Verb::TALK_TO);
    host.inventory["key"] = operand("key", {"look_at", "use"}, Verb::LOOK_AT, true);
    host.inventory["map"] = operand("map", {"look_at", "give"}, Verb::LOOK_AT, false);
    return host;
}

ObjectRef room_ref(const std::string& id) {
    return {ObjectKind::ROOM_OBJECT, id};
}

ObjectRef inv_ref(const std::string& id) {
    return {ObjectKind::INVENTORY_OBJECT, id};
}

} // namespace

TEST_CASE("command controller previews idle hover targets") {
    FakeHost host = make_host();
    CommandController controller(host);

    CHECK(controller.state().preview_text.empty());

    controller.on_verb_hovered(Verb::USE);
    CHECK(controller.state().hover == CommandHoverKind::VERB);
    CHECK(controller.state().preview_text == "Use");

    controller.on_hotspot_hovered({"door"});
    CHECK(controller.state().hover == CommandHoverKind::OBJECT);
    CHECK(controller.state().preview_text == "door");

    controller.on_walkable_hovered();
    CHECK(controller.state().hover == CommandHoverKind::WALKABLE);
    CHECK(controller.state().preview_text == "Walk to");

    controller.on_inventory_page_changed({2});
    CHECK(controller.state().inventory_page_index == 2);
}

TEST_CASE("command controller builds preview from selected verb and hovered valid object") {
    FakeHost host = make_host();
    CommandController controller(host);

    controller.on_verb_selected({Verb::USE});
    CHECK(controller.state().builder_state == State::EXPECTING_PARAM1_ANY_OBJECT);
    CHECK(controller.state().selected_verb == Verb::USE);
    CHECK(controller.state().preview_text == "Use");

    controller.on_hotspot_hovered({"door"});
    CHECK(controller.state().preview_text == "Use door");

    controller.clear_hover();
    CHECK(controller.state().preview_text == "Use");
}

TEST_CASE("command controller uses an operand default verb on plain hotspot click") {
    FakeHost host = make_host();
    CommandController controller(host);

    const std::optional<Command> command = controller.on_hotspot_clicked({"door"});
    REQUIRE(command.has_value());
    CHECK(command->verb == Verb::OPEN);
    CHECK(command->param1 == room_ref("door"));
    CHECK_FALSE(command->param2.has_value());
    CHECK(controller.state().builder_state == State::COMMAND_EXECUTING);
    CHECK(controller.state().preview_text == "Open door");

    controller.finish_execution();
    CHECK(controller.state().builder_state == State::IDLE);
}

TEST_CASE("command controller keeps combinable inventory as selected state until target click") {
    FakeHost host = make_host();
    CommandController controller(host);

    controller.on_verb_selected({Verb::USE});
    CHECK_FALSE(controller.on_inventory_item_selected({"key"}).has_value());
    CHECK(controller.state().builder_state == State::EXPECTING_PARAM2_ANY_OBJECT);
    CHECK(controller.state().selected_inventory_item_id == "key");
    CHECK(controller.state().preview_text == "Use key with");

    controller.on_hotspot_hovered({"door"});
    CHECK(controller.state().preview_text == "Use key with door");

    const std::optional<Command> command = controller.on_hotspot_clicked({"door"});
    REQUIRE(command.has_value());
    CHECK(command->verb == Verb::USE);
    CHECK(command->param1 == inv_ref("key"));
    REQUIRE(command->param2.has_value());
    CHECK(*command->param2 == room_ref("door"));
    CHECK(controller.state().builder_state == State::COMMAND_EXECUTING);
    CHECK(controller.state().preview_text == "Use key with door");
}

TEST_CASE("command controller rejects invalid object without changing state") {
    FakeHost host = make_host();
    CommandController controller(host);

    controller.on_verb_selected({Verb::GIVE});
    CHECK(controller.state().builder_state == State::EXPECTING_PARAM1_INVENTORY_OBJECT);
    CHECK_FALSE(controller.on_hotspot_clicked({"door"}).has_value());
    CHECK(controller.state().builder_state == State::EXPECTING_PARAM1_INVENTORY_OBJECT);
    CHECK(controller.state().preview_text == "Give");

    controller.on_hotspot_hovered({"door"});
    CHECK(controller.state().preview_text == "Give");
}
