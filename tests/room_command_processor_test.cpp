#include "engine/pnc/command_controller.hpp"
#include "engine/pnc/room_command_processor.hpp"

#include <doctest/doctest.h>

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace pac::pnc;

namespace {

std::string key(const ObjectRef& object) {
    return (object.kind == ObjectKind::INVENTORY_OBJECT ? "inventory:" : "room:") + object.id;
}

struct FakeProcessorHost : RoomCommandProcessorHost {
    bool enabled = true;
    bool view_active = true;
    bool blocked = false;
    bool player_available = true;
    bool player_moving = false;
    bool spoke = false;
    bool start_dialog_on_hotspot = false;
    sf::Vector2f player_position{0.0f, 0.0f};
    std::string player_facing = "right";
    std::map<std::string, CommandOperandValidation> operands;
    std::map<std::string, RoomCommandTarget> targets;
    VerbResult inventory_result;
    VerbResult hotspot_result;
    VerbResult game_result;
    std::optional<pac::core::TaskId> armed_task;
    std::vector<std::string> calls;
    std::vector<sf::Vector2f> routes;
    std::string caption;
    int unhandled = 0;
    int finished = 0;
    std::vector<Command> recorded;

    bool command_submission_enabled() const override { return enabled && view_active; }

    CommandOperandValidation validate_command_operand(const ObjectRef& object,
                                                      Verb) const override {
        const auto it = operands.find(key(object));
        return it == operands.end() ? CommandOperandValidation{} : it->second;
    }

    RoomCommandTarget resolve_room_command_target(const std::string& id) const override {
        const auto it = targets.find(id);
        return it == targets.end() ? RoomCommandTarget{} : it->second;
    }

    bool command_player_available() const override { return player_available; }
    sf::Vector2f command_player_position() const override { return player_position; }
    bool command_player_moving() const override { return player_moving; }
    std::string command_player_facing() const override { return player_facing; }

    sf::Vector2f route_command_player_to(sf::Vector2f target, const std::string&) override {
        routes.push_back(target);
        player_moving = true;
        return target;
    }

    sf::Vector2f reroute_command_player_to(sf::Vector2f target) override {
        routes.push_back(target);
        player_moving = true;
        return target;
    }

    void face_command_target(const Command&) override { calls.push_back("face"); }
    void restore_command_player_facing(const std::string& facing) override {
        player_facing = facing;
        calls.push_back("restore-facing");
    }

    VerbResult call_inventory_command(const std::string& item,
                                      const std::string& verb,
                                      std::optional<std::string> operand) override {
        calls.push_back("inventory:" + item + ":" + verb + ":" + operand.value_or(""));
        return inventory_result;
    }

    VerbResult call_hotspot_command(const std::string& hotspot,
                                    const std::string& verb,
                                    std::optional<std::string> operand) override {
        calls.push_back("hotspot:" + hotspot + ":" + verb + ":" + operand.value_or(""));
        if (start_dialog_on_hotspot) {
            view_active = false;
        }
        return hotspot_result;
    }

    VerbResult call_game_command(const std::string& verb,
                                 const std::string& first,
                                 std::optional<std::string> second) override {
        calls.push_back("game:" + verb + ":" + first + ":" + second.value_or(""));
        return game_result;
    }

    void record_command_submission(const Command& command) override { recorded.push_back(command); }

    void begin_command_dispatch() override { spoke = false; }
    bool command_view_active() const override { return view_active; }
    bool command_handler_task_armed() const override { return armed_task.has_value(); }
    void arm_command_handler_task(pac::core::TaskId task) override { armed_task = task; }
    void block_for_command() override {
        blocked = true;
        view_active = false;
    }
    void unblock_after_command() override {
        blocked = false;
        view_active = true;
    }
    bool command_spoke_during_dispatch() const override { return spoke; }
    void present_command_caption(const std::string& text) override { caption = text; }
    void present_unhandled_command() override { ++unhandled; }
    void finish_command_execution() override { ++finished; }
};

struct ComposerHost : CommandControllerHost {
    CommandOperandInfo resolve_command_operand(const ObjectRef& object) const override {
        CommandOperandInfo info;
        info.found = object.kind == ObjectKind::ROOM_OBJECT && object.id == "door";
        info.name = object.id;
        info.affordances = {"open"};
        info.default_verb = Verb::OPEN;
        return info;
    }
    std::string command_verb_label(Verb) const override { return "Open"; }
    std::string command_connector_label(Verb) const override { return "with"; }
    std::string command_walk_label() const override { return "Walk to"; }
};

void add_room_operand(FakeProcessorHost& host, const std::string& id) {
    host.operands["room:" + id] = {true, true, false};
    host.targets[id].found = true;
}

Command open_door() {
    return {Verb::OPEN, {ObjectKind::ROOM_OBJECT, "door"}, std::nullopt};
}

} // namespace

