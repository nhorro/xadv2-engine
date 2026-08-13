#pragma once

#include <map>
#include <string>

namespace pac::core {

/// Game-authored translations for one language. Spanish/source text stays inline
/// in rooms and Lua; additional languages map stable contextual ids to text.
struct TranslationCatalog {
    std::string language;
    std::map<std::string, std::string> translations;

    [[nodiscard]] const std::string* find(const std::string& id) const;
};

/// Catalog format:
///   version: 1
///   language: en
///   translations:
///     dialog.malena.start.npc.1: "Hello."
TranslationCatalog parse_translation_catalog(const std::string& yaml_text);

} // namespace pac::core
