#pragma once

#include <SFML/Graphics/Rect.hpp>
#include <SFML/System/Vector2.hpp>

namespace pac::pnc {

/// The room camera: a world-space rectangle the size of the scenery viewport,
/// always clamped to the room bounds. A room no larger than the viewport is
/// centered and does not scroll. Pure/headless (no graphics context), so the
/// clamp + dead-zone follow are unit-testable; the room view turns `view_rect`
/// into an sf::View.
class Camera {
public:
    Camera(sf::Vector2f viewport_size, sf::Vector2u room_size);

    void set_viewport_size(sf::Vector2f size);
    void set_room_size(sf::Vector2u size);

    [[nodiscard]] sf::Vector2f center() const { return center_; }
    void set_center(sf::Vector2f center); // clamped to room bounds
    void snap_to(sf::Vector2f target);    // center on the target (clamped)

    /// Move only when `target` leaves the central dead-zone band, then clamp.
    void follow(sf::Vector2f target, sf::Vector2f dead_zone);

    [[nodiscard]] sf::FloatRect view_rect() const; // world rect shown
    [[nodiscard]] sf::Vector2f top_left() const;   // world coord of the viewport's top-left

private:
    [[nodiscard]] sf::Vector2f clamp_center(sf::Vector2f center) const;

    sf::Vector2f viewport_;
    sf::Vector2f room_;
    sf::Vector2f center_;
};

} // namespace pac::pnc
