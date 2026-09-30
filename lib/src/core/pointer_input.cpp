#include "engine/core/pointer_input.hpp"

#include <SFML/Window/Mouse.hpp>
namespace pac::core {
std::span<const sf::Event> PointerInput::translate(const sf::Event& event) {
    if (const auto* touch = event.getIf<sf::Event::TouchBegan>()) {
        if (primary_touch_)
            return {};
        primary_touch_ = touch->finger;
        translated_[0] = sf::Event::MouseMoved{touch->position};
        translated_[1] = sf::Event::MouseButtonPressed{sf::Mouse::Button::Left, touch->position};
        return translated_;
    }
    if (const auto* touch = event.getIf<sf::Event::TouchMoved>()) {
        if (primary_touch_ != touch->finger)
            return {};
        translated_[0] = sf::Event::MouseMoved{touch->position};
        return {translated_.data(), 1};
    }
    if (const auto* touch = event.getIf<sf::Event::TouchEnded>()) {
        if (primary_touch_ != touch->finger)
            return {};
        primary_touch_.reset();
        translated_[0] = sf::Event::MouseMoved{touch->position};
        translated_[1] = sf::Event::MouseButtonReleased{sf::Mouse::Button::Left, touch->position};
        return translated_;
    }
    translated_[0] = event;
    return {translated_.data(), 1};
}
} // namespace pac::core
