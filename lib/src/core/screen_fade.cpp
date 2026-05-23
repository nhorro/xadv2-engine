#include "engine/core/screen_fade.hpp"

#include <algorithm>
#include <cmath>

namespace pac::core {

namespace {
constexpr float kEps = 0.001f;
}

void ScreenFade::start(float target, float duration_s) {
    target_ = std::clamp(target, 0.0f, 1.0f);
    if (duration_s <= 0.0f) {
        alpha_ = target_; // instant
        speed_ = 0.0f;
        return;
    }
    speed_ = 1.0f / duration_s;
}

void ScreenFade::fade_out(float duration_s) {
    start(1.0f, duration_s);
}

void ScreenFade::fade_in(float duration_s) {
    start(0.0f, duration_s);
}

void ScreenFade::update(float dt) {
    if (speed_ <= 0.0f || dt <= 0.0f) {
        return;
    }
    const float step = speed_ * dt;
    if (alpha_ < target_) {
        alpha_ = std::min(target_, alpha_ + step);
    } else {
        alpha_ = std::max(target_, alpha_ - step);
    }
    if (std::abs(alpha_ - target_) <= kEps) {
        alpha_ = target_;
        speed_ = 0.0f; // reached the target; idle
    }
}

void ScreenFade::skip() {
    alpha_ = target_;
    speed_ = 0.0f;
}

sf::Uint8 ScreenFade::alpha255() const {
    const float a = std::clamp(alpha_, 0.0f, 1.0f);
    return static_cast<sf::Uint8>(std::lround(a * 255.0f));
}

bool ScreenFade::opaque() const {
    return alpha_ >= 1.0f - kEps;
}

bool ScreenFade::clear() const {
    return alpha_ <= kEps;
}

bool ScreenFade::active() const {
    return speed_ > 0.0f && std::abs(alpha_ - target_) > kEps;
}

} // namespace pac::core
