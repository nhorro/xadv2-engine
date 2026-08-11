#include "engine/pnc/ui_sound_cues.hpp"

#include "engine/core/audio.hpp"
#include "engine/core/engine_context.hpp"
#include "engine/core/scene_params.hpp"

#include <algorithm>
#include <cmath>
#include <exception>

namespace pac::pnc {
namespace {

float volume_or(const pac::core::SceneParams& params,
                const std::string& key,
                float fallback) {
    const auto value = params.get(key);
    if (!value) {
        return fallback;
    }
    try {
        const float parsed = std::stof(*value);
        return std::isfinite(parsed) ? std::clamp(parsed, 0.0f, 1.0f) : fallback;
    } catch (const std::exception&) {
        return fallback;
    }
}

} // namespace

UiSoundCues::UiSoundCues(const pac::core::SceneParams& params, const std::string& prefix) {
    const std::string base = prefix.empty() ? std::string() : prefix + ".";
    selection_ = params.get_or(base + "selection", "");
    activate_ = params.get_or(base + "activate", "");
    selection_volume_ = volume_or(params, base + "selection_volume", selection_volume_);
    activate_volume_ = volume_or(params, base + "activate_volume", activate_volume_);
}

void UiSoundCues::selection(pac::core::EngineContext& ctx) const {
    if (!selection_.empty()) {
        ctx.audio.sfx.play(selection_, selection_volume_);
    }
}

void UiSoundCues::activate(pac::core::EngineContext& ctx) const {
    if (!activate_.empty()) {
        ctx.audio.sfx.play(activate_, activate_volume_);
    }
}

} // namespace pac::pnc
