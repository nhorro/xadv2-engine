#include "engine/core/application.hpp"

#include "engine/core/audio.hpp"
#include "engine/core/diagnostics.hpp"
#include "engine/core/display.hpp"
#include "engine/core/engine_context.hpp"
#include "engine/core/lua_api.hpp"
#include "engine/core/manifest.hpp"
#include "engine/core/resource_cache.hpp"
#include "engine/core/resource_source.hpp"
#include "engine/core/save_service.hpp"
#include "engine/core/scene.hpp"
#include "engine/core/scene_factory.hpp"
#include "engine/core/scene_manager.hpp"
#include "engine/core/scripting.hpp"
#include "engine/core/settings.hpp"
#include "engine/core/state_store.hpp"
#include "engine/core/strings.hpp"
#include "engine/core/user_data.hpp"

#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/Graphics/Texture.hpp>
#include <SFML/System/Clock.hpp>
#include <SFML/Window/Event.hpp>

#include <cmath>
#include <string>

namespace pac::core {

namespace {

constexpr float kFixedDt = 1.0f / 60.0f; // 60 Hz simulation
constexpr int kMaxStepsPerFrame = 5;     // cap to avoid the spiral of death

// Engine-handled scenes are located by their conventional manifest type string;
// this is a data convention, not a dependency on the genre layer's types.
constexpr char kSettingsSceneType[] = "SettingsScene";

int as_int(float value) {
    return static_cast<int>(std::lround(value));
}

// Rewrite pointer coordinates from window pixels into virtual space so scenes
// only ever see virtual coordinates.
sf::Event to_virtual_event(const sf::Event& in, const Display& display) {
    sf::Event ev = in;
    switch (in.type) {
    case sf::Event::MouseMoved: {
        const sf::Vector2f v = display.to_virtual({in.mouseMove.x, in.mouseMove.y});
        ev.mouseMove.x = as_int(v.x);
        ev.mouseMove.y = as_int(v.y);
        break;
    }
    case sf::Event::MouseButtonPressed:
    case sf::Event::MouseButtonReleased: {
        const sf::Vector2f v = display.to_virtual({in.mouseButton.x, in.mouseButton.y});
        ev.mouseButton.x = as_int(v.x);
        ev.mouseButton.y = as_int(v.y);
        break;
    }
    default:
        break;
    }
    return ev;
}

} // namespace

int run(const std::string& manifest_path, const SceneFactory& factory, const RunOptions& opts) {
    Diagnostics log;

    Manifest manifest;
    try {
        manifest = load_manifest(manifest_path);
    } catch (const std::exception& e) {
        log.error(e.what());
        return 1;
    }
    log.info("loaded manifest '" + manifest_path + "' (resource root: " + manifest.resources_src +
             ")");

    Settings settings;
    settings.audio.music_volume = manifest.settings.music_volume;
    settings.audio.sfx_volume = manifest.settings.sfx_volume;
    settings.fullscreen = manifest.window.fullscreen;
    settings.clamp();

    FilesystemResourceSource source(manifest.resources_src);
    ResourceCache resources(source, log);
    Strings strings = load_strings(source, manifest.strings_path, log);
    AudioServices audio(resources, log, settings);
    Scripting scripting(log);
    StateStore state;
    Display display(manifest.resolution, {manifest.window.width, manifest.window.height});
    SceneManager scenes;
    SaveService saves(user_data_dir(manifest.id) / "saves", log);

    EngineContext ctx{display,
                      resources,
                      audio,
                      scripting,
                      state,
                      settings,
                      scenes,
                      strings,
                      log,
                      manifest.development,
                      manifest.id,
                      saves};
    bind_core_api(ctx);

    scenes.set_builder([&](const std::string& id) -> std::unique_ptr<Scene> {
        const SceneDesc* desc = manifest.find_scene(id);
        if (!desc) {
            log.error("scene id not found in manifest: '" + id + "'");
            return nullptr;
        }
        std::unique_ptr<Scene> scene = factory.create(desc->type, ctx, desc->parameters);
        if (!scene) {
            log.error("unknown scene type '" + desc->type + "' for scene '" + id + "'");
        }
        return scene;
    });

    for (const SceneDesc& desc : manifest.scenes) {
        if (desc.type == kSettingsSceneType) {
            scenes.set_settings_scene_id(desc.id);
            break;
        }
    }

    scenes.goto_scene(manifest.entry);
    scenes.apply_pending();
    if (!scenes.running() || scenes.top() == nullptr) {
        log.error("failed to enter entry scene '" + manifest.entry + "'");
        return 1;
    }

    const sf::Uint32 style =
        manifest.window.fullscreen ? sf::Style::Fullscreen : sf::Style::Default;
    sf::RenderWindow window(sf::VideoMode(manifest.window.width, manifest.window.height),
                            "Extraordinary Adventures",
                            style);
    window.setVerticalSyncEnabled(true);
    display.set_window_size(window.getSize());

    sf::Clock clock;
    float accumulator = 0.0f;
    int frames = 0;

    while (window.isOpen() && scenes.running()) {
        sf::Event event;
        while (window.pollEvent(event)) {
            if (event.type == sf::Event::Closed) {
                scenes.quit();
            } else if (event.type == sf::Event::Resized) {
                display.set_window_size({event.size.width, event.size.height});
            } else {
                scenes.handle_event(to_virtual_event(event, display));
            }
        }
        scenes.apply_pending();
        if (!scenes.running()) {
            break;
        }

        accumulator += clock.restart().asSeconds();
        int steps = 0;
        while (accumulator >= kFixedDt && steps < kMaxStepsPerFrame) {
            scripting.update(kFixedDt);
            scenes.update(kFixedDt);
            scenes.apply_pending();
            accumulator -= kFixedDt;
            ++steps;
            if (!scenes.running()) {
                break;
            }
        }
        if (accumulator > kFixedDt * kMaxStepsPerFrame) {
            accumulator = 0.0f; // drop backlog rather than spiral
        }
        if (!scenes.running()) {
            break;
        }

        window.clear(sf::Color::Black); // letterbox bars
        window.setView(display.view());
        scenes.draw(window);

        const bool last_frame = (opts.max_frames > 0 && frames + 1 >= opts.max_frames);
        if (last_frame && !opts.screenshot_path.empty()) {
            sf::Texture shot;
            shot.create(window.getSize().x, window.getSize().y);
            shot.update(window); // capture before display swaps buffers
            if (shot.copyToImage().saveToFile(opts.screenshot_path)) {
                log.info("wrote screenshot " + opts.screenshot_path);
            }
        }
        window.display();

        if (opts.max_frames > 0 && ++frames >= opts.max_frames) {
            log.info("smoke run reached max_frames=" + std::to_string(opts.max_frames) +
                     ", exiting");
            break;
        }
    }

    return 0;
}

} // namespace pac::core
