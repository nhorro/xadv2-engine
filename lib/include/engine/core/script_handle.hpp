#pragma once

#include <string>

namespace pac::core {

/// An id-only script handle. It stores a stable id (and a kind tag) — never a raw
/// C++ pointer — so a handle that outlives its entity (e.g. across a room change)
/// fails loudly when the engine cannot resolve the id, instead of dangling.
///
/// The usertype is registered by the scripting service; entity-owning layers add
/// the concrete factories (the `avatar(id)` handle and its methods arrive in M3).
struct ScriptHandle {
    std::string kind; // e.g. "avatar"
    std::string id;   // stable entity id
};

} // namespace pac::core
