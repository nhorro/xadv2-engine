#pragma once

#include <SFML/Graphics/Image.hpp>
#include <SFML/Graphics/RenderTexture.hpp>
#include <SFML/System/Vector2.hpp>

#include <cstdint>
#include <memory>

namespace sf {
class RenderWindow;
}

namespace pac::core {

struct Viewport;

/// A small captured framebuffer image (issue #119). The application loop calls
/// `capture()` on the frames that are good thumbnail candidates (driven by
/// `Scene::wants_thumbnail()`); the save UI pulls the latest one and writes it
/// next to the save file. The image's address is stable for the service's
/// lifetime, but its contents are overwritten by each successful capture, so
/// callers that need to retain the bytes (the in-flight save) must copy.
///
/// Cropping uses the Display's letterbox so the thumbnail shows the gameplay
/// area only, never the black bars. The downsample target is fixed
/// (`kWidth`/`kHeight`) so saved thumbnails are small and uniform.
class Thumbnail {
public:
    static constexpr unsigned kWidth = 256;
    static constexpr unsigned kHeight = 144; // 16:9 to match the canonical virtual aspect

    Thumbnail();
    ~Thumbnail();

    Thumbnail(const Thumbnail&) = delete;
    Thumbnail& operator=(const Thumbnail&) = delete;

    /// Capture `window`'s framebuffer into the cached image, cropped to the
    /// gameplay rect (`vp.offset` + `vp.size`, in window pixels) and downscaled
    /// to `kWidth x kHeight`. Silently no-ops on a zero-sized window or
    /// viewport. Must be called between `scenes.draw` and `window.display`.
    void capture(sf::RenderWindow& window, const Viewport& vp);

    /// Mark the currently cached image as stale (e.g. after the player leaves
    /// COMMAND state for a long while). Calling `image()` then returns an
    /// empty image and `valid()` is false. Doesn't free the RT — the next
    /// `capture()` reuses it.
    void invalidate();

    /// Latest captured image, or an empty image if nothing's been captured yet.
    [[nodiscard]] const sf::Image& image() const { return image_; }

    [[nodiscard]] bool valid() const { return image_.getSize().x > 0; }

private:
    /// Lazily create the off-screen RT. Returns false on a GPU that can't host
    /// one; capture() then no-ops.
    bool ensure_rt();

    std::unique_ptr<sf::RenderTexture> rt_;
    bool rt_ready_ = false;
    sf::Image image_;
};

} // namespace pac::core
