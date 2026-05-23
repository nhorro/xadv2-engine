#include "engine/pnc/avatar.hpp"

#include <SFML/Graphics/RenderTarget.hpp>

#include <initializer_list>
#include <string>
#include <utility>

namespace pac::pnc {

Avatar::Avatar(gfx::AnimatedSprite sprite, float scale)
    : sprite_(std::move(sprite)), scale_(scale) {
    sprite_.setScale(scale_, scale_);
    apply_animation();
}

void Avatar::apply_animation() {
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
    mover_.move_to(target);
    apply_animation();
}

void Avatar::follow_path(std::vector<geom::Point> path) {
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
}

void Avatar::draw(sf::RenderTarget& target) const {
    target.draw(sprite_);
}

} // namespace pac::pnc
