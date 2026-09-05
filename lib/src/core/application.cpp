#include "engine/core/application.hpp"

#include "engine/core/audio.hpp"
#include "engine/core/cursor.hpp"
#include "engine/core/diagnostics.hpp"
#include "engine/core/display.hpp"
#include "engine/core/engine_context.hpp"
#include "engine/core/gameplay_recorder.hpp"
#include "engine/core/localization.hpp"
#include "engine/core/lua_api.hpp"
#include "engine/core/manifest.hpp"
#include "engine/core/pack_resource_source.hpp"
#include "engine/core/pointer_input.hpp"
#include "engine/core/profiler.hpp"
#include "engine/core/render_stats.hpp"
#include "engine/core/resource_cache.hpp"
#include "engine/core/resource_source.hpp"
#include "engine/core/save_service.hpp"
#include "engine/core/scene.hpp"
#include "engine/core/scene_factory.hpp"
#include "engine/core/scene_manager.hpp"
#include "engine/core/screenshot.hpp"
#include "engine/core/scripting.hpp"
#include "engine/core/settings.hpp"
#include "engine/core/settings_store.hpp"
#include "engine/core/state_store.hpp"
#include "engine/core/strings.hpp"
#include "engine/core/system_language.hpp"
#include "engine/core/text_encoding.hpp"
#include "engine/core/thumbnail.hpp"
#include "engine/core/user_data.hpp"
#include "gfx/gles2_compat.hpp"

#include <SFML/Graphics/Image.hpp>
#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/Graphics/Text.hpp>
#include <SFML/System/Clock.hpp>
#include <SFML/System/Sleep.hpp>
#include <SFML/Window/Cursor.hpp>
#include <SFML/Window/Event.hpp>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>

