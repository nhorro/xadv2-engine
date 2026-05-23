#pragma once

#include <SFML/Graphics/Rect.hpp>
#include <SFML/System/Vector2.hpp>

namespace pac::pnc {

/// The room camera: a world-space rectangle the size of the scenery viewport,
/// always clamped to the room bounds. A room no larger than the viewport is
/// centered and does not scroll. Pure/headless (no graphics context), so the
/// scroll mapping is unit-testable; the room view turns `view_rect` into an
/// sf::View.
///
/// Scroll follows the player by mapping the player's *reachable* range (the
/// follow bounds — usually the walkable area's bounding box) onto the camera's
/// full clamped scroll range, per axis. So the player's leftmost/topmost
/// reachable point shows the start of the background and the rightmost/bottommost
/// shows the end: the whole background is covered even though the player can
/// never reach the room's literal edges (issue #28). With follow bounds equal to
/// the room (the default) this degenerates to ordinary clamped follow.
///
/// That proportional reveal is then constrained so the player's pivot always
/// stays on screen with a margin: when the walkable area covers only a thin slice
/// of a much taller/wider room, the raw reveal would scroll the player out of the
/// viewport (or into an unclickable sliver). The on-screen clamp caps how far the
/// reveal may scroll, so the background coverage is honored only as far as it
/// keeps the player comfortably visible. Room bounds remain the hard limit.
class Camera {
public:
    Camera(sf::Vector2f viewport_size, sf::Vector2u room_size);

    void set_viewport_size(sf::Vector2f size);
    void set_room_size(sf::Vector2u size);

    /// The world-space area the follow target (player pivot) can occupy — the
    /// walkable area's bounding box. Maps onto the full scroll range. Defaults to
    /// the whole room; resets to the room whenever the room size changes.
    void set_follow_bounds(sf::FloatRect bounds);

    /// Fraction of the viewport (per axis) kept between the player's pivot and the
    /// viewport edge by the on-screen clamp. 0 lets the player reach the very edge
    /// (full reveal preserved); larger values keep more clickable floor around the
    /// player at the cost of revealing less of the background's edges. Clamped to
    /// [0, 0.45].
    void set_follow_margin(float fraction);

    [[nodiscard]] sf::Vector2f center() const { return center_; }
    void set_center(sf::Vector2f center); // clamped to room bounds
    void snap_to(sf::Vector2f target);    // position for the target immediately

    /// Position the camera for `target` using the proportional scroll mapping.
    void follow(sf::Vector2f target);

    [[nodiscard]] sf::FloatRect view_rect() const; // world rect shown
    [[nodiscard]] sf::Vector2f top_left() const;   // world coord of the viewport's top-left

private:
    [[nodiscard]] sf::Vector2f clamp_center(sf::Vector2f center) const;
    [[nodiscard]] sf::Vector2f scroll_center(sf::Vector2f target) const;
    [[nodiscard]] float
    scroll_axis(float target, float reach_min, float reach_max, float room, float view) const;

    sf::Vector2f viewport_;
    sf::Vector2f room_;
    sf::Vector2f center_;
    sf::FloatRect follow_bounds_;
    float follow_margin_ = 0.15f; // keep the player within the central 70% of the view
};

} // namespace pac::pnc
