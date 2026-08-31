#include "engine/pnc/routed_input.hpp"

#include <SFML/Window/Event.hpp>
#include <SFML/Window/Mouse.hpp>

namespace pac::pnc {

std::optional<RoutedInput> routed_pointer_input(const sf::Event& event) {
    if (event.type == sf::Event::MouseMoved) {
        return RoutedInput{
            RoutedInputKind::POINTER_MOVED,
            {static_cast<float>(event.mouseMove.x), static_cast<float>(event.mouseMove.y)}};
    }
    if (event.type != sf::Event::MouseButtonPressed &&
        event.type != sf::Event::MouseButtonReleased) {
        return std::nullopt;
    }

    RoutedInputKind kind;
    if (event.mouseButton.button == sf::Mouse::Left) {
        kind = event.type == sf::Event::MouseButtonPressed ? RoutedInputKind::PRIMARY_PRESSED
                                                           : RoutedInputKind::PRIMARY_RELEASED;
    } else if (event.mouseButton.button == sf::Mouse::Right) {
        kind = event.type == sf::Event::MouseButtonPressed ? RoutedInputKind::SECONDARY_PRESSED
                                                           : RoutedInputKind::SECONDARY_RELEASED;
    } else {
        return std::nullopt;
    }
    return RoutedInput{
        kind,
        {static_cast<float>(event.mouseButton.x), static_cast<float>(event.mouseButton.y)}};
}

} // namespace pac::pnc
