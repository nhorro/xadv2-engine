#include "engine/pnc/command.hpp"

#include <array>
#include <utility>

namespace pac::pnc {

namespace {
constexpr std::array<std::pair<Verb, std::string_view>, 9> kVerbIds{{
    {Verb::LOOK_AT, "look_at"},
    {Verb::TALK_TO, "talk_to"},
    {Verb::PICK_UP, "pick_up"},
    {Verb::USE, "use"},
    {Verb::GIVE, "give"},
    {Verb::OPEN, "open"},
    {Verb::CLOSE, "close"},
    {Verb::PUSH, "push"},
    {Verb::PULL, "pull"},
}};
} // namespace

std::string_view verb_id(Verb verb) {
    for (const auto& [v, id] : kVerbIds) {
        if (v == verb) {
            return id;
        }
    }
    return "look_at";
}

std::optional<Verb> verb_from_id(std::string_view id) {
    for (const auto& [v, name] : kVerbIds) {
        if (name == id) {
            return v;
        }
    }
    return std::nullopt;
}

ArgClass verb_param1_class(Verb verb) {
    switch (verb) {
    case Verb::TALK_TO:
    case Verb::PICK_UP:
        return ArgClass::ROOM_OBJECT;
    case Verb::GIVE:
        return ArgClass::INVENTORY_OBJECT;
    case Verb::LOOK_AT:
    case Verb::USE:
    case Verb::OPEN:
    case Verb::CLOSE:
    case Verb::PUSH:
    case Verb::PULL:
        return ArgClass::ANY_OBJECT;
    }
    return ArgClass::ANY_OBJECT;
}

bool kind_matches(ObjectKind kind, ArgClass arg_class) {
    switch (arg_class) {
    case ArgClass::ROOM_OBJECT:
        return kind == ObjectKind::ROOM_OBJECT;
    case ArgClass::INVENTORY_OBJECT:
        return kind == ObjectKind::INVENTORY_OBJECT;
    case ArgClass::ANY_OBJECT:
        return kind == ObjectKind::ROOM_OBJECT || kind == ObjectKind::INVENTORY_OBJECT;
    }
    return false;
}

} // namespace pac::pnc
