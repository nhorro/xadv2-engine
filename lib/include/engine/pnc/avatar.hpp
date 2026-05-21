#pragma once

#include "engine/geom/geometry.hpp"
#include "engine/gfx/animated_sprite.hpp"

#include <string>

namespace sf {
class RenderTarget;
}

namespace pac::pnc {

struct RoomData;

/// The in-room visual + spatial instance of a character. M3: a single
/// AnimatedSprite positioned by its walking pivot, with straight-line movement
/// gated by the room's walkable area (the stand-in behind the future pathfinder).
class Avatar {
public:
    explicit Avatar(gfx::AnimatedSprite sprite, float scale = 1.0f);

    void set_position(geom::Point p);
    geom::Point position() const { return pos_; }
    void face(const std::string& direction) { facing_ = direction; }
    const std::string& facing() const { return facing_; }

    void move_to(geom::Point target);
    void stop();
    bool moving() const { return moving_; }

    void update(float dt, const RoomData& room);
    void draw(sf::RenderTarget& target) const;

    float z() const { return pos_.y; } // depth key = walking-pivot y

private:
    void apply_position();

    gfx::AnimatedSprite sprite_;
    geom::Point pos_{0.0f, 0.0f};
    geom::Point target_{0.0f, 0.0f};
    bool moving_ = false;
    float speed_ = 240.0f; // world px/s
    float scale_ = 1.0f;
    std::string facing_ = "down";
};

} // namespace pac::pnc
