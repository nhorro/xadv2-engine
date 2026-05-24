#include "engine/pnc/command.hpp"
#include "engine/pnc/command_builder.hpp"

#include <doctest/doctest.h>

// verb_id() returns std::string_view; doctest stringifies CHECK operands via
// operator<<, and MSVC's <string_view> only forward-declares std::ostream. Pull
// in the full definition so that operator instantiates (libstdc++ does so transitively).
#include <ostream>

using namespace pac::pnc;
using State = CommandBuilder::State;

namespace {
ObjectRef room(const std::string& id) {
    return {ObjectKind::ROOM_OBJECT, id};
}
ObjectRef inv(const std::string& id) {
    return {ObjectKind::INVENTORY_OBJECT, id};
}
} // namespace

TEST_CASE("verb id round-trip") {
    CHECK(verb_id(Verb::LOOK_AT) == "look_at");
    CHECK(verb_id(Verb::USE) == "use");
    CHECK(verb_from_id("give") == Verb::GIVE);
    CHECK_FALSE(verb_from_id("fly").has_value());
}

TEST_CASE("look_at takes any single operand and becomes ready") {
    CommandBuilder b;
    b.select_verb(Verb::LOOK_AT);
    CHECK(b.state() == State::EXPECTING_PARAM1_ANY_OBJECT);
    CHECK(b.provide_object(room("drawer"), true));
    CHECK(b.state() == State::COMMAND_READY);

    const auto cmd = b.take_ready();
    REQUIRE(cmd.has_value());
    CHECK(cmd->verb == Verb::LOOK_AT);
    CHECK(cmd->param1 == room("drawer"));
    CHECK_FALSE(cmd->param2.has_value());
    CHECK(b.state() == State::COMMAND_EXECUTING);
    b.finish_execution();
    CHECK(b.state() == State::IDLE);
}

TEST_CASE("talk_to and pick_up accept only room objects") {
    CommandBuilder b;
    b.select_verb(Verb::TALK_TO);
    CHECK(b.state() == State::EXPECTING_PARAM1_ROOM_OBJECT);
    CHECK_FALSE(b.provide_object(inv("key"), true)); // inventory rejected
    CHECK(b.state() == State::EXPECTING_PARAM1_ROOM_OBJECT);
    CHECK(b.provide_object(room("stan"), true));
    CHECK(b.state() == State::COMMAND_READY);
}

TEST_CASE("affordance failure is rejected and keeps the state") {
    CommandBuilder b;
    b.select_verb(Verb::OPEN);
    CHECK(b.state() == State::EXPECTING_PARAM1_ANY_OBJECT);
    CHECK_FALSE(b.provide_object(room("wall"), /*affordance_ok=*/false));
    CHECK(b.state() == State::EXPECTING_PARAM1_ANY_OBJECT);
    CHECK(b.provide_object(room("door"), true));
    CHECK(b.state() == State::COMMAND_READY);
}

TEST_CASE("use: room hotspot is always one-operand") {
    CommandBuilder b;
    b.select_verb(Verb::USE);
    CHECK(b.provide_object(room("lever"), true, /*combinable=*/true)); // combinable ignored
    CHECK(b.state() == State::COMMAND_READY);
}

TEST_CASE("use: non-combinable inventory item is one-operand") {
    CommandBuilder b;
    b.select_verb(Verb::USE);
    CHECK(b.provide_object(inv("apple"), true, /*combinable=*/false));
    CHECK(b.state() == State::COMMAND_READY);
}

TEST_CASE("use: combinable inventory item expects a second operand") {
    CommandBuilder b;
    b.select_verb(Verb::USE);
    CHECK(b.provide_object(inv("key"), true, /*combinable=*/true));
    CHECK(b.state() == State::EXPECTING_PARAM2_ANY_OBJECT);
    CHECK(b.provide_object(room("door"), true));
    CHECK(b.state() == State::COMMAND_READY);

    const auto cmd = b.take_ready();
    REQUIRE(cmd.has_value());
    CHECK(cmd->verb == Verb::USE);
    CHECK(cmd->param1 == inv("key"));
    REQUIRE(cmd->param2.has_value());
    CHECK(*cmd->param2 == room("door"));
}

TEST_CASE("give: inventory then room object") {
    CommandBuilder b;
    b.select_verb(Verb::GIVE);
    CHECK(b.state() == State::EXPECTING_PARAM1_INVENTORY_OBJECT);
    CHECK_FALSE(b.provide_object(room("stan"), true)); // first must be inventory
    CHECK(b.provide_object(inv("map"), true));
    CHECK(b.state() == State::EXPECTING_PARAM2_ROOM_OBJECT);
    CHECK_FALSE(b.provide_object(inv("coin"), true)); // recipient must be a room object
    CHECK(b.provide_object(room("stan"), true));
    CHECK(b.state() == State::COMMAND_READY);

    const auto cmd = b.take_ready();
    REQUIRE(cmd.has_value());
    CHECK(cmd->param1 == inv("map"));
    REQUIRE(cmd->param2.has_value());
    CHECK(*cmd->param2 == room("stan"));
}

TEST_CASE("selecting another verb mid-build clears operands") {
    CommandBuilder b;
    b.select_verb(Verb::GIVE);
    b.provide_object(inv("map"), true);
    CHECK(b.state() == State::EXPECTING_PARAM2_ROOM_OBJECT);
    b.select_verb(Verb::LOOK_AT); // replace
    CHECK(b.state() == State::EXPECTING_PARAM1_ANY_OBJECT);
    CHECK_FALSE(b.param1().has_value());
}

TEST_CASE("would_accept previews validity without mutating") {
    CommandBuilder b;
    CHECK_FALSE(b.would_accept(room("door"), true)); // IDLE: nothing accepted

    b.select_verb(Verb::GIVE);                       // EXPECTING_PARAM1_INVENTORY_OBJECT
    CHECK_FALSE(b.would_accept(room("stan"), true)); // wrong kind
    CHECK_FALSE(b.would_accept(inv("map"), false));  // affordance fails
    CHECK(b.would_accept(inv("map"), true));         // valid
    CHECK(b.state() == State::EXPECTING_PARAM1_INVENTORY_OBJECT); // unchanged

    b.provide_object(inv("map"), true);                      // EXPECTING_PARAM2_ROOM_OBJECT
    CHECK_FALSE(b.would_accept(inv("coin"), true));          // recipient must be a room object
    CHECK(b.would_accept(room("stan"), true));               // valid
    CHECK(b.state() == State::EXPECTING_PARAM2_ROOM_OBJECT); // unchanged
}

TEST_CASE("cancel and take_ready guards") {
    CommandBuilder b;
    CHECK_FALSE(b.take_ready().has_value()); // nothing to take
    b.select_verb(Verb::USE);
    b.cancel();
    CHECK(b.state() == State::IDLE);
    CHECK_FALSE(b.verb().has_value());
}
