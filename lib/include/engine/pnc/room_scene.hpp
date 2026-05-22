#pragma once

#include "engine/core/game_state.hpp"
#include "engine/core/scene.hpp"
#include "engine/core/scripting.hpp"   // ScopeId
#include "engine/core/state_store.hpp" // StateValue
#include "engine/pnc/avatar.hpp"
#include "engine/pnc/camera.hpp"
#include "engine/pnc/cast.hpp"
#include "engine/pnc/command.hpp"
#include "engine/pnc/command_builder.hpp"
#include "engine/pnc/dialog.hpp"
#include "engine/pnc/inventory.hpp"
#include "engine/pnc/room_renderer.hpp"
#include "engine/pnc/room_runtime.hpp"
#include "engine/pnc/scumm_panel.hpp"
#include "engine/pnc/speech_manager.hpp"

#include <map>
#include <memory>
#include <optional>
#include <string>

namespace sf {
class Font;
}

namespace pac::core {
struct EngineContext;
class SceneParams;
} // namespace pac::core

namespace pac::pnc {

/// SCUMM-style room gameplay (M4): rooms (YAML + Lua) + cast + inventory, a
/// scrolling camera over the scenery viewport, the SCUMM panel + command builder
/// + dispatcher, regions/objects with z-order, and zone-driven room transitions.
/// State persists across room changes (precursor to GameState in M5).
class RoomScene : public pac::core::Scene {
public:
    /// High-level state of the room view (design 04 §Room view states). The
    /// SCUMM panel layout, input routing, and scripted-input gating all key off
    /// this. BLOCKED is reserved for cutscene-like sections and not yet driven
    /// by an API in M5a. MENU is the in-game pause/save/load overlay (M5c/2):
    /// the player presses Escape from COMMAND to open it.
    enum class ViewState { COMMAND, DIALOG, BLOCKED, MENU };

    RoomScene(pac::core::EngineContext& ctx, const pac::core::SceneParams& params);
    ~RoomScene() override;

    void enter() override;
    void leave() override;
    void handle_event(const sf::Event& event) override;
    void update(float dt) override;
    void draw(sf::RenderTarget& target) const override;

    // --- genre Lua API targets (invoked by the bound script globals) ---
    void api_change_room(const std::string& id, const std::string& entry_point);
    void api_set_region_state(const std::string& region_id, const std::string& state);
    [[nodiscard]] std::string api_get_region_state(const std::string& region_id) const;
    void api_show_object(const std::string& object_id, bool visible);
    void api_set_hotspot_enabled(const std::string& hotspot_id, bool enabled);
    void api_set_room_state(const std::string& key, pac::core::StateValue value);
    [[nodiscard]] std::optional<pac::core::StateValue>
    api_get_room_state(const std::string& key) const;
    void api_talk(const std::string& speaker_id, const std::string& text);
    void api_start_dialog(const std::string& npc_id);
    [[nodiscard]] std::string api_current_room() const { return current_room_id_; }
    [[nodiscard]] InventoryModel& inventory() { return inventory_; }
    [[nodiscard]] ViewState view_state() const { return view_state_; }

    /// Snapshot of all persistent state — the payload SaveService writes. Pure
    /// read: no side effects, safe to call mid-update from autosave hooks.
    [[nodiscard]] pac::core::GameState snap() const;

    /// Replace all stores with `state`, kill any transient runtime (dialog,
    /// command builder), and schedule a room change so the next `update()`
    /// loads the saved room and reseats the player at the saved position.
    void restore(const pac::core::GameState& state);

private:
    void load_room(const std::string& id, const std::string& entry_point);
    void unload_room();
    void seat_player(const std::string& entry_point);
    void spawn_room_npcs();
    [[nodiscard]] std::optional<Avatar> make_avatar(const std::string& character_id);
    void say(const std::string& text, sf::Color color);
    void say_at(const std::string& text, sf::Color color, geom::Point world);

    // --- pause / save / load menu (M5c/2) ---
    enum class MenuAction {
        SAVE_SLOT_1,
        SAVE_SLOT_2,
        SAVE_SLOT_3,
        LOAD_SLOT_1,
        LOAD_SLOT_2,
        LOAD_SLOT_3,
        RESUME,
        QUIT_TO_TITLE
    };
    struct MenuButton {
        sf::FloatRect rect;
        MenuAction action;
        bool enabled = true;
    };
    [[nodiscard]] std::vector<MenuButton> menu_buttons() const;
    void handle_menu_event(const sf::Event& event);
    void draw_menu(sf::RenderTarget& target) const;
    void trigger_menu(MenuAction action);
    void object_clicked(const ObjectRef& object);
    void execute_ready_command();
    std::optional<std::string> dispatch(const Command& cmd);
    [[nodiscard]] std::string command_preview() const;
    [[nodiscard]] geom::Point virtual_to_world(sf::Vector2f virtual_point) const;
    [[nodiscard]] float scenery_height() const;
    void check_zones();

    pac::core::EngineContext& ctx_;
    std::string cast_path_;
    std::string rooms_dir_;
    std::string start_room_;
    std::string player_char_;
    std::string font_path_;
    std::string inventory_path_;
    std::string inventory_logic_;
    std::string logic_path_;

    const sf::Font* font_ = nullptr;
    Cast cast_;
    InventoryModel inventory_;
    std::optional<RoomRuntime> room_;
    std::string current_room_id_;
    std::string room_dir_;
    std::optional<Avatar> player_;
    std::optional<Camera> camera_;
    SpeechManager speech_;
    RoomRenderer renderer_;
    CommandBuilder builder_;
    std::optional<ScummPanel> panel_;
    pac::core::ScopeId room_scope_ = 0;

    // Persistent state across room changes (folds into GameState in M5).
    std::map<std::string, std::map<std::string, pac::core::StateValue>> room_state_;
    std::map<std::string, std::map<std::string, std::string>> region_state_persist_;

    bool change_pending_ = false;
    std::string pending_room_;
    std::string pending_entry_;
    // When set, the next room load overrides the default seat with this pose
    // (used by restore() to put the player back at the saved position rather
    // than the room's entry point).
    std::optional<pac::core::GameState::RoomView::Player> pending_restore_player_;
    std::string current_zone_;

    ViewState view_state_ = ViewState::COMMAND;
    std::optional<DialogRuntime> dialog_;
    // SaveService now lives in EngineContext (ctx_.saves) so TitleScreen can
    // read it for Continue and stage a restore for us to consume in enter().

    struct Lua; // pimpl: inventory.lua + game.lua tables (sol kept out of header)
    std::unique_ptr<Lua> lua_;
};

} // namespace pac::pnc
