#include "engine/pnc/direct_action.hpp"

#include <doctest/doctest.h>

#include <map>
#include <string>
#include <utility>

using namespace pac::pnc;

namespace {

struct FakeHost final : DirectActionHost {
    std::map<std::string, CommandOperandInfo> room;
    std::map<std::string, CommandOperandInfo> inventory;
    std::map<std::string, bool> npcs;

    CommandOperandInfo resolve_direct_operand(const ObjectRef& object) const override {
        const auto& source = object.kind == ObjectKind::INVENTORY_OBJECT ? inventory : room;
        const auto found = source.find(object.id);
        return found == source.end() ? CommandOperandInfo{} : found->second;
    }

    bool direct_target_is_npc(const ObjectRef& object) const override {
        const auto found = npcs.find(object.id);
        return found != npcs.end() && found->second;
    }
};

CommandOperandInfo
operand(Verb default_verb, std::vector<std::string> affordances, bool combinable = false) {
    CommandOperandInfo info;
    info.found = true;
    info.default_verb = default_verb;
    info.affordances = std::move(affordances);
    info.combinable = combinable;
    return info;
}

ObjectRef room(std::string id) {
    return {ObjectKind::ROOM_OBJECT, std::move(id)};
}

ObjectRef item(std::string id) {
    return {ObjectKind::INVENTORY_OBJECT, std::move(id)};
}

} // namespace

TEST_CASE("direct examine always produces one look-at command for a known operand") {
    FakeHost host;
    host.room["door"] = operand(Verb::OPEN, {"open"});
    DirectActionComposer composer(host);

    const auto command = composer.examine(room("door"));
    REQUIRE(command);
    CHECK(command->verb == Verb::LOOK_AT);
    CHECK(command->param1 == room("door"));
    CHECK_FALSE(command->param2);
    CHECK_FALSE(composer.examine(room("missing")));
}

TEST_CASE("direct cursor families follow the default verb and override exits") {
    CHECK(direct_cursor_kind(Verb::LOOK_AT) == pac::core::CursorKind::LOOK);
    CHECK(direct_cursor_kind(Verb::TALK_TO) == pac::core::CursorKind::TALK);
    CHECK(direct_cursor_kind(Verb::OPEN) == pac::core::CursorKind::INTERACT);
    CHECK(direct_cursor_kind(Verb::PICK_UP) == pac::core::CursorKind::INTERACT);
    CHECK(direct_cursor_kind(Verb::USE) == pac::core::CursorKind::DEFAULT);
    CHECK(direct_cursor_kind(Verb::GIVE) == pac::core::CursorKind::DEFAULT);
    CHECK(direct_cursor_kind(Verb::LOOK_AT, true) == pac::core::CursorKind::EXIT);
}

TEST_CASE("direct interact uses the authored default verb without selecting a UI verb") {
    FakeHost host;
    host.room["door"] = operand(Verb::OPEN, {"look_at", "open"});
    host.inventory["folder"] = operand(Verb::OPEN, {"look_at", "open"});
    DirectActionComposer composer(host);

    REQUIRE(composer.interact(room("door")));
    CHECK(composer.interact(room("door"))->verb == Verb::OPEN);
    REQUIRE(composer.interact(item("folder")));
    CHECK(composer.interact(item("folder"))->verb == Verb::OPEN);
}

TEST_CASE("direct interact refuses invisible two-operand command states") {
    FakeHost host;
    host.inventory["key"] = operand(Verb::USE, {"look_at", "use"}, true);
    host.inventory["letter"] = operand(Verb::GIVE, {"look_at", "give"});
    DirectActionComposer composer(host);

    CHECK_FALSE(composer.interact(item("key")));
    CHECK_FALSE(composer.interact(item("letter")));
}

TEST_CASE("direct perform validates an explicitly chosen secondary action") {
    FakeHost host;
    host.room["door"] = operand(Verb::OPEN, {"look_at", "open", "close"});
    DirectActionComposer composer(host);

    REQUIRE(composer.perform(room("door"), Verb::CLOSE));
    CHECK(composer.perform(room("door"), Verb::CLOSE)->verb == Verb::CLOSE);
    REQUIRE(composer.perform(room("door"), Verb::LOOK_AT));
    CHECK_FALSE(composer.perform(room("door"), Verb::PUSH));
    CHECK_FALSE(composer.perform(room("door"), Verb::GIVE));
}

TEST_CASE("dragging a combinable item onto another usable operand produces use") {
    FakeHost host;
    host.inventory["key"] = operand(Verb::LOOK_AT, {"look_at", "use"}, true);
    host.room["door"] = operand(Verb::OPEN, {"look_at", "use", "open"});
    host.inventory["wax"] = operand(Verb::LOOK_AT, {"look_at", "use"});
    DirectActionComposer composer(host);

    auto command = composer.drop_inventory(item("key"), room("door"));
    REQUIRE(command);
    CHECK(command->verb == Verb::USE);
    REQUIRE(command->param2);
    CHECK(*command->param2 == room("door"));

    command = composer.drop_inventory(item("key"), item("wax"));
    REQUIRE(command);
    CHECK(command->verb == Verb::USE);
}

TEST_CASE("NPC drops prefer give and other invalid drops are silent") {
    FakeHost host;
    host.inventory["letter"] = operand(Verb::LOOK_AT, {"look_at", "give"});
    // NPCs intrinsically accept a give attempt; authors only need a handler
    // when they want something other than the normal fallback response.
    host.room["clerk"] = operand(Verb::TALK_TO, {"look_at", "talk_to"});
    host.npcs["clerk"] = true;
    DirectActionComposer composer(host);

    const auto command = composer.drop_inventory(item("letter"), room("clerk"));
    REQUIRE(command);
    CHECK(command->verb == Verb::GIVE);
    REQUIRE(command->param2);
    CHECK(*command->param2 == room("clerk"));

    CHECK_FALSE(composer.drop_inventory(item("letter"), item("letter")));
    CHECK_FALSE(composer.drop_inventory(item("letter"), room("missing")));
}
