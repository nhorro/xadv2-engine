#include "engine/pnc/avatar.hpp"

#include "engine/pnc/room.hpp"

#include <SFML/Graphics/RenderTarget.hpp>

#include <cmath>
#include <initializer_list>
#include <string>
#include <utility>

namespace pac::pnc {

Direction nearest_direction(geom::Point delta) {
    // Snap to the dominant axis; the >= bias sends 45° ties to the horizontal.
    if (std::abs(delta.x) >= std::abs(delta.y)) {
        return delta.x >= 0.0f ? Direction::Right : Direction::Left;
    }
    return delta.y >= 0.0f ? Direction::Down : Direction::Up;
}

const char* to_string(Direction d) {
    switch (d) {
    case Direction::Up:
        return "up";
    case Direction::Right:
        return "right";
    case Direction::Down:
        return "down";
    case Direction::Left:
        return "left";
    }
    return "down";
}

Direction direction_from_string(const std::string& s) {
    if (s == "up") {
        return Direction::Up;
    }
    if (s == "right") {
        return Direction::Right;
    }
    if (s == "left") {
        return Direction::Left;
    }
    return Direction::Down; // covers "down" and any unrecognized value
}

Avatar::Avatar(gfx::AnimatedSprite sprite, float scale)
    : sprite_(std::move(sprite)), scale_(scale) {
    sprite_.setScale(scale_, scale_);
    apply_animation();
}

void Avatar::apply_position() {
    sprite_.setPosition(pos_.x, pos_.y);
}

void Avatar::apply_animation() {
    const char* dir = to_string(facing_);
    const char* base = action_ == Action::Walk ? "walk" : "stand";
    // Prefer the directional variant, then the bare action, then the universal
    // `stand` fallback (the placeholder rig still names its rest pose `idle`).
    for (const std::string& candidate : {std::string(base) + "_" + dir,
                                         std::string(base),
                                         std::string("stand"),
                                         std::string("idle")}) {
        if (sprite_.has(candidate)) {
            sprite_.play(candidate, false);
            return;
        }
    }
}

void Avatar::set_position(geom::Point p) {
    // Teleport cancels any walk in progress. Without this, room transitions
    // and save-restores would keep walking toward the previous room's stale
    // target — which can loop back through a transition zone (e.g.
    // hall->study left the player still walking into study's `to_hall`).
    pos_ = p;
    stop();
    apply_position();
}

void Avatar::face(Direction direction) {
    facing_ = direction;
    apply_animation(); // refresh the current action's directional variant
}

void Avatar::move_to(geom::Point target) {
    target_ = target;
    const geom::Point delta{target.x - pos_.x, target.y - pos_.y};
    // Already on the spot: don't flash a one-frame walk or spin to face it.
    if (delta.x * delta.x + delta.y * delta.y < 1.0f) {
        stop();
        return;
    }
    facing_ = nearest_direction(delta);
    moving_ = true;
    action_ = Action::Walk;
    apply_animation();
}

void Avatar::stop() {
    moving_ = false;
    action_ = Action::Stand;
    apply_animation();
}

void Avatar::update(float dt, const RoomData& room) {
    sprite_.update(dt);
    if (!moving_) {
        return;
    }
    const geom::Point delta{target_.x - pos_.x, target_.y - pos_.y};
    const float dist = std::sqrt(delta.x * delta.x + delta.y * delta.y);
    if (dist < 1.0f) {
        pos_ = target_;
        stop();
        apply_position();
        return;
    }
    const float step = speed_ * dt;
    geom::Point next;
    if (step >= dist) {
        next = target_;
    } else {
        next = {pos_.x + delta.x / dist * step, pos_.y + delta.y / dist * step};
    }
    // Straight-line stand-in: refuse a step that leaves the walkable area.
    if (room.walkable.empty() || room.is_walkable(next)) {
        pos_ = next;
        apply_position();
        if (step >= dist) {
            stop();
        }
    } else {
        stop();
    }
}

void Avatar::draw(sf::RenderTarget& target) const {
    target.draw(sprite_);
}

} // namespace pac::pnc
