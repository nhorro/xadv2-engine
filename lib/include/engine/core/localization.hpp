#pragma once

#include "engine/core/manifest.hpp" // LanguageEntry
#include "engine/core/strings.hpp"
#include "engine/core/translation_catalog.hpp"

#include <set>
#include <string>
#include <vector>

namespace pac::core {

class ResourceSource;
class Diagnostics;

/// Owns the active UI-strings resource and the set of selectable languages
/// (issue #72). The active `Strings` object is held by value and mutated in place
/// when the language changes, so a `const Strings&` handed out at startup (e.g.
/// through `EngineContext`) stays valid and transparently reflects the new
/// language. Game-content catalogs use the same active language but remain
/// separate from the complete engine-UI Strings resource.
class Localization {
public:
    /// Loads `active_id`'s strings. If that fails, falls back to the manifest
    /// default; if the default also fails, rethrows (UI strings are required and
    /// startup must fail loudly). `languages` must be non-empty.
    Localization(const ResourceSource& source,
                 std::vector<LanguageEntry> languages,
                 const std::string& default_id,
                 std::string active_id,
                 Diagnostics& log,
                 bool warn_missing = false);

    const Strings& strings() const { return current_; }
    const std::vector<LanguageEntry>& languages() const { return languages_; }
    const std::string& active() const { return active_id_; }

    /// Index of the active language within `languages()` (0 if not found).
    std::size_t active_index() const;

    /// True when more than one language is selectable.
    bool has_choices() const { return languages_.size() > 1; }

    /// Switch to language `id`. No-op (returns false) when `id` is already active,
    /// unknown, or its strings file fails to load — the current language is kept
    /// in every failure case so a bad pick never blanks the UI. Returns true only
    /// when the active strings actually changed.
    bool set_language(const std::string& id);

    /// Resolve game-authored text. `source_text` is the inline default-language
    /// text and is always the fallback. This keeps original-language authoring
    /// readable while additional languages live in external catalogs.
    [[nodiscard]] std::string text(const std::string& id, const std::string& source_text) const;

private:
    const LanguageEntry* find(const std::string& id) const;

    const ResourceSource& source_;
    Diagnostics& log_;
    std::vector<LanguageEntry> languages_;
    std::string active_id_;
    Strings current_;
    TranslationCatalog catalog_;
    std::string default_id_;
    bool warn_missing_ = false;
    mutable std::set<std::string> warned_missing_;
};

} // namespace pac::core
