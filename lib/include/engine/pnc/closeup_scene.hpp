#pragma once

#include "engine/core/scene.hpp"
#include "engine/geom/geometry.hpp"
#include "engine/pnc/closeup.hpp"
#include "engine/pnc/speech_manager.hpp"

#include <string>

namespace sf {
class Font;
}

namespace pac::core {
struct EngineContext;
class SceneParams;
} // namespace pac::core

namespace pac::pnc {

/// Full-screen close-up / examine view (issue #76, design 01 R2 / 04 §Genre
/// scenes): a background that fills the screen, hotspot hit-testing, and a single
/// action per hotspot — a `look` caption (its localized name) or a `goto` scene
/// transition. No SCUMM panel, no avatar, no walking. Pushed as an opaque overlay
/// over the calling scene so backing out (Esc / right-click) restores it exactly;
/// an `on_exit` parameter overrides that with a full scene switch. Reuses the
/// hotspot polygons, the SpeechManager, and the custom cursor affordance.
class CloseUpScene : public pac::core::Scene {
public:
    CloseUpScene(pac::core::EngineContext& ctx, const pac::core::SceneParams& params);

    void enter() override;
    void handle_event(const sf::Event& event) override;
    void update(float dt) override;
    void draw(sf::RenderTarget& target) const override;

private:
    void exit();
    void activate(const CloseUpHotspot& hs);

    pac::core::EngineContext& ctx_;
    std::string data_path_;
    std::string on_exit_; // scene id on back-out; empty -> pop the overlay
    const sf::Font* font_ = nullptr;
    CloseUpData data_;
    bool loaded_ = false;
    SpeechManager speech_;
    geom::Point hover_{-1.0f, -1.0f};
    const CloseUpHotspot* hovered_ = nullptr;
};

} // namespace pac::pnc
