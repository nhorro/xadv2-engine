#pragma once

#include "engine/core/scene.hpp"
#include "engine/core/settings.hpp"

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
/// display mode (windowed resolution / fullscreen), language, and audio volumes.
///
/// Edits are made to a working copy and only take effect on APPLY: display and
/// language changes are staged, so the window is never recreated until the player
/// confirms (BACK discards them). Audio volume previews live while editing and is
/// restored on BACK. APPLY commits the working copy to the engine settings,
/// requests any display change through `Display` (applied by the main loop —
/// transparent to the game; only the letterbox changes, R6), swaps the active
/// language, and persists everything to the per-user settings file.
class SettingsScene : public pac::core::Scene {
public:
    SettingsScene(pac::core::EngineContext& ctx, const pac::core::SceneParams& params);

    void handle_event(const sf::Event& event) override;
    void draw(sf::RenderTarget& target) const override;

private:
    enum Row {
        ROW_RESOLUTION = 0,
        ROW_FULLSCREEN,
        ROW_LANGUAGE,
        ROW_MUSIC,
        ROW_SFX,
        ROW_APPLY,
        ROW_BACK,
        ROW_COUNT
    };

    void adjust(int dir); // change the selected value row by direction
    void activate();      // Enter/Space on the selected row
    void apply();         // commit the working copy + persist + request display
    void cancel();        // discard staged changes (restore audio preview) + pop

    pac::core::EngineContext& ctx_;
    const sf::Font* font_ = nullptr; // owned by ResourceCache; null if unavailable
    int row_ = 0;

    // Editable draft of the settings; committed to the engine only on APPLY.
    pac::core::Settings working_;
    std::vector<sf::Vector2u> sizes_; // selectable windowed sizes
    int size_idx_ = 0;
    int lang_idx_ = 0; // index into ctx_.localization.languages()
};

} // namespace pac::pnc
