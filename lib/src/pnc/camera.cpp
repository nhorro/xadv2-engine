#include "engine/pnc/camera.hpp"

#include <algorithm>
#include <cmath>

namespace pac::pnc {

Camera::Camera(sf::Vector2f viewport_size, sf::Vector2u room_size)
    : viewport_(viewport_size),
      room_(static_cast<float>(room_size.x), static_cast<float>(room_size.y)),
      follow_bounds_(0.0f, 0.0f, room_.x, room_.y) {
    center_ = clamp_center({room_.x / 2.0f, room_.y / 2.0f});
}

void Camera::set_viewport_size(sf::Vector2f size) {
    viewport_ = size;
    center_ = clamp_center(center_);
}

void Camera::set_room_size(sf::Vector2u size) {
    room_ = {static_cast<float>(size.x), static_cast<float>(size.y)};
    follow_bounds_ = {0.0f, 0.0f, room_.x, room_.y};
    center_ = clamp_center(center_);
}

void Camera::set_follow_bounds(sf::FloatRect bounds) {
    follow_bounds_ = bounds;
}

void Camera::set_follow_margin(float fraction) {
    follow_margin_ = std::clamp(fraction, 0.0f, 0.45f);
}

sf::Vector2f Camera::clamp_center(sf::Vector2f center) const {
    sf::Vector2f result = center;
    const float half_w = viewport_.x / 2.0f;
    const float half_h = viewport_.y / 2.0f;
    // If the room is no wider/taller than the viewport, lock to the room center.
    result.x =
        (room_.x <= viewport_.x) ? room_.x / 2.0f : std::clamp(result.x, half_w, room_.x - half_w);
    result.y =
        (room_.y <= viewport_.y) ? room_.y / 2.0f : std::clamp(result.y, half_h, room_.y - half_h);
    return result;
}

float Camera::scroll_axis(float target,
                          float reach_min,
                          float reach_max,
                          float room,
                          float view) const {
    if (room <= view) {
        return room / 2.0f; // room fits the viewport: no scroll, stay centered
    }
    const float lo = view / 2.0f;        // center showing the start edge of the room
    const float hi = room - view / 2.0f; // center showing the end edge of the room
    const float span = reach_max - reach_min;
    float center;
    if (span <= 1e-6f) {
        center = std::clamp(target, lo, hi); // no reachable span: plain clamp-follow
    } else {
        // Map the reachable span [reach_min, reach_max] onto [lo, hi] so the
        // extremes of the player's reach show the extremes of the background.
        const float t = std::clamp((target - reach_min) / span, 0.0f, 1.0f);
        center = lo + t * (hi - lo);
    }
    // On-screen clamp: keep the player's pivot at least `margin` from the viewport
    // edges. When the walkable area is a thin slice of a much larger room the raw
    // reveal above would scroll the player off screen (or into an unclickable
    // sliver); this caps the scroll so the player stays visible. The reveal is
    // honored wherever it already keeps the player inside the margin band.
    const float margin = follow_margin_ * view;
    const float vis_lo = target - view / 2.0f + margin; // player at the bottom/right margin
    const float vis_hi = target + view / 2.0f - margin; // player at the top/left margin
    center = std::clamp(center, vis_lo, vis_hi);
    // Room bounds stay the hard limit: never show outside the room, even if that
    // leaves the player closer to the edge than the margin (they are at the room
    // edge, so there is nothing more to reveal anyway).
    return std::clamp(center, lo, hi);
}

sf::Vector2f Camera::scroll_center(sf::Vector2f target) const {
    return {scroll_axis(target.x,
                        follow_bounds_.left,
                        follow_bounds_.left + follow_bounds_.width,
                        room_.x,
                        viewport_.x),
            scroll_axis(target.y,
                        follow_bounds_.top,
                        follow_bounds_.top + follow_bounds_.height,
                        room_.y,
                        viewport_.y)};
}

void Camera::set_center(sf::Vector2f center) {
    center_ = clamp_center(center);
}

void Camera::snap_to(sf::Vector2f target) {
    center_ = scroll_center(target);
}

void Camera::follow(sf::Vector2f target) {
    center_ = scroll_center(target);
}

sf::FloatRect Camera::view_rect() const {
    return {center_.x - viewport_.x / 2.0f,
            center_.y - viewport_.y / 2.0f,
            viewport_.x,
            viewport_.y};
}

sf::Vector2f Camera::top_left() const {
    return {center_.x - viewport_.x / 2.0f, center_.y - viewport_.y / 2.0f};
}

} // namespace pac::pnc
