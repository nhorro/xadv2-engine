#pragma once

#include "engine/geom/geometry.hpp"

#include <SFML/Graphics/Color.hpp>

#include <string>

namespace sf {
class Font;
class RenderTarget;
} // namespace sf

namespace pac::pnc {

/// Shows one spoken/caption line over the scenery near the speaker, for a computed
/// duration, always skippable. (Voice-over hookup is design-for; see R4.)
class SpeechManager {
public:
    void show(const std::string& text, geom::Point pos, sf::Color color, float duration);
    void update(float dt);
    void skip();
    bool active() const { return active_; }

    void draw(sf::RenderTarget& target, const sf::Font* font) const;

private:
    std::string text_;
    geom::Point pos_{0.0f, 0.0f};
    sf::Color color_ = sf::Color::White;
    float remaining_ = 0.0f;
    bool active_ = false;
};

} // namespace pac::pnc
