#include "engine/pnc/camera.hpp"

#include <algorithm>

namespace pac::pnc {

Camera::Camera(sf::Vector2f viewport_size, sf::Vector2u room_size)
    : viewport_(viewport_size),
      room_(static_cast<float>(room_size.x), static_cast<float>(room_size.y)) {
    center_ = clamp_center({room_.x / 2.0f, room_.y / 2.0f});
}

void Camera::set_viewport_size(sf::Vector2f size) {
    viewport_ = size;
    center_ = clamp_center(center_);
}

void Camera::set_room_size(sf::Vector2u size) {
    room_ = {static_cast<float>(size.x), static_cast<float>(size.y)};
    center_ = clamp_center(center_);
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

void Camera::set_center(sf::Vector2f center) {
    center_ = clamp_center(center);
}

void Camera::snap_to(sf::Vector2f target) {
    center_ = clamp_center(target);
}

void Camera::follow(sf::Vector2f target, sf::Vector2f dead_zone) {
    sf::Vector2f next = center_;
    const float dx = target.x - center_.x;
    if (dx > dead_zone.x) {
        next.x += dx - dead_zone.x;
    } else if (dx < -dead_zone.x) {
        next.x += dx + dead_zone.x;
    }
    const float dy = target.y - center_.y;
    if (dy > dead_zone.y) {
        next.y += dy - dead_zone.y;
    } else if (dy < -dead_zone.y) {
        next.y += dy + dead_zone.y;
    }
    center_ = clamp_center(next);
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
