#include "engine/pnc/speech_manager.hpp"

#include <SFML/Graphics/Font.hpp>
#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/Graphics/Text.hpp>

namespace pac::pnc {

namespace {
constexpr unsigned kFontSize = 24;
constexpr float kWrapWidth = 360.0f; // world px; a line breaks past this width
} // namespace

std::vector<std::string> wrap_text(const std::string& text,
                                   float max_width,
                                   const std::function<float(const std::string&)>& measure) {
    std::vector<std::string> lines;
    std::string line;
    std::string word;
    const auto flush_word = [&]() {
        if (word.empty()) {
            return;
        }
        if (line.empty()) {
            line = word; // a word wider than max_width still starts a line (no split)
        } else if (measure(line + " " + word) <= max_width) {
            line += " " + word;
        } else {
            lines.push_back(line);
            line = word;
        }
        word.clear();
    };
    for (const char c : text) {
        if (c == '\n') {
            flush_word();
            lines.push_back(line);
            line.clear();
        } else if (c == ' ') {
            flush_word();
        } else {
            word.push_back(c);
        }
    }
    flush_word();
    if (!line.empty() || lines.empty()) {
        lines.push_back(line);
    }
    return lines;
}

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
    const auto measure = [&](const std::string& s) {
        return sf::Text(s, *font, kFontSize).getLocalBounds().width;
    };
    const std::vector<std::string> lines = wrap_text(text_, kWrapWidth, measure);
    const float line_h = font->getLineSpacing(kFontSize);
    const float block_h = line_h * static_cast<float>(lines.size());
    // Stack lines centered horizontally on the speaker and vertically on pos_.
    float y = pos_.y - block_h / 2.0f;
    for (const std::string& ln : lines) {
        sf::Text text(ln, *font, kFontSize);
        text.setFillColor(color_);
        text.setOutlineColor(sf::Color(0, 0, 0, 200));
        text.setOutlineThickness(2.0f);
        const sf::FloatRect b = text.getLocalBounds();
        text.setPosition(pos_.x - b.width / 2.0f - b.left, y - b.top);
        target.draw(text);
        y += line_h;
    }
}

} // namespace pac::pnc
