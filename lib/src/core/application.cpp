#include "engine/core/application.hpp"

#include "engine/core/audio.hpp"
#include "engine/core/cursor.hpp"
#include "engine/core/diagnostics.hpp"
#include "engine/core/display.hpp"
#include "engine/core/engine_context.hpp"
#include "engine/core/localization.hpp"
#include "engine/core/lua_api.hpp"
#include "engine/core/manifest.hpp"
#include "engine/core/pack_resource_source.hpp"
#include "engine/core/resource_cache.hpp"
#include "engine/core/resource_source.hpp"
#include "engine/core/save_service.hpp"
#include "engine/core/scene.hpp"
#include "engine/core/scene_factory.hpp"
#include "engine/core/scene_manager.hpp"
#include "engine/core/scripting.hpp"
#include "engine/core/settings.hpp"
#include "engine/core/settings_store.hpp"
#include "engine/core/state_store.hpp"
#include "engine/core/strings.hpp"
#include "engine/core/user_data.hpp"

#include <SFML/Graphics/Image.hpp>
#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/Graphics/Texture.hpp>
#include <SFML/System/Clock.hpp>
#include <SFML/Window/Cursor.hpp>
#include <SFML/Window/Event.hpp>

#include <cmath>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <system_error>

namespace pac::core {

namespace {

constexpr float kFixedDt = 1.0f / 60.0f;  // 60 Hz simulation
constexpr int kMaxStepsPerFrame = 5;      // cap to avoid the spiral of death
constexpr float kSceneTransition = 0.35f; // fade-to-black seconds between scenes

// Engine-handled scenes are located by their conventional manifest type string;
// this is a data convention, not a dependency on the genre layer's types.
constexpr char kSettingsSceneType[] = "SettingsScene";

// Canonical archive name searched next to the executable / in the working dir
// (#109). The packed manifest inside is always named `game.yaml`.
constexpr char kPakFileName[] = "resources.pak";
constexpr char kPakManifestLogical[] = "game.yaml";

// Best-effort host path of the running executable's directory. Linux reads it
// from /proc; Windows uses GetModuleFileName (not pulled in here to avoid
// linking shenanigans — Windows currently falls back to argv0's parent or CWD).
// `argv0` is the application-provided hint when present.
std::filesystem::path executable_dir(const std::string& argv0) {
#if defined(__linux__)
    std::error_code ec;
    const auto exe = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (!ec) {
        return exe.parent_path();
    }
#endif
    if (!argv0.empty()) {
        std::error_code ec;
        const auto p = std::filesystem::weakly_canonical(argv0, ec);
        if (!ec && !p.empty()) {
            return p.parent_path();
        }
    }
    return std::filesystem::current_path();
}

/// Locate the shipped pak archive (#109). Order: the `--pak` override, then
/// `resources.pak` next to the running executable, then in the current working
/// directory. Returns an empty path when none of those exist — startup then
/// falls back to the loose-files manifest workflow.
std::filesystem::path discover_pak(const std::string& argv0, const std::string& override_path) {
    if (!override_path.empty()) {
        return std::filesystem::path(override_path);
    }
    std::error_code ec;
    const auto exe_dir = executable_dir(argv0);
    if (!exe_dir.empty()) {
        const auto candidate = exe_dir / kPakFileName;
        if (std::filesystem::is_regular_file(candidate, ec)) {
            return candidate;
        }
    }
    const auto cwd_candidate = std::filesystem::current_path(ec) / kPakFileName;
    if (!ec && std::filesystem::is_regular_file(cwd_candidate, ec)) {
        return cwd_candidate;
    }
    return {};
}

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

constexpr char kWindowTitle[] = "Extraordinary Adventures";

// (Re)create the OS window for `mode`. Fullscreen uses the desktop's native video
// mode (no mode switch) and letterboxes the virtual resolution within it; windowed
// uses the requested client size. Keeping the framebuffer at the desktop size is
// what makes input map correctly in fullscreen — a mode switch leaves SFML's mouse
// coordinates in the old desktop space and the click/avatar mapping breaks (#71).
// The virtual resolution is unchanged either way, so gameplay coordinates are
// stable across the switch (R6).
void apply_window_mode(sf::RenderWindow& window, const DisplayMode& mode) {
    if (mode.fullscreen) {
        window.create(sf::VideoMode::getDesktopMode(), kWindowTitle, sf::Style::Fullscreen);
    } else {
        window.create(sf::VideoMode(mode.size.x, mode.size.y), kWindowTitle, sf::Style::Default);
    }
    window.setVerticalSyncEnabled(true);
}

// Load a hardware cursor from a logical image path. Returns nullptr on any
// failure (no path, missing/undecodable image, or a platform that can't host a
// pixel cursor) so the caller keeps the OS cursor.
std::unique_ptr<sf::Cursor> load_cursor(const ResourceSource& source,
                                        const std::string& logical,
                                        sf::Vector2u hotspot,
                                        Diagnostics& log) {
    if (logical.empty()) {
        return nullptr;
    }
    try {
        const std::vector<std::byte> bytes = source.read_bytes(logical);
        sf::Image image;
        if (!image.loadFromMemory(bytes.data(), bytes.size())) {
            log.warn("cursor: could not decode image '" + logical + "'");
            return nullptr;
        }
        auto cursor = std::make_unique<sf::Cursor>();
        if (!cursor->loadFromPixels(image.getPixelsPtr(), image.getSize(), hotspot)) {
            log.warn("cursor: hardware cursor unsupported for '" + logical + "'");
            return nullptr;
        }
        return cursor;
    } catch (const std::exception& e) {
        log.warn(std::string("cursor: failed to load '") + logical + "': " + e.what());
        return nullptr;
    }
}

} // namespace

int run(const std::string& manifest_path, const SceneFactory& factory, const RunOptions& opts) {
    Diagnostics log;

    // Resource backend (#109): prefer a `resources.pak` archive next to the
    // executable / in CWD; fall back to the loose-files manifest. In pak mode
    // the manifest lives inside the archive at `game.yaml`; the CLI manifest
    // argument is ignored. In filesystem mode the argument names the manifest
    // file on disk (today's behavior).
    const std::filesystem::path pak = discover_pak(opts.argv0, opts.pak_path);

    std::unique_ptr<ResourceSource> source_holder;
    Manifest manifest;
    if (!pak.empty()) {
        try {
            source_holder = std::make_unique<PackResourceSource>(pak);
        } catch (const std::exception& e) {
            log.error(e.what());
            return 1;
        }
        try {
            manifest = parse_manifest(source_holder->read_text(kPakManifestLogical));
        } catch (const std::exception& e) {
            log.error(std::string("manifest in pak: ") + e.what());
            return 1;
        }
        // Inside a pak, every entry is keyed by a logical path; the manifest's
        // `resources.src` no longer points at a host directory. Normalize to
        // an empty value so downstream string handling stays consistent.
        manifest.resources_src.clear();
        log.info("loaded manifest from pak '" + pak.string() + "'");
    } else {
        try {
            manifest = load_manifest(manifest_path);
        } catch (const std::exception& e) {
            log.error(e.what());
            return 1;
        }
        source_holder = std::make_unique<FilesystemResourceSource>(manifest.resources_src);
        log.info("loaded manifest '" + manifest_path +
                 "' (resource root: " + manifest.resources_src + ")");
    }
    ResourceSource& source = *source_holder;

    Settings settings;
    settings.audio.music_volume = manifest.settings.music_volume;
    settings.audio.sfx_volume = manifest.settings.sfx_volume;
    settings.fullscreen = manifest.window.fullscreen;
    settings.window_width = manifest.window.width;
    settings.window_height = manifest.window.height;
    settings.language = manifest.default_language;
    settings.clamp();

    // Player settings override manifest defaults (issue #66): a stored file
    // (per-user config dir) is overlaid on top of the defaults above. A missing
    // file is the normal first-run case; a corrupt one is warned and ignored.
    SettingsStore settings_store(user_config_dir(manifest.id) / "settings.yaml", log);
    settings_store.load(settings);
    settings.clamp();

    ResourceCache resources(source, log);

    // Active UI-strings language (issue #72): the stored preference when it names
    // a known language, else the manifest default. Construction loads the strings
    // (a required resource) and throws if neither the preference nor the default
    // resolves — startup then fails loudly.
    std::optional<Localization> localization_opt;
    try {
        localization_opt.emplace(source,
                                 manifest.languages,
                                 manifest.default_language,
                                 settings.language,
                                 log);
    } catch (const std::exception&) {
        return 1; // load_strings already logged the diagnostic
    }
    Localization& localization = *localization_opt;
    settings.language = localization.active(); // reflect any fallback

    AudioServices audio(resources, log, settings);
    Scripting scripting(log);
    StateStore state;
    Display display(manifest.resolution,
                    {settings.window_width, settings.window_height},
                    settings.fullscreen);
    SceneManager scenes;
    SaveService saves(user_data_dir(manifest.id) / "saves", log);
    CursorState cursor_state;

    EngineContext ctx{display,
                      resources,
                      audio,
                      scripting,
                      state,
                      settings,
                      scenes,
                      localization.strings(),
                      log,
                      manifest.development,
                      manifest.id,
                      saves,
                      cursor_state,
                      localization,
                      settings_store};
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

    sf::RenderWindow window;
    apply_window_mode(window,
                      {{settings.window_width, settings.window_height}, settings.fullscreen});
    display.set_window_size(window.getSize());

    // Custom point-and-click cursor (#73). When the manifest declares one, swap
    // the OS cursor for it; an interact variant (optional) is shown over hotspots
    // via cursor_state. Both keep the OS cursor on any load failure.
    //
    // Skipped in the headless smoke (--frames): setMouseCursor is an X11/Win call,
    // and a minimal/virtual display (no ARGB-cursor support) raises an
    // unrecoverable BadCursor that would abort the smoke. The cursors are still
    // loaded above, so that path is exercised.
    const bool apply_cursor = opts.max_frames == 0;
    const std::unique_ptr<sf::Cursor> cursor_default =
        load_cursor(source, manifest.cursor.image, manifest.cursor.hotspot, log);
    const std::unique_ptr<sf::Cursor> cursor_interact =
        cursor_default ? load_cursor(source, manifest.cursor.interact, manifest.cursor.hotspot, log)
                       : nullptr;
    if (apply_cursor && cursor_default) {
        window.setMouseCursor(*cursor_default);
    }
    CursorKind applied_cursor = CursorKind::DEFAULT;

    // Enable fade-to-black between full-screen scene swaps, and fade the first
    // scene in from black at startup. (Set after the entry scene is already in
    // place so the entry itself is instant.)
    scenes.set_transition_duration(kSceneTransition);
    scenes.start_fade_in();

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

        // A scene (e.g. the settings menu) may have requested a display-mode
        // change; recreate the window before simulating/drawing this frame.
        if (const std::optional<DisplayMode> mode = display.take_pending_mode()) {
            apply_window_mode(window, *mode);
            display.set_window_size(window.getSize());
            display.set_fullscreen(mode->fullscreen);
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

        // Apply the cursor a scene requested this frame (only when an interact
        // variant exists to swap to), then reset so INTERACT must be re-asserted.
        if (apply_cursor && cursor_default && cursor_interact &&
            cursor_state.requested != applied_cursor) {
            window.setMouseCursor(cursor_state.requested == CursorKind::INTERACT ? *cursor_interact
                                                                                 : *cursor_default);
            applied_cursor = cursor_state.requested;
        }
        cursor_state.reset();

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
