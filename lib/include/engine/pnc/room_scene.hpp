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
#include "engine/pnc/pause_overlay.hpp"
#include "engine/pnc/room_renderer.hpp"
#include "engine/pnc/room_runtime.hpp"
#include "engine/pnc/scumm_panel.hpp"
#include "engine/pnc/speech_manager.hpp"

#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace sf {
class Font;
class RenderTexture;
}

namespace pac::core {
struct EngineContext;
class SceneParams;
} // namespace pac::core

namespace pac::pnc {

class RoomLightingRenderer;

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
    /// Also skip while a change_room fade is on screen: the autosave fires at the
    /// end of the fade-out, so capturing during it would darken the thumbnail
    /// (the fade is a black quad over everything). Gating on a clear screen keeps
    /// `ctx.thumbnail` on the last non-faded gameplay frame.
    bool wants_thumbnail() const override {
        return view_state_ == ViewState::COMMAND && room_fade_.alpha255() == 0;
    }

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
    // Shows the line and returns a unique event the Lua `talk` wrapper waits on so
    // the speaker blocks until the line is dismissed (design 05: talk "yields until
    // done"). Empty -> nothing to show, so the wrapper never waits. The wrapper only
    // waits when running inside a coroutine task; on the main thread (a plain hook /
    // verb handler) talk stays fire-and-forget.
    [[nodiscard]] std::string
    api_talk(const std::string& speaker_id,
             const std::string& text,
             bool continue_action = false,
             const std::optional<std::string>& face_target = std::nullopt);
    // Start dialog `dialog_id` (file `dialogs/<dialog_id>.lua`), spoken by cast
    // character `speaker_id` (its speech colour + over-head bubble; defaults to
    // dialog_id). The split lets one NPC have several topic-named dialogs.
    void api_start_dialog(const std::string& dialog_id, const std::string& speaker_id);
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
    void api_avatar_set_visible(const std::string& id, bool visible);
    void api_avatar_set_shadow_opacity(const std::string& id,
                                       float opacity,
                                       float transition_seconds);
    [[nodiscard]] std::optional<geom::Point> api_avatar_position(const std::string& id) const;
    void api_avatar_play(const std::string& id, const std::string& sequence);
    /// Start a one-shot sequence and return the event the Lua wrapper waits on
    /// (empty when the avatar/sequence is missing, so the script never hangs);
    /// update() emits it once the avatar stops "acting" (#149).
    [[nodiscard]] std::string api_avatar_play_until_end(const std::string& id,
                                                        const std::string& sequence);
    [[nodiscard]] std::optional<geom::Point> api_avatar_anchor(const std::string& id,
                                                               const std::string& name) const;

    /// Start/stop the engine-owned animation associated with the single active
    /// speech bubble. `continue_action` deliberately leaves the avatar alone.
    void begin_talk_animation(const std::string& speaker_id,
                              bool continue_action,
                              const std::optional<std::string>& face_target = std::nullopt);
    void end_talk_animation();
    /// Dialogs are conversations: stop both participants and turn them toward
    /// each other before the first line.
    void prepare_dialog_participants(const std::string& npc_id);

    // Scripted object transform/movement — the `object(id)` Lua handle (#142).
    // `move_to` is a free linear move (objects are not pathfound); it returns an
    // event name the wrapper waits on (empty when the object is unknown).
    [[nodiscard]] std::string
    api_object_move_to(const std::string& id, geom::Point target, float speed);
    void api_object_set_position(const std::string& id, geom::Point p);
    [[nodiscard]] std::optional<geom::Point> api_object_position(const std::string& id) const;
    void api_object_set_scale(const std::string& id, float scale);
    void api_object_set_rotation(const std::string& id, float degrees);
    [[nodiscard]] std::optional<float> api_object_rotation(const std::string& id) const;
    void api_object_play(const std::string& id, const std::string& sequence);
    /// Play a one-shot sequence on an animated object and return the event the Lua
    /// wrapper waits on (empty when the object isn't animated / lacks the sequence,
    /// so the script never hangs); update() emits it once the object stops acting.
    [[nodiscard]] std::string api_object_play_until_end(const std::string& id,
                                                        const std::string& sequence);
    [[nodiscard]] bool object_exists(const std::string& id) const;

