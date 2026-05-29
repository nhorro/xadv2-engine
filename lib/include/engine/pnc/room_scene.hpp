#pragma once

#include "engine/core/game_state.hpp"
#include "engine/core/scene.hpp"
#include "engine/core/screen_fade.hpp"
#include "engine/core/scripting.hpp"   // ScopeId
#include "engine/core/state_store.hpp" // StateValue
#include "engine/pnc/avatar.hpp"
#include "engine/pnc/camera.hpp"
#include "engine/pnc/cast.hpp"
#include "engine/pnc/command.hpp"
#include "engine/pnc/command_controller.hpp"
#include "engine/pnc/debug_overlay.hpp"
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
class RoomScene : public pac::core::Scene, private CommandControllerHost {
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

    /// Save-thumbnail capture (issue #119) is only meaningful while the room
    /// shows uncluttered gameplay — the COMMAND state. DIALOG / MENU / BLOCKED
    /// frames carry overlays the player wouldn't want immortalized in a save.
    bool wants_thumbnail() const override { return view_state_ == ViewState::COMMAND; }

    // --- genre Lua API targets (invoked by the bound script globals) ---
    void api_change_room(const std::string& id, const std::string& entry_point);
    void api_set_region_state(const std::string& region_id, const std::string& state);
    [[nodiscard]] std::string api_get_region_state(const std::string& region_id) const;
    void api_show_object(const std::string& object_id, bool visible);
    void api_set_layer_visible(const std::string& layer_id, bool visible);
    void api_set_hotspot_enabled(const std::string& hotspot_id, bool enabled);
    void api_set_room_state(const std::string& key, pac::core::StateValue value);
    [[nodiscard]] std::optional<pac::core::StateValue>
    api_get_room_state(const std::string& key) const;
    void api_talk(const std::string& speaker_id, const std::string& text);
    void api_start_dialog(const std::string& npc_id);
    // Scripted camera overrides (issue #25). look_at snaps; go_to returns the
    // tween duration (s) so the Lua wrapper can yield for it; both suspend follow.
    void api_camera_look_at(geom::Point target);
    float api_camera_go_to(geom::Point target);
    void api_camera_follow_player();

    // Scripted avatar control — the `avatar(id)` Lua handle (#139). `id` is a cast
    // character id; it resolves to the persistent player or a room NPC each call
    // (never a held pointer). `move_to` starts a pathfound walk and returns an
    // event name the Lua wrapper waits on (empty when the avatar is missing, so the
    // script does not hang); RoomScene::update emits it once the avatar stops.
    [[nodiscard]] std::string api_avatar_move_to(const std::string& id, geom::Point target);
    void api_avatar_face(const std::string& id, const std::string& direction);
    void api_avatar_look_at(const std::string& id, geom::Point target);
    [[nodiscard]] std::optional<geom::Point> api_avatar_position(const std::string& id) const;

    // Scripted NPC presence (#140). `spawn_npc` creates a room NPC from a cast
    // character (or repositions it if already present) and seats it; `despawn_npc`
    // removes it. The player character is never spawnable. Presence is not
    // persisted — drive it from on_load against global state.
    void api_spawn_npc(const std::string& id, geom::Point start, const std::string& orientation);
    void api_despawn_npc(const std::string& id);
    [[nodiscard]] std::string api_current_room() const { return current_room_id_; }
    [[nodiscard]] InventoryModel& inventory() { return inventory_; }
    [[nodiscard]] ViewState view_state() const { return view_state_; }

    /// Snapshot of all persistent state — the payload SaveService writes. Pure
    /// read: no side effects, safe to call mid-update from autosave hooks.
    [[nodiscard]] pac::core::GameState snap() const;

    /// True when a `snap()` taken right now would be a coherent save: the
    /// view is in COMMAND (or the pause MENU that was entered from COMMAND),
    /// no dialog is mid-conversation, and no room change is pending. DIALOG /
    /// BLOCKED hold transient runtime that isn't part of GameState. Used as a
    /// precondition gate by the autosave hook and the in-game menu.
    [[nodiscard]] bool can_save() const;

