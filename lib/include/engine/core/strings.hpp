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

    /// Lookups return the mapped value, or a visible `?key` placeholder when the
    /// key is missing, so absent strings are obvious rather than silent.
    std::string verb_label(const std::string& verb_id) const;
    std::string connector(const std::string& verb_id) const;
    std::string ui_label(const std::string& key) const;
};

Strings parse_strings(const std::string& yaml_text);
Strings load_strings(const ResourceSource& resources, const std::string& logical, Diagnostics& log);

} // namespace pac::core
