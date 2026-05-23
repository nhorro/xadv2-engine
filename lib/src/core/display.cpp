#include "engine/core/display.hpp"

#include <algorithm>
#include <cmath>

namespace pac::core {

std::vector<sf::Vector2u> windowed_size_options(sf::Vector2u desktop, sf::Vector2u virtual_res) {
    std::vector<sf::Vector2u> out;
    if (virtual_res.x == 0 || virtual_res.y == 0) {
        return out;
    }
    const float factors[] = {1.0f, 1.5f, 2.0f, 3.0f};
    for (const float f : factors) {
        const sf::Vector2u s{static_cast<unsigned>(std::lround(virtual_res.x * f)),
                             static_cast<unsigned>(std::lround(virtual_res.y * f))};
        const bool fits =
            desktop.x == 0 || desktop.y == 0 || (s.x <= desktop.x && s.y <= desktop.y);
        if (!fits) {
            continue;
        }
        if (std::find(out.begin(), out.end(), s) == out.end()) {
            out.push_back(s);
        }
    }
    if (out.empty()) {
        out.push_back(virtual_res); // always offer at least the native size
    }
    return out;
}

Viewport letterbox(sf::Vector2u window, sf::Vector2u virtual_res) {
    Viewport vp;
    if (window.x == 0 || window.y == 0 || virtual_res.x == 0 || virtual_res.y == 0) {
        vp.scale = 1.0f;
        vp.size = {static_cast<float>(virtual_res.x), static_cast<float>(virtual_res.y)};
        vp.offset = {0.0f, 0.0f};
        return vp;
    }
    const float sx = static_cast<float>(window.x) / static_cast<float>(virtual_res.x);
    const float sy = static_cast<float>(window.y) / static_cast<float>(virtual_res.y);
    vp.scale = std::min(sx, sy);
    vp.size = {virtual_res.x * vp.scale, virtual_res.y * vp.scale};
    vp.offset = {(static_cast<float>(window.x) - vp.size.x) / 2.0f,
                 (static_cast<float>(window.y) - vp.size.y) / 2.0f};
    return vp;
}

sf::Vector2f window_to_virtual(sf::Vector2i px, sf::Vector2u window, sf::Vector2u virtual_res) {
    const Viewport vp = letterbox(window, virtual_res);
    return {(static_cast<float>(px.x) - vp.offset.x) / vp.scale,
            (static_cast<float>(px.y) - vp.offset.y) / vp.scale};
}

Display::Display(sf::Vector2u virtual_res, sf::Vector2u window, bool fullscreen)
    : virtual_res_(virtual_res), window_(window), fullscreen_(fullscreen) {}

void Display::set_window_size(sf::Vector2u window) {
    window_ = window;
}

std::optional<DisplayMode> Display::take_pending_mode() {
    std::optional<DisplayMode> m = pending_;
    pending_.reset();
    return m;
}

Viewport Display::viewport() const {
    return letterbox(window_, virtual_res_);
}

sf::View Display::view() const {
    sf::View v(sf::FloatRect(0.0f,
                             0.0f,
                             static_cast<float>(virtual_res_.x),
                             static_cast<float>(virtual_res_.y)));
    const Viewport vp = letterbox(window_, virtual_res_);
    if (window_.x > 0 && window_.y > 0) {
        v.setViewport(sf::FloatRect(vp.offset.x / static_cast<float>(window_.x),
                                    vp.offset.y / static_cast<float>(window_.y),
                                    vp.size.x / static_cast<float>(window_.x),
                                    vp.size.y / static_cast<float>(window_.y)));
    }
    return v;
}

sf::Vector2f Display::to_virtual(sf::Vector2i px) const {
    return window_to_virtual(px, window_, virtual_res_);
}

sf::FloatRect Display::viewport_for(sf::FloatRect virtual_rect) const {
    const Viewport vp = letterbox(window_, virtual_res_);
    if (window_.x == 0 || window_.y == 0) {
        return {0.0f, 0.0f, 1.0f, 1.0f};
    }
    const float wx = static_cast<float>(window_.x);
    const float wy = static_cast<float>(window_.y);
    return {(vp.offset.x + virtual_rect.left * vp.scale) / wx,
            (vp.offset.y + virtual_rect.top * vp.scale) / wy,
            (virtual_rect.width * vp.scale) / wx,
            (virtual_rect.height * vp.scale) / wy};
}

} // namespace pac::core