    // Scripted dynamic-light control — the `light(id)` Lua handle. Overrides
    // are room-scoped and reset to the authored values on the next room load.
    void api_light_set_enabled(const std::string& id, bool enabled);
    [[nodiscard]] std::optional<bool> api_light_enabled(const std::string& id) const;
    void api_light_set_intensity(const std::string& id,
                                 float intensity,
                                 float transition_seconds);
    [[nodiscard]] std::optional<float> api_light_intensity(const std::string& id) const;
    void api_light_occluder_set_enabled(const std::string& id, bool enabled);
    [[nodiscard]] std::optional<bool> api_light_occluder_enabled(const std::string& id) const;
    /// Load AnimatedSprite/CompositeSprite visuals for YAML-backed objects and
    /// seat their initial sequence. Static textures stay in RoomRenderer.
    void build_object_sprites();

    // Scripted NPC presence (#140). `spawn_npc` creates a room NPC from a cast
    // character (or repositions it if already present) and seats it; `despawn_npc`
    // removes it. The player character is never spawnable. Presence is not
    // persisted — drive it from on_load against global state.
    void api_spawn_npc(const std::string& id, geom::Point start, const std::string& orientation);
    void api_despawn_npc(const std::string& id);
    [[nodiscard]] std::string api_current_room() const { return current_room_id_; }

    // Declarative room configurations (#185). `set_room_config` is the transition
    // primitive (replaces the hand-managed `set_state("<room>.cfg", N)` + flow
    // helper): for the live room it reconciles presence at once; for another room
    // it records the value, reconciled when that room next loads.
    // `current_room_config` reads a room's config id (replaces `get_state` checks).
    void api_set_room_config(const std::string& room_id, const std::string& config_id);
    [[nodiscard]] std::string api_current_room_config(const std::string& room_id) const;
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
    void seat_player(const std::string& entry_point, bool allow_entry_walk);
    void spawn_room_npcs();
    // Declarative configs (#185). `apply_room_config` resolves the room's effective
    // config (persisted value, else its `start`), persists it, reconciles presence,
    // and returns the id ("" for a room without `configs:`). `reconcile_to_config`
    // applies one config's exhaustive presence. `run_config_beat` spawns the
    // matching first-enter / re-enter beat (blocking input until it drains).
    std::string apply_room_config();
    void reconcile_to_config(const std::string& config_id);
    void run_config_beat(const std::string& config_id);
    [[nodiscard]] std::optional<Avatar> make_avatar(const std::string& character_id);
    /// Resolve a cast character id to its live avatar: the persistent player when
    /// `id` is the player character, else a room NPC, else nullptr. Re-resolved on
    /// every scripted avatar call so a stale id fails safely instead of dangling.
    [[nodiscard]] Avatar* resolve_avatar(const std::string& id);
    [[nodiscard]] const Avatar* resolve_avatar(const std::string& id) const;
    // The point a speech balloon floats above: the speaker's "head_pivot" sprite
    // anchor when the rig defines one, else an estimate (top-centre of the frame).
    [[nodiscard]] geom::Point speech_anchor(const Avatar& a) const;
    // `gap` is the speaker's side-placement clearance (Character::speech_gap); it
    // defaults to the engine default for callers without a known speaker. `world`
    // is the head anchor the balloon floats above (see speech_anchor).
    void say(const std::string& text, sf::Color color, float gap = 48.0f);
    void say_at(const std::string& text, sf::Color color, geom::Point world, float gap = 48.0f);

    // Ambient floating text (`float_text` Lua API): non-blocking, time-limited
    // labels over the scenery, independent of the single SpeechManager line —
    // onomatopoeia ("¡CLICK!") and background NPC chatter. Several can coexist; an
    // npc/object anchor follows the (moving) thing, a point/coords anchor is fixed.
    struct AmbientLabel {
        enum class Anchor { POINT, NPC, OBJECT };
        std::string text;
        sf::Color color{245, 245, 250};
        float remaining = 0.0f; // seconds left before it fades out
        Anchor anchor = Anchor::POINT;
        geom::Point fixed{0.0f, 0.0f}; // POINT anchor
        std::string ref;               // NPC / OBJECT id when following
    };
    void api_float_text(const std::string& text,
                        AmbientLabel::Anchor anchor,
                        geom::Point fixed,
                        std::string ref,
                        sf::Color color,
                        float duration);
    [[nodiscard]] geom::Point ambient_anchor_point(const AmbientLabel& label) const;

