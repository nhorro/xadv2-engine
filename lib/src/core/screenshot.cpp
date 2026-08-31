#include "engine/core/screenshot.hpp"

#include <SFML/Graphics/Image.hpp>
#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/Graphics/Texture.hpp>

namespace pac::core {

bool save_screenshot(const sf::RenderWindow& window, const std::filesystem::path& path) {
    const sf::Vector2u size = window.getSize();
    if (size.x == 0 || size.y == 0) {
        return false;
    }

    sf::Texture framebuffer;
    if (!framebuffer.create(size.x, size.y)) {
        return false;
    }
    framebuffer.update(window);
    return framebuffer.copyToImage().saveToFile(path.string());
}

} // namespace pac::core
