#pragma once

#include "engine/core/scene.hpp"

#include <SFML/System/Vector2.hpp>

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

/// Main menu. Renders a vertical list of borderless text entries over an optional
/// full-screen background image (solid black when none), with optional looping
/// music. Mouse hover highlights an entry; click triggers it; Escape exits. The
/// menu block is anchored at a configurable screen-fraction position.
/// Entering the title stops all active SFX and room ambience; optional title
/// music uses the separate music service and starts afterward.
///
/// Configured entirely from the manifest scene `parameters`:
///   background  (opt path)   full-screen image; solid black if omitted
///   font        (opt path)   menu label font
///   font_size   (opt int)    menu label size in virtual pixels
///   music       (opt path)   background track, played in a loop
///   menu.position.{x,y}      menu anchor as a 0..1 screen fraction
///   menu.options.{new_game,continue,exit}   outcome scene ids
///   menu.continue_fallback   (opt) `new_game` — see below
///
/// Outcomes `new_game`, `continue`, and `exit` are wired by the manifest;
/// `settings` is engine-handled (pushes the SettingsScene). By default the
/// `Continue` entry only appears when a save exists (the engine's
/// `SaveService::latest_slot()`, i.e. the most recent of the autosave and the
/// manual slots).
///
/// `menu.continue_fallback: new_game` instead keeps `Continue` always listed, and
/// makes it start a new game when there is no save — so the entry is a stable
/// "just play" affordance rather than one that appears and disappears. Omitting
/// the key preserves the show-only-when-a-save-exists behavior.
class TitleScreen : public pac::core::Scene {
public:
    TitleScreen(pac::core::EngineContext& ctx, const pac::core::SceneParams& params);

    void enter() override;
    void leave() override;
    void handle_event(const sf::Event& event) override;
    void update(float dt) override;
    void draw(sf::RenderTarget& target) const override;

private:
    enum class Action { NEW_GAME, CONTINUE, LOAD_GAME, SETTINGS, EXIT };
    struct Entry {
        std::string label;
        Action action;
        float width = 0.0f; // cached text width in virtual px (0 when no font)
    };

    void rebuild_entries();
    void trigger(Action action);
    /// Clear world + staged state and go to the `new_game` target. Shared by the
    /// New game entry and by Continue's no-save fallback.
    void start_new_game();
    sf::Vector2f entry_center(int index, int count) const;
    int entry_at(float virtual_x, float virtual_y) const;

    pac::core::EngineContext& ctx_;
    std::string background_path_;
    std::string music_path_;
    std::string new_game_target_;
    std::string continue_target_;
    std::string load_game_target_; // optional (#108); when wired, shows the load picker
    std::string exit_target_;
    bool continue_starts_new_game_ = false; // menu.continue_fallback: new_game
    sf::Vector2f menu_anchor_{0.5f, 0.5f};  // screen-fraction position of the menu block
    unsigned font_size_ = 28;
    std::vector<Entry> entries_;
    const sf::Font* font_ = nullptr; // owned by ResourceCache; null if unavailable
    int hovered_ = -1;
};

} // namespace pac::pnc
