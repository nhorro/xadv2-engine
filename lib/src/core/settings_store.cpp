#include "engine/core/settings_store.hpp"

#include "engine/core/diagnostics.hpp"
#include "engine/core/settings.hpp"

#include <yaml-cpp/yaml.h>

#include <fstream>
#include <sstream>

namespace pac::core {

bool parse_settings_into(const std::string& yaml_text, Settings& settings) {
    YAML::Node root;
    try {
        root = YAML::Load(yaml_text);
    } catch (const YAML::Exception&) {
        return false;
    }
    if (!root || !root.IsMap()) {
        return false;
    }

    if (const YAML::Node audio = root["audio"]) {
        if (audio["music_volume"]) {
            settings.audio.music_volume = audio["music_volume"].as<float>();
        }
        if (audio["sfx_volume"]) {
            settings.audio.sfx_volume = audio["sfx_volume"].as<float>();
        }
    }
    if (const YAML::Node display = root["display"]) {
        if (display["fullscreen"]) {
            settings.fullscreen = display["fullscreen"].as<bool>();
        }
        if (display["width"]) {
            settings.window_width = display["width"].as<unsigned>();
        }
        if (display["height"]) {
            settings.window_height = display["height"].as<unsigned>();
        }
    }
    if (root["language"]) {
        settings.language = root["language"].as<std::string>();
    }

    settings.clamp();
    return true;
}

std::string serialize_settings(const Settings& settings) {
    YAML::Emitter out;
    out << YAML::BeginMap;
    out << YAML::Key << "version" << YAML::Value << 1;
    out << YAML::Key << "audio" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "music_volume" << YAML::Value << settings.audio.music_volume;
    out << YAML::Key << "sfx_volume" << YAML::Value << settings.audio.sfx_volume;
    out << YAML::EndMap;
    out << YAML::Key << "display" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "fullscreen" << YAML::Value << settings.fullscreen;
    out << YAML::Key << "width" << YAML::Value << settings.window_width;
    out << YAML::Key << "height" << YAML::Value << settings.window_height;
    out << YAML::EndMap;
    if (!settings.language.empty()) {
        out << YAML::Key << "language" << YAML::Value << settings.language;
    }
    out << YAML::EndMap;
    return std::string(out.c_str()) + "\n";
}

SettingsStore::SettingsStore(std::filesystem::path path, Diagnostics& log)
    : path_(std::move(path)), log_(log) {}

void SettingsStore::load(Settings& settings) const {
    std::ifstream in(path_, std::ios::binary);
    if (!in) {
        return; // first run: no file yet, keep manifest defaults
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    if (!parse_settings_into(ss.str(), settings)) {
        log_.warn("settings: ignoring unreadable settings file '" + path_.string() + "'");
    }
}

bool SettingsStore::save(const Settings& settings) const {
    std::ofstream out(path_, std::ios::binary | std::ios::trunc);
    if (!out) {
        log_.warn("settings: could not open '" + path_.string() + "' for writing");
        return false;
    }
    out << serialize_settings(settings);
    if (!out) {
        log_.warn("settings: failed to write '" + path_.string() + "'");
        return false;
    }
    return true;
}

} // namespace pac::core
