#pragma once

// Private loader helper: bridges yaml-cpp node positions into the public
// `pac::core::LoadError` envelope. Lives under src/ (never include/) so yaml-cpp
// stays an implementation detail and out of any public engine header. Include
// this only from loader translation units.

#include "engine/core/load_error.hpp"

#include <yaml-cpp/yaml.h>

#include <utility>

namespace pac::core {

/// Best-effort source position for a parsed node. Programmatically-created or
/// missing nodes carry a null mark (line < 0), which maps to "unknown".
inline SourceLocation loc_of(const YAML::Node& node) {
    if (!node) {
        return {};
    }
    const YAML::Mark mark = node.Mark();
    if (mark.line < 0) {
        return {};
    }
    return SourceLocation{.file = "", .line = mark.line + 1, .column = mark.column + 1};
}

/// Throw loader diagnostic `E` (a `LoadError` or subclass) pinned to `node`'s
/// YAML position. Defaults to the base `LoadError`; loaders pass their typed
/// subclass (`DataError`, `AssetError`, `ManifestError`) so existing
/// `CHECK_THROWS_AS(..., DataError)`-style catches keep matching.
template <class E = LoadError>
[[noreturn]] void
fail_at(std::string source, std::string code, std::string message, const YAML::Node& node) {
    throw E(std::move(source), std::move(code), std::move(message), loc_of(node));
}

} // namespace pac::core
