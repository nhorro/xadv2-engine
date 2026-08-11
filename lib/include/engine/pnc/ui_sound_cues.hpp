#pragma once

#include <string>

namespace pac::core {
struct EngineContext;
class SceneParams;
} // namespace pac::core

namespace pac::pnc {

/// Optional, manifest-driven sounds shared by menu-like scenes.
/// Keys below `prefix` are `selection`, `activate`, `selection_volume`, and
/// `activate_volume`. Missing paths keep built-in scenes silent.
class UiSoundCues {
public:
    explicit UiSoundCues(const pac::core::SceneParams& params,
                         const std::string& prefix = "sounds");

    void selection(pac::core::EngineContext& ctx) const;
    void activate(pac::core::EngineContext& ctx) const;

private:
    std::string selection_;
    std::string activate_;
    float selection_volume_ = 0.25f;
    float activate_volume_ = 0.45f;
};

} // namespace pac::pnc
