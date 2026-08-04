#include "engine/core/manifest.hpp"

#include "core/load_error_yaml.hpp"

#include <yaml-cpp/yaml.h>

#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

namespace pac::core {

const SceneDesc* Manifest::find_scene(const std::string& id) const {
    for (const auto& scene : scenes) {
        if (scene.id == id) {
            return &scene;
        }
    }
    return nullptr;
}

namespace {

[[noreturn]] void manifest_fail(const std::string& code,
                                const std::string& msg,
                                const YAML::Node& at = YAML::Node()) {
    fail_at<ManifestError>("manifest-loader", code, msg, at);
}

bool is_valid_id_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
}

void validate_id(const std::string& id, const YAML::Node& node) {
    if (id.empty()) {
        manifest_fail("manifest.id-missing", "'id' is required", node);
    }
    for (const char c : id) {
        if (!is_valid_id_char(c)) {
            manifest_fail("manifest.id-invalid",
                          "'id' must match [a-z0-9_-]+ (got '" + id + "')",
                          node);
        }
    }
}

// Parse the UI-strings language declaration (issue #72). Two accepted forms:
//
//   strings: strings/es.yaml            # single-language shorthand
//
//   languages:                          # explicit, design-ready map
//     - { id: es, name: "Español", strings: strings/es.yaml }
//   default_language: es                # optional; defaults to the first entry
//
// Always leaves `m.languages` non-empty and `m.default_language` / `m.strings_path`
// pointing at the default entry.
void parse_languages(const YAML::Node& root, Manifest& m) {
    const YAML::Node langs = root["languages"];
    if (langs && langs.IsSequence() && langs.size() > 0) {
        std::set<std::string> seen;
        for (const YAML::Node& ln : langs) {
            LanguageEntry e;
            if (!ln["id"] || ln["id"].as<std::string>().empty()) {
                manifest_fail("manifest.language-id-missing",
                              "a 'languages' entry is missing 'id'",
                              ln);
            }
            e.id = ln["id"].as<std::string>();
            if (!seen.insert(e.id).second) {
                manifest_fail("manifest.duplicate-language-id",
                              "duplicate language id '" + e.id + "'",
                              ln);
            }
            if (!ln["strings"] || ln["strings"].as<std::string>().empty()) {
                manifest_fail("manifest.language-strings-missing",
                              "language '" + e.id + "' is missing 'strings'",
                              ln);
            }
            e.strings_path = ln["strings"].as<std::string>();
            e.name = ln["name"] ? ln["name"].as<std::string>() : e.id;
            m.languages.push_back(std::move(e));
        }
        m.default_language = root["default_language"] ? root["default_language"].as<std::string>()
                                                      : m.languages.front().id;
        const LanguageEntry* def = nullptr;
        for (const LanguageEntry& e : m.languages) {
            if (e.id == m.default_language) {
                def = &e;
                break;
            }
        }
        if (!def) {
            manifest_fail("manifest.default-language-unknown",
                          "default_language '" + m.default_language + "' is not in 'languages'",
                          root);
        }
        m.strings_path = def->strings_path;
        return;
    }

    if (!root["strings"] || root["strings"].as<std::string>().empty()) {
        manifest_fail("manifest.strings-missing",
                      "'strings' (or a 'languages' list) is required",
                      root);
    }
    m.strings_path = root["strings"].as<std::string>();
    m.default_language = "default";
    m.languages.push_back({m.default_language, "", m.strings_path});
}

// Flatten a scene `parameters` mapping into the flat string store, joining nested
// map keys with dots (e.g. `menu.position.x`, `menu.options.new_game`). This keeps
// SceneParams a flat scalar map while letting authors group related parameters in
// the manifest. Sequences are not used by any scene type and are skipped.
void flatten_params(const YAML::Node& node, const std::string& prefix, SceneParams& out) {
    if (!node.IsMap()) {
        return;
    }
    for (const auto& kv : node) {
        const std::string key =
            prefix.empty() ? kv.first.as<std::string>() : prefix + "." + kv.first.as<std::string>();
        if (kv.second.IsScalar()) {
            out.set(key, kv.second.as<std::string>());
        } else if (kv.second.IsMap()) {
            flatten_params(kv.second, key, out);
        }
    }
}

unsigned require_dimension(const YAML::Node& node, const char* what) {
    if (!node) {
        manifest_fail("manifest.dimension-missing", std::string(what) + " is required", node);
    }
    const auto value = node.as<long long>();
    if (value <= 0) {
        manifest_fail("manifest.dimension-invalid",
                      std::string(what) + " must be a positive integer",
                      node);
    }
    return static_cast<unsigned>(value);
}

} // namespace

