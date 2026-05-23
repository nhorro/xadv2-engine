#include "engine/pnc/cast.hpp"

#include "core/load_error_yaml.hpp"
#include "engine/pnc/data_error.hpp"

#include <yaml-cpp/yaml.h>

#include <utility>

namespace pac::pnc {

namespace {

constexpr const char* kSource = "cast-loader";

[[noreturn]] void
cast_fail(const std::string& code, const std::string& msg, const YAML::Node& at = YAML::Node()) {
    pac::core::fail_at<DataError>(kSource, code, msg, at);
}

} // namespace

const Appearance* Cast::appearance(const std::string& id) const {
    const auto it = appearances.find(id);
    return it != appearances.end() ? &it->second : nullptr;
}

const Character* Cast::character(const std::string& id) const {
    const auto it = characters.find(id);
    return it != characters.end() ? &it->second : nullptr;
}

Cast parse_cast(const std::string& yaml_text) {
    YAML::Node root;
    try {
        root = YAML::Load(yaml_text);
    } catch (const YAML::Exception& e) {
        cast_fail("cast.invalid-yaml", std::string("invalid YAML: ") + e.what());
    }
    if (!root || !root.IsMap()) {
        cast_fail("cast.root-not-map", "root must be a mapping");
    }

    Cast cast;

    if (const YAML::Node appearances = root["appearances"]) {
        for (const auto& kv : appearances) {
            Appearance app;
            const YAML::Node node = kv.second;
            app.type = node["type"] ? node["type"].as<std::string>() : std::string();
            if (app.type.empty()) {
                cast_fail("cast.appearance-type-missing",
                          "appearance '" + kv.first.as<std::string>() + "' is missing 'type'",
                          node);
            }
            app.sprite = node["sprite"] ? node["sprite"].as<std::string>() : std::string();
            app.composite = node["composite"] ? node["composite"].as<std::string>() : std::string();
            cast.appearances.emplace(kv.first.as<std::string>(), std::move(app));
        }
    }

    if (const YAML::Node characters = root["characters"]) {
        for (const auto& kv : characters) {
            Character ch;
            ch.id = kv.first.as<std::string>();
            const YAML::Node node = kv.second;
            if (!node["appearance"]) {
                cast_fail("cast.character-appearance-missing",
                          "character '" + ch.id + "' is missing 'appearance'",
                          node);
            }
            ch.appearance = node["appearance"].as<std::string>();
            ch.name = node["name"] ? node["name"].as<std::string>() : ch.id;
            if (const YAML::Node color = node["speech_color"]) {
                ch.speech_color = sf::Color(color["r"].as<unsigned>(),
                                            color["g"].as<unsigned>(),
                                            color["b"].as<unsigned>());
            }
            cast.characters.emplace(ch.id, std::move(ch));
        }
    }

    return cast;
}

} // namespace pac::pnc
