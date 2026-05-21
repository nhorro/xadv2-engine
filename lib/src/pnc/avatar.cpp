#include "engine/pnc/avatar.hpp"

#include "engine/pnc/room.hpp"

#include <SFML/Graphics/RenderTarget.hpp>

#include <cmath>
#include <utility>

namespace pac::pnc {

Avatar::Avatar(gfx::AnimatedSprite sprite, float scale)
    : sprite_(std::move(sprite)), scale_(scale) {
    sprite_.setScale(scale_, scale_);
    sprite_.play("idle");
}

void Avatar::apply_position() {
    sprite_.setPosition(pos_.x, pos_.y);
}

void Avatar::set_position(geom::Point p) {
    pos_ = p;
    apply_position();
}

void Avatar::move_to(geom::Point target) {
    target_ = target;
    moving_ = true;
    sprite_.play("walk", false);
}

void Avatar::stop() {
    if (moving_) {
        sprite_.play("idle", false);
    }
    moving_ = false;
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
