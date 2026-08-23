#include "engine/core/manifest.hpp"

#include "core/load_error_yaml.hpp"
#include "engine/core/resource_source.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cmath>
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

const ChapterDesc* Manifest::find_chapter(const std::string& id) const {
    for (const auto& chapter : chapters) {
        if (chapter.id == id) {
            return &chapter;
        }
    }
    return nullptr;
}

const ChapterDesc* Manifest::chapter_for_scene(const std::string& scene_id) const {
    for (const auto& chapter : chapters) {
        if (chapter.scene == scene_id) {
            return &chapter;
        }
    }
    return nullptr;
}

const ChapterDesc* Manifest::next_chapter(const std::string& id) const {
    for (std::size_t i = 0; i + 1 < chapters.size(); ++i) {
        if (chapters[i].id == id) {
            return &chapters[i + 1];
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

sf::Color parse_cursor_color(const YAML::Node& node, const std::string& field, sf::Color fallback) {
    if (!node) {
        return fallback;
    }
    if (!node.IsMap() || !node["r"] || !node["g"] || !node["b"]) {
        manifest_fail("manifest.cursor-blink-color-invalid",
                      "'cursor.blink." + field + "' must contain r, g, and b",
                      node);
    }
    const int r = node["r"].as<int>();
    const int g = node["g"].as<int>();
    const int b = node["b"].as<int>();
    if (r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255) {
        manifest_fail("manifest.cursor-blink-color-invalid",
                      "'cursor.blink." + field + "' channels must be between 0 and 255",
                      node);
    }
    return {static_cast<sf::Uint8>(r), static_cast<sf::Uint8>(g), static_cast<sf::Uint8>(b)};
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
            e.translations_path =
                ln["translations"] ? ln["translations"].as<std::string>() : std::string();
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
    m.languages.push_back({m.default_language, "", m.strings_path, ""});
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

YAML::Node merge_maps(const YAML::Node& base, const YAML::Node& overlay) {
    YAML::Node out = base && base.IsMap() ? YAML::Clone(base) : YAML::Node(YAML::NodeType::Map);
    if (!overlay || !overlay.IsMap()) {
        return out;
    }
    for (const auto& kv : overlay) {
        const std::string key = kv.first.as<std::string>();
        if (out[key] && out[key].IsMap() && kv.second.IsMap()) {
            out[key] = merge_maps(out[key], kv.second);
        } else {
            out[key] = YAML::Clone(kv.second);
        }
    }
    return out;
}

std::string resolve_declared_path(const std::string& value,
                                  const std::string& declaring_file,
                                  const YAML::Node& at) {
    if (value.empty() || (value.front() != '/' && !value.starts_with("./"))) {
        return value;
    }
    std::filesystem::path path;
    if (value.front() == '/') {
        path = std::filesystem::path(value.substr(1));
    } else {
        path = std::filesystem::path(logical_dir(declaring_file)) / value.substr(2);
    }
    const std::string logical = path.lexically_normal().generic_string();
    if (!is_valid_logical_path(logical)) {
        manifest_fail("manifest.relative-path-invalid",
                      "path '" + value + "' in '" + declaring_file +
                          "' escapes the resource root or is not a logical path",
                      at);
    }
    return logical;
}

void resolve_declared_paths(YAML::Node node, const std::string& declaring_file) {
    if (!node) {
        return;
    }
    if (node.IsScalar()) {
        const std::string value = node.as<std::string>();
        node = resolve_declared_path(value, declaring_file, node);
        return;
    }
    if (node.IsSequence()) {
        for (std::size_t i = 0; i < node.size(); ++i) {
            resolve_declared_paths(node[i], declaring_file);
        }
        return;
    }
    if (node.IsMap()) {
        for (auto kv : node) {
            resolve_declared_paths(kv.second, declaring_file);
        }
    }
}

YAML::Node scene_fields(const YAML::Node& declaration) {
    YAML::Node out(YAML::NodeType::Map);
    if (const YAML::Node parameters = declaration["parameters"]) {
        if (!parameters.IsMap()) {
            manifest_fail("manifest.scene-parameters-invalid",
                          "scene 'parameters' must be a mapping",
                          parameters);
        }
        out = merge_maps(out, parameters);
    }
    static const std::set<std::string> reserved = {"id", "type", "profile", "source", "parameters"};
    for (const auto& kv : declaration) {
        const std::string key = kv.first.as<std::string>();
        if (!reserved.contains(key)) {
            out[key] = YAML::Clone(kv.second);
        }
    }
    return out;
}

YAML::Node defaults_for(const YAML::Node& defaults, const std::string& type) {
    YAML::Node out(YAML::NodeType::Map);
    if (!defaults || defaults.IsNull()) {
        return out;
    }
    if (!defaults.IsMap()) {
        manifest_fail("manifest.scene-defaults-invalid",
                      "'scene_defaults' must be a mapping",
                      defaults);
    }
    out = merge_maps(out, defaults["all"]);
    out = merge_maps(out, defaults[type]);
    return out;
}

YAML::Node expand_scene(const YAML::Node& declaration,
                        const YAML::Node& game_defaults,
                        const YAML::Node& local_defaults,
                        const YAML::Node& game_profiles,
                        const YAML::Node& local_profiles,
                        const ResourceSource& resources) {
    if (!declaration || !declaration.IsMap()) {
        manifest_fail("manifest.scene-invalid",
                      "a scene declaration must be a mapping",
                      declaration);
    }
    if (!declaration["id"] || declaration["id"].as<std::string>().empty()) {
        manifest_fail("manifest.scene-id-missing", "a scene is missing 'id'", declaration);
    }

    YAML::Node profile;
    if (declaration["profile"]) {
        const std::string name = declaration["profile"].as<std::string>();
        profile =
            local_profiles && local_profiles[name] ? local_profiles[name] : game_profiles[name];
        if (!profile) {
            manifest_fail("manifest.scene-profile-unknown",
                          "scene '" + declaration["id"].as<std::string>() +
                              "' uses unknown profile '" + name + "'",
                          declaration["profile"]);
        }
        if (!profile.IsMap()) {
            manifest_fail("manifest.scene-profile-invalid",
                          "scene profile '" + name + "' must be a mapping",
                          profile);
        }
    }

    const std::string type =
        declaration["type"]
            ? declaration["type"].as<std::string>()
            : (profile && profile["type"] ? profile["type"].as<std::string>() : std::string());
    if (type.empty()) {
        manifest_fail("manifest.scene-type-missing",
                      "scene '" + declaration["id"].as<std::string>() + "' is missing 'type'",
                      declaration);
    }

    YAML::Node parameters = defaults_for(game_defaults, type);
    parameters = merge_maps(parameters, defaults_for(local_defaults, type));
    if (profile) {
        parameters = merge_maps(parameters, scene_fields(profile));
    }
    parameters = merge_maps(parameters, scene_fields(declaration));

    if (declaration["source"]) {
        const std::string source = declaration["source"].as<std::string>();
        if (type != "CloseUp") {
            manifest_fail("manifest.scene-source-unsupported",
                          "the concise 'source' convention is only defined for CloseUp scenes",
                          declaration["source"]);
        }
        const bool yaml_file = source.ends_with(".yaml") || source.ends_with(".yml");
        const std::string data = yaml_file ? source : source + "/closeup.yml";
        if (!resources.exists(data)) {
            manifest_fail("manifest.closeup-source-missing",
                          "CloseUp source does not contain '" + data + "'",
                          declaration["source"]);
        }
        parameters["data"] = data;
        if (!parameters["logic"]) {
            const std::string logic = logical_dir(data) + "/logic.lua";
            if (resources.exists(logic)) {
                parameters["logic"] = logic;
            }
        }
    }

    YAML::Node out(YAML::NodeType::Map);
    out["id"] = declaration["id"].as<std::string>();
    out["type"] = type;
    if (parameters.size() > 0) {
        out["parameters"] = parameters;
    }
    return out;
}

YAML::Node load_yaml_resource(const ResourceSource& resources, const std::string& logical_path) {
    if (!is_valid_logical_path(logical_path)) {
        manifest_fail("manifest.import-path-invalid",
                      "manifest import is not a logical resource path: '" + logical_path + "'");
    }
    YAML::Node document;
    try {
        document = YAML::Load(resources.read_text(logical_path));
    } catch (const YAML::Exception& e) {
        manifest_fail("manifest.invalid-yaml",
                      "invalid YAML in '" + logical_path + "': " + e.what());
    } catch (const std::exception& e) {
        manifest_fail("manifest.import-unreadable",
                      "cannot read manifest resource '" + logical_path + "': " + e.what());
    }
    if (!document || !document.IsMap()) {
        manifest_fail("manifest.root-not-map", "'" + logical_path + "' must contain a mapping");
    }
    resolve_declared_paths(document, logical_path);
    return document;
}

std::string compose_manifest(const ResourceSource& resources, const std::string& logical_path) {
    YAML::Node root = load_yaml_resource(resources, logical_path);
    const int version = root["version"] ? root["version"].as<int>() : 1;
    if (version < 2) {
        return resources.read_text(logical_path);
    }

    YAML::Node combined = YAML::Clone(root);
    YAML::Node scenes(YAML::NodeType::Sequence);
    const YAML::Node game_defaults = root["scene_defaults"];
    const YAML::Node game_profiles = root["scene_profiles"];
    if (const YAML::Node common = root["scenes"]) {
        if (!common.IsSequence()) {
            manifest_fail("manifest.scenes-invalid", "'scenes' must be a sequence", common);
        }
        for (const YAML::Node& scene : common) {
            scenes.push_back(expand_scene(scene,
                                          game_defaults,
                                          YAML::Node(),
                                          game_profiles,
                                          YAML::Node(),
                                          resources));
        }
    }

    YAML::Node chapters(YAML::NodeType::Sequence);
    const YAML::Node imports = root["chapters"];
    if (imports) {
        if (!imports.IsSequence() || imports.size() == 0) {
            manifest_fail("manifest.chapters-invalid",
                          "'chapters' must be a non-empty sequence",
                          imports);
        }
        for (const YAML::Node& import : imports) {
            std::string chapter_path;
            if (import.IsScalar()) {
                chapter_path = import.as<std::string>();
            } else if (import.IsMap() && import["source"]) {
                chapter_path = import["source"].as<std::string>();
            } else {
                manifest_fail("manifest.chapter-import-invalid",
                              "a version-2 chapter entry must be a path or contain 'source'",
                              import);
            }
            YAML::Node chapter = load_yaml_resource(resources, chapter_path);
            if (!chapter["id"] || chapter["id"].as<std::string>().empty()) {
                manifest_fail("manifest.chapter-id-missing",
                              "chapter '" + chapter_path + "' is missing 'id'",
                              chapter);
            }
            if (!chapter["facts"] || chapter["facts"].as<std::string>().empty()) {
                manifest_fail("manifest.chapter-facts-empty",
                              "chapter '" + chapter["id"].as<std::string>() +
                                  "' must declare 'facts'",
                              chapter);
            }
            const YAML::Node room = chapter["room"];
            if (!room || !room.IsMap() || !room["id"]) {
                manifest_fail("manifest.chapter-room-missing",
                              "chapter '" + chapter["id"].as<std::string>() +
                                  "' must declare 'room.id'",
                              chapter);
            }

            YAML::Node room_scene(YAML::NodeType::Map);
            room_scene["id"] = room["id"].as<std::string>();
            room_scene["type"] = "RoomScene";
            for (const char* field :
                 {"cast", "rooms", "dialogs", "inventory", "inventory_logic", "logic"}) {
                if (chapter[field]) {
                    room_scene[field] = YAML::Clone(chapter[field]);
                }
            }
            for (const auto& kv : room) {
                const std::string key = kv.first.as<std::string>();
                if (key == "id") {
                    continue;
                }
                room_scene[key == "start" ? "start_room" : key] = YAML::Clone(kv.second);
            }

            const YAML::Node local_defaults = chapter["scene_defaults"];
            const YAML::Node local_profiles = chapter["scene_profiles"];
            scenes.push_back(expand_scene(room_scene,
                                          game_defaults,
                                          local_defaults,
                                          game_profiles,
                                          local_profiles,
                                          resources));
            if (const YAML::Node local_scenes = chapter["scenes"]) {
                if (!local_scenes.IsSequence()) {
                    manifest_fail("manifest.scenes-invalid",
                                  "chapter 'scenes' must be a sequence",
                                  local_scenes);
                }
                for (const YAML::Node& scene : local_scenes) {
                    scenes.push_back(expand_scene(scene,
                                                  game_defaults,
                                                  local_defaults,
                                                  game_profiles,
                                                  local_profiles,
                                                  resources));
                }
            }

            YAML::Node chapter_desc(YAML::NodeType::Map);
            chapter_desc["id"] = chapter["id"].as<std::string>();
            chapter_desc["scene"] = room["id"].as<std::string>();
            chapter_desc["facts"] = chapter["facts"].as<std::string>();
            chapters.push_back(chapter_desc);
            if (!combined["facts"]) {
                combined["facts"] = chapter["facts"].as<std::string>();
            }
        }
    }

    combined["scenes"] = scenes;
    if (chapters.size() > 0) {
        combined["chapters"] = chapters;
    } else {
        combined.remove("chapters");
    }
    combined.remove("scene_defaults");
    combined.remove("scene_profiles");
    YAML::Emitter emitter;
    emitter << combined;
    return emitter.c_str();
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
    m.title = root["title"] ? root["title"].as<std::string>() : m.id;
    if (m.title.empty()) {
        manifest_fail("manifest.title-empty", "'title' must not be empty", root["title"]);
    }

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

    if (root["facts"]) {
        m.facts_path = root["facts"].as<std::string>();
        if (m.facts_path.empty()) {
            manifest_fail("manifest.facts-empty", "'facts' must not be empty", root["facts"]);
        }
    }

    parse_languages(root, m);

    if (const YAML::Node settings = root["settings"]) {
        if (const YAML::Node audio = settings["audio"]) {
            if (audio["music_volume"]) {
                m.settings.music_volume = audio["music_volume"].as<float>();
            }
            if (audio["sfx_volume"]) {
                m.settings.sfx_volume = audio["sfx_volume"].as<float>();
            }
            if (audio["speech_enabled"]) {
                m.settings.speech_enabled = audio["speech_enabled"].as<bool>();
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
        if (const YAML::Node blink = cursor["blink"]) {
            if (!blink.IsMap() || !blink["interval"]) {
                manifest_fail("manifest.cursor-blink-invalid",
                              "'cursor.blink' must be a mapping with an interval",
                              blink);
            }
            m.cursor.blink.interval = blink["interval"].as<float>();
            if (!std::isfinite(m.cursor.blink.interval) || m.cursor.blink.interval <= 0.0f) {
                manifest_fail("manifest.cursor-blink-interval-invalid",
                              "'cursor.blink.interval' must be greater than zero",
                              blink["interval"]);
            }
            m.cursor.blink.steps = blink["steps"] ? blink["steps"].as<unsigned>() : 12u;
            if (m.cursor.blink.steps < 2 || m.cursor.blink.steps > 32) {
                manifest_fail("manifest.cursor-blink-steps-invalid",
                              "'cursor.blink.steps' must be between 2 and 32",
                              blink["steps"]);
            }
            m.cursor.blink.dark = parse_cursor_color(blink["dark"], "dark", m.cursor.blink.dark);
            m.cursor.blink.light =
                parse_cursor_color(blink["light"], "light", m.cursor.blink.light);
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
        m.speech.voice_directory =
            speech["voice_directory"] ? speech["voice_directory"].as<std::string>() : std::string();
        if (!m.speech.voice_directory.empty() && !is_valid_logical_path(m.speech.voice_directory)) {
            manifest_fail("manifest.speech-voice-directory-invalid",
                          "'speech.voice_directory' must be a logical resource path",
                          speech["voice_directory"]);
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
        m.development.warn_missing_translations =
            dev["warn_missing_translations"] ? dev["warn_missing_translations"].as<bool>() : false;
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

    if (const YAML::Node chapters = root["chapters"]) {
        if (!chapters.IsSequence() || chapters.size() == 0) {
            manifest_fail("manifest.chapters-invalid",
                          "'chapters' must be a non-empty sequence",
                          chapters);
        }
        std::set<std::string> chapter_ids;
        std::set<std::string> chapter_scenes;
        for (const YAML::Node& cn : chapters) {
            ChapterDesc chapter;
            if (!cn.IsMap() || !cn["id"] || cn["id"].as<std::string>().empty()) {
                manifest_fail("manifest.chapter-id-missing",
                              "a 'chapters' entry is missing 'id'",
                              cn);
            }
            chapter.id = cn["id"].as<std::string>();
            validate_id(chapter.id, cn["id"]);
            if (!chapter_ids.insert(chapter.id).second) {
                manifest_fail("manifest.duplicate-chapter-id",
                              "duplicate chapter id '" + chapter.id + "'",
                              cn);
            }
            if (!cn["scene"] || cn["scene"].as<std::string>().empty()) {
                manifest_fail("manifest.chapter-scene-missing",
                              "chapter '" + chapter.id + "' is missing 'scene'",
                              cn);
            }
            chapter.scene = cn["scene"].as<std::string>();
            if (!m.find_scene(chapter.scene)) {
                manifest_fail("manifest.chapter-scene-unknown",
                              "chapter '" + chapter.id + "' references unknown scene '" +
                                  chapter.scene + "'",
                              cn["scene"]);
            }
            if (!chapter_scenes.insert(chapter.scene).second) {
                manifest_fail("manifest.duplicate-chapter-scene",
                              "scene '" + chapter.scene + "' belongs to more than one chapter",
                              cn["scene"]);
            }
            chapter.facts_path = cn["facts"] ? cn["facts"].as<std::string>() : m.facts_path;
            if (chapter.facts_path.empty()) {
                manifest_fail("manifest.chapter-facts-empty",
                              "chapter '" + chapter.id + "' has an empty 'facts' path",
                              cn);
            }
            m.chapters.push_back(std::move(chapter));
        }
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
    YAML::Node root;
    try {
        root = YAML::Load(ss.str());
    } catch (const YAML::Exception& e) {
        SourceLocation loc;
        loc.file = file_path;
        throw ManifestError("manifest-loader",
                            "manifest.invalid-yaml",
                            std::string("invalid YAML: ") + e.what(),
                            loc);
    }
    if (!root || !root.IsMap()) {
        SourceLocation loc;
        loc.file = file_path;
        throw ManifestError("manifest-loader",
                            "manifest.root-not-map",
                            "root must be a mapping",
                            loc);
    }
    const YAML::Node resources = root["resources"];
    if (!resources || !resources["src"] || resources["src"].as<std::string>().empty()) {
        SourceLocation loc;
        loc.file = file_path;
        throw ManifestError("manifest-loader",
                            "manifest.resources-src-missing",
                            "'resources.src' is required",
                            loc);
    }

    std::filesystem::path src(resources["src"].as<std::string>());
    if (src.is_relative()) {
        src = std::filesystem::path(file_path).parent_path() / src;
    }
    src = src.lexically_normal();
    std::error_code ec;
    const std::filesystem::path logical_host =
        std::filesystem::relative(std::filesystem::path(file_path).lexically_normal(), src, ec);
    if (ec || !is_valid_logical_path(logical_host.generic_string())) {
        SourceLocation loc;
        loc.file = file_path;
        throw ManifestError("manifest-loader",
                            "manifest.file-outside-resource-root",
                            "manifest must live below its declared resource root",
                            loc);
    }
    FilesystemResourceSource source(src.string());
    Manifest m;
    try {
        m = load_manifest(source, logical_host.generic_string());
    } catch (LoadError& e) {
        e.with_file(file_path);
        throw;
    }
    m.resources_src = src.string();
    return m;
}

Manifest load_manifest(ResourceSource& resources, const std::string& logical_path) {
    try {
        return parse_manifest(compose_manifest(resources, logical_path));
    } catch (LoadError& e) {
        if (e.location().file.empty()) {
            e.with_file(logical_path);
        }
        throw;
    }
}

} // namespace pac::core
