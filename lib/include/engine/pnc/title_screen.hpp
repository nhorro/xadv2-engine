#pragma once

#include "engine/core/scene.hpp"

#include <string>
#include <vector>

namespace sf {
class Font;
}

namespace pac::core {
struct EngineContext;
class SceneParams;
} // namespace pac::core

namespace pac::pnc {

/// Main menu. Renders a vertical list of menu entries (mouse hover + click,
/// Escape to exit). Outcomes `new_game`, `continue`, and `exit` are wired by
/// the manifest; `settings` is engine-handled (pushes the SettingsScene). The
/// `Continue` entry only appears when a save exists (loaded via the engine's
/// SaveService.latest_slot()).
class TitleScreen : public pac::core::Scene {
public:
    TitleScreen(pac::core::EngineContext& ctx, const pac::core::SceneParams& params);

    void enter() override;
    void handle_event(const sf::Event& event) override;
    void draw(sf::RenderTarget& target) const override;

private:
    enum class Action { NEW_GAME, CONTINUE, SETTINGS, EXIT };
    struct Entry {
        std::string label;
        Action action;
    };

    void rebuild_entries();
    void trigger(Action action);
    int entry_at(float virtual_x, float virtual_y) const;

    pac::core::EngineContext& ctx_;
    std::string new_game_target_;
    std::string continue_target_;
    std::string exit_target_;
    std::vector<Entry> entries_;
    const sf::Font* font_ = nullptr; // owned by ResourceCache; null if unavailable
    int hovered_ = -1;
};

} // namespace pac::pnc
