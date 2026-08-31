#include "engine/pnc/widget_presentation.hpp"

#include <SFML/Graphics/Color.hpp>
#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/Graphics/RenderTexture.hpp>
#include <SFML/Graphics/Sprite.hpp>
#include <SFML/Graphics/View.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

namespace pac::pnc {

sf::Vector2f place_widget(sf::FloatRect container,
                          sf::Vector2f widget_size,
                          const WidgetPlacement& placement) {
    const sf::Vector2f point{container.left + container.width * placement.position.x,
                            container.top + container.height * placement.position.y};
    sf::Vector2f anchor_factor;
    switch (placement.anchor) {
    case WidgetAnchor::TOP_LEFT: anchor_factor = {0.0f, 0.0f}; break;
    case WidgetAnchor::TOP_CENTER: anchor_factor = {0.5f, 0.0f}; break;
    case WidgetAnchor::TOP_RIGHT: anchor_factor = {1.0f, 0.0f}; break;
    case WidgetAnchor::CENTER_LEFT: anchor_factor = {0.0f, 0.5f}; break;
    case WidgetAnchor::CENTER: anchor_factor = {0.5f, 0.5f}; break;
    case WidgetAnchor::CENTER_RIGHT: anchor_factor = {1.0f, 0.5f}; break;
    case WidgetAnchor::BOTTOM_LEFT: anchor_factor = {0.0f, 1.0f}; break;
    case WidgetAnchor::BOTTOM_CENTER: anchor_factor = {0.5f, 1.0f}; break;
    case WidgetAnchor::BOTTOM_RIGHT: anchor_factor = {1.0f, 1.0f}; break;
    }
    return {point.x - widget_size.x * anchor_factor.x + placement.offset.x,
            point.y - widget_size.y * anchor_factor.y + placement.offset.y};
}

WidgetPresentation::WidgetPresentation(bool initially_visible, WidgetTransition transition)
    : transition_(transition),
      visibility_(initially_visible ? WidgetVisibility::VISIBLE : WidgetVisibility::HIDDEN),
      progress_(initially_visible ? 1.0f : 0.0f) {}

void WidgetPresentation::set_position(sf::Vector2f position) {
    bounds_.left = position.x;
    bounds_.top = position.y;
}

void WidgetPresentation::set_size(sf::Vector2f size) {
    bounds_.width = std::max(0.0f, size.x);
    bounds_.height = std::max(0.0f, size.y);
}

void WidgetPresentation::set_opacity(float opacity) {
    base_opacity_ = std::clamp(opacity, 0.0f, 1.0f);
}

void WidgetPresentation::show() {
    if (visibility_ == WidgetVisibility::VISIBLE || visibility_ == WidgetVisibility::SHOWING) return;
    if (transition_.fade_duration <= 0.0f) {
        progress_ = 1.0f;
        visibility_ = WidgetVisibility::VISIBLE;
    } else {
        visibility_ = WidgetVisibility::SHOWING;
    }
}

void WidgetPresentation::hide() {
    if (visibility_ == WidgetVisibility::HIDDEN || visibility_ == WidgetVisibility::HIDING) return;
    if (transition_.fade_duration <= 0.0f) {
        progress_ = 0.0f;
        visibility_ = WidgetVisibility::HIDDEN;
    } else {
        visibility_ = WidgetVisibility::HIDING;
    }
}

void WidgetPresentation::update(float dt) {
    if (transition_.fade_duration <= 0.0f) return;
    const float delta = std::max(0.0f, dt) / transition_.fade_duration;
    if (visibility_ == WidgetVisibility::SHOWING) {
        progress_ = std::min(1.0f, progress_ + delta);
        if (progress_ >= 1.0f) visibility_ = WidgetVisibility::VISIBLE;
    } else if (visibility_ == WidgetVisibility::HIDING) {
        progress_ = std::max(0.0f, progress_ - delta);
        if (progress_ <= 0.0f) visibility_ = WidgetVisibility::HIDDEN;
    }
}

sf::FloatRect WidgetPresentation::bounds() const {
    return {bounds_.left + translation_.x,
            bounds_.top + translation_.y,
            bounds_.width,
            bounds_.height};
}

float WidgetPresentation::opacity() const {
    const float eased = progress_ * progress_ * (3.0f - 2.0f * progress_);
    return base_opacity_ * eased;
}

bool WidgetPresentation::captures_input() const {
    if (!input_enabled_ || visibility_ == WidgetVisibility::HIDDEN) return false;
    return visibility_ != WidgetVisibility::HIDING || transition_.capture_while_hiding;
}

WidgetSurface::WidgetSurface() = default;
WidgetSurface::~WidgetSurface() = default;
WidgetSurface::WidgetSurface(WidgetSurface&&) noexcept = default;
WidgetSurface& WidgetSurface::operator=(WidgetSurface&&) noexcept = default;

void WidgetSurface::draw(sf::RenderTarget& target,
                         const WidgetPresentation& presentation,
                         sf::FloatRect content_bounds,
                         const std::function<void(sf::RenderTarget&)>& painter) const {
    if (!presentation.rendered() || presentation.opacity() <= 0.0f) return;
    const sf::FloatRect bounds = presentation.bounds();
    const unsigned width = std::max(1u, static_cast<unsigned>(std::ceil(bounds.width)));
    const unsigned height = std::max(1u, static_cast<unsigned>(std::ceil(bounds.height)));
    if (!texture_ || texture_->getSize() != sf::Vector2u{width, height}) {
        auto next = std::make_unique<sf::RenderTexture>();
        if (!next->create(width, height)) return;
        texture_ = std::move(next);
    }
    texture_->setView(sf::View(content_bounds));
    texture_->clear(sf::Color::Transparent);
    painter(*texture_);
    texture_->display();
    sf::Sprite sprite(texture_->getTexture());
    sprite.setPosition(bounds.left, bounds.top);
    sprite.setColor({255, 255, 255, static_cast<sf::Uint8>(std::lround(255.0f * presentation.opacity()))});
    target.draw(sprite);
}

} // namespace pac::pnc
