#pragma once

#include <SFML/System/String.hpp>

#include <string>

namespace pac::core {

/// Build an `sf::String` from a UTF-8 `std::string`.
///
/// The implicit `std::string` -> `sf::String` conversion SFML offers decodes the
/// bytes through the global C++ locale (ANSI), which mangles multibyte UTF-8 —
/// accents, `ñ`, `¿`/`¡` come out as mojibake or `.notdef` boxes. All game and UI
/// text is authored UTF-8 (manifest `strings`, cast/room `name`s, speech, dialog),
/// so route every such string through here before constructing an `sf::Text`.
inline sf::String utf8(const std::string& s) {
    return sf::String::fromUtf8(s.begin(), s.end());
}

} // namespace pac::core
