#pragma once

#include <map>
#include <string>

namespace pac::core {

class ResourceSource;
class Diagnostics;

/// UI strings resource: every user-facing string the engine itself emits (verb
/// labels, connectors, built-in menu labels). Game-content strings stay inline in
/// their own files. One file in the MVP (Spanish); a language map is design-for.
class Strings {
public:
    std::string language;
    std::map<std::string, std::string> verbs;
    std::map<std::string, std::string> connectors;
    std::map<std::string, std::string> ui;
    /// Engine last-resort captions, fired when no game handler produced text for a
    /// verb (e.g. `nothing_happens`, `cant_look_at`). The loader validates this
    /// block against the required key set; see `parse_strings`.
    std::map<std::string, std::string> defaults;

    /// Lookups return the mapped value, or a visible `?key` placeholder when the
    /// key is missing, so absent strings are obvious rather than silent.
    std::string verb_label(const std::string& verb_id) const;
    std::string connector(const std::string& verb_id) const;
    std::string ui_label(const std::string& key) const;
    std::string caption(const std::string& key) const;
};

/// Parse + validate a UI strings file. Throws `pac::core::LoadError` (source
/// `strings-loader`) when a required block (`verbs`/`connectors`/`ui`/`defaults`)
/// is missing/empty, when a required `defaults` caption key is absent, or when an
/// unknown `defaults` key is present (likely a typo).
Strings parse_strings(const std::string& yaml_text);

/// Read + parse the strings file, logging with the file path on failure and
/// rethrowing (UI strings are a required resource; startup fails loudly).
Strings load_strings(const ResourceSource& resources, const std::string& logical, Diagnostics& log);

} // namespace pac::core
