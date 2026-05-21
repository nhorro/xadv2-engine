#pragma once

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
class SceneManager {
public:
    using Builder = std::function<std::unique_ptr<Scene>(const std::string& id)>;

    void set_builder(Builder builder);
    void set_settings_scene_id(std::string id);

    void goto_scene(const std::string& id); // replace stack; "QUIT" token quits
    void push_scene(const std::string& id); // overlay above the current scene
    void pop_scene();                       // remove the top scene
    void open_settings();                   // engine-handled: push the SettingsScene
    void quit();

    void apply_pending();
    bool running() const { return running_; }
    std::size_t size() const { return stack_.size(); }
    Scene* top() const;

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

    Builder builder_;
    std::string settings_scene_id_;
    std::vector<std::unique_ptr<Scene>> stack_;
    std::vector<Op> pending_;
    bool running_ = true;
};

} // namespace pac::core
