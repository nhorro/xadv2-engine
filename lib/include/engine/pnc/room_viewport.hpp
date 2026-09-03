#pragma once

#include <SFML/Graphics/Rect.hpp>
#include <SFML/System/Vector2.hpp>

namespace pac::pnc {

/// Runtime room-rendering extent in virtual pixels.
///
/// This is deliberately independent of every UI/widget rectangle. The normal
/// composition root supplies the complete virtual display resolution; focused
/// examples and tests may inject a different extent explicitly.
struct RoomViewport {
    sf::Vector2f size{0.0f, 0.0f};

    [[nodiscard]] static RoomViewport from_runtime(sf::Vector2u runtime_size) {
        return {{static_cast<float>(runtime_size.x), static_cast<float>(runtime_size.y)}};
    }

    [[nodiscard]] bool valid() const { return size.x > 0.0f && size.y > 0.0f; }
    [[nodiscard]] sf::FloatRect virtual_rect() const { return {0.0f, 0.0f, size.x, size.y}; }
};

} // namespace pac::pnc
