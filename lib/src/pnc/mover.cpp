#include "engine/pnc/mover.hpp"

#include "engine/pnc/room.hpp"

#include <cmath>
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

void Mover::set_position(geom::Point p) {
    // Teleport cancels any walk in progress. Without this, room transitions and
    // save-restores would keep walking toward the previous room's stale target —
    // which can loop back through a transition zone.
    pos_ = p;
    stop();
}

void Mover::move_to(geom::Point target) {
    follow_path({target});
}

void Mover::follow_path(std::vector<geom::Point> path) {
    path_ = std::move(path);
    path_pos_ = 0;
    if (!start_leg()) {
        stop();
    }
}

bool Mover::start_leg() {
    while (path_pos_ < path_.size()) {
        target_ = path_[path_pos_];
        const geom::Point delta{target_.x - pos_.x, target_.y - pos_.y};
        // Skip a waypoint we are already on rather than flashing a null walk.
        if (delta.x * delta.x + delta.y * delta.y >= 1.0f) {
            facing_ = nearest_direction(delta);
            moving_ = true;
            action_ = Action::Walk;
            return true;
        }
        ++path_pos_;
    }
    return false;
}

void Mover::stop() {
    moving_ = false;
    action_ = Action::Stand;
    path_.clear();
    path_pos_ = 0;
}

void Mover::update(float dt, const RoomData& room) {
    if (!moving_) {
        return;
    }
    const geom::Point delta{target_.x - pos_.x, target_.y - pos_.y};
    const float dist = std::sqrt(delta.x * delta.x + delta.y * delta.y);
    if (dist < 1.0f) {
        pos_ = target_;
        ++path_pos_;
        if (!start_leg()) {
            stop();
        }
        return;
    }
    const float step = speed_ * dt;
    geom::Point next;
    if (step >= dist) {
        next = target_;
    } else {
        next = {pos_.x + delta.x / dist * step, pos_.y + delta.y / dist * step};
    }
    // Refuse a step that leaves the walkable area; a blocked step abandons the
    // rest of the path rather than burrowing into a wall.
    if (room.walkable.empty() || room.is_walkable(next)) {
        pos_ = next;
        if (step >= dist) {
            ++path_pos_;
            if (!start_leg()) {
                stop();
            }
        }
    } else {
        stop();
    }
}

} // namespace pac::pnc