TEST_CASE("composed and direct commands cross the same processor boundary") {
    ComposerHost composer_host;
    CommandController composer(composer_host);
    const std::optional<Command> composed = composer.on_hotspot_clicked({"door"});
    REQUIRE(composed.has_value());

    FakeProcessorHost composed_host;
    add_room_operand(composed_host, "door");
    composed_host.hotspot_result = {true, "Opened", std::nullopt};
    RoomCommandProcessor composed_processor(composed_host);

    FakeProcessorHost direct_host;
    add_room_operand(direct_host, "door");
    direct_host.hotspot_result = {true, "Opened", std::nullopt};
    RoomCommandProcessor direct_processor(direct_host);

    CHECK(composed_processor.submit(*composed) == CommandSubmission::DISPATCHED);
    CHECK(direct_processor.submit(open_door()) == CommandSubmission::DISPATCHED);
    CHECK(composed_host.calls == direct_host.calls);
    CHECK(composed_host.caption == direct_host.caption);
    CHECK(composed_host.finished == direct_host.finished);
}

TEST_CASE("processor owns inventory hotspot and game handler precedence") {
    FakeProcessorHost host;
    host.operands["inventory:key"] = {true, true, true};
    add_room_operand(host, "door");
    host.hotspot_result = {true, "Unlocked", std::nullopt};
    RoomCommandProcessor processor(host);
    const Command command{Verb::USE,
                          {ObjectKind::INVENTORY_OBJECT, "key"},
                          ObjectRef{ObjectKind::ROOM_OBJECT, "door"}};

    CHECK(processor.submit(command) == CommandSubmission::DISPATCHED);
    REQUIRE(host.calls.size() == 3);
    CHECK(host.calls[0] == "face");
    CHECK(host.calls[1] == "inventory:key:use:door");
    CHECK(host.calls[2] == "hotspot:door:use:key");
    CHECK(host.caption == "Unlocked");

    FakeProcessorHost inventory_host;
    inventory_host.operands = host.operands;
    inventory_host.targets = host.targets;
    inventory_host.inventory_result = {true, std::nullopt, std::nullopt};
    RoomCommandProcessor inventory_processor(inventory_host);
    CHECK(inventory_processor.submit(command) == CommandSubmission::DISPATCHED);
    REQUIRE(inventory_host.calls.size() == 2);
    CHECK(inventory_host.calls[1] == "inventory:key:use:door");
}

TEST_CASE("static approach command is deferred and dispatched on arrival") {
    FakeProcessorHost host;
    add_room_operand(host, "door");
    host.targets["door"].approach = sf::Vector2f{100.0f, 0.0f};
    host.targets["door"].requires_approach = true;
    host.hotspot_result.handled = true;
    RoomCommandProcessor processor(host);

    CHECK(processor.submit(open_door()) == CommandSubmission::DEFERRED);
    CHECK(processor.has_deferred_command());
    CHECK(host.blocked);
    CHECK(host.calls.empty());
    REQUIRE(host.routes.size() == 1);

    host.player_position = host.routes.front();
    host.player_moving = false;
    processor.update(0.016f);
    CHECK_FALSE(processor.has_deferred_command());
    CHECK_FALSE(host.blocked);
    CHECK(host.calls[0] == "face");
    CHECK(host.calls[1] == "hotspot:door:open:");
}

TEST_CASE("moving approach re-resolves its target and rejects a stale target") {
    FakeProcessorHost host;
    add_room_operand(host, "door");
    host.targets["door"].requires_approach = true;
    host.targets["door"].moving = true;
    host.targets["door"].live_position = sf::Vector2f{100.0f, 0.0f};
    host.hotspot_result.handled = true;
    RoomCommandProcessor processor(host);

    CHECK(processor.submit(open_door()) == CommandSubmission::DEFERRED);
    host.player_position = {100.0f, 0.0f};
    host.player_moving = false;
    processor.update(0.016f);
    REQUIRE(host.calls.size() >= 2);
    CHECK(host.calls[1] == "hotspot:door:open:");

    FakeProcessorHost stale_host;
    add_room_operand(stale_host, "door");
    stale_host.targets["door"] = host.targets["door"];
    RoomCommandProcessor stale_processor(stale_host);
    CHECK(stale_processor.submit(open_door()) == CommandSubmission::DEFERRED);
    stale_host.targets.erase("door");
    stale_processor.update(0.016f);
    CHECK_FALSE(stale_processor.has_deferred_command());
    CHECK(stale_host.calls.empty());
    CHECK(stale_host.finished == 1);
}

