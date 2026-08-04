#pragma once

#include "engine/gfx/shader_effect.hpp"

#include <SFML/Graphics/Color.hpp>
#include <SFML/System/Vector2.hpp>

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace pac::pnc {

/// A cheap ground shadow drawn under an avatar (a filled ellipse blob). `size` is
/// the full width/height of the ellipse in room pixels at unit scale; it scales
/// with the avatar's perspective scale at render time. See design 05/06 §appearance.
struct Shadow {
    sf::Vector2f size{0.0f, 0.0f};
    sf::Color color = sf::Color(0, 0, 0, 70);
};

/// A reusable avatar visual definition. CompositeSprite is implemented for room
/// objects in gfx/pnc; wiring it as an Avatar strategy remains pending here.
/// `shaders` is an optional declarative stack applied to the animated sprite at
/// draw time (issue #106 — same data model as a layer/region/object shader, see
/// design 03 §Shaders); the per-room avatar instance inherits this list.
struct Appearance {
    std::string type;      // "animated_sprite" | "composite"
    std::string sprite;    // *.anim.yaml (animated_sprite)
    std::string composite; // *.composite.yaml (composite)
    std::optional<Shadow> shadow;
    std::vector<gfx::ShaderEffect> shaders;
};

struct Character {
    std::string id;
    std::string appearance; // appearance id
    std::string name;       // localized display name
    sf::Color speech_color = sf::Color(230, 230, 230);
    // Clearance (room px) left between the speaker and a speech line when it has to
    // be placed beside them (speaker near the top/bottom edge). Wider characters
    // want a larger value so the line clears the sprite. See design 06 §cast.
    float speech_gap = 48.0f;
};

/// Parsed `cast.yaml`: appearances + characters.
struct Cast {
    std::map<std::string, Appearance> appearances;
    std::map<std::string, Character> characters;

    const Appearance* appearance(const std::string& id) const;
    const Character* character(const std::string& id) const;
};

/// Parse + validate cast YAML. Throws DataError on malformed input.
Cast parse_cast(const std::string& yaml_text);

} // namespace pac::pnc
