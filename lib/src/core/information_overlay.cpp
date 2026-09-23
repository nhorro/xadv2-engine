#include "engine/core/information_overlay.hpp"

#include "engine/core/diagnostics.hpp"
#include "engine/core/display.hpp"
#include "engine/core/resource_cache.hpp"
#include "engine/core/text_encoding.hpp"
#include "engine/core/text_layout.hpp"

#include <SFML/Graphics/CircleShape.hpp>
#include <SFML/Graphics/ConvexShape.hpp>
#include <SFML/Graphics/Font.hpp>
#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/Graphics/Sprite.hpp>
#include <SFML/Graphics/Text.hpp>
#include <SFML/Graphics/Texture.hpp>
#include <SFML/Graphics/View.hpp>
#include <SFML/Window/Event.hpp>
#include <SFML/Window/Keyboard.hpp>

#include <algorithm>
#include <cmath>
#include <exception>
#include <utility>

namespace pac::core {
namespace {

float direction_rotation(IndicatorDirection direction) {
    switch (direction) {
    case IndicatorDirection::Up:
        return 180.0f;
    case IndicatorDirection::Left:
        return 90.0f;
    case IndicatorDirection::Right:
        return -90.0f;
    case IndicatorDirection::Down:
    default:
        return 0.0f;
    }
}

sf::Color scaled_alpha(sf::Color color, float amount) {
    color.a = static_cast<sf::Uint8>(
        std::clamp(std::lround(static_cast<float>(color.a) * amount), 0L, 255L));
    return color;
}

} // namespace

InformationOverlay::InformationOverlay(Display& display,
                                       ResourceCache& resources,
                                       Scripting& scripting,
                                       Diagnostics& log,
                                       InformationOverlayConfig config)
    : display_(display), resources_(resources), scripting_(scripting), log_(log),
      config_(std::move(config)) {
    if (!config_.font.empty()) {
        font_ = resources_.try_font(config_.font);
    }
}

std::string InformationOverlay::show(InformationPage page, ScopeId scope) {
    if (page_) {
        dismiss();
    }

    page_texture_ = nullptr;
    page_texture_attempted_ = false;

    saved_indicator_ = indicator_;
    if (page.indicator) {
        indicator_ = page.indicator;
    }
    page_ = std::move(page);
    waiting_scope_ = scope;
    waiting_event_ = "__information_overlay.dismissed." + std::to_string(++event_serial_);
    elapsed_ = 0.0f;
    return waiting_event_;
}

void InformationOverlay::dismiss() {
    if (!page_) {
        return;
    }
    page_.reset();
    page_texture_ = nullptr;
    page_texture_attempted_ = false;
    indicator_ = saved_indicator_;
    saved_indicator_.reset();
    const ScopeId scope = waiting_scope_;
    const std::string event = std::move(waiting_event_);
    waiting_scope_ = 0;
    waiting_event_.clear();
    if (scope != 0 && !event.empty()) {
        scripting_.emit(scope, event);
    }
}

void InformationOverlay::show_indicator(InformationIndicator indicator) {
    indicator_ = indicator;
    elapsed_ = 0.0f;
}

void InformationOverlay::hide_indicator() {
    indicator_.reset();
    saved_indicator_.reset();
}

bool InformationOverlay::handle_event(const sf::Event& event) {
    if (!page_) {
        return false;
    }
    const bool mouse_release = event.type == sf::Event::MouseButtonReleased;
    const bool key_release =
        event.type == sf::Event::KeyReleased &&
        (event.key.code == sf::Keyboard::Enter || event.key.code == sf::Keyboard::Space ||
         event.key.code == sf::Keyboard::Escape);
    if (mouse_release || key_release) {
        dismiss();
    }
    return true;
}

void InformationOverlay::update(float dt) {
    if (dt > 0.0f && (page_ || indicator_)) {
        elapsed_ = std::fmod(elapsed_ + dt, 1000.0f);
    }
}

void InformationOverlay::draw(sf::RenderTarget& target) const {
    if (!page_ && !indicator_) {
        return;
    }
    const sf::View previous = target.getView();
    target.setView(display_.view());
    if (page_) {
        const sf::Vector2u resolution = display_.virtual_resolution();
        sf::RectangleShape backdrop(
            {static_cast<float>(resolution.x), static_cast<float>(resolution.y)});
        backdrop.setFillColor(config_.backdrop_color);
        target.draw(backdrop);
    }
    draw_indicator(target);
    draw_page(target);
    target.setView(previous);
}

void InformationOverlay::draw_indicator(sf::RenderTarget& target) const {
    if (!indicator_) {
        return;
    }
    const float wave = 0.5f + 0.5f * std::sin(elapsed_ * 5.5f);
    const float pulse = 0.92f + wave * 0.16f;
    const float bob = 3.0f + wave * 6.0f;
    const float rotation = direction_rotation(indicator_->direction);

    sf::CircleShape ring(17.0f, 48);
    ring.setOrigin(17.0f, 17.0f);
    ring.setPosition(indicator_->position);
    ring.setScale(pulse, pulse);
    ring.setFillColor(sf::Color::Transparent);
    ring.setOutlineThickness(2.0f);
    ring.setOutlineColor(scaled_alpha(config_.indicator_color, 0.55f + wave * 0.4f));
    target.draw(ring);

    if (!config_.indicator_image.empty() && !indicator_texture_attempted_) {
        indicator_texture_attempted_ = true;
        try {
            indicator_texture_ = &resources_.texture(config_.indicator_image);
        } catch (const std::exception& e) {
            log_.warn("information overlay: could not load indicator image '" +
                      config_.indicator_image + "': " + e.what());
        }
    }
    if (indicator_texture_) {
        sf::Sprite sprite(*indicator_texture_);
        sprite.setOrigin(config_.indicator_hotspot);
        sprite.setPosition(indicator_->position.x, indicator_->position.y - bob);
        sprite.setRotation(rotation);
        const float scale = config_.indicator_scale * pulse;
        sprite.setScale(scale, scale);
        sprite.setColor(scaled_alpha(sf::Color::White, 0.82f + wave * 0.18f));
        target.draw(sprite);
        return;
    }

    // Down-pointing arrow authored around its tip at (0,0); rotation handles
    // the other directions while keeping the indicated coordinate exact.
    sf::ConvexShape arrow(7);
    arrow.setPoint(0, {0.0f, 0.0f});
    arrow.setPoint(1, {-18.0f, -20.0f});
    arrow.setPoint(2, {-7.0f, -20.0f});
    arrow.setPoint(3, {-7.0f, -49.0f});
    arrow.setPoint(4, {7.0f, -49.0f});
    arrow.setPoint(5, {7.0f, -20.0f});
    arrow.setPoint(6, {18.0f, -20.0f});
    arrow.setPosition(indicator_->position.x, indicator_->position.y - bob);
    arrow.setRotation(rotation);
    arrow.setScale(pulse, pulse);
    arrow.setFillColor(config_.indicator_color);
    arrow.setOutlineColor(sf::Color(35, 25, 15, config_.indicator_color.a));
    arrow.setOutlineThickness(1.5f);
    target.draw(arrow);
}

void InformationOverlay::draw_page(sf::RenderTarget& target) const {
    if (!page_) {
        return;
    }

    if (!page_->image.empty() && !page_texture_attempted_) {
        page_texture_attempted_ = true;
        try {
            page_texture_ = &resources_.texture(page_->image);
        } catch (const std::exception& e) {
            log_.warn("information overlay: could not load illustration '" + page_->image +
                      "': " + e.what());
        }
    }

    const sf::Vector2u resolution = display_.virtual_resolution();
    const float screen_w = static_cast<float>(resolution.x);
    const float screen_h = static_cast<float>(resolution.y);
    const float width = std::min(config_.panel_width, screen_w - 40.0f);
    const float padding = std::min(config_.panel_padding, width * 0.15f);
    const float content_w = std::max(1.0f, width - padding * 2.0f);
    const float gap = padding * 0.65f;

    float image_h = 0.0f;
    float image_w = 0.0f;
    if (page_texture_) {
        const sf::Vector2u size = page_texture_->getSize();
        if (size.x > 0 && size.y > 0) {
            const float scale = std::min(content_w / static_cast<float>(size.x),
                                         config_.image_max_height / static_cast<float>(size.y));
            image_w = static_cast<float>(size.x) * scale;
            image_h = static_cast<float>(size.y) * scale;
        }
    }

    TextLayout text_layout;
    float text_h = 0.0f;
    if (font_ && !page_->text.empty()) {
        const auto measure = [&](const std::string& text) {
            return sf::Text(utf8(text), *font_, config_.text_size).getLocalBounds().width;
        };
        text_layout =
            layout_text(page_->text, content_w, font_->getLineSpacing(config_.text_size), measure);
        text_h = text_layout.height;
    }

    const unsigned hint_size = std::max(14u, config_.text_size * 2u / 3u);
    const float hint_h =
        font_ && !page_->dismiss_text.empty() ? font_->getLineSpacing(hint_size) : 0.0f;
    float content_h = image_h + text_h + hint_h;
    if (image_h > 0.0f && text_h > 0.0f) {
        content_h += gap;
    }
    if (hint_h > 0.0f && (image_h > 0.0f || text_h > 0.0f)) {
        content_h += gap * 0.7f;
    }
    const float height = std::min(screen_h - 30.0f, content_h + padding * 2.0f);
    const float left = (screen_w - width) * 0.5f;
    const float top = (screen_h - height) * 0.5f;

    sf::RectangleShape shadow({width + 12.0f, height + 12.0f});
    shadow.setPosition(left + 7.0f, top + 9.0f);
    shadow.setFillColor(sf::Color(0, 0, 0, 110));
    target.draw(shadow);

    sf::RectangleShape panel({width, height});
    panel.setPosition(left, top);
    panel.setFillColor(config_.panel_color);
    panel.setOutlineColor(config_.panel_outline_color);
    panel.setOutlineThickness(2.0f);
    target.draw(panel);

    float y = top + padding;
    if (page_texture_ && image_h > 0.0f) {
        sf::Sprite image(*page_texture_);
        const sf::Vector2u size = page_texture_->getSize();
        image.setScale(image_w / static_cast<float>(size.x), image_h / static_cast<float>(size.y));
        image.setPosition(left + (width - image_w) * 0.5f, y);
        target.draw(image);
        y += image_h + (text_h > 0.0f ? gap : 0.0f);
    }
    if (font_ && text_h > 0.0f) {
        TextStyle style;
        style.size = config_.text_size;
        style.color = config_.text_color;
        style.outline_color = sf::Color(12, 10, 8, 180);
        style.outline_thickness = 0.75f;
        draw_text_block(target,
                        *font_,
                        page_->text,
                        style,
                        {left + padding, y},
                        content_w,
                        HAlign::Left,
                        VAnchor::Top);
    }
    if (font_ && hint_h > 0.0f) {
        sf::Text hint(utf8(page_->dismiss_text), *font_, hint_size);
        hint.setFillColor(config_.hint_color);
        const sf::FloatRect bounds = hint.getLocalBounds();
        hint.setPosition(left + (width - bounds.width) * 0.5f - bounds.left,
                         top + height - padding * 0.65f - bounds.height - bounds.top);
        target.draw(hint);
    }
}

} // namespace pac::core
