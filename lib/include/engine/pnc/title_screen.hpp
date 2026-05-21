#pragma once

#include "engine/core/scene.hpp"

#include <SFML/Graphics/Font.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace pac::core {
struct EngineContext;
class SceneParams;
} // namespace pac::core

namespace pac::pnc {

/// Main menu. Renders a vertical list of menu entries (mouse hover + click,
/// Escape to exit). Outcomes `new_game` and `exit` are wired by the manifest;
/// `settings` is engine-handled (pushes the SettingsScene). `continue` is omitted
/// until save/load (M5).
class TitleScreen : public pac::core::Scene {
public:
    TitleScreen(pac::core::EngineContext& ctx, const pac::core::SceneParams& params);

    void handle_event(const sf::Event& event) override;
    void draw(sf::RenderTarget& target) const override;

private:
    enum class Action { NEW_GAME, SETTINGS, EXIT };
    struct Entry {
        std::string label;
        Action action;
    };

    void trigger(Action action);
    int entry_at(float virtual_x, float virtual_y) const;

    pac::core::EngineContext& ctx_;
    std::string new_game_target_;
    std::string exit_target_;
    std::vector<Entry> entries_;
    sf::Font font_;
    std::vector<std::byte> font_data_; // backs font_ when loaded from memory
    bool font_loaded_ = false;
    int hovered_ = -1;
};

} // namespace pac::pnc
