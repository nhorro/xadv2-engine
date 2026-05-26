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

    void set_builder(Builder builder);
    void set_settings_scene_id(std::string id);

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
    void quit();

    void apply_pending();
    bool running() const { return running_; }
    std::size_t size() const { return stack_.size(); }
    Scene* top() const;
    bool transitioning() const { return transition_pending_; }

    void handle_event(const sf::Event& event);
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
    std::string settings_scene_id_;
    std::string save_scene_id_;
    std::string load_scene_id_;
    std::vector<std::unique_ptr<Scene>> stack_;
    std::vector<Op> pending_;
    bool running_ = true;

    ScreenFade fade_;
    float transition_duration_ = 0.0f;
    bool transition_pending_ = false; // a GOTO is faded out, waiting for black
    std::string transition_target_;
};

} // namespace pac::core