    // --- pause / save / load / settings menu (M5c/2; the picker is the
    // SaveLoadScene from issue #108) ---
    enum class MenuAction {
        RESUME,
        OPEN_SAVE,
        OPEN_LOAD,
        PUSH_OVERLAY,
        OPEN_SETTINGS,
        QUIT_TO_TITLE
    };
    struct MenuButton {
        sf::FloatRect rect;
        MenuAction action;
        std::string label;
        std::string overlay_scene;
        int order = 0;
        bool enabled = true;
    };
    [[nodiscard]] std::vector<MenuButton> menu_buttons() const;
    void handle_menu_event(const sf::Event& event);
    void skip_active_cutscene();
    void draw_menu(sf::RenderTarget& target) const;
    void draw_ambient(sf::RenderTarget& target) const; // float_text labels (world space)
    void trigger_menu(const MenuButton& button);
    void sync_command_hover();
    /// Route the player to a hotspot's approach point through the find_path seam:
    /// clamps an approach outside the walkable area to the nearest reachable point
    /// (dev warning) and short-circuits when already near it (issue #22).
    void walk_to_approach(geom::Point approach, const std::string& hotspot_id);
    /// Route the player toward `target` (clamped into the walkable area) via the
    /// find_path seam and return the clamped destination the path was built to.
    /// The moving-target approach (#158) re-routes here as its target moves.
    geom::Point route_to(geom::Point target);
    /// Live floor/anchor position of a hotspot's `npc:`/`object:` bound target (the
    /// point on the walkable plane to walk toward), or nullopt if the target is
    /// absent. Used by the moving-target approach (#158) — distinct from
    /// `hotspot_focus`, which returns the bounds *center* for facing.
    std::optional<geom::Point> live_bind_target(const std::string& bind) const;
    void execute_command(const Command& cmd);
    /// Route a command through the handler chain (inventory -> hotspot ->
    /// game.lua fallback), stopping at the first handler that exists. The result's
    /// `handled` flag lets the caller suppress the default caption when a handler
    /// ran (even silently); `caption` carries a returned line.
    VerbResult dispatch(const Command& cmd);
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
    /// Height of the scenery viewport, derived in enter() from the panel's top
    /// edge. 0 until then; scenery_height() falls back to the built-in fraction.
    float scenery_height_ = 0.0f;
    std::string inventory_path_;
    std::string inventory_logic_;
    std::string logic_path_;
    std::string development_logic_path_;
    std::vector<PauseOverlayAction> pause_overlays_;

    const sf::Font* font_ = nullptr;
    const sf::Font* speech_font_ = nullptr;
    Cast cast_;
    InventoryModel inventory_;
    std::optional<RoomRuntime> room_;
    std::string current_room_id_;
    std::string room_dir_;
    std::optional<Avatar> player_;
    std::optional<Camera> camera_;
    SpeechManager speech_;
    std::vector<AmbientLabel> ambient_; // active float_text labels (transient)
    // Set whenever a line is shown via say()/say_at(); cleared before each command
    // dispatch so execute_command() can tell whether the verb handler already
    // spoke (e.g. called talk()) and skip the "nothing happens" fallback caption.
    bool spoke_during_command_ = false;
    RoomRenderer renderer_;
    // Room-level lighting/post-processing composites only the scenery into this
    // target, runs the built-in lighting prefix, then the authored shader stack.
    // The targets are pooled; rooms using neither keep the direct-render path.
    mutable std::unique_ptr<sf::RenderTexture> post_process_target_;
    mutable gfx::ShaderChain post_process_chain_;
    mutable std::unique_ptr<RoomLightingRenderer> lighting_renderer_;
    mutable std::size_t post_process_rt_bytes_ = 0;
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
    // Auto-spawned verb / hotspot / inventory / game-fallback handler that
    // yielded (M9 #183). While set, the view is BLOCKED and update() polls
    // is_task_alive; when the task drains, finish_execution + restore COMMAND.
    std::optional<pac::core::TaskId> awaiting_handler_task_;
    // Present only for an explicitly skippable cutscene. ESC cancels that one
    // task, invokes this synchronous finalizer, and restores command mode.
    std::function<void()> cutscene_skip_;

