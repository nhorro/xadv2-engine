#include "engine/core/pointer_input.hpp"

#include <SFML/Window/Mouse.hpp>

namespace pac::core {

namespace {

sf::Event mouse_move_from(const sf::Event::TouchEvent& touch) {
    sf::Event event{};
    event.type = sf::Event::MouseMoved;
    event.mouseMove.x = touch.x;
    event.mouseMove.y = touch.y;
    return event;
}

sf::Event mouse_button_from(const sf::Event::TouchEvent& touch, sf::Event::EventType type) {
    sf::Event event{};
    event.type = type;
    event.mouseButton.button = sf::Mouse::Left;
    event.mouseButton.x = touch.x;
    event.mouseButton.y = touch.y;
    return event;
}

} // namespace

std::span<const sf::Event> PointerInput::translate(const sf::Event& event) {
    switch (event.type) {
    case sf::Event::TouchBegan:
        if (primary_touch_) {
            return {};
        }
        primary_touch_ = event.touch.finger;
        translated_[0] = mouse_move_from(event.touch);
        translated_[1] = mouse_button_from(event.touch, sf::Event::MouseButtonPressed);
        return translated_;

    case sf::Event::TouchMoved:
        if (primary_touch_ != event.touch.finger) {
            return {};
        }
        translated_[0] = mouse_move_from(event.touch);
        return {translated_.data(), 1};

    case sf::Event::TouchEnded:
        if (primary_touch_ != event.touch.finger) {
            return {};
        }
        primary_touch_.reset();
        translated_[0] = mouse_move_from(event.touch);
        translated_[1] = mouse_button_from(event.touch, sf::Event::MouseButtonReleased);
        return translated_;

    default:
        translated_[0] = event;
        return {translated_.data(), 1};
    }
}

} // namespace pac::core
