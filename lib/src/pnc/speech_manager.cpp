#include "engine/pnc/speech_manager.hpp"

#include "engine/core/text_encoding.hpp"

#include <SFML/Graphics/Font.hpp>
#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/Graphics/Text.hpp>
#include <SFML/Graphics/View.hpp>

#include <algorithm>

namespace pac::pnc {

namespace {
constexpr unsigned kFontSize = 24;
constexpr float kWrapWidth = 360.0f;   // world px; a line breaks past this width
constexpr float kScreenMargin = 12.0f; // keep the text this far from the edges
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

geom::Point
contain_block(geom::Point anchor, sf::Vector2f size, const sf::FloatRect& bounds, float margin) {
    const float lo_x = bounds.left + margin;
    const float hi_x = bounds.left + bounds.width - margin - size.x;
    const float lo_y = bounds.top + margin;
    const float hi_y = bounds.top + bounds.height - margin - size.y;
    float x = anchor.x - (size.x / 2.0f);
    float y = anchor.y - (size.y / 2.0f);
    // When the block is wider/taller than the available span (hi < lo), pin to the
    // low edge so the start of the text stays on screen rather than centering off it.
    x = (hi_x < lo_x) ? lo_x : std::clamp(x, lo_x, hi_x);
    y = (hi_y < lo_y) ? lo_y : std::clamp(y, lo_y, hi_y);
    return {x, y};
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
        return sf::Text(pac::core::utf8(s), *font, kFontSize).getLocalBounds().width;
    };
    const std::vector<std::string> lines = wrap_text(text_, kWrapWidth, measure);
    const float line_h = font->getLineSpacing(kFontSize);
    const float block_h = line_h * static_cast<float>(lines.size());

    // The block is as wide as its widest line; remember each line's width to
    // center it within that block.
    std::vector<float> widths;
    widths.reserve(lines.size());
    float block_w = 0.0f;
    for (const std::string& ln : lines) {
        const float w = measure(ln);
        widths.push_back(w);
        block_w = std::max(block_w, w);
    }

    // Clamp the block to the visible world rect (the active scenery view) with a
    // margin, so a speaker near an edge keeps its text on screen while staying
    // centered on the speaker where there is room.
    const sf::View& view = target.getView();
    const sf::Vector2f vc = view.getCenter();
    const sf::Vector2f vs = view.getSize();
    const sf::FloatRect bounds(vc.x - (vs.x / 2.0f), vc.y - (vs.y / 2.0f), vs.x, vs.y);
    const geom::Point top_left = contain_block(pos_, {block_w, block_h}, bounds, kScreenMargin);

    float y = top_left.y;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        sf::Text text(pac::core::utf8(lines[i]), *font, kFontSize);
        text.setFillColor(color_);
        text.setOutlineColor(sf::Color(0, 0, 0, 200));
        text.setOutlineThickness(2.0f);
        const sf::FloatRect b = text.getLocalBounds();
        const float line_x = top_left.x + ((block_w - widths[i]) / 2.0f);
        text.setPosition(line_x - b.left, y - b.top);
        target.draw(text);
        y += line_h;
    }
}

} // namespace pac::pnc
