#include "engine/core/manifest.hpp"

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

[[noreturn]] void fail(const std::string& msg) {
    throw ManifestError("manifest: " + msg);
}

unsigned require_dimension(const YAML::Node& node, const char* what) {
    if (!node) {
        fail(std::string(what) + " is required");
    }
    const auto value = node.as<long long>();
    if (value <= 0) {
        fail(std::string(what) + " must be a positive integer");
    }
    return static_cast<unsigned>(value);
}

} // namespace

Manifest parse_manifest(const std::string& yaml_text) {
    YAML::Node root;
    try {
        root = YAML::Load(yaml_text);
    } catch (const YAML::Exception& e) {
        fail(std::string("invalid YAML: ") + e.what());
    }
    if (!root || !root.IsMap()) {
        fail("root must be a mapping");
    }

    Manifest m;
    m.version = root["version"] ? root["version"].as<int>() : 1;

    const YAML::Node res = root["resolution"];
    if (!res) {
        fail("'resolution' is required");
    }
    m.resolution = {require_dimension(res["width"], "resolution.width"),
                    require_dimension(res["height"], "resolution.height")};

    const YAML::Node win = root["window"];
    if (!win) {
        fail("'window' is required");
    }
    m.window.fullscreen = win["fullscreen"] ? win["fullscreen"].as<bool>() : false;
    m.window.width = win["width"] ? win["width"].as<unsigned>() : m.resolution.x;
    m.window.height = win["height"] ? win["height"].as<unsigned>() : m.resolution.y;

    const YAML::Node resources = root["resources"];
    if (!resources || !resources["src"] || resources["src"].as<std::string>().empty()) {
        fail("'resources.src' is required");
    }
    m.resources_src = resources["src"].as<std::string>();

    if (!root["strings"] || root["strings"].as<std::string>().empty()) {
        fail("'strings' is required");
    }
    m.strings_path = root["strings"].as<std::string>();

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

    if (const YAML::Node dev = root["development"]) {
        m.development.edit_mode = dev["edit_mode"] ? dev["edit_mode"].as<bool>() : false;
        m.development.show_walkboxes =
            dev["show_walkboxes"] ? dev["show_walkboxes"].as<bool>() : false;
        m.development.show_hotspots =
            dev["show_hotspots"] ? dev["show_hotspots"].as<bool>() : false;
        m.development.allow_room_reload =
            dev["allow_room_reload"] ? dev["allow_room_reload"].as<bool>() : false;
    }

    if (!root["entry"] || root["entry"].as<std::string>().empty()) {
        fail("'entry' is required");
    }
    m.entry = root["entry"].as<std::string>();

    const YAML::Node scenes = root["scenes"];
    if (!scenes || !scenes.IsSequence() || scenes.size() == 0) {
        fail("'scenes' must be a non-empty sequence");
    }

    std::set<std::string> seen_ids;
    for (const YAML::Node& sn : scenes) {
        SceneDesc desc;
        if (!sn["id"] || sn["id"].as<std::string>().empty()) {
            fail("a scene is missing 'id'");
        }
        desc.id = sn["id"].as<std::string>();
        if (!seen_ids.insert(desc.id).second) {
            fail("duplicate scene id '" + desc.id + "'");
        }
        if (!sn["type"] || sn["type"].as<std::string>().empty()) {
            fail("scene '" + desc.id + "' is missing 'type'");
        }
        desc.type = sn["type"].as<std::string>();

        if (const YAML::Node params = sn["parameters"]) {
            for (const auto& kv : params) {
                if (kv.second.IsScalar()) {
                    desc.parameters.set(kv.first.as<std::string>(), kv.second.as<std::string>());
                }
                // Nested/structured parameters (e.g. RoomScene) are parsed in
                // their owning milestone; M0 scenes use scalar parameters only.
            }
        }
        m.scenes.push_back(std::move(desc));
    }

    if (!m.find_scene(m.entry)) {
        fail("entry scene '" + m.entry + "' is not in 'scenes'");
    }
    return m;
}

Manifest load_manifest(const std::string& file_path) {
    std::ifstream in(file_path, std::ios::binary);
    if (!in) {
        throw ManifestError("manifest: cannot open file '" + file_path + "'");
    }
    std::ostringstream ss;
    ss << in.rdbuf();

    Manifest m = parse_manifest(ss.str());

    std::filesystem::path src(m.resources_src);
    if (src.is_relative()) {
        src = std::filesystem::path(file_path).parent_path() / src;
    }
    m.resources_src = src.lexically_normal().string();
    return m;
}

} // namespace pac::core
