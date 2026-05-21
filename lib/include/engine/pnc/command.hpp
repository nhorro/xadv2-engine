#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace pac::pnc {

/// The first-implementation verbs. Verb ids (`look_at`, ...) are the canonical
/// string form used for strings lookup and Lua handler names.
enum class Verb { LOOK_AT, TALK_TO, PICK_UP, USE, GIVE, OPEN, CLOSE, PUSH, PULL };

/// What kind of operand a command argument is.
enum class ObjectKind { NONE, ROOM_OBJECT, INVENTORY_OBJECT };

/// The argument classes the command builder distinguishes.
enum class ArgClass { ROOM_OBJECT, INVENTORY_OBJECT, ANY_OBJECT };

/// A reference to a command operand: a room hotspot or an inventory item.
struct ObjectRef {
    ObjectKind kind = ObjectKind::NONE;
    std::string id;

    [[nodiscard]] bool valid() const { return kind != ObjectKind::NONE && !id.empty(); }
    friend bool operator==(const ObjectRef&, const ObjectRef&) = default;
};

/// A language-independent command: `verb param1 param2?`. The UI formats it for
/// display; the dispatcher routes it to a handler.
struct Command {
    Verb verb{};
    ObjectRef param1;
    std::optional<ObjectRef> param2;
};

[[nodiscard]] std::string_view verb_id(Verb verb);
[[nodiscard]] std::optional<Verb> verb_from_id(std::string_view id);

/// The argument class a verb expects for its first parameter.
[[nodiscard]] ArgClass verb_param1_class(Verb verb);

/// True if the operand's kind satisfies the argument class.
[[nodiscard]] bool kind_matches(ObjectKind kind, ArgClass arg_class);

} // namespace pac::pnc
