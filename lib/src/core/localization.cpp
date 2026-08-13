#include "engine/core/localization.hpp"

#include "engine/core/diagnostics.hpp"
#include "engine/core/resource_source.hpp"

#include <utility>

namespace pac::core {

Localization::Localization(const ResourceSource& source,
                           std::vector<LanguageEntry> languages,
                           const std::string& default_id,
                           std::string active_id,
                           Diagnostics& log,
                           bool warn_missing)
    : source_(source), log_(log), languages_(std::move(languages)),
      active_id_(std::move(active_id)), default_id_(default_id), warn_missing_(warn_missing) {
    const LanguageEntry* entry = find(active_id_);
    if (!entry) {
        entry = find(default_id);
        if (entry) {
            active_id_ = default_id;
        }
    }
    if (!entry) {
        // Neither the requested nor the default id resolved — the manifest
        // guarantees a non-empty list, so fall back to the first entry.
        entry = &languages_.front();
        active_id_ = entry->id;
    }

    try {
        current_ = load_strings(source_, entry->strings_path, log_);
    } catch (const std::exception&) {
        // The requested language failed to load. Fall back to the default once;
        // if that also fails, rethrow (startup needs valid UI strings).
        const LanguageEntry* fallback = find(default_id);
        if (!fallback || fallback == entry) {
            throw;
        }
        log_.warn("localization: language '" + active_id_ + "' failed to load; falling back to '" +
                  default_id + "'");
        current_ = load_strings(source_, fallback->strings_path, log_);
        active_id_ = default_id;
    }
    const LanguageEntry* active = find(active_id_);
    if (active && !active->translations_path.empty()) {
        try {
            catalog_ = parse_translation_catalog(source_.read_text(active->translations_path));
        } catch (const std::exception& e) {
            log_.warn("localization: could not load content translations '" +
                      active->translations_path + "': " + e.what());
        }
    }
}

const LanguageEntry* Localization::find(const std::string& id) const {
    for (const LanguageEntry& e : languages_) {
        if (e.id == id) {
            return &e;
        }
    }
    return nullptr;
}

std::size_t Localization::active_index() const {
    for (std::size_t i = 0; i < languages_.size(); ++i) {
        if (languages_[i].id == active_id_) {
            return i;
        }
    }
    return 0;
}

bool Localization::set_language(const std::string& id) {
    if (id == active_id_) {
        return false;
    }
    const LanguageEntry* entry = find(id);
    if (!entry) {
        log_.warn("localization: unknown language '" + id + "'");
        return false;
    }
    Strings next_strings;
    TranslationCatalog next_catalog;
    try {
        next_strings = load_strings(source_, entry->strings_path, log_);
    } catch (const std::exception&) {
        // load_strings already logged; keep the current language.
        return false;
    }
    if (!entry->translations_path.empty()) {
        try {
            next_catalog = parse_translation_catalog(source_.read_text(entry->translations_path));
        } catch (const std::exception& e) {
            // UI language selection remains usable even when the optional game
            // catalog is absent or malformed; authored source text will show.
            log_.warn("localization: could not load content translations '" +
                      entry->translations_path + "': " + e.what());
        }
    }
    current_ = std::move(next_strings);
    catalog_ = std::move(next_catalog);
    active_id_ = id;
    warned_missing_.clear();
    return true;
}

std::string Localization::text(const std::string& id, const std::string& source_text) const {
    if (active_id_ == default_id_) {
        return source_text;
    }
    if (!id.empty()) {
        if (const std::string* translated = catalog_.find(id)) {
            return *translated;
        }
    }
    if (warn_missing_) {
        const std::string warning_key = id.empty() ? "<unidentified>:" + source_text : id;
        if (warned_missing_.insert(warning_key).second) {
            log_.warn(id.empty()
                          ? "localization: translatable text has no id: '" + source_text + "'"
                          : "localization: missing '" + active_id_ + "' translation for '" + id +
                                "'");
        }
    }
    return source_text;
}

} // namespace pac::core
