#include "engine/gfx/spritesheet.hpp"

#include "engine/core/resource_cache.hpp"
#include "engine/core/resource_source.hpp"
#include "engine/gfx/asset_error.hpp"

#include <yaml-cpp/yaml.h>

#include <utility>

namespace pac::gfx {

const sf::Vector2f* Frame::anchor(const std::string& name) const {
    const auto it = anchors.find(name);
    return it != anchors.end() ? &it->second : nullptr;
}

const Frame* SpritesheetData::frame(const std::string& id) const {
    const auto it = frames.find(id);
    return it != frames.end() ? &it->second : nullptr;
}

SpritesheetData parse_spritesheet(const std::string& yaml_text) {
    YAML::Node root;
    try {
        root = YAML::Load(yaml_text);
    } catch (const YAML::Exception& e) {
        throw AssetError(std::string("spritesheet: invalid YAML: ") + e.what());
    }

    SpritesheetData data;
    if (root["image"]) {
        data.image = root["image"].as<std::string>();
    }
    if (const YAML::Node size = root["size"]) {
        data.size = {size["width"].as<unsigned>(), size["height"].as<unsigned>()};
    }

    const YAML::Node sprites = root["sprites"];
    if (!sprites || !sprites.IsSequence()) {
        throw AssetError("spritesheet: 'sprites' must be a sequence");
    }
    for (const YAML::Node& sn : sprites) {
        if (!sn["id"]) {
            throw AssetError("spritesheet: a sprite is missing 'id'");
        }
        const std::string id = sn["id"].as<std::string>();
        const YAML::Node r = sn["rect"];
        if (!r) {
            throw AssetError("spritesheet: sprite '" + id + "' is missing 'rect'");
        }
        Frame frame;
        frame.rect = sf::IntRect(r["x"].as<int>(),
                                 r["y"].as<int>(),
                                 r["width"].as<int>(),
                                 r["height"].as<int>());
        if (const YAML::Node anchors = sn["anchors"]) {
            for (const auto& kv : anchors) {
                frame.anchors[kv.first.as<std::string>()] = {kv.second["x"].as<float>(),
                                                             kv.second["y"].as<float>()};
            }
        }
        data.frames.emplace(id, std::move(frame));
    }
    return data;
}

Spritesheet::Spritesheet(SpritesheetData data, const sf::Texture& texture)
    : data_(std::move(data)), texture_(&texture) {}

Spritesheet load_spritesheet(pac::core::ResourceCache& res, const std::string& logical) {
    SpritesheetData data = parse_spritesheet(res.read_text(logical));
    const std::string image_logical =
        pac::core::logical_join(pac::core::logical_dir(logical), data.image);
    const sf::Texture& texture = res.texture(image_logical);
    return Spritesheet(std::move(data), texture);
}

} // namespace pac::gfx
