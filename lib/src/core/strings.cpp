#include "engine/core/strings.hpp"

#include "engine/core/diagnostics.hpp"
#include "engine/core/resource_source.hpp"

#include <yaml-cpp/yaml.h>

namespace pac::core {

namespace {

std::map<std::string, std::string> read_string_map(const YAML::Node& node) {
    std::map<std::string, std::string> out;
    if (node && node.IsMap()) {
        for (const auto& kv : node) {
            out[kv.first.as<std::string>()] = kv.second.as<std::string>();
        }
    }
    return out;
}

std::string lookup(const std::map<std::string, std::string>& m, const std::string& key) {
    const auto it = m.find(key);
    return it != m.end() ? it->second : ("?" + key);
}

} // namespace

std::string Strings::verb_label(const std::string& verb_id) const {
    return lookup(verbs, verb_id);
}

std::string Strings::connector(const std::string& verb_id) const {
    return lookup(connectors, verb_id);
}

std::string Strings::ui_label(const std::string& key) const {
    return lookup(ui, key);
}

Strings parse_strings(const std::string& yaml_text) {
    const YAML::Node root = YAML::Load(yaml_text);
    Strings s;
    if (root["language"]) {
        s.language = root["language"].as<std::string>();
    }
    s.verbs = read_string_map(root["verbs"]);
    s.connectors = read_string_map(root["connectors"]);
    s.ui = read_string_map(root["ui"]);
    return s;
}

Strings
load_strings(const ResourceSource& resources, const std::string& logical, Diagnostics& log) {
    try {
        return parse_strings(resources.read_text(logical));
    } catch (const std::exception& e) {
        log.error(std::string("strings: failed to load '") + logical + "': " + e.what());
        return Strings{};
    }
}

} // namespace pac::core