Manifest parse_manifest(const std::string& yaml_text) {
    YAML::Node root;
    try {
        root = YAML::Load(yaml_text);
    } catch (const YAML::Exception& e) {
        manifest_fail("manifest.invalid-yaml", std::string("invalid YAML: ") + e.what());
    }
    if (!root || !root.IsMap()) {
        manifest_fail("manifest.root-not-map", "root must be a mapping");
    }

    Manifest m;
    m.version = root["version"] ? root["version"].as<int>() : 1;

    if (!root["id"]) {
        manifest_fail("manifest.id-missing", "'id' is required", root);
    }
    m.id = root["id"].as<std::string>();
    validate_id(m.id, root["id"]);

    const YAML::Node res = root["resolution"];
    if (!res) {
        manifest_fail("manifest.resolution-missing", "'resolution' is required", root);
    }
    m.resolution = {require_dimension(res["width"], "resolution.width"),
                    require_dimension(res["height"], "resolution.height")};

    const YAML::Node win = root["window"];
    if (!win) {
        manifest_fail("manifest.window-missing", "'window' is required", root);
    }
    m.window.fullscreen = win["fullscreen"] ? win["fullscreen"].as<bool>() : false;
    m.window.width = win["width"] ? win["width"].as<unsigned>() : m.resolution.x;
    m.window.height = win["height"] ? win["height"].as<unsigned>() : m.resolution.y;

    if (const YAML::Node rendering = root["rendering"]) {
        if (!rendering.IsMap()) {
            manifest_fail("manifest.rendering-not-map", "'rendering' must be a mapping", rendering);
        }
        m.rendering.smooth_textures =
            rendering["smooth_textures"] ? rendering["smooth_textures"].as<bool>() : true;
    }

    const YAML::Node resources = root["resources"];
    if (!resources || !resources["src"] || resources["src"].as<std::string>().empty()) {
        manifest_fail("manifest.resources-src-missing", "'resources.src' is required", root);
    }
    m.resources_src = resources["src"].as<std::string>();

    parse_languages(root, m);

    if (const YAML::Node settings = root["settings"]) {
        if (const YAML::Node audio = settings["audio"]) {
            if (audio["music_volume"]) {
                m.settings.music_volume = audio["music_volume"].as<float>();
            }
            if (audio["sfx_volume"]) {
                m.settings.sfx_volume = audio["sfx_volume"].as<float>();
            }
        }
    }

    if (const YAML::Node cursor = root["cursor"]) {
        m.cursor.image = cursor["image"] ? cursor["image"].as<std::string>() : std::string();
        m.cursor.interact =
            cursor["interact"] ? cursor["interact"].as<std::string>() : std::string();
        if (const YAML::Node hot = cursor["hotspot"]) {
            m.cursor.hotspot = {hot["x"] ? hot["x"].as<unsigned>() : 0u,
                                hot["y"] ? hot["y"].as<unsigned>() : 0u};
        }
    }

    if (const YAML::Node speech = root["speech"]) {
        if (!speech.IsMap()) {
            manifest_fail("manifest.speech-invalid", "'speech' must be a mapping", speech);
        }
        m.speech.font = speech["font"] ? speech["font"].as<std::string>() : std::string();
        if (speech["font_size"]) {
            m.speech.font_size = require_dimension(speech["font_size"], "speech.font_size");
        }
    }

    if (const YAML::Node dev = root["development"]) {
        m.development.edit_mode = dev["edit_mode"] ? dev["edit_mode"].as<bool>() : false;
        m.development.show_walkboxes =
            dev["show_walkboxes"] ? dev["show_walkboxes"].as<bool>() : false;
        m.development.show_hotspots =
            dev["show_hotspots"] ? dev["show_hotspots"].as<bool>() : false;
        m.development.show_anchors = dev["show_anchors"] ? dev["show_anchors"].as<bool>() : false;
        m.development.show_state = dev["show_state"] ? dev["show_state"].as<bool>() : false;
        m.development.allow_room_reload =
            dev["allow_room_reload"] ? dev["allow_room_reload"].as<bool>() : false;
        m.development.profiling = dev["profiling"] ? dev["profiling"].as<bool>() : false;
        if (dev["profiling_interval"]) {
            m.development.profiling_interval = dev["profiling_interval"].as<double>();
        }
    }

    if (!root["entry"] || root["entry"].as<std::string>().empty()) {
        manifest_fail("manifest.entry-missing", "'entry' is required", root);
    }
    m.entry = root["entry"].as<std::string>();

    const YAML::Node scenes = root["scenes"];
    if (!scenes || !scenes.IsSequence() || scenes.size() == 0) {
        manifest_fail("manifest.scenes-missing", "'scenes' must be a non-empty sequence", root);
    }

    std::set<std::string> seen_ids;
    for (const YAML::Node& sn : scenes) {
        SceneDesc desc;
        if (!sn["id"] || sn["id"].as<std::string>().empty()) {
            manifest_fail("manifest.scene-id-missing", "a scene is missing 'id'", sn);
        }
        desc.id = sn["id"].as<std::string>();
        if (!seen_ids.insert(desc.id).second) {
            manifest_fail("manifest.duplicate-scene-id",
                          "duplicate scene id '" + desc.id + "'",
                          sn);
        }
        if (!sn["type"] || sn["type"].as<std::string>().empty()) {
            manifest_fail("manifest.scene-type-missing",
                          "scene '" + desc.id + "' is missing 'type'",
                          sn);
        }
        desc.type = sn["type"].as<std::string>();

        if (const YAML::Node params = sn["parameters"]) {
            // Scalar parameters store directly; nested maps flatten into dotted
            // keys (e.g. `menu.position.x`) so scenes can read structured config.
            flatten_params(params, "", desc.parameters);
        }
        m.scenes.push_back(std::move(desc));
    }

    if (!m.find_scene(m.entry)) {
        manifest_fail("manifest.entry-not-in-scenes",
                      "entry scene '" + m.entry + "' is not in 'scenes'");
    }
    return m;
}

Manifest load_manifest(const std::string& file_path) {
    std::ifstream in(file_path, std::ios::binary);
    if (!in) {
        SourceLocation loc;
        loc.file = file_path;
        throw ManifestError("manifest-loader",
                            "manifest.file-unreadable",
                            "cannot open file '" + file_path + "'",
                            loc);
    }
    std::ostringstream ss;
    ss << in.rdbuf();

    Manifest m;
    try {
        m = parse_manifest(ss.str());
    } catch (LoadError& e) {
        e.with_file(file_path);
        throw;
    }

    std::filesystem::path src(m.resources_src);
    if (src.is_relative()) {
        src = std::filesystem::path(file_path).parent_path() / src;
    }
    m.resources_src = src.lexically_normal().string();
    return m;
}

} // namespace pac::core
