#pragma once

#include <SFML/Graphics/Color.hpp>
#include <SFML/System/Vector2.hpp>

#include <functional>
#include <string>
#include <vector>

namespace sf {
class Font;
class RenderTarget;
} // namespace sf

namespace pac::core {

/// Greedy word-wrap of `text` into lines no wider than `max_width`, using
/// `measure` to size a candidate string. Words longer than `max_width` are left
/// whole (no mid-word splitting); explicit '\n' forces a break. Pure logic, so it
/// is headless-testable with a fake measurer. Always returns at least one line.
std::vector<std::string> wrap_text(const std::string& text,
                                   float max_width,
                                   const std::function<float(const std::string&)>& measure);

/// Horizontal alignment of each line within a text block (and of the block
/// relative to the anchor x: Left pins the anchor to the left edge, Center to the
/// middle, Right to the right edge).
enum class HAlign { Left, Center, Right };

/// Which edge of the block the anchor y refers to: the block's top, vertical
/// center, or bottom.
enum class VAnchor { Top, Center, Bottom };

/// A wrapped, measured multi-line block. `width` is the widest line; `height` is
/// `lines.size() * line_height`. `line_widths[i]` is the measured width of
/// `lines[i]` (for per-line alignment). Pure data — no SFML.
struct TextLayout {
    std::vector<std::string> lines;
    std::vector<float> line_widths;
    float width = 0.0f;
    float height = 0.0f;
};

/// Wrap `text` to `max_width` and report the resulting block bounds. Generalizes
/// `wrap_text` by also measuring each line and the overall block size, given a
/// `line_height` (the caller's font line spacing). Pure; headless-testable.
TextLayout layout_text(const std::string& text,
                       float max_width,
                       float line_height,
                       const std::function<float(const std::string&)>& measure);

/// Fill color, plus an optional outline, for a drawn text block. A zero
/// `outline_thickness` skips the outline entirely.
struct TextStyle {
    unsigned size = 24;
    sf::Color color = sf::Color::White;
    sf::Color outline_color = sf::Color(0, 0, 0, 0);
    float outline_thickness = 0.0f;
};

/// Word-wrap `text` to `max_width` and draw it as a multi-line block positioned
/// relative to `anchor`: `h_align` places the anchor x at the block's left edge /
/// center / right edge (and aligns each line the same way), and `v_anchor` says
/// whether the anchor y is the block's top, center, or bottom. The SFML-facing
/// counterpart to `layout_text`. Lives in `core` (generic 2D text), so any layer
/// above can reuse it.
void draw_text_block(sf::RenderTarget& target,
                     const sf::Font& font,
                     const std::string& text,
                     const TextStyle& style,
                     sf::Vector2f anchor,
                     float max_width,
                     HAlign h_align = HAlign::Center,
                     VAnchor v_anchor = VAnchor::Center);

} // namespace pac::core
