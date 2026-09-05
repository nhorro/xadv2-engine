#include "engine/core/screenshot.hpp"

#include <SFML/Graphics/Image.hpp>
#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/Graphics/Texture.hpp>

namespace pac::core {

bool save_screenshot(sf::RenderWindow& window, const std::filesystem::path& path) {
    const sf::Vector2u size = window.getSize();
    if (size.x == 0 || size.y == 0) {
        return false;
    }

    // Thumbnail generation uses a RenderTexture and therefore changes the active
    // OpenGL framebuffer/context. Explicitly reactivate the window before every
    // full-size readback; otherwise a screenshot taken on a thumbnail-refresh
    // frame can copy the 256x144 off-screen target instead.
    if (!window.setActive(true)) {
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
