#include "engine/pnc/avatar.hpp"

#include "engine/pnc/room.hpp"

#include <SFML/Graphics/CircleShape.hpp>
#include <SFML/Graphics/RenderTarget.hpp>

#include <initializer_list>
#include <string>
#include <utility>

namespace pac::pnc {

Avatar::Avatar(gfx::AnimatedSprite sprite, float scale, std::optional<Shadow> shadow)
    : sprite_(std::move(sprite)), scale_(scale), draw_scale_(scale), shadow_(shadow) {
    sprite_.setScale(scale_, scale_);
    apply_animation();
}

void Avatar::play(const std::string& sequence) {
    if (!sprite_.has(sequence)) {
        return; // unknown sequence: leave the mover-driven animation in place
    }
    acting_ = sequence;
    sprite_.play(sequence, true); // restart from the first frame
}

std::optional<geom::Point> Avatar::anchor(const std::string& name) const {
    if (const auto p = sprite_.anchor_world(name)) {
        return geom::Point{p->x, p->y};
    }
    return std::nullopt;
}

void Avatar::apply_animation() {
    if (!acting_.empty()) {
        // A scripted play() sequence holds until a non-looping one finishes (then
        // control returns to stand/walk); a looping one runs until movement
        // cancels it (see move_to / follow_path).
        if (!sprite_.finished()) {
            return;
        }
        acting_.clear();
    }
    const char* dir = to_string(mover_.facing());
    const char* base = mover_.action() == Action::Walk ? "walk" : "stand";
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

void Avatar::sync_sprite() {
    const geom::Point p = mover_.position();
    sprite_.setPosition(p.x, p.y);
    apply_animation(); // play(restart=false) is a no-op while the sequence holds
}

void Avatar::set_position(geom::Point p) {
    mover_.set_position(p);
    sync_sprite();
}

void Avatar::face(Direction direction) {
    mover_.face(direction);
    apply_animation();
}

void Avatar::move_to(geom::Point target) {
    acting_.clear(); // walking cancels a scripted play()
    mover_.move_to(target);
    apply_animation();
}

void Avatar::follow_path(std::vector<geom::Point> path) {
    acting_.clear(); // walking cancels a scripted play()
    mover_.follow_path(std::move(path));
    apply_animation();
}

void Avatar::stop() {
    mover_.stop();
    apply_animation();
}

void Avatar::update(float dt, const RoomData& room) {
    sprite_.update(dt);
    mover_.update(dt, room);
    sync_sprite();
    // Perspective: scale by walking-pivot y when the room defines it, else keep
    // the base scale. The sprite scales about its pivot (feet), so the avatar
    // stays planted as the scale changes.
    const float s = room.avatar_scale_at(mover_.position().y, scale_);
    sprite_.setScale(s, s);
    draw_scale_ = s;
}

void Avatar::draw_shadow(sf::RenderTarget& target) const {
    if (!shadow_ || shadow_->size.x <= 0.0f || shadow_->size.y <= 0.0f) {
        return;
    }
    const geom::Point feet = mover_.position();
    // A unit circle (origin centered) scaled to the shadow's footprint gives a
    // cheap ellipse; it shrinks/grows with the avatar's perspective scale so it
    // stays planted under the feet at any depth.
    sf::CircleShape blob(0.5f, 24);
    blob.setOrigin(0.5f, 0.5f);
    blob.setFillColor(shadow_->color);
    blob.setScale(shadow_->size.x * draw_scale_, shadow_->size.y * draw_scale_);
    blob.setPosition(feet.x, feet.y);
    target.draw(blob);
}

void Avatar::draw(sf::RenderTarget& target,
                  pac::core::ResourceCache& resources,
                  float time,
                  pac::gfx::ShaderChain* chain) const {
    draw_shadow(target); // beneath the sprite, grounded on the pivot
    sprite_.draw(target, resources, time, chain);
}

} // namespace pac::pnc
