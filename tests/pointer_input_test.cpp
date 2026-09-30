#include "engine/core/pointer_input.hpp"

#include <SFML/Window/Keyboard.hpp>
#include <SFML/Window/Mouse.hpp>

#include <doctest/doctest.h>

namespace {

template <class Touch>
sf::Event touch(unsigned int finger, int x, int y) {
    return Touch{finger, {x, y}};
}

} // namespace

TEST_CASE("primary touch establishes hover before left click") {
    pac::core::PointerInput input;

    const auto began = input.translate(touch<sf::Event::TouchBegan>(4, 120, 80));
    REQUIRE(began.size() == 2);
    CHECK(began[0].is<sf::Event::MouseMoved>());
    CHECK(began[0].getIf<sf::Event::MouseMoved>()->position.x == 120);
    CHECK(began[0].getIf<sf::Event::MouseMoved>()->position.y == 80);
    CHECK(began[1].is<sf::Event::MouseButtonPressed>());
    CHECK(began[1].getIf<sf::Event::MouseButtonPressed>()->button == sf::Mouse::Button::Left);
    CHECK(began[1].getIf<sf::Event::MouseButtonPressed>()->position.x == 120);
    CHECK(began[1].getIf<sf::Event::MouseButtonPressed>()->position.y == 80);

    const auto ended = input.translate(touch<sf::Event::TouchEnded>(4, 125, 85));
    REQUIRE(ended.size() == 2);
    CHECK(ended[0].is<sf::Event::MouseMoved>());
    CHECK(ended[0].getIf<sf::Event::MouseMoved>()->position.x == 125);
    CHECK(ended[0].getIf<sf::Event::MouseMoved>()->position.y == 85);
    CHECK(ended[1].is<sf::Event::MouseButtonReleased>());
    CHECK(ended[1].getIf<sf::Event::MouseButtonReleased>()->button == sf::Mouse::Button::Left);
    CHECK(ended[1].getIf<sf::Event::MouseButtonReleased>()->position.x == 125);
    CHECK(ended[1].getIf<sf::Event::MouseButtonReleased>()->position.y == 85);
}

TEST_CASE("additional touches are ignored while the primary touch is active") {
    pac::core::PointerInput input;
    CHECK(input.translate(touch<sf::Event::TouchBegan>(7, 10, 20)).size() == 2);
    CHECK(input.translate(touch<sf::Event::TouchBegan>(8, 30, 40)).empty());
    CHECK(input.translate(touch<sf::Event::TouchMoved>(8, 35, 45)).empty());
    CHECK(input.translate(touch<sf::Event::TouchEnded>(8, 35, 45)).empty());

    const auto moved = input.translate(touch<sf::Event::TouchMoved>(7, 15, 25));
    REQUIRE(moved.size() == 1);
    CHECK(moved[0].is<sf::Event::MouseMoved>());
    CHECK(moved[0].getIf<sf::Event::MouseMoved>()->position.x == 15);
    CHECK(moved[0].getIf<sf::Event::MouseMoved>()->position.y == 25);

    CHECK(input.translate(touch<sf::Event::TouchEnded>(7, 15, 25)).size() == 2);
    CHECK(input.translate(touch<sf::Event::TouchBegan>(8, 30, 40)).size() == 2);
}

TEST_CASE("non-touch events pass through unchanged") {
    pac::core::PointerInput input;
    sf::Event event{sf::Event::Closed{}};
    event = sf::Event::KeyPressed{};
    event.getIf<sf::Event::KeyPressed>()->code = sf::Keyboard::Key::Escape;

    const auto translated = input.translate(event);
    REQUIRE(translated.size() == 1);
    CHECK(translated[0].is<sf::Event::KeyPressed>());
    CHECK(translated[0].getIf<sf::Event::KeyPressed>()->code == sf::Keyboard::Key::Escape);
}
