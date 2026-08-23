#pragma once

#include <SFML/Window/Event.hpp>

#include <array>
#include <optional>
#include <span>

namespace pac::core {

/// Converts platform pointer input into the mouse-shaped event contract used by
/// scenes. Mouse events pass through unchanged. A primary touch becomes pointer
/// movement followed by a left-button press/release, so existing hover state is
/// established before an action fires and game code never branches on platform.
class PointerInput {
public:
    /// The returned view remains valid until the next call on this object.
    [[nodiscard]] std::span<const sf::Event> translate(const sf::Event& event);

private:
    std::array<sf::Event, 2> translated_{};
    std::optional<unsigned int> primary_touch_;
};

} // namespace pac::core
