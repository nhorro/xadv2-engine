#include "engine/core/pointer_input.hpp"

#include <SFML/Window/Keyboard.hpp>
#include <SFML/Window/Mouse.hpp>

#include <doctest/doctest.h>

namespace {

sf::Event touch(sf::Event::EventType type, unsigned int finger, int x, int y) {
    sf::Event event{};
    event.type = type;
    event.touch = {finger, x, y};
    return event;
}

} // namespace

TEST_CASE("primary touch establishes hover before left click") {
    pac::core::PointerInput input;

    const auto began = input.translate(touch(sf::Event::TouchBegan, 4, 120, 80));
    REQUIRE(began.size() == 2);
    CHECK(began[0].type == sf::Event::MouseMoved);
    CHECK(began[0].mouseMove.x == 120);
    CHECK(began[0].mouseMove.y == 80);
    CHECK(began[1].type == sf::Event::MouseButtonPressed);
    CHECK(began[1].mouseButton.button == sf::Mouse::Left);
    CHECK(began[1].mouseButton.x == 120);
    CHECK(began[1].mouseButton.y == 80);

    const auto ended = input.translate(touch(sf::Event::TouchEnded, 4, 125, 85));
    REQUIRE(ended.size() == 2);
    CHECK(ended[0].type == sf::Event::MouseMoved);
    CHECK(ended[0].mouseMove.x == 125);
    CHECK(ended[0].mouseMove.y == 85);
    CHECK(ended[1].type == sf::Event::MouseButtonReleased);
    CHECK(ended[1].mouseButton.button == sf::Mouse::Left);
    CHECK(ended[1].mouseButton.x == 125);
    CHECK(ended[1].mouseButton.y == 85);
}

TEST_CASE("additional touches are ignored while the primary touch is active") {
    pac::core::PointerInput input;
    CHECK(input.translate(touch(sf::Event::TouchBegan, 7, 10, 20)).size() == 2);
    CHECK(input.translate(touch(sf::Event::TouchBegan, 8, 30, 40)).empty());
    CHECK(input.translate(touch(sf::Event::TouchMoved, 8, 35, 45)).empty());
    CHECK(input.translate(touch(sf::Event::TouchEnded, 8, 35, 45)).empty());

    const auto moved = input.translate(touch(sf::Event::TouchMoved, 7, 15, 25));
    REQUIRE(moved.size() == 1);
    CHECK(moved[0].type == sf::Event::MouseMoved);
    CHECK(moved[0].mouseMove.x == 15);
    CHECK(moved[0].mouseMove.y == 25);

    CHECK(input.translate(touch(sf::Event::TouchEnded, 7, 15, 25)).size() == 2);
    CHECK(input.translate(touch(sf::Event::TouchBegan, 8, 30, 40)).size() == 2);
}

TEST_CASE("non-touch events pass through unchanged") {
    pac::core::PointerInput input;
    sf::Event event{};
    event.type = sf::Event::KeyPressed;
    event.key.code = sf::Keyboard::Escape;

    const auto translated = input.translate(event);
    REQUIRE(translated.size() == 1);
    CHECK(translated[0].type == sf::Event::KeyPressed);
    CHECK(translated[0].key.code == sf::Keyboard::Escape);
}
