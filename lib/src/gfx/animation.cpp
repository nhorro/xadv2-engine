#include "engine/gfx/animation.hpp"

#include "core/load_error_yaml.hpp"
#include "engine/gfx/asset_error.hpp"

#include <yaml-cpp/yaml.h>

#include <utility>

namespace pac::gfx {

namespace {

constexpr const char* kSource = "anim-loader";

[[noreturn]] void
anim_fail(const std::string& code, const std::string& msg, const YAML::Node& at = YAML::Node()) {
    pac::core::fail_at<AssetError>(kSource, code, msg, at);
}

} // namespace

const Sequence* Animation::sequence(const std::string& name) const {
    const auto it = sequences.find(name);
    return it != sequences.end() ? &it->second : nullptr;
}

Animation parse_animation(const std::string& yaml_text) {
    YAML::Node root;
    try {
        root = YAML::Load(yaml_text);
    } catch (const YAML::Exception& e) {
        anim_fail("anim.invalid-yaml", std::string("invalid YAML: ") + e.what());
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
        anim_fail("anim.sequences-not-map", "'sequences' must be a mapping", root);
    }
    for (const auto& kv : sequences) {
        Sequence seq;
        const YAML::Node node = kv.second;
        seq.loop = node["loop"] ? node["loop"].as<bool>() : false;
        seq.h_mirror = node["h_mirror"] ? node["h_mirror"].as<bool>() : false;
        if (const YAML::Node frames = node["frames"]) {
            for (const YAML::Node& fn : frames) {
                if (!fn["sprite"]) {
                    anim_fail("anim.frame-sprite-missing", "a frame is missing 'sprite'", fn);
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
