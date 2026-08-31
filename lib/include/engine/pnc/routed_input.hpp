#pragma once

#include <SFML/System/Vector2.hpp>

#include <optional>

namespace sf {
class Event;
}

namespace pac::pnc {

enum class RoutedInputKind {
    POINTER_MOVED,
    PRIMARY_PRESSED,
    PRIMARY_RELEASED,
    SECONDARY_PRESSED,
    SECONDARY_RELEASED,
};

/// Pointer input after application touch translation and virtual-coordinate
/// rewriting. Room input layers never inspect a platform/window event.
struct RoutedInput {
    RoutedInputKind kind = RoutedInputKind::POINTER_MOVED;
    sf::Vector2f position{0.0f, 0.0f};

    [[nodiscard]] bool moved() const { return kind == RoutedInputKind::POINTER_MOVED; }
    [[nodiscard]] bool primary_release() const { return kind == RoutedInputKind::PRIMARY_RELEASED; }
    [[nodiscard]] bool secondary_release() const {
        return kind == RoutedInputKind::SECONDARY_RELEASED;
    }
};

/// Convert the mouse-shaped scene event contract into routed pointer input.
/// Keyboard and non-pointer events return nullopt and remain scene-level input.
[[nodiscard]] std::optional<RoutedInput> routed_pointer_input(const sf::Event& event);

} // namespace pac::pnc
