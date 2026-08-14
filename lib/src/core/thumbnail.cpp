#include "engine/core/thumbnail.hpp"

#include "engine/core/display.hpp"

#include <SFML/Graphics/Rect.hpp>
#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/Graphics/Sprite.hpp>
#include <SFML/Graphics/Texture.hpp>
#include <SFML/Graphics/View.hpp>

namespace pac::core {

Thumbnail::Thumbnail() = default;
Thumbnail::~Thumbnail() = default;

bool Thumbnail::ensure_rt() {
    if (rt_ready_) {
        return true;
    }
    rt_ = std::make_unique<sf::RenderTexture>();
    if (!rt_->create(kWidth, kHeight)) {
        rt_.reset();
        return false;
    }
    rt_->setSmooth(true);
    rt_ready_ = true;
    return true;
}

void Thumbnail::capture(const sf::RenderWindow& window, const Viewport& vp) {
#if defined(__ANDROID__)
    // SFML 2.6's OpenGL ES 1 backend reads textures through optional
    // framebuffer extension entry points. They are unavailable on some Android
    // devices/emulators, where copyToImage() dereferences a null function
    // pointer. Save thumbnails are auxiliary, so leave them empty until the
    // Android renderer has a supported framebuffer-readback implementation.
    (void) window;
    (void) vp;
    return;
#else
    const sf::Vector2u wsize = window.getSize();
    if (wsize.x == 0 || wsize.y == 0 || vp.size.x <= 0.0f || vp.size.y <= 0.0f) {
        return;
    }
    if (!ensure_rt()) {
        return;
    }

    // Read the whole window into a texture, then blit the gameplay sub-rect
    // (`vp.offset`/`vp.size` in window pixels) onto the small RT scaled to fit.
    // SFML's sf::Texture::update(window) copies the framebuffer to a GPU texture
    // in one call; it's the same path the `--shot` smoke uses.
    sf::Texture full;
    if (!full.create(wsize.x, wsize.y)) {
        return;
    }
    full.update(window);

    sf::Sprite sprite(full);
    const sf::IntRect src(static_cast<int>(vp.offset.x),
                          static_cast<int>(vp.offset.y),
                          static_cast<int>(vp.size.x),
                          static_cast<int>(vp.size.y));
    sprite.setTextureRect(src);

    // Scale the sprite so the (src.width, src.height) subset fills the small RT.
    const float sx = static_cast<float>(kWidth) / static_cast<float>(src.width);
    const float sy = static_cast<float>(kHeight) / static_cast<float>(src.height);
    sprite.setScale(sx, sy);

    rt_->clear(sf::Color::Black);
    rt_->setView(rt_->getDefaultView());
    rt_->draw(sprite);
    rt_->display();

    image_ = rt_->getTexture().copyToImage();
#endif
}

void Thumbnail::invalidate() {
    image_ = sf::Image();
}

} // namespace pac::core
