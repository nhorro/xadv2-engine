#include "engine/pnc/speech_manager.hpp"

#include <SFML/Graphics/Font.hpp>
#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/Graphics/Text.hpp>

namespace pac::pnc {

void SpeechManager::show(const std::string& text,
                         geom::Point pos,
                         sf::Color color,
                         float duration) {
    text_ = text;
    pos_ = pos;
    color_ = color;
    remaining_ = duration;
    active_ = true;
}

void SpeechManager::update(float dt) {
    if (!active_) {
        return;
    }
    remaining_ -= dt;
    if (remaining_ <= 0.0f) {
        active_ = false;
    }
}

void SpeechManager::skip() {
    active_ = false;
}

void SpeechManager::draw(sf::RenderTarget& target, const sf::Font* font) const {
    if (!active_ || !font) {
        return;
    }
    sf::Text text(text_, *font, 24);
    text.setFillColor(color_);
    text.setOutlineColor(sf::Color(0, 0, 0, 200));
    text.setOutlineThickness(2.0f);
    const sf::FloatRect b = text.getLocalBounds();
    text.setPosition(pos_.x - b.width / 2.0f - b.left, pos_.y - b.height / 2.0f - b.top);
    target.draw(text);
}

} // namespace pac::pnc
