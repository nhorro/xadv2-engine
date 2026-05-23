#pragma once

#include "engine/geom/geometry.hpp"
#include "engine/gfx/animated_sprite.hpp"
#include "engine/pnc/mover.hpp"

#include <string>
#include <vector>

namespace sf {
class RenderTarget;
}

namespace pac::pnc {

struct RoomData;

/// The in-room visual instance of a character: an AnimatedSprite driven by a
/// headless `Mover`. The Mover owns all movement state and logic; the Avatar only
/// syncs the sprite (walking-pivot position + directional animation) from it, so
/// movement stays testable without a graphics context.
class Avatar {
public:
    explicit Avatar(gfx::AnimatedSprite sprite, float scale = 1.0f);

    void set_position(geom::Point p);
    geom::Point position() const { return mover_.position(); }
    void face(const std::string& direction) { face(direction_from_string(direction)); }
    void face(Direction direction);
    std::string facing() const { return to_string(mover_.facing()); }

    /// Walk straight to a single point, gated by the walkable area.
    void move_to(geom::Point target);
    /// Walk an ordered list of waypoints (e.g. a `geom::find_path` result),
    /// turning at each corner. An empty path stops the avatar.
    void follow_path(std::vector<geom::Point> path);
    void stop();
    bool moving() const { return mover_.moving(); }

    void update(float dt, const RoomData& room);
    void draw(sf::RenderTarget& target) const;

    float z() const { return mover_.position().y; } // depth key = walking-pivot y

private:
    /// Mirror the mover's position and facing/action onto the sprite.
    void sync_sprite();
    /// Play the sequence for the current action + facing, falling back from
    /// `<action>_<direction>` to the bare `<action>` and finally to `stand`.
    void apply_animation();

    gfx::AnimatedSprite sprite_;
    Mover mover_;
    float scale_ = 1.0f; // base scale; the fallback when the room has no perspective
};

} // namespace pac::pnc