    /// Replace all stores with `state`, kill any transient runtime (dialog,
    /// command builder), and schedule a room change so the next `update()`
    /// loads the saved room and reseats the player at the saved position.
    /// Returns false (and logs) if the state is rejected (unsupported
    /// save_version, wrong scene id, empty room id); the scene is left
    /// untouched in that case so the caller can fall back.
    bool restore(const pac::core::GameState& state);

private:
    void load_room(const std::string& id, const std::string& entry_point);
    void unload_room();
    void seat_player(const std::string& entry_point);
    void spawn_room_npcs();
    [[nodiscard]] std::optional<Avatar> make_avatar(const std::string& character_id);
    /// Resolve a cast character id to its live avatar: the persistent player when
    /// `id` is the player character, else a room NPC, else nullptr. Re-resolved on
    /// every scripted avatar call so a stale id fails safely instead of dangling.
    [[nodiscard]] Avatar* resolve_avatar(const std::string& id);
    [[nodiscard]] const Avatar* resolve_avatar(const std::string& id) const;
    void say(const std::string& text, sf::Color color);
    void say_at(const std::string& text, sf::Color color, geom::Point world);

    // --- pause / save / load / settings menu (M5c/2; the picker is the
    // SaveLoadScene from issue #108) ---
    enum class MenuAction { RESUME, OPEN_SAVE, OPEN_LOAD, OPEN_SETTINGS, QUIT_TO_TITLE };
    struct MenuButton {
        sf::FloatRect rect;
        MenuAction action;
        bool enabled = true;
    };
    [[nodiscard]] std::vector<MenuButton> menu_buttons() const;
    void handle_menu_event(const sf::Event& event);
    void draw_menu(sf::RenderTarget& target) const;
    void trigger_menu(MenuAction action);
    void sync_command_hover();
    /// Route the player to a hotspot's approach point through the find_path seam:
    /// clamps an approach outside the walkable area to the nearest reachable point
    /// (dev warning) and short-circuits when already near it (issue #22).
    void walk_to_approach(geom::Point approach, const std::string& hotspot_id);
    void execute_command(const Command& cmd);
    /// Route a command through the handler chain (inventory -> hotspot ->
    /// game.lua fallback). Returns the handler's caption string, if any.
    std::optional<std::string> dispatch(const Command& cmd);
    /// Run a command and produce its feedback: shows a returned caption, or the
    /// "nothing happens" fallback only when the handler returned no caption and
    /// did not speak (e.g. via talk()). Shared by the immediate path and the
    /// deferred approach-arrival path.
    void dispatch_and_feedback(const Command& cmd);
    /// Turn the player to face the room hotspot a command targets, so the avatar
    /// looks at what it examines / talks to. No-op when the command has no room
    /// target or the player isn't placed.
    void face_target(const Command& cmd);
    /// A representative world point for a hotspot (its area centroid, else the
    /// bound region/object centre), used to decide which way to face. nullopt when
    /// the hotspot has no resolvable footprint.
    [[nodiscard]] std::optional<geom::Point> hotspot_focus(const RoomHotspot& hs) const;

    [[nodiscard]] CommandOperandInfo
    resolve_command_operand(const ObjectRef& object) const override;
    [[nodiscard]] std::string command_verb_label(Verb verb) const override;
    [[nodiscard]] std::string command_connector_label(Verb verb) const override;
    [[nodiscard]] std::string command_walk_label() const override;
    /// Multi-line debug HUD text (#37): room/zone/view-state, command-builder
    /// state, and a dump of world/room/region state. Drawn by the F4 overlay.
    [[nodiscard]] std::string debug_hud_text() const;

    // --- dev actions (#38): in-engine authoring helpers, edit_mode-gated, bound
    // to F5-F8. Logic only (no UI); each logs what it did.
    /// Hot-reload the current room's behavior script: on_unload, cancel + reopen
    /// the room scope, reload rooms/<id>.lua, on_load. Keeps RoomData, the player
    /// pose, and persistent state. Additionally gated by allow_room_reload.
    void dev_reload_room();
    /// change_room to the next room id found in the rooms directory (cyclic).
    void dev_jump_to_next_room();
    /// Add the first defined inventory item the player isn't holding.
    void dev_give_next_item();
    /// Remove the most recently added held inventory item.
    void dev_remove_last_item();
    [[nodiscard]] geom::Point virtual_to_world(sf::Vector2f virtual_point) const;
    [[nodiscard]] float scenery_height() const;
    void check_zones();
    /// Hit-test the scenery at `world` with full bind support: supplies object
    /// frame bounds (from the loaded textures) to RoomRuntime::hotspot_at (#22).
    [[nodiscard]] const RoomHotspot* hotspot_under(geom::Point world) const;
    /// Frame bounds of a visible bound object (top-left + texture size), or
    /// nullopt when hidden / imageless / the texture is unavailable.
    [[nodiscard]] std::optional<sf::FloatRect>
    object_frame_bounds(const std::string& object_id) const;

