#include "engine/core/text_layout.hpp"

#include "engine/core/text_encoding.hpp"

#include <SFML/Graphics/Font.hpp>
#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/Graphics/Text.hpp>

#include <algorithm>

namespace pac::core {

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

TextLayout layout_text(const std::string& text,
                       float max_width,
                       float line_height,
                       const std::function<float(const std::string&)>& measure) {
    TextLayout out;
    out.lines = wrap_text(text, max_width, measure);
    out.line_widths.reserve(out.lines.size());
    for (const std::string& line : out.lines) {
        const float w = measure(line);
        out.line_widths.push_back(w);
        out.width = std::max(out.width, w);
    }
    out.height = line_height * static_cast<float>(out.lines.size());
    return out;
}

void draw_text_block(sf::RenderTarget& target,
                     const sf::Font& font,
                     const std::string& text,
                     const TextStyle& style,
                     sf::Vector2f anchor,
                     float max_width,
                     HAlign h_align,
                     VAnchor v_anchor) {
    const auto measure = [&](const std::string& s) {
        return sf::Text(utf8(s), font, style.size).getLocalBounds().width;
    };
    const float line_h = font.getLineSpacing(style.size);
    const TextLayout lay = layout_text(text, max_width, line_h, measure);

    // The anchor y picks which edge of the block it pins; from there each line's
    // vertical CENTER walks down the block on a line_h grid.
    float block_top = anchor.y;
    if (v_anchor == VAnchor::Center) {
        block_top = anchor.y - lay.height / 2.0f;
    } else if (v_anchor == VAnchor::Bottom) {
        block_top = anchor.y - lay.height;
    }
    float center_y = block_top + line_h / 2.0f;

    for (const std::string& line : lay.lines) {
        sf::Text t(utf8(line), font, style.size);
        t.setFillColor(style.color);
        if (style.outline_thickness > 0.0f) {
            t.setOutlineColor(style.outline_color);
            t.setOutlineThickness(style.outline_thickness);
        }
        // Set the origin to the alignment point so positioning at the anchor x
        // (and the line's center y) lands the line where h_align/v_anchor ask,
        // independent of the glyph bearing carried in getLocalBounds().
        const sf::FloatRect b = t.getLocalBounds();
        float origin_x = b.left;
        if (h_align == HAlign::Center) {
            origin_x = b.left + b.width / 2.0f;
        } else if (h_align == HAlign::Right) {
            origin_x = b.left + b.width;
        }
        t.setOrigin(origin_x, b.top + b.height / 2.0f);
        t.setPosition(anchor.x, center_y);
        target.draw(t);
        center_y += line_h;
    }
}

} // namespace pac::core
