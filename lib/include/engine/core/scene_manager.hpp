#pragma once

#include "engine/core/screen_fade.hpp"

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace sf {
class Event;
class RenderTarget;
} // namespace sf

namespace pac::core {

class Scene;

/// Owns the scene stack and applies transitions. Transitions are queued and
/// applied at a safe point (apply_pending) so a scene can request one from inside
/// its own event/update without being destroyed mid-call. Decoupled from the
/// factory/context via a Builder so the stack logic is headless-testable.
///
/// A full-screen `goto_scene` can fade to black and back: when a transition
/// duration is set, a queued GOTO fades out, swaps the stack at black, then fades
/// in. Overlays (PUSH/POP — e.g. the pause/settings menu) and QUIT are never
/// faded. With the default duration of 0 the swap is instant (and the headless
/// stack tests see the original behavior).
class SceneManager {
public:
    using Builder = std::function<std::unique_ptr<Scene>(const std::string& id)>;
    using SceneEntered = std::function<void(const std::string& id)>;

    /// User-initiated action currently waiting in the standard confirmation
    /// overlay. NONE means no modal confirmation is active.
    enum class ConfirmationAction { NONE, QUIT_APPLICATION, GOTO_SCENE };

    void set_builder(Builder builder);
    void set_scene_entered_callback(SceneEntered callback);
    void set_settings_scene_id(std::string id);
    void set_confirmation_scene_id(std::string id);

    /// Configure the manifest scene ids the save/load picker (issue #108) is
    /// found at. Auto-detected at startup from any `SaveLoadScene` entries
    /// with `parameters.mode: save` / `mode: load`. Empty when the manifest
    /// declares no such scene — the corresponding `open_*` is then a no-op
    /// (the game runs without a save/load UI, like the M5c MVP).
    void set_save_scene_id(std::string id);
    void set_load_scene_id(std::string id);
    [[nodiscard]] const std::string& save_scene_id() const { return save_scene_id_; }
    [[nodiscard]] const std::string& load_scene_id() const { return load_scene_id_; }

    /// Seconds for a `goto_scene` fade-out/in. 0 (default) = instant swap.
    void set_transition_duration(float seconds) { transition_duration_ = seconds; }
    /// Start a fade-in from black (e.g. at startup), using the transition duration.
    void start_fade_in() { fade_.fade_in(transition_duration_); }

    void goto_scene(const std::string& id); // replace stack; "QUIT" token quits
    void push_scene(const std::string& id); // overlay above the current scene
    void pop_scene();                       // remove the top scene
    void open_settings();                   // engine-handled: push the SettingsScene
    void open_save();                       // engine-handled: push the save picker
    void open_load();                       // engine-handled: push the load picker
    /// Ask before a user-initiated application exit. Falls back to an immediate
    /// quit when the manifest has no ConfirmationScene.
    void request_quit();
    /// Ask before a user-initiated full-screen navigation (normally returning
    /// from gameplay to the title). "QUIT" is treated as request_quit().
    void request_goto_scene(const std::string& id);
    /// Resolve the active ConfirmationScene. These are no-ops without a pending
    /// action, which keeps repeated input harmless.
    void accept_confirmation();
    void cancel_confirmation();
    [[nodiscard]] ConfirmationAction confirmation_action() const { return confirmation_action_; }
    [[nodiscard]] const std::string& confirmation_target() const { return confirmation_target_; }
    void quit();

    void apply_pending();
    bool running() const { return running_; }
    std::size_t size() const { return stack_.size(); }
    Scene* top() const;
    /// Id of the active top-level (GOTO) scene, for diagnostics/profiling (#112).
    /// Unchanged by PUSH/POP overlays so it labels the gameplay scene, not a
    /// transient menu. Empty until the first scene is entered.
    const std::string& current_scene_id() const { return current_scene_id_; }
    bool transitioning() const { return transition_pending_; }

    void handle_event(const sf::Event& event);
    [[nodiscard]] bool enter_pause_menu();
    void leave_pause_menu();
    [[nodiscard]] bool pause_menu_active() const;
    /// Advance only the manager-owned fade. Used while gameplay simulation is
    /// paused so confirmed scene changes can still finish cleanly.
    void update_transition(float dt);
    void update(float dt);
    void draw(sf::RenderTarget& target) const;

private:
    enum class OpKind { GOTO, PUSH, POP, QUIT };
    struct Op {
        OpKind kind;
        std::string id;
    };

    std::unique_ptr<Scene> build(const std::string& id);
    void do_goto(const std::string& id);

    Builder builder_;
    SceneEntered scene_entered_;
    std::string settings_scene_id_;
    std::string confirmation_scene_id_;
    std::string save_scene_id_;
    std::string load_scene_id_;
    std::string current_scene_id_;
    std::vector<std::unique_ptr<Scene>> stack_;
    std::vector<Op> pending_;
    bool running_ = true;

    ConfirmationAction confirmation_action_ = ConfirmationAction::NONE;
    std::string confirmation_target_;

    ScreenFade fade_;
    float transition_duration_ = 0.0f;
    bool transition_pending_ = false; // a GOTO is faded out, waiting for black
    std::string transition_target_;
};

} // namespace pac::core