    pac::core::EngineContext& ctx_;
    std::string cast_path_;
    std::string rooms_dir_;
    std::string start_room_;
    std::string player_char_;
    std::string font_path_;
    std::string scumm_panel_path_;
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
    // Set whenever a line is shown via say()/say_at(); cleared before each command
    // dispatch so execute_command() can tell whether the verb handler already
    // spoke (e.g. called talk()) and skip the "nothing happens" fallback caption.
    bool spoke_during_command_ = false;
    RoomRenderer renderer_;
    DebugOverlay debug_overlay_;
    // Dev overlay layer toggles (#37). Seeded from ctx_.dev in enter(); flipped by
    // F1-F4. Only rendered / responsive to keys when ctx_.dev.edit_mode is set.
    DebugOverlayFlags debug_flags_;
    CommandController command_controller_;
    std::optional<ScummPanel> panel_;
    // Last known pointer position in virtual coords; drives the top-bar hover
    // preview. Off-screen until the first MouseMoved so nothing is "hovered".
    sf::Vector2f hover_vp_{-1.0f, -1.0f};
    pac::core::ScopeId room_scope_ = 0;
    pac::core::ScopeId dialog_scope_ = 0;
    pac::core::TaskId run_task_ = 0;

    // Persistent state across room changes — all folded into GameState by snap().
    std::map<std::string, std::map<std::string, pac::core::StateValue>> room_state_;
    std::map<std::string, std::map<std::string, std::string>> region_state_persist_;
    std::map<std::string, std::map<std::string, bool>> hotspot_enabled_persist_;
    std::map<std::string, std::map<std::string, bool>> object_visible_persist_;
    std::map<std::string, std::map<std::string, bool>> layer_visible_persist_;

    bool change_pending_ = false;
    std::string pending_room_;
    std::string pending_entry_;
    // change_room fades the scenery to black, loads at black, then fades back in.
    // `change_armed_` means a change_room is waiting for the fade-out to finish.
    pac::core::ScreenFade room_fade_;
    bool change_armed_ = false;
    float fade_duration_ = 0.0f;
    // Monotonic seconds since the scene began, fed to shaders' `u_time` uniform.
    float shader_time_ = 0.0f;
    // When set, the next room load overrides the default seat with this pose
    // (used by restore() to put the player back at the saved position rather
    // than the room's entry point).
    std::optional<pac::core::GameState::RoomView::Player> pending_restore_player_;
    std::string current_zone_;

    ViewState view_state_ = ViewState::COMMAND;
    // Previous frame's view state: a transition back into COMMAND resumes camera
    // follow after a scripted override (issue #25).
    ViewState prev_view_state_ = ViewState::COMMAND;

    // A command whose hotspot is marked `requires_approach`: the player must walk
    // to the approach point before the action fires. While set, the view is
    // BLOCKED (no new input) and the builder stays in COMMAND_EXECUTING; update()
    // runs the deferred dispatch once the avatar stops at the point.
    struct PendingApproach {
        Command cmd;
        geom::Point target;
        std::string hotspot_id;
    };
    std::optional<PendingApproach> pending_approach_;

    // Scripted `avatar(id):move_to(...)` calls in flight (#139): each records the
    // moving avatar, the script scope to wake, and the unique event the Lua
    // wrapper is blocked on. update() emits the event (and drops the entry) once
    // that avatar stops moving. Cleared on room unload (the room scope's tasks die
    // with it). `move_seq_` makes each event name unique.
    struct PendingMove {
        std::string avatar_id;
        pac::core::ScopeId scope = 0;
        std::string event;
    };
    std::vector<PendingMove> pending_moves_;
    std::uint64_t move_seq_ = 0;

    // Resolved static point for the running dialog's NPC speech (from the dialog
    // file's `text_anchor`). Empty -> bubble follows the NPC avatar position.
    std::optional<geom::Point> dialog_text_anchor_;

    std::optional<DialogRuntime> dialog_;
    // SaveService now lives in EngineContext (ctx_.saves) so TitleScreen can
    // read it for Continue and stage a restore for us to consume in enter().

    struct Lua; // pimpl: inventory.lua + game.lua tables (sol kept out of header)
    std::unique_ptr<Lua> lua_;
};

} // namespace pac::pnc
