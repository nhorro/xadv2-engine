#pragma once

#include <SFML/Graphics/View.hpp>
#include <SFML/System/Vector2.hpp>
#include <SFML/Window/VideoMode.hpp>

#include <optional>
#include <vector>

namespace pac::core {

/// Letterbox transform from virtual space into a window: uniform `scale`, plus
/// the `offset`/`size` of the scaled image inside the window (bars fill the rest).
struct Viewport {
    float scale = 1.0f;
    sf::Vector2f offset{0.0f, 0.0f};
    sf::Vector2f size{0.0f, 0.0f};
};

/// A requested or applied display mode. `size` is the window client size in
/// windowed mode; in fullscreen the desktop's native mode is used and `size` is
/// informational. The virtual resolution never changes with the mode — gameplay
/// coordinates are unaffected (R6); only the letterbox does.
struct DisplayMode {
    sf::Vector2u size{0, 0};
    bool fullscreen = false;

    bool operator==(const DisplayMode& o) const {
        return size == o.size && fullscreen == o.fullscreen;
    }
    bool operator!=(const DisplayMode& o) const { return !(*this == o); }
};

/// Pure: aspect-preserving fit of `virtual_res` inside `window`.
Viewport letterbox(sf::Vector2u window, sf::Vector2u virtual_res);

/// Pure: map a physical window pixel back into virtual coordinates. The result
/// may fall outside [0, virtual_res] when the pixel is inside a bar.
sf::Vector2f window_to_virtual(sf::Vector2i px, sf::Vector2u window, sf::Vector2u virtual_res);

/// Pure: candidate windowed sizes for the settings UI — integer-ish multiples of
/// the virtual resolution that fit within `desktop` (a zero desktop means "no
/// limit", for headless tests). Always returns at least one entry. No bars result
/// at these sizes because they share the virtual aspect ratio.
std::vector<sf::Vector2u> windowed_size_options(sf::Vector2u desktop, sf::Vector2u virtual_res);

/// Owns the virtual resolution and current window size, and produces the SFML
/// view + input mapping for the `world → virtual → window` pipeline.
class Display {
public:
    Display(sf::Vector2u virtual_res, sf::Vector2u window, bool fullscreen = false);

    void set_window_size(sf::Vector2u window);
    void set_fullscreen(bool fullscreen) { fullscreen_ = fullscreen; }

    sf::Vector2u virtual_resolution() const { return virtual_res_; }
    sf::Vector2u window_size() const { return window_; }
    bool fullscreen() const { return fullscreen_; }
    DisplayMode current_mode() const { return {window_, fullscreen_}; }

    /// Request a window mode change. The change is not applied here (Display does
    /// not own the OS window); the main loop consumes the request with
    /// take_pending_mode() and recreates the window. Lets a scene (e.g. the
    /// settings menu) drive display changes through EngineContext without reaching
    /// into the core harness.
    void request_mode(const DisplayMode& mode) { pending_ = mode; }
    std::optional<DisplayMode> take_pending_mode();

    Viewport viewport() const;
    sf::View view() const;
    sf::Vector2f to_virtual(sf::Vector2i px) const;

    /// Normalized window viewport rect (for sf::View::setViewport) covering a
    /// region given in virtual coordinates — e.g. the scenery sub-region above the
    /// SCUMM panel. Accounts for the letterbox offset and scale.
    sf::FloatRect viewport_for(sf::FloatRect virtual_rect) const;

private:
    sf::Vector2u virtual_res_;
    sf::Vector2u window_;
    bool fullscreen_ = false;
    std::optional<DisplayMode> pending_;
};

} // namespace pac::core
