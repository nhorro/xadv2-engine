#pragma once

#include "engine/geom/geometry.hpp"

#include <SFML/Graphics/Color.hpp>

#include <functional>
#include <string>
#include <vector>

namespace sf {
class Font;
class RenderTarget;
} // namespace sf

namespace pac::pnc {

/// Greedy word-wrap of `text` into lines no wider than `max_width`, using
/// `measure` to size a candidate string. Words longer than `max_width` are left
/// whole (no mid-word splitting); explicit '\n' forces a break. Pure logic, so it
/// is headless-testable with a fake measurer. Always returns at least one line.
std::vector<std::string> wrap_text(const std::string& text,
                                   float max_width,
                                   const std::function<float(const std::string&)>& measure);

/// Top-left for a `size`-sized text block centered on `anchor`, clamped to stay
/// inside `bounds` shrunk by `margin` on every side ("balloon without the
/// balloon"). If the block is larger than the available span on an axis it is
/// pinned to the low (left/top) edge so the start of the text stays visible.
/// Pure geometry; headless-testable.
geom::Point
contain_block(geom::Point anchor, sf::Vector2f size, const sf::FloatRect& bounds, float margin);

/// Comic-balloon placement of a speech line, given the speaker's head `anchor`
/// (the top-centre of the speaker), within `bounds` shrunk by `margin`. The block
/// floats *above* the head: centered horizontally on the anchor with its bottom
/// edge `tail` above it, so it never overlaps the speaker. A speaker near the
/// left/right edge is just nudged horizontally to stay on screen. When there is no
/// room above (speaker near the top edge) the block moves beside the head instead —
/// to the right by `side_gap`, or to the left when the right side would run past the
/// edge — vertically centered on the head. Falls back to a clamped above-block when
/// neither side fits (very wide block / narrow view). Pure geometry; headless-testable.
geom::Point place_speech(geom::Point anchor,
                         sf::Vector2f size,
                         const sf::FloatRect& bounds,
                         float margin,
                         float side_gap,
                         float tail);

/// Shows one spoken/caption line over the scenery near the speaker, for a computed
/// duration, always skippable. (Voice-over hookup is design-for; see R4.)
class SpeechManager {
public:
    void
    show(const std::string& text, geom::Point pos, sf::Color color, float duration, float side_gap);
    void update(float dt);
    void skip();
    bool active() const { return active_; }

    void draw(sf::RenderTarget& target, const sf::Font* font) const;

private:
    std::string text_;
    geom::Point pos_{0.0f, 0.0f};
    sf::Color color_ = sf::Color::White;
    float remaining_ = 0.0f;
    float side_gap_ = 48.0f; // clearance when a line is placed beside the speaker
    bool active_ = false;
};

} // namespace pac::pnc
