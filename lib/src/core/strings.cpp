#include "engine/core/strings.hpp"

#include "core/load_error_yaml.hpp"
#include "engine/core/diagnostics.hpp"
#include "engine/core/resource_source.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <array>

namespace pac::core {

namespace {

constexpr const char* kSource = "strings-loader";

/// Engine last-resort caption keys the `defaults` block must define, one per
/// missing-handler case (design 06 §UI strings; issue #36 design review).
constexpr std::array<const char*, 10> kRequiredDefaults = {"cant_look_at",
                                                           "cant_pick_up",
                                                           "wont_open",
                                                           "wont_close",
                                                           "wont_push",
                                                           "wont_pull",
                                                           "cant_use_that_way",
                                                           "no_one_to_give_to",
                                                           "nothing_to_say",
                                                           "nothing_happens"};

[[noreturn]] void
strings_fail(const std::string& code, const std::string& msg, const YAML::Node& at = YAML::Node()) {
    fail_at(kSource, code, msg, at);
}

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

std::string Strings::verb_panel_label(const std::string& verb_id) const {
    const auto it = verb_panel.find(verb_id);
    return it != verb_panel.end() ? it->second : verb_label(verb_id);
}

std::string Strings::connector(const std::string& verb_id) const {
    return lookup(connectors, verb_id);
}

std::string Strings::ui_label(const std::string& key) const {
    return lookup(ui, key);
}

std::string Strings::caption(const std::string& key) const {
    return lookup(defaults, key);
}

Strings parse_strings(const std::string& yaml_text) {
    YAML::Node root;
    try {
        root = YAML::Load(yaml_text);
    } catch (const YAML::Exception& e) {
        strings_fail("strings.invalid-yaml", std::string("invalid YAML: ") + e.what());
    }
    if (!root || !root.IsMap()) {
        strings_fail("strings.root-not-map", "root must be a mapping");
    }

    Strings s;
    if (root["language"]) {
        s.language = root["language"].as<std::string>();
    }
    s.verbs = read_string_map(root["verbs"]);
    s.verb_panel = read_string_map(root["verb_panel"]); // optional, falls back to verbs
    s.connectors = read_string_map(root["connectors"]);
    s.ui = read_string_map(root["ui"]);
    s.defaults = read_string_map(root["defaults"]);

    if (s.verbs.empty()) {
        strings_fail("strings.verbs-missing", "'verbs' must be a non-empty mapping", root);
    }
    if (s.connectors.empty()) {
        strings_fail("strings.connectors-missing",
                     "'connectors' must be a non-empty mapping",
                     root);
    }
    if (s.ui.empty()) {
        strings_fail("strings.ui-missing", "'ui' must be a non-empty mapping", root);
    }
    if (s.defaults.empty()) {
        strings_fail("strings.defaults-missing", "'defaults' must be a non-empty mapping", root);
    }

    // Every required last-resort caption must be present: the engine has no other
    // text to show when a handler is absent.
    for (const char* key : kRequiredDefaults) {
        if (!s.defaults.contains(key)) {
            strings_fail("strings.defaults-missing-key",
                         std::string("'defaults' is missing required key '") + key + "'",
                         root["defaults"]);
        }
    }
    // Reject unknown `defaults` keys (almost always a typo of a required key).
    // `defaults` is engine-reserved, so there is no legitimate custom key here.
    for (const auto& kv : s.defaults) {
        const std::string& key = kv.first;
        const bool known = std::any_of(kRequiredDefaults.begin(),
                                       kRequiredDefaults.end(),
                                       [&](const char* k) { return key == k; });
        if (!known) {
            strings_fail("strings.defaults-unknown-key",
                         "'defaults' has unknown key '" + key + "' (typo?)",
                         root["defaults"]);
        }
    }
    return s;
}

Strings
load_strings(const ResourceSource& resources, const std::string& logical, Diagnostics& log) {
    try {
        return parse_strings(resources.read_text(logical));
    } catch (LoadError& e) {
        log.error(e.with_file(logical).what());
        throw;
    } catch (const std::exception& e) {
        log.error(std::string("strings: failed to load '") + logical + "': " + e.what());
        throw;
    }
}

} // namespace pac::core