namespace pac::core {

namespace {

constexpr float kFixedDt = 1.0f / 60.0f;  // 60 Hz simulation
constexpr int kMaxStepsPerFrame = 5;      // cap to avoid the spiral of death
constexpr float kSceneTransition = 0.35f; // fade-to-black seconds between scenes

void draw_generic_pause(sf::RenderTarget& target,
                        sf::Vector2u resolution,
                        const Strings& strings,
                        const sf::Font* font) {
    const float width = static_cast<float>(resolution.x);
    const float height = static_cast<float>(resolution.y);
    sf::RectangleShape dim({width, height});
    dim.setFillColor(sf::Color(0, 0, 0, 180));
    target.draw(dim);
    if (!font) {
        return;
    }

    sf::Text title(utf8(strings.ui_label("pause")), *font, 36);
    title.setFillColor(sf::Color(255, 240, 180));
    const sf::FloatRect title_bounds = title.getLocalBounds();
    title.setPosition((width - title_bounds.width) / 2.0f - title_bounds.left,
                      height * 0.34f - title_bounds.top);
    target.draw(title);

    const sf::Vector2f button_size{360.0f, 56.0f};
    const sf::Vector2f button_pos{(width - button_size.x) / 2.0f, height * 0.52f};
    sf::RectangleShape button(button_size);
    button.setPosition(button_pos);
    button.setFillColor(sf::Color(34, 38, 54));
    button.setOutlineColor(sf::Color(90, 100, 130));
    button.setOutlineThickness(1.5f);
    target.draw(button);

    sf::Text resume(utf8(strings.ui_label("resume")), *font, 20);
    resume.setFillColor(sf::Color(220, 224, 235));
    const sf::FloatRect resume_bounds = resume.getLocalBounds();
    resume.setPosition(
        button_pos.x + (button_size.x - resume_bounds.width) / 2.0f - resume_bounds.left,
        button_pos.y + (button_size.y - resume_bounds.height) / 2.0f - resume_bounds.top);
    target.draw(resume);
}

bool resumes_generic_pause(const sf::Event& event) {
    if (event.type == sf::Event::KeyPressed) {
        return event.key.code == sf::Keyboard::Space || event.key.code == sf::Keyboard::Escape ||
               event.key.code == sf::Keyboard::Enter;
    }
    return event.type == sf::Event::MouseButtonReleased &&
           event.mouseButton.button == sf::Mouse::Left;
}

// Engine-handled scenes are located by their conventional manifest type string;
// this is a data convention, not a dependency on the genre layer's types.
constexpr char kSettingsSceneType[] = "SettingsScene";
constexpr char kSaveLoadSceneType[] = "SaveLoadScene";
constexpr char kConfirmationSceneType[] = "ConfirmationScene";

// Canonical archive name searched next to the executable / in the working dir
// (#109). The packed manifest inside is always named `game.yaml`.
constexpr char kPakFileName[] = "resources.pak";
constexpr char kPakManifestLogical[] = "game.yaml";

#ifndef NDEBUG
/// Pick a collision-free, human-readable path in the working tree. Development
/// launchers run from the game root, so captures are easy to find and compare.
std::filesystem::path next_development_screenshot_path(const std::string& game_id) {
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif

    std::ostringstream stem;
    stem << game_id << '-' << std::put_time(&local, "%Y%m%d-%H%M%S");
    const std::filesystem::path directory = "screenshots";
    for (unsigned sequence = 1;; ++sequence) {
        std::ostringstream filename;
        filename << stem.str();
        if (sequence > 1) {
            filename << '-' << sequence;
        }
        filename << ".png";
        const std::filesystem::path candidate = directory / filename.str();
        std::error_code ec;
        if (!std::filesystem::exists(candidate, ec)) {
            return candidate;
        }
    }
}
#endif

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

// (Re)create the OS window for `mode`. Fullscreen uses the desktop's native video
// mode (no mode switch) and letterboxes the virtual resolution within it; windowed
// uses the requested client size. Keeping the framebuffer at the desktop size is
// what makes input map correctly in fullscreen — a mode switch leaves SFML's mouse
// coordinates in the old desktop space and the click/avatar mapping breaks (#71).
// The virtual resolution is unchanged either way, so gameplay coordinates are
// stable across the switch (R6).
void apply_window_mode(sf::RenderWindow& window,
                       const DisplayMode& mode,
                       const std::string& title) {
    if (mode.fullscreen) {
        window.create(sf::VideoMode::getDesktopMode(), title, sf::Style::Fullscreen);
    } else {
        window.create(sf::VideoMode(mode.size.x, mode.size.y), title, sf::Style::Default);
    }
    window.setVerticalSyncEnabled(true);
}

// Load a hardware cursor from a logical image path. Returns nullptr on any
// failure (no path, missing/undecodable image, or a platform that can't host a
// pixel cursor) so the caller keeps the OS cursor.
std::unique_ptr<sf::Cursor> load_cursor(const ResourceSource& source,
                                        const std::string& logical,
                                        sf::Vector2u hotspot,
                                        Diagnostics& log,
                                        bool inverted = false,
                                        std::optional<sf::Color> solid_tint = std::nullopt) {
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
        if (solid_tint || inverted) {
            const sf::Vector2u size = image.getSize();
            for (unsigned y = 0; y < size.y; ++y) {
                for (unsigned x = 0; x < size.x; ++x) {
                    sf::Color pixel = image.getPixel(x, y);
                    if (solid_tint) {
                        pixel.r = solid_tint->r;
                        pixel.g = solid_tint->g;
                        pixel.b = solid_tint->b;
                    }
                    if (inverted) {
                        pixel.r = static_cast<sf::Uint8>(255U - pixel.r);
                        pixel.g = static_cast<sf::Uint8>(255U - pixel.g);
                        pixel.b = static_cast<sf::Uint8>(255U - pixel.b);
                    }
                    image.setPixel(x, y, pixel);
                }
            }
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

sf::Color blend_cursor_color(sf::Color dark, sf::Color light, float amount) {
    const auto blend = [amount](sf::Uint8 a, sf::Uint8 b) {
        return static_cast<sf::Uint8>(
            std::lround(static_cast<float>(a) + (static_cast<float>(b) - a) * amount));
    };
    return {blend(dark.r, light.r), blend(dark.g, light.g), blend(dark.b, light.b)};
}

std::vector<std::unique_ptr<sf::Cursor>> load_cursor_blink_frames(const ResourceSource& source,
                                                                  const std::string& logical,
                                                                  sf::Vector2u hotspot,
                                                                  Diagnostics& log,
                                                                  const CursorBlinkConfig& blink,
                                                                  bool inverted) {
    std::vector<std::unique_ptr<sf::Cursor>> frames;
    if (!blink.enabled() || blink.steps < 2) {
        return frames;
    }
    frames.reserve(blink.steps);
    for (unsigned i = 0; i < blink.steps; ++i) {
        const float amount = static_cast<float>(i) / static_cast<float>(blink.steps - 1);
        auto frame = load_cursor(source,
                                 logical,
                                 hotspot,
                                 log,
                                 inverted,
                                 blend_cursor_color(blink.dark, blink.light, amount));
        if (!frame) {
            return {};
        }
        frames.push_back(std::move(frame));
    }
    return frames;
}

} // namespace

std::string
parse_run_options(int argc, char** argv, RunOptions& opts, const std::string& default_manifest) {
    std::string manifest = default_manifest;
    if (argc > 0 && argv[0]) {
        opts.argv0 = argv[0];
    }

    const auto value_of = [&](const std::string& arg, const char* flag, int& i) -> std::string {
        const std::string prefix = std::string(flag) + "=";
        if (arg.rfind(prefix, 0) == 0) {
            return arg.substr(prefix.size());
        }
        if (arg == flag && i + 1 < argc) {
            return argv[++i];
        }
        return {};
    };

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (std::string v = value_of(arg, "--frames", i); !v.empty()) {
            opts.max_frames = std::atoi(v.c_str());
        } else if (std::string v = value_of(arg, "--shot", i); !v.empty()) {
            opts.screenshot_path = std::move(v);
        } else if (std::string v = value_of(arg, "--record", i); !v.empty()) {
            opts.recording_path = std::move(v);
        } else if (std::string v = value_of(arg, "--pak", i); !v.empty()) {
            opts.pak_path = std::move(v);
        } else if (!arg.empty() && arg[0] != '-') {
            manifest = arg;
        }
    }
    return manifest;
}

static int run_impl(const std::string& manifest_path,
                    ResourceSource* supplied_source,
                    const SceneFactory& factory,
                    const RunOptions& opts,
                    const ApplicationHooks& hooks) {
    Diagnostics log;

    // A platform may supply a source directly. Otherwise preserve the desktop
    // backend selection (#109): prefer resources.pak, then loose files.
    std::unique_ptr<ResourceSource> source_holder;
    Manifest manifest;
    ResourceSource* source_ptr = supplied_source;
    if (source_ptr) {
        try {
            manifest = load_manifest(*source_ptr, manifest_path);
        } catch (const std::exception& e) {
            log.error(std::string("manifest resource '") + manifest_path + "': " + e.what());
            return 1;
        }
        // A supplied source is already rooted at the packaged game data. The
        // manifest's host-oriented resources.src value must not be applied a
        // second time by downstream code.
        manifest.resources_src.clear();
        log.info("loaded manifest resource '" + manifest_path + "'");
    } else {
        const std::filesystem::path pak = discover_pak(opts.argv0, opts.pak_path);
        if (!pak.empty()) {
            try {
                source_holder = std::make_unique<PackResourceSource>(pak);
            } catch (const std::exception& e) {
                log.error(e.what());
                return 1;
            }
            try {
                manifest = load_manifest(*source_holder, kPakManifestLogical);
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
        source_ptr = source_holder.get();
    }
    ResourceSource& source = *source_ptr;

    Settings settings;
    settings.audio.music_volume = manifest.settings.music_volume;
    settings.audio.sfx_volume = manifest.settings.sfx_volume;
    settings.audio.speech_enabled = manifest.settings.speech_enabled;
    settings.fullscreen = manifest.window.fullscreen;
    settings.window_width = manifest.window.width;
    settings.window_height = manifest.window.height;
    // An empty language is significant until the settings file has been read:
    // it means the player has never saved a preference, so startup may follow
    // the operating system locale.
    settings.language.clear();
    settings.clamp();

    // Player settings override manifest defaults (issue #66): a stored file
    // (per-user config dir) is overlaid on top of the defaults above. A missing
    // file is the normal first-run case; a corrupt one is warned and ignored.
    SettingsStore settings_store(user_config_dir(manifest.id) / "settings.yaml", log);
    settings_store.load(settings);
    if (settings.language.empty()) {
        const std::string locale = system_locale_name();
        settings.language =
            select_initial_language(manifest.languages, manifest.default_language, locale);
        log.info("localization: no saved language; system locale '" +
                 (locale.empty() ? std::string("unknown") : locale) + "' selected '" +
                 settings.language + "'");
    }
    settings.clamp();

    ResourceCache resources(source, log, manifest.rendering.smooth_textures);

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
                                 log,
                                 manifest.development.warn_missing_translations);
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
    SaveService saves(save_data_dir(manifest.id, executable_dir(opts.argv0)), log);
    CursorState cursor_state;
    Thumbnail thumbnail;
    GameplayRecorder recorder;
    if (!opts.recording_path.empty()) {
        if (!recorder.start_csv(opts.recording_path)) {
            log.error("could not open gameplay recording '" + opts.recording_path + "'");
            return 1;
        }
        log.info("recording gameplay to " +
                 std::filesystem::absolute(recorder.csv_path()).string());
    }

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
                      settings_store,
                      thumbnail,
                      recorder,
                      manifest.speech,
                      {}};
    bind_core_api(ctx, manifest.facts_path);
    if (hooks.configure) {
        try {
            hooks.configure(ctx, manifest);
        } catch (const std::exception& e) {
            log.error(std::string("application setup failed: ") + e.what());
            return 1;
        } catch (...) {
            log.error("application setup failed: unknown exception");
            return 1;
        }
    }

    // Session-only chapter identity survives intervening cutscenes/credits. It
    // lets a declarative on_finish route directly to the next chapter RoomScene
    // while still resetting state after every outgoing scene has left.
    std::string active_chapter_id;
    scenes.set_builder([&](const std::string& id) -> std::unique_ptr<Scene> {
        const SceneDesc* desc = manifest.find_scene(id);
        if (!desc) {
            log.error("scene id not found in manifest: '" + id + "'");
            return nullptr;
        }
        SceneParams params = desc->parameters;
        params.set("__scene_id", id);
        if (const ChapterDesc* chapter = manifest.chapter_for_scene(id)) {
            const bool restoring = saves.has_pending_restore();
            if (!active_chapter_id.empty() && active_chapter_id != chapter->id && !restoring) {
                state.clear();
                saves.clear_staged();
                log.info("chapter transition: '" + active_chapter_id + "' -> '" + chapter->id +
                         "'");
            }
            active_chapter_id = chapter->id;
            params.set("__chapter_id", chapter->id);
            params.set("__chapter_facts", chapter->facts_path);
            if (const ChapterDesc* next = manifest.next_chapter(chapter->id)) {
                params.set("__next_chapter_scene", next->scene);
            }
        }
        std::unique_ptr<Scene> scene = factory.create(desc->type, ctx, params);
        if (!scene) {
            log.error("unknown scene type '" + desc->type + "' for scene '" + id + "'");
        }
        return scene;
    });
    scenes.set_scene_entered_callback([&recorder](const std::string& id) {
        recorder.record("scene_enter", id);
    });

    bool settings_seen = false;
    bool confirmation_seen = false;
    for (const SceneDesc& desc : manifest.scenes) {
        if (desc.type == kSettingsSceneType && !settings_seen) {
            scenes.set_settings_scene_id(desc.id);
            settings_seen = true;
        } else if (desc.type == kConfirmationSceneType && !confirmation_seen) {
            scenes.set_confirmation_scene_id(desc.id);
            confirmation_seen = true;
        } else if (desc.type == kSaveLoadSceneType) {
            const std::string mode = desc.parameters.get_or("mode", "");
            if (mode == "save") {
                scenes.set_save_scene_id(desc.id);
            } else if (mode == "load") {
                scenes.set_load_scene_id(desc.id);
            } else {
                log.warn("manifest: SaveLoadScene '" + desc.id +
                         "' needs parameters.mode = save | load");
            }
        }
    }

    scenes.goto_scene(manifest.entry);
    scenes.apply_pending();
    if (!scenes.running() || scenes.top() == nullptr) {
        log.error("failed to enter entry scene '" + manifest.entry + "'");
        return 1;
    }
    log.info("entered entry scene '" + manifest.entry + "'");

    sf::RenderWindow window;
    apply_window_mode(window,
                      {{settings.window_width, settings.window_height}, settings.fullscreen},
                      manifest.title);
    if (!pac::gfx::initialize_gles2_renderer(window, log)) {
        return 1;
    }
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
    const std::unique_ptr<sf::Cursor> cursor_default_inverted =
        cursor_default
            ? load_cursor(source, manifest.cursor.image, manifest.cursor.hotspot, log, true)
            : nullptr;
    const std::unique_ptr<sf::Cursor> cursor_interact_inverted =
        cursor_interact
            ? load_cursor(source, manifest.cursor.interact, manifest.cursor.hotspot, log, true)
            : nullptr;
    const std::vector<std::unique_ptr<sf::Cursor>> cursor_blink_frames =
        cursor_default ? load_cursor_blink_frames(source,
                                                  manifest.cursor.image,
                                                  manifest.cursor.hotspot,
                                                  log,
                                                  manifest.cursor.blink,
                                                  false)
                       : std::vector<std::unique_ptr<sf::Cursor>>{};
    const std::vector<std::unique_ptr<sf::Cursor>> cursor_blink_frames_inverted =
        !cursor_blink_frames.empty() ? load_cursor_blink_frames(source,
                                                                manifest.cursor.image,
                                                                manifest.cursor.hotspot,
                                                                log,
                                                                manifest.cursor.blink,
                                                                true)
                                     : std::vector<std::unique_ptr<sf::Cursor>>{};
    const bool cursor_blinks = !cursor_blink_frames.empty();

    const sf::Cursor* initial_cursor =
        cursor_blinks ? cursor_blink_frames.front().get() : cursor_default.get();
    if (apply_cursor && initial_cursor) {
        window.setMouseCursor(*initial_cursor);
    }
    const sf::Cursor* applied_cursor = initial_cursor;
    CursorKind active_cursor_kind = CursorKind::DEFAULT;
    bool active_cursor_inverted = false;
    bool active_cursor_hidden = false;
    bool applied_cursor_visible = true;
    float cursor_blink_elapsed = 0.0f;

    // Enable fade-to-black between full-screen scene swaps, and fade the first
    // scene in from black at startup. (Set after the entry scene is already in
    // place so the entry itself is instant.)
    scenes.set_transition_duration(kSceneTransition);
    scenes.start_fade_in();

    // Resource-profiling mode (#112): development-only. Samples frame timing, RAM,
    // and resource-cache footprint, and writes a report at exit. Off unless the
    // manifest's `development.profiling` flag is set.
    std::optional<Profiler> profiler;
    if (manifest.development.profiling) {
        profiler.emplace(
            log,
            user_data_dir(manifest.id) / "profiling-report.txt",
            manifest.id,
            [&resources] { return resources.stats(); },
            manifest.development.profiling_interval);
    }

    sf::Clock clock;
    sf::Clock work_clock; // CPU+draw cost of a frame, excluding the vsync wait
    float accumulator = 0.0f;
    int frames = 0;
    bool first_frame_rendered = false;
    PointerInput pointer_input;
    bool paused = false;
    bool generic_pause_overlay = false;
    bool space_down = false;
#ifndef NDEBUG
    bool screenshot_requested = false;
#endif
    const sf::Font* pause_font =
        manifest.speech.font.empty() ? nullptr : resources.try_font(manifest.speech.font);

    auto begin_pause = [&]() {
        if (paused) {
            return;
        }
        generic_pause_overlay = !scenes.pause_menu_active() && !scenes.enter_pause_menu();
        paused = true;
        audio.pause();
        accumulator = 0.0f;
        log.info("application paused");
    };
    auto finish_pause = [&](bool close_scene_menu) {
        if (!paused) {
            return;
        }
        if (close_scene_menu) {
            scenes.leave_pause_menu();
        }
        paused = false;
        generic_pause_overlay = false;
        audio.resume();
        accumulator = 0.0f;
        clock.restart();
        log.info("application resumed");
    };

    while (window.isOpen() && scenes.running()) {
        work_clock.restart();
        bool rendering_context_needs_activation = false;
        sf::Event event;
        while (window.pollEvent(event)) {
            if (event.type == sf::Event::Closed) {
                scenes.request_quit();
            } else if (event.type == sf::Event::LostFocus) {
                space_down = false;
                begin_pause();
            } else if (event.type == sf::Event::GainedFocus) {
                // Android recreates its EGL surface while delivering this event.
                // Keep the application paused until the player explicitly resumes.
                space_down = false;
                rendering_context_needs_activation = true;
            } else if (event.type == sf::Event::Resized) {
                display.set_window_size({event.size.width, event.size.height});
            } else {
                for (const sf::Event& pointer_event : pointer_input.translate(event)) {
                    const sf::Event virtual_event = to_virtual_event(pointer_event, display);
                    if (virtual_event.type == sf::Event::KeyReleased &&
                        virtual_event.key.code == sf::Keyboard::Space) {
                        space_down = false;
                        continue;
                    }
                    if (virtual_event.type == sf::Event::KeyPressed &&
                        virtual_event.key.code == sf::Keyboard::Space) {
                        if (!space_down) {
                            space_down = true;
                            if (paused) {
                                finish_pause(true);
                            } else {
                                begin_pause();
                            }
                        }
                        continue;
                    }
#ifndef NDEBUG
                    if (virtual_event.type == sf::Event::KeyPressed &&
                        virtual_event.key.code == sf::Keyboard::F12) {
                        screenshot_requested = true;
                        continue;
                    }
#endif
                    if (paused && generic_pause_overlay) {
                        if (resumes_generic_pause(virtual_event)) {
                            finish_pause(false);
                        }
                        continue;
                    }
                    scenes.handle_event(virtual_event);
                    if (!paused && scenes.pause_menu_active()) {
                        begin_pause();
                    } else if (paused && !generic_pause_overlay && !scenes.pause_menu_active()) {
                        finish_pause(false);
                    }
                }
            }
        }
        // SFML queues GainedFocus before its Android processEvents() call creates
        // the replacement EGL surface. The final poll above performs that work;
        // only now can the context be made current again.
        if (rendering_context_needs_activation) {
            if (!window.setActive(true)) {
                log.error("could not reactivate rendering context after focus gain");
            }
            display.set_window_size(window.getSize());
        }
        scenes.apply_pending();
        if (paused && !generic_pause_overlay && !scenes.pause_menu_active()) {
            finish_pause(false);
        }
        if (!scenes.running()) {
            break;
        }

        // A scene (e.g. the settings menu) may have requested a display-mode
        // change; recreate the window before simulating/drawing this frame.
        if (const std::optional<DisplayMode> mode = display.take_pending_mode()) {
            apply_window_mode(window, *mode, manifest.title);
            pac::gfx::configure_gles2_target(window);
            display.set_window_size(window.getSize());
            display.set_fullscreen(mode->fullscreen);
            // A recreated OS window starts with its cursor visible and has not
            // received our custom cursor yet.
            applied_cursor = nullptr;
            applied_cursor_visible = true;
        }

        const float frame_seconds = clock.restart().asSeconds();
        int steps = 0;
        if (paused) {
            accumulator = 0.0f;
            scenes.update_transition(frame_seconds);
            scenes.apply_pending();
            if (!generic_pause_overlay && !scenes.pause_menu_active()) {
                finish_pause(false);
            }
        } else {
            cursor_blink_elapsed += frame_seconds;
            accumulator += frame_seconds;
            while (accumulator >= kFixedDt && steps < kMaxStepsPerFrame) {
                audio.update(kFixedDt);
                scripting.update(kFixedDt);
                scenes.update(kFixedDt);
                if (hooks.update) {
                    hooks.update(kFixedDt);
                }
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
        }
        if (!scenes.running()) {
            break;
        }

// Android destroys the native/EGL surface while the activity is in the
// background. Continue polling lifecycle events, but never issue GL calls
// until SFML reports focus (and therefore a recreated surface) again. Desktop
// windows retain a renderable surface while unfocused, which is also important
// for offscreen smoke runs that do not have window-manager focus.
#if defined(SFML_SYSTEM_ANDROID)
        if (!window.hasFocus()) {
            accumulator = 0.0f;
            clock.restart();
            sf::sleep(sf::milliseconds(20));
            continue;
        }
#endif

        // Consume the appearance a scene requested, then reset so INTERACT must
        // be re-asserted. The selected kind persists between fixed updates while
        // the resting cursor's hardware variants continue pulsing per frame.
        //
        // Gated on a simulation step having run: scenes request the cursor from
        // update(), which is driven by the fixed-timestep loop above, while this
        // runs once per *rendered* frame. Without vsync the render rate far exceeds
        // the fixed rate, so most frames take zero steps — and an unconditional
        // reset() there would revert the cursor to DEFAULT on every one of them,
        // leaving INTERACT visible only on the rare stepping frame. Consuming the
        // request at the cadence it is produced keeps it stable (issue #73).
        if (steps > 0) {
            active_cursor_kind = cursor_state.requested;
            active_cursor_inverted = cursor_state.inverted;
            active_cursor_hidden = cursor_state.hidden;
            cursor_state.reset();
        }

        const bool interact = active_cursor_kind == CursorKind::INTERACT && cursor_interact;
        const float blink_progress =
            cursor_blink_progress(cursor_blink_elapsed, manifest.cursor.blink.interval);
        const std::size_t blink_frame =
            cursor_blinks
                ? static_cast<std::size_t>(std::lround(
                      blink_progress * static_cast<float>(cursor_blink_frames.size() - 1)))
                : 0;
        const sf::Cursor* requested_cursor = nullptr;
        if (interact) {
            requested_cursor = active_cursor_inverted && cursor_interact_inverted
                                   ? cursor_interact_inverted.get()
                                   : cursor_interact.get();
        } else if (cursor_blinks) {
            if (active_cursor_inverted && !cursor_blink_frames_inverted.empty()) {
                requested_cursor = cursor_blink_frames_inverted[blink_frame].get();
            } else if (active_cursor_inverted && cursor_default_inverted) {
                requested_cursor = cursor_default_inverted.get();
            } else {
                requested_cursor = cursor_blink_frames[blink_frame].get();
            }
        } else {
            requested_cursor = active_cursor_inverted && cursor_default_inverted
                                   ? cursor_default_inverted.get()
                                   : cursor_default.get();
        }
        // Some OS backends make a hardware cursor visible again when its image is
        // replaced. Freeze animated/tinted cursor swaps while hidden; on return,
        // apply the current frame before restoring visibility.
        if (apply_cursor && !active_cursor_hidden && requested_cursor &&
            requested_cursor != applied_cursor) {
            window.setMouseCursor(*requested_cursor);
            applied_cursor = requested_cursor;
        }
        const bool requested_cursor_visible = !active_cursor_hidden;
        if (apply_cursor && requested_cursor_visible != applied_cursor_visible) {
            window.setMouseCursorVisible(requested_cursor_visible);
            applied_cursor_visible = requested_cursor_visible;
        }

        window.clear(sf::Color::Black); // letterbox bars
        window.setView(display.view());
        scenes.draw(window);
        if (hooks.draw) {
            hooks.draw(window);
        }
        if (paused && generic_pause_overlay) {
            window.setView(display.view());
            draw_generic_pause(window,
                               display.virtual_resolution(),
                               localization.strings(),
                               pause_font);
        }

        // Full-window readbacks come before thumbnail generation. The latter
        // binds a 256x144 off-screen framebuffer, so keeping these phases
        // ordered prevents F12/--shot from ever observing that target.
        const bool last_frame = (opts.max_frames > 0 && frames + 1 >= opts.max_frames);
        if (last_frame && !opts.screenshot_path.empty()) {
            if (save_screenshot(window, opts.screenshot_path)) {
                log.info("wrote screenshot " + opts.screenshot_path);
            } else {
                log.error("could not write screenshot " + opts.screenshot_path);
            }
        }

#ifndef NDEBUG
        if (screenshot_requested) {
            screenshot_requested = false;
            const std::filesystem::path shot_path = next_development_screenshot_path(manifest.id);
            std::error_code ec;
            std::filesystem::create_directories(shot_path.parent_path(), ec);
            if (ec) {
                log.error("could not create screenshot directory '" +
                          shot_path.parent_path().string() + "': " + ec.message());
            } else if (save_screenshot(window, shot_path)) {
                log.info("wrote screenshot " + std::filesystem::absolute(shot_path, ec).string());
            } else {
                log.error("could not write screenshot " + shot_path.string());
            }
        }
#endif

        // Thumbnail refresh (issue #119): every ~0.5s while the active scene
        // is in a thumbnail-friendly state (RoomScene COMMAND), capture the
        // current framebuffer cropped to the gameplay viewport. The save
        // picker reads `ctx.thumbnail.image()` when the player saves. Throttle
        // keeps the per-frame GPU readback off the hot path.
        constexpr int kThumbnailEveryFrames = 30;
        const Scene* top = scenes.top();
        if (!paused && top && top->wants_thumbnail() && (frames % kThumbnailEveryFrames) == 0) {
            thumbnail.capture(window, display.viewport());
        }

        // Feed the profiler before the vsync wait so `work_seconds` reflects only
        // CPU + draw cost, while `frame_seconds` carries the full frame-to-frame
        // pacing (vsync included → real fps). The render counters were accumulated
        // during scenes.draw() above; read them here, then reset for the next frame.
        if (profiler) {
            const RenderStats rs = render_stats();
            profiler->frame({frame_seconds,
                             work_clock.getElapsedTime().asSeconds(),
                             rs.shader_passes,
                             rs.shader_rt_bytes,
                             scenes.current_scene_id()});
            reset_shader_passes();
        }
        window.display();
        if (!first_frame_rendered) {
            first_frame_rendered = true;
            log.info("rendered first frame");
        }

        if (opts.max_frames > 0 && ++frames >= opts.max_frames) {
            log.info("smoke run reached max_frames=" + std::to_string(opts.max_frames) +
                     ", exiting");
            break;
        }
    }

    // Close the window now, while the custom cursors below are still alive. SFML's
    // X11 teardown re-asserts the last-set cursor (setMouseCursorVisible(true) in
    // ~WindowImplX11::cleanup). The sf::Cursors are declared after `window`, so at
    // scope exit they are freed first; that re-assert would then reference a freed
    // X cursor → BadCursor on exit. Closing here runs the teardown against a live
    // cursor. (A no-op when the window is already closed.)
    window.close();

    if (profiler) {
        profiler->finish();
    }
    return 0;
}

int run(const std::string& manifest_path,
        const SceneFactory& factory,
        const RunOptions& opts,
        const ApplicationHooks& hooks) {
    return run_impl(manifest_path, nullptr, factory, opts, hooks);
}

int run_from_resources(ResourceSource& resources,
                       const std::string& manifest_logical_path,
                       const SceneFactory& factory,
                       const RunOptions& opts,
                       const ApplicationHooks& hooks) {
    return run_impl(manifest_logical_path, &resources, factory, opts, hooks);
}

} // namespace pac::core
