#include "engine/core/translation_catalog.hpp"

#include "core/load_error_yaml.hpp"

#include <yaml-cpp/yaml.h>

namespace pac::core {

namespace {
constexpr const char* kSource = "translation-loader";

[[noreturn]] void
fail(const std::string& code, const std::string& message, const YAML::Node& at = YAML::Node()) {
    fail_at(kSource, code, message, at);
}
} // namespace

const std::string* TranslationCatalog::find(const std::string& id) const {
    const auto it = translations.find(id);
    return it == translations.end() ? nullptr : &it->second;
}

TranslationCatalog parse_translation_catalog(const std::string& yaml_text) {
    YAML::Node root;
    try {
        root = YAML::Load(yaml_text);
    } catch (const YAML::Exception& e) {
        fail("translations.invalid-yaml", std::string("invalid YAML: ") + e.what());
    }
    if (!root || !root.IsMap()) {
        fail("translations.root-not-map", "root must be a mapping");
    }
    if (root["version"] && root["version"].as<int>() != 1) {
        fail("translations.version-unsupported",
             "only translation catalog version 1 is supported",
             root["version"]);
    }
    if (!root["translations"] || !root["translations"].IsMap()) {
        fail("translations.mapping-missing", "'translations' must be a mapping", root);
    }

    TranslationCatalog out;
    out.language = root["language"] ? root["language"].as<std::string>() : std::string();
    for (const auto& kv : root["translations"]) {
        const std::string id = kv.first.as<std::string>();
        if (id.empty() || !kv.second.IsScalar()) {
            fail("translations.entry-invalid",
                 "translation ids must be non-empty and values must be strings",
                 kv.second);
        }
        const std::string value = kv.second.as<std::string>();
        if (value.empty()) {
            fail("translations.entry-empty",
                 "translation '" + id + "' must not be empty (omit it to use fallback)",
                 kv.second);
        }
        out.translations.emplace(id, value);
    }
    return out;
}

} // namespace pac::core