    struct PendingPlayerEntry {
        geom::Point target;
        std::string final_orientation;
    };
    // Declarative `avatars[].enter_from`: while present, input stays blocked
    // until the player reaches the normal start point. Completion fires the
    // room's optional `on_player_entered` hook.
    std::optional<PendingPlayerEntry> pending_player_entry_;

    // Persistent state across room changes — all folded into GameState by snap().
    std::map<std::string, std::map<std::string, pac::core::StateValue>> room_state_;
    std::map<std::string, std::map<std::string, std::string>> region_state_persist_;
    std::map<std::string, std::map<std::string, bool>> hotspot_enabled_persist_;
    std::map<std::string, std::map<std::string, bool>> object_visible_persist_;
    std::map<std::string, std::map<std::string, bool>> layer_visible_persist_;
    std::map<std::string, std::map<std::string, bool>> obstacle_enabled_persist_;

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

    // Like PendingApproach, but for a hotspot bound to a *moving* NPC/object with no
    // static approach point (#158). The destination is recomputed from the target's
    // live position each frame; `last_dest` is where the current path was routed and
    // `elapsed` bounds the chase (give-up timeout). Mutually exclusive with
    // pending_approach_.
    struct PendingMovingApproach {
        Command cmd;
        std::string hotspot_id;
        geom::Point last_dest;
        float elapsed = 0.0f;
    };
    std::optional<PendingMovingApproach> pending_moving_approach_;

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

    // Scripted `avatar(id):play_until_end(...)` calls in flight (#149): same
    // pattern as PendingMove, but the wake condition is the avatar no longer
    // "acting" (its one-shot sequence finished). Cleared on room unload.
    std::vector<PendingMove> pending_anim_;
    std::uint64_t anim_seq_ = 0;

    // Scripted `object(id):move_to(...)` calls in flight (#142): wake condition is
    // the object no longer moving. `avatar_id` holds the object id. Cleared on
    // room unload. `obj_move_seq_` makes each arrival event unique.
    std::vector<PendingMove> pending_obj_moves_;
    std::uint64_t obj_move_seq_ = 0;

    // Scripted `object(id):play_until_end(...)` in flight (#142): wake when the
    // object stops "acting" (its one-shot sequence finished). Cleared on unload.
    std::vector<PendingMove> pending_obj_anim_;
    std::uint64_t obj_anim_seq_ = 0;

    // Scripted `talk(...)` calls in flight: wake condition is speech no longer
    // active (the line's duration elapsed or it was skipped). `avatar_id` is unused
    // (speech is a single shared bubble). Cleared on room unload. `talk_seq_` makes
    // each event unique. Because each talk replaces the current bubble, a coroutine
    // that waits per line keeps at most one entry live; fire-and-forget talks just
    // leave an entry that emits to no waiter, which is harmless.
    std::vector<PendingMove> pending_speech_;
    std::uint64_t talk_seq_ = 0;
    std::string talking_avatar_id_;

    // Resolved static point for the running dialog's NPC speech (from the dialog
    // file's `text_anchor`). Empty -> bubble follows the NPC avatar position.
    std::optional<geom::Point> dialog_text_anchor_;

    std::optional<DialogRuntime> dialog_;
    // Current page of the dialog option list (paged when the options don't all
    // fit). Reset to 0 when a dialog starts and after each choice.
    int dialog_page_ = 0;
    // SaveService now lives in EngineContext (ctx_.saves) so TitleScreen can
    // read it for Continue and stage a restore for us to consume in enter().

    struct Lua; // pimpl: inventory.lua + game.lua tables (sol kept out of header)
    std::unique_ptr<Lua> lua_;
};

} // namespace pac::pnc
