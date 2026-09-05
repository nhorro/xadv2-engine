#include "engine/core/display.hpp"
#include "engine/core/screenshot.hpp"
#include "engine/core/thumbnail.hpp"

#include <doctest/doctest.h>
#include <SFML/Graphics/Image.hpp>
#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/Window/VideoMode.hpp>

#include <filesystem>

using namespace pac::core;

TEST_CASE("full screenshot remains window-sized after thumbnail framebuffer capture") {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "pac_full_screenshot_after_thumbnail.png";
    std::filesystem::remove(path);

    sf::RenderWindow window(sf::VideoMode(320, 180), "screenshot test", sf::Style::None);
    Display display({1280, 720}, {320, 180}, false);
    Thumbnail thumbnail;
    for (int capture = 0; capture < 5; ++capture) {
        window.clear(sf::Color(20, 40, static_cast<sf::Uint8>(60 + capture)));
        thumbnail.capture(window, display.viewport());
        REQUIRE(thumbnail.valid());
        REQUIRE(save_screenshot(window, path));

        sf::Image image;
        REQUIRE(image.loadFromFile(path.string()));
        CHECK(image.getSize() == sf::Vector2u(320, 180));
    }

    window.close();
    std::filesystem::remove(path);
}
