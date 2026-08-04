#include "engine/pnc/avatar.hpp"

#include "engine/pnc/room.hpp"

#include <SFML/Graphics/CircleShape.hpp>
#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/Graphics/Transform.hpp>

#include <algorithm>
#include <array>
#include <cmath>
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
    talking_ = false;
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
    if (talking_) {
        const std::string preferred = std::string("talk_") + to_string(mover_.facing());
        // Preserve the current facing when that rig exists. A partial rig may
        // only ship one talk direction, though, so accept any authored direction
        // before falling back to a direction-neutral loop.
        for (const std::string& candidate : {preferred,
                                             std::string("talk_right"),
                                             std::string("talk_down"),
                                             std::string("talk_left"),
                                             std::string("talk_up"),
                                             std::string("talk")}) {
            if (sprite_.has(candidate)) {
                sprite_.play(candidate, false);
                return;
            }
        }
        talking_ = false; // no talk rig: use the normal stand fallback below
    }
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

void Avatar::set_shadow_opacity(float opacity, float transition_seconds) {
    if (!std::isfinite(opacity)) {
        return;
    }
    const float target = std::clamp(opacity, 0.0f, 1.0f);
    const float duration = std::isfinite(transition_seconds)
                               ? std::max(transition_seconds, 0.0f)
                               : 0.0f;
    if (duration <= 0.0f || target == shadow_opacity_) {
        shadow_opacity_ = target;
        shadow_opacity_start_ = target;
        shadow_opacity_target_ = target;
        shadow_opacity_elapsed_ = 0.0f;
        shadow_opacity_duration_ = 0.0f;
        return;
    }

    shadow_opacity_start_ = shadow_opacity_;
    shadow_opacity_target_ = target;
    shadow_opacity_elapsed_ = 0.0f;
    shadow_opacity_duration_ = duration;
}

void Avatar::face(Direction direction) {
    mover_.face(direction);
    apply_animation();
}

void Avatar::move_to(geom::Point target) {
    talking_ = false; // walking cancels a talk loop
    acting_.clear();  // walking cancels a scripted play()
    mover_.move_to(target);
    apply_animation();
}

void Avatar::follow_path(std::vector<geom::Point> path) {
    talking_ = false; // walking cancels a talk loop
    acting_.clear();  // walking cancels a scripted play()
    mover_.follow_path(std::move(path));
    apply_animation();
}

void Avatar::stop() {
    mover_.stop();
    apply_animation();
}

void Avatar::talk() {
    mover_.stop();
    acting_.clear();
    talking_ = true;
    apply_animation();
}

void Avatar::stop_talking() {
    if (!talking_) {
        return;
    }
    talking_ = false;
    apply_animation();
}

void Avatar::update(float dt, const RoomData& room) {
    sprite_.update(dt);
    mover_.update(dt, room);
    sync_sprite();
    if (shadow_opacity_duration_ > 0.0f) {
        shadow_opacity_elapsed_ += std::max(dt, 0.0f);
        const float progress =
            std::clamp(shadow_opacity_elapsed_ / shadow_opacity_duration_, 0.0f, 1.0f);
        shadow_opacity_ = shadow_opacity_start_ +
                          (shadow_opacity_target_ - shadow_opacity_start_) * progress;
        if (progress >= 1.0f) {
            shadow_opacity_ = shadow_opacity_target_;
            shadow_opacity_duration_ = 0.0f;
        }
    }
    // Perspective: scale by walking-pivot y when the room defines it, else keep
    // the base scale. The sprite scales about its pivot (feet), so the avatar
    // stays planted as the scale changes.
    const float s = room.avatar_scale_at(mover_.position().y, scale_);
    sprite_.setScale(s, s);
    draw_scale_ = s;
}

void Avatar::draw_shadow(sf::RenderTarget& target, float opacity_scale) const {
    if (!shadow_ || shadow_->size.x <= 0.0f || shadow_->size.y <= 0.0f) {
        return;
    }
    const geom::Point feet = mover_.position();
    // A unit circle (origin centered) scaled to the shadow's footprint gives a
    // cheap ellipse; it shrinks/grows with the avatar's perspective scale so it
    // stays planted under the feet at any depth.
    sf::CircleShape blob(0.5f, 24);
    blob.setOrigin(0.5f, 0.5f);
    sf::Color color = shadow_->color;
    color.a = static_cast<sf::Uint8>(
        std::lround(static_cast<float>(color.a) * std::clamp(opacity_scale, 0.0f, 1.0f)));
    blob.setFillColor(color);
    blob.setScale(shadow_->size.x * draw_scale_, shadow_->size.y * draw_scale_);
    blob.setPosition(feet.x, feet.y);
    target.draw(blob);
}

