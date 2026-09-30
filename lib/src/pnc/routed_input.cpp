#include "engine/pnc/routed_input.hpp"

#include <SFML/Window/Event.hpp>
#include <SFML/Window/Mouse.hpp>

namespace pac::pnc {

std::optional<RoutedInput> routed_pointer_input(const sf::Event& event) {
    if (event.is<sf::Event::MouseMoved>()) {
        return RoutedInput{RoutedInputKind::POINTER_MOVED,
                           {static_cast<float>(event.getIf<sf::Event::MouseMoved>()->position.x),
                            static_cast<float>(event.getIf<sf::Event::MouseMoved>()->position.y)}};
    }
    if (!event.is<sf::Event::MouseButtonPressed>() && !event.is<sf::Event::MouseButtonReleased>()) {
        return std::nullopt;
    }

    const auto* pressed = event.getIf<sf::Event::MouseButtonPressed>();
    const auto* released = event.getIf<sf::Event::MouseButtonReleased>();
    const auto button = pressed ? pressed->button : released->button;
    const auto position = pressed ? pressed->position : released->position;
    RoutedInputKind kind;
    if (button == sf::Mouse::Button::Left) {
        kind = event.is<sf::Event::MouseButtonPressed>() ? RoutedInputKind::PRIMARY_PRESSED
                                                         : RoutedInputKind::PRIMARY_RELEASED;
    } else if (button == sf::Mouse::Button::Right) {
        kind = event.is<sf::Event::MouseButtonPressed>() ? RoutedInputKind::SECONDARY_PRESSED
                                                         : RoutedInputKind::SECONDARY_RELEASED;
    } else {
        return std::nullopt;
    }
    return RoutedInput{kind, {static_cast<float>(position.x), static_cast<float>(position.y)}};
}

} // namespace pac::pnc
