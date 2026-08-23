#pragma once

namespace sf {
class Event;
class RenderTarget;
} // namespace sf

namespace pac::core {

/// A top-level application state. Only the focused (top) scene receives input and
/// updates; scenes draw bottom-to-top. Concrete scenes are constructed with their
/// manifest parameters and an EngineContext.
class Scene {
public:
    virtual ~Scene() = default;

    virtual void enter() {}
    virtual void leave() {}
    virtual void handle_event(const sf::Event& event) { (void) event; }
    virtual void update(float dt) { (void) dt; }
    virtual void draw(sf::RenderTarget& target) const = 0;

    /// Let a gameplay scene expose its native pause UI while the application
    /// freezes simulation globally. Scenes without one return false and the
    /// application supplies a small generic pause overlay instead.
    virtual bool enter_pause_menu() { return false; }
    virtual void leave_pause_menu() {}
    [[nodiscard]] virtual bool pause_menu_active() const { return false; }

    /// Last chance to persist coherent state before the application exits. The
    /// scene manager calls this on every stacked scene, top-to-bottom, before
    /// `leave()`. Most scenes have nothing to do; gameplay scenes may autosave.
    virtual void prepare_for_application_exit() {}

    /// An opaque scene fully covers the scenes beneath it, so the manager can skip
    /// drawing them. A transparent overlay (e.g. a HUD) returns false.
    bool opaque() const { return opaque_; }

    /// True when the current frame is a good candidate for a thumbnail capture
    /// (issue #119): the scene shows gameplay-like content without modal
    /// overlays. Default is false — only RoomScene's COMMAND state opts in.
    virtual bool wants_thumbnail() const { return false; }

protected:
    bool opaque_ = true;
};

} // namespace pac::core