void Avatar::draw_projected_shadow(sf::RenderTarget& target,
                                   const ProjectedShadow& shadow,
                                   float opacity_scale) const {
    opacity_scale = std::clamp(opacity_scale, 0.0f, 1.0f);
    if (!shadow.enabled || shadow.opacity <= 0.0f || opacity_scale <= 0.0f) {
        return;
    }

    const geom::Point feet = mover_.position();
    sf::Vector2f away(feet.x - shadow.light.x, feet.y - shadow.light.y);
    const float distance = std::hypot(away.x, away.y);
    if (distance > 0.001f) {
        away /= distance;
    } else {
        away = {0.0f, 1.0f};
    }

    // A compact Poisson-like disk. Repeated low-alpha silhouettes approximate a
    // soft penumbra without allocating another render target or running a blur
    // over the full room. The alpha is normalized so the fully-overlapped center
    // converges to the authored opacity regardless of sample count.
    static const std::array<sf::Vector2f, 13> kSoftSamples{{
        {0.0f, 0.0f},
        {-0.42f, -0.08f},
        {0.35f, -0.30f},
        {0.18f, 0.44f},
        {-0.30f, 0.34f},
        {-0.92f, -0.22f},
        {-0.62f, 0.72f},
        {0.02f, -0.96f},
        {0.72f, -0.62f},
        {0.96f, 0.08f},
        {0.58f, 0.80f},
        {-0.10f, 0.98f},
        {-0.82f, 0.52f},
    }};

    const std::size_t samples = shadow.softness > 0.01f ? kSoftSamples.size() : 1;
    const float total_alpha = std::clamp(shadow.opacity * opacity_scale *
                                             (static_cast<float>(shadow.color.a) / 255.0f),
                                         0.0f,
                                         1.0f);
    const float sample_alpha =
        1.0f - std::pow(1.0f - total_alpha, 1.0f / static_cast<float>(samples));
    sf::Color tint = shadow.color;
    tint.a = static_cast<sf::Uint8>(std::clamp(std::lround(sample_alpha * 255.0f), 0l, 255l));

    // Frame-local X remains the silhouette's cross-axis. Frame-local Y grows
    // upward from the walking pivot, so mapping it against `away` lays the live
    // pose down onto the room plane in the direction opposite the light.
    const float across = draw_scale_ * shadow.width;
    const sf::Vector2f down_column(-away.x * draw_scale_ * shadow.length,
                                   -away.y * draw_scale_ * shadow.length);

    for (std::size_t i = 0; i < samples; ++i) {
        const sf::Vector2f jitter = i == 0 ? sf::Vector2f{} : kSoftSamples[i] * shadow.softness;
        const sf::Transform projection(across,
                                       down_column.x,
                                       feet.x + jitter.x,
                                       0.0f,
                                       down_column.y,
                                       feet.y + jitter.y,
                                       0.0f,
                                       0.0f,
                                       1.0f);
        sprite_.draw_transformed(target, projection, tint);
    }
}

void Avatar::draw(sf::RenderTarget& target,
                  pac::core::ResourceCache& resources,
                  float time,
                  pac::gfx::ShaderChain* chain,
                  const ProjectedShadow* projected_shadow) const {
    draw_shadows(target, projected_shadow);
    draw_sprite(target, resources, time, chain);
}

void Avatar::draw_shadows(sf::RenderTarget& target,
                          const ProjectedShadow* projected_shadow) const {
    if (!visible_) {
        return;
    }
    if (projected_shadow) {
        draw_projected_shadow(target, *projected_shadow, shadow_opacity_);
    }
    const float contact_opacity =
        shadow_opacity_ * (projected_shadow ? projected_shadow->contact_shadow : 1.0f);
    draw_shadow(target, contact_opacity);
}

void Avatar::draw_sprite(sf::RenderTarget& target,
                         pac::core::ResourceCache& resources,
                         float time,
                         pac::gfx::ShaderChain* chain) const {
    if (!visible_) {
        return;
    }
    sprite_.draw(target, resources, time, chain);
}

} // namespace pac::pnc
