#pragma once

#include "engine/core/scene.hpp"

#include <SFML/Graphics/Font.hpp>

#include <cstddef>
#include <vector>

namespace pac::core {
struct EngineContext;
class SceneParams;
} // namespace pac::core

namespace pac::pnc {

/// Settings overlay, pushed over the current scene. M0 placeholder: shows the
/// music volume and adjusts it with Left/Right; Escape pops back.
class SettingsScene : public pac::core::Scene {
public:
    SettingsScene(pac::core::EngineContext& ctx, const pac::core::SceneParams& params);

    void handle_event(const sf::Event& event) override;
    void draw(sf::RenderTarget& target) const override;

private:
    pac::core::EngineContext& ctx_;
    sf::Font font_;
    std::vector<std::byte> font_data_; // backs font_ when loaded from memory
    bool font_loaded_ = false;
};

} // namespace pac::pnc
