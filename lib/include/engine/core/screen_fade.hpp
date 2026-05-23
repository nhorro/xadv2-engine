#pragma once

#include <SFML/Config.hpp>

namespace pac::core {

/// Headless-safe fade-to-black controller: pure alpha-over-time logic with no
/// render target. A scene or the main loop advances it with `update(dt)` and
/// reads `alpha255()` to draw a black overlay. `0` = clear, `1` = fully black.
///
/// Kept free of SFML graphics on purpose (only `sf::Uint8` for the convenience
/// accessor), so transition timing is unit-testable without a window.
class ScreenFade {
public:
    void fade_out(float duration_s); // animate toward black
    void fade_in(float duration_s);  // animate toward clear
    void update(float dt);
    void skip(); // jump straight to the current target (skippable transitions)

    float alpha() const { return alpha_; } // 0..1
    sf::Uint8 alpha255() const;            // 0..255, for the overlay color
    bool opaque() const;                   // fully black (safe point to swap)
    bool clear() const;                    // fully transparent
    bool active() const;                   // still animating toward the target

private:
    void start(float target, float duration_s);

    float alpha_ = 0.0f;  // current opacity
    float target_ = 0.0f; // 0 or 1
    float speed_ = 0.0f;  // alpha units per second; 0 when idle
};

} // namespace pac::core
