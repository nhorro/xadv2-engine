#include "engine/core/text_encoding.hpp"

#include <doctest/doctest.h>

using namespace pac::core;

TEST_CASE("utf8 decodes multibyte UTF-8 into single code points") {
    // "ñ" is two UTF-8 bytes (0xC3 0xB1) but one code point U+00F1. The implicit
    // std::string -> sf::String conversion would (mis)read it as two ANSI chars,
    // which is exactly the mojibake bug this guards against.
    const sf::String enye = utf8("ñ");
    REQUIRE(enye.getSize() == 1);
    CHECK(enye[0] == 0x00F1);

    const sf::String word = utf8("jarrón"); // j a r r ó n -> 6 code points
    CHECK(word.getSize() == 6);

    const sf::String punct = utf8("¿¡"); // U+00BF, U+00A1
    REQUIRE(punct.getSize() == 2);
    CHECK(punct[0] == 0x00BF);
    CHECK(punct[1] == 0x00A1);
}

TEST_CASE("utf8 leaves ASCII unchanged") {
    const sf::String s = utf8("Look at");
    CHECK(s.getSize() == 7);
    CHECK(s == sf::String("Look at"));
}
