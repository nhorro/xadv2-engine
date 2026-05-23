#pragma once

#include "engine/core/scene.hpp"

#include <SFML/System/Vector2.hpp>

#include <vector>

namespace sf {
class Font;
}

namespace pac::core {
struct EngineContext;
class SceneParams;
} // namespace pac::core

namespace pac::pnc {

/// Settings overlay, pushed over the current scene. Lets the player pick the
/// display mode (windowed resolution / fullscreen) and adjust music volume, then
/// pops back. Display changes are *requested* through `Display` and applied by the
/// main loop, so they stay transparent to the game: the virtual resolution is
/// fixed and only the letterbox changes (R6).
class SettingsScene : public pac::core::Scene {
public:
    SettingsScene(pac::core::EngineContext& ctx, const pac::core::SceneParams& params);

    void handle_event(const sf::Event& event) override;
    void draw(sf::RenderTarget& target) const override;

private:
    enum Row { ROW_RESOLUTION = 0, ROW_FULLSCREEN, ROW_MUSIC, ROW_COUNT };

    void adjust(int dir);        // change the selected row's value by direction
    void request_current_mode(); // push the current settings as a display mode

    pac::core::EngineContext& ctx_;
    const sf::Font* font_ = nullptr; // owned by ResourceCache; null if unavailable
    int row_ = 0;
    std::vector<sf::Vector2u> sizes_; // selectable windowed sizes
    int size_idx_ = 0;
};

} // namespace pac::pnc