TEST_CASE("processor preserves yielded dialog and unhandled feedback behavior") {
    SUBCASE("yielded handler blocks and arms its task") {
        FakeProcessorHost host;
        add_room_operand(host, "door");
        host.hotspot_result = {true, std::nullopt, pac::core::TaskId{42}};
        RoomCommandProcessor processor(host);
        CHECK(processor.submit(open_door()) == CommandSubmission::DISPATCHED);
        CHECK(host.armed_task == 42);
        CHECK(host.blocked);
        CHECK(host.finished == 0);
    }

    SUBCASE("dialog-starting handler finishes without overwriting dialog feedback") {
        FakeProcessorHost host;
        add_room_operand(host, "door");
        host.start_dialog_on_hotspot = true;
        host.hotspot_result.handled = true;
        RoomCommandProcessor processor(host);
        CHECK(processor.submit(open_door()) == CommandSubmission::DISPATCHED);
        CHECK(host.caption.empty());
        CHECK(host.unhandled == 0);
        CHECK(host.finished == 1);
    }

    SUBCASE("unhandled command uses the fallback feedback") {
        FakeProcessorHost host;
        add_room_operand(host, "door");
        RoomCommandProcessor processor(host);
        CHECK(processor.submit(open_door()) == CommandSubmission::DISPATCHED);
        CHECK(host.calls[2] == "game:open:door:");
        CHECK(host.unhandled == 1);
        CHECK(host.finished == 1);
    }
}

TEST_CASE("processor rejects disabled stale and malformed submissions") {
    FakeProcessorHost host;
    add_room_operand(host, "door");
    host.operands["inventory:key"] = {true, true, false};
    RoomCommandProcessor processor(host);

    CHECK(processor.submit({Verb::OPEN, {ObjectKind::ROOM_OBJECT, "missing"}, std::nullopt}) ==
          CommandSubmission::REJECTED);
    CHECK(processor.submit({Verb::GIVE, {ObjectKind::INVENTORY_OBJECT, "key"}, std::nullopt}) ==
          CommandSubmission::REJECTED);
    CHECK(processor.submit({Verb::OPEN,
                            {ObjectKind::ROOM_OBJECT, "door"},
                            ObjectRef{ObjectKind::ROOM_OBJECT, "door"}}) ==
          CommandSubmission::REJECTED);
    CHECK(host.calls.empty());
    CHECK(host.finished == 3);

    host.operands["room:ghost"] = {true, true, false};
    CHECK(processor.submit({Verb::OPEN, {ObjectKind::ROOM_OBJECT, "ghost"}, std::nullopt}) ==
          CommandSubmission::REJECTED);
    CHECK(host.finished == 4);

    host.enabled = false;
    CHECK(processor.submit(open_door()) == CommandSubmission::REJECTED);
    CHECK(host.finished == 5);
    CHECK(host.recorded.empty());
}

TEST_CASE("processor records an accepted deferred command exactly once") {
    FakeProcessorHost host;
    add_room_operand(host, "door");
    host.targets["door"].approach = sf::Vector2f{100.0f, 0.0f};
    host.targets["door"].requires_approach = true;
    host.hotspot_result.handled = true;
    RoomCommandProcessor processor(host);

    CHECK(processor.submit(open_door()) == CommandSubmission::DEFERRED);
    REQUIRE(host.recorded.size() == 1);
    CHECK(host.recorded[0].verb == Verb::OPEN);
    CHECK(host.recorded[0].param1.kind == ObjectKind::ROOM_OBJECT);
    CHECK(host.recorded[0].param1.id == "door");
    CHECK_FALSE(host.recorded[0].param2.has_value());
    host.player_position = {100.0f, 0.0f};
    host.player_moving = false;
    processor.update(0.016f);
    CHECK(host.recorded.size() == 1);
}
