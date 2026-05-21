#include "engine/gfx/animation.hpp"

#include "engine/gfx/asset_error.hpp"

#include <yaml-cpp/yaml.h>

#include <utility>

namespace pac::gfx {

const Sequence* Animation::sequence(const std::string& name) const {
    const auto it = sequences.find(name);
    return it != sequences.end() ? &it->second : nullptr;
}

Animation parse_animation(const std::string& yaml_text) {
    YAML::Node root;
    try {
        root = YAML::Load(yaml_text);
    } catch (const YAML::Exception& e) {
        throw AssetError(std::string("animation: invalid YAML: ") + e.what());
    }

    Animation anim;
    if (root["spritesheet"]) {
        anim.spritesheet = root["spritesheet"].as<std::string>();
    }
    if (root["pivot"]) {
        anim.pivot = root["pivot"].as<std::string>();
    }

    const YAML::Node sequences = root["sequences"];
    if (!sequences || !sequences.IsMap()) {
        throw AssetError("animation: 'sequences' must be a mapping");
    }
    for (const auto& kv : sequences) {
        Sequence seq;
        const YAML::Node node = kv.second;
        seq.loop = node["loop"] ? node["loop"].as<bool>() : false;
        if (const YAML::Node frames = node["frames"]) {
            for (const YAML::Node& fn : frames) {
                if (!fn["sprite"]) {
                    throw AssetError("animation: a frame is missing 'sprite'");
                }
                FrameRef ref;
                ref.sprite = fn["sprite"].as<std::string>();
                ref.duration = fn["duration"] ? fn["duration"].as<float>() : 0.1f;
                seq.frames.push_back(std::move(ref));
            }
        }
        anim.sequences.emplace(kv.first.as<std::string>(), std::move(seq));
    }
    return anim;
}

} // namespace pac::gfx
