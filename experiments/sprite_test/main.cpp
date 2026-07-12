// sprite_test — manual/visual check of pac::gfx::AnimatedSprite against a real
// spritesheet. Loads an *.anim.yml via the resource cache and animates it.
//
//   pac_sprite_test [data_root] [anim_logical] [--frames N]
//
// Keys: 1 = idle, 2 = walk, 3 = wave_once, Esc = quit.
#include "engine/core/diagnostics.hpp"
#include "engine/core/resource_cache.hpp"
#include "engine/core/resource_source.hpp"
#include "engine/gfx/animated_sprite.hpp"

#include <SFML/Graphics.hpp>

#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    std::string data_root = "examples/01_hello_room/data";
    std::string anim_logical = "characters/hero/hero.anim.yml";
    std::string shot_path;
    int max_frames = 0;

    int positional = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--frames" && i + 1 < argc) {
            max_frames = std::atoi(argv[++i]);
        } else if (arg.rfind("--frames=", 0) == 0) {
            max_frames = std::atoi(arg.c_str() + 9);
        } else if (arg == "--shot" && i + 1 < argc) {
            shot_path = argv[++i];
        } else if (arg.rfind("--shot=", 0) == 0) {
            shot_path = arg.c_str() + 7;
        } else if (positional == 0) {
            data_root = arg;
            ++positional;
        } else {
            anim_logical = arg;
        }
    }

    pac::core::Diagnostics log;
    pac::core::FilesystemResourceSource source(data_root);
    pac::core::ResourceCache cache(source, log);

    try {
        pac::gfx::AnimatedSprite sprite = pac::gfx::load_animated_sprite(cache, anim_logical);
        sprite.play("walk");
        sprite.setPosition(400.0f, 540.0f); // place pivot (feet) here
        sprite.setScale(1.6f, 1.6f);

        sf::RenderWindow window(sf::VideoMode(800, 600), "sprite_test");
        window.setVerticalSyncEnabled(true);

        sf::Clock clock;
        int frames = 0;
        while (window.isOpen()) {
            sf::Event event;
            while (window.pollEvent(event)) {
                if (event.type == sf::Event::Closed) {
                    window.close();
                } else if (event.type == sf::Event::KeyPressed) {
                    switch (event.key.code) {
                    case sf::Keyboard::Num1:
                        sprite.play("idle");
                        break;
                    case sf::Keyboard::Num2:
                        sprite.play("walk");
                        break;
                    case sf::Keyboard::Num3:
                        sprite.play("wave_once");
                        break;
                    case sf::Keyboard::Escape:
                        window.close();
                        break;
                    default:
                        break;
                    }
                }
            }

            sprite.update(clock.restart().asSeconds());

            window.clear(sf::Color(30, 32, 42));
            window.draw(sprite);

            const bool last_frame = (max_frames > 0 && frames + 1 >= max_frames);
            if (last_frame && !shot_path.empty()) {
                sf::Texture shot;
                shot.create(window.getSize().x, window.getSize().y);
                shot.update(window); // capture the rendered frame before display
                if (shot.copyToImage().saveToFile(shot_path)) {
                    log.info("sprite_test: wrote screenshot " + shot_path);
                }
            }
            window.display();

            if (max_frames > 0 && ++frames >= max_frames) {
                break;
            }
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "sprite_test error: " << e.what() << '\n';
        return 1;
    }
}
