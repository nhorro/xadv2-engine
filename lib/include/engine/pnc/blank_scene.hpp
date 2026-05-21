#pragma once

#include "engine/core/scene.hpp"

#include <SFML/Graphics/Color.hpp>

namespace pac::core {
struct EngineContext;
class SceneParams;
} // namespace pac::core

namespace pac::pnc {

/// A solid-color scene. M0 placeholder for "gameplay started" and a trivial
/// fallback. Press Escape to quit.
class BlankScene : public pac::core::Scene {
public:
    BlankScene(pac::core::EngineContext& ctx, const pac::core::SceneParams& params);

    void handle_event(const sf::Event& event) override;
    void draw(sf::RenderTarget& target) const override;

private:
    pac::core::EngineContext& ctx_;
    sf::Color color_;
};

} // namespace pac::pnc
