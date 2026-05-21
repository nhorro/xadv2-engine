#pragma once

#include <SFML/Graphics/View.hpp>
#include <SFML/System/Vector2.hpp>

namespace pac::core {

/// Letterbox transform from virtual space into a window: uniform `scale`, plus
/// the `offset`/`size` of the scaled image inside the window (bars fill the rest).
struct Viewport {
    float scale = 1.0f;
    sf::Vector2f offset{0.0f, 0.0f};
    sf::Vector2f size{0.0f, 0.0f};
};

/// Pure: aspect-preserving fit of `virtual_res` inside `window`.
Viewport letterbox(sf::Vector2u window, sf::Vector2u virtual_res);

/// Pure: map a physical window pixel back into virtual coordinates. The result
/// may fall outside [0, virtual_res] when the pixel is inside a bar.
sf::Vector2f window_to_virtual(sf::Vector2i px, sf::Vector2u window, sf::Vector2u virtual_res);

/// Owns the virtual resolution and current window size, and produces the SFML
/// view + input mapping for the `world → virtual → window` pipeline.
class Display {
public:
    Display(sf::Vector2u virtual_res, sf::Vector2u window);

    void set_window_size(sf::Vector2u window);

    sf::Vector2u virtual_resolution() const { return virtual_res_; }
    sf::Vector2u window_size() const { return window_; }

    Viewport viewport() const;
    sf::View view() const;
    sf::Vector2f to_virtual(sf::Vector2i px) const;

private:
    sf::Vector2u virtual_res_;
    sf::Vector2u window_;
};

} // namespace pac::core
