#include "engine/core/localization.hpp"

#include "engine/core/diagnostics.hpp"
#include "engine/core/resource_source.hpp"

#include <utility>

namespace pac::core {

Localization::Localization(const ResourceSource& source,
                           std::vector<LanguageEntry> languages,
                           const std::string& default_id,
                           std::string active_id,
                           Diagnostics& log)
    : source_(source), log_(log), languages_(std::move(languages)),
      active_id_(std::move(active_id)) {
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
    try {
        current_ = load_strings(source_, entry->strings_path, log_);
    } catch (const std::exception&) {
        // load_strings already logged; keep the current language.
        return false;
    }
    active_id_ = id;
    return true;
}

} // namespace pac::core
