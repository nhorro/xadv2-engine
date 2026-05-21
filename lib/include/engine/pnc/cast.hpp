#pragma once

#include <SFML/Graphics/Color.hpp>

#include <map>
#include <string>

namespace pac::pnc {

/// A reusable visual definition. M3 supports `animated_sprite`; `composite` is
/// parsed but realized later. `shadow` is omitted for the M3 slice.
struct Appearance {
    std::string type;      // "animated_sprite" | "composite"
    std::string sprite;    // *.anim.yaml (animated_sprite)
    std::string composite; // *.composite.yaml (composite)
};

struct Character {
    std::string id;
    std::string appearance; // appearance id
    std::string name;       // localized display name
    sf::Color speech_color = sf::Color(230, 230, 230);
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
