#pragma once

#include "engine/core/dev_flags.hpp"
#include "engine/core/load_error.hpp"
#include "engine/core/scene_params.hpp"
#include "engine/core/speech_config.hpp"

#include <SFML/Graphics/Color.hpp>
#include <SFML/System/Vector2.hpp>

#include <string>
#include <vector>

namespace pac::core {

/// Manifest loader diagnostic. A `LoadError` with `source = "manifest-loader"`.
class ManifestError : public LoadError {
public:
    using LoadError::LoadError;
    explicit ManifestError(const std::string& message)
        : LoadError("manifest-loader", "", message) {}
};

struct WindowConfig {
    bool fullscreen = false;
    unsigned width = 1280;
    unsigned height = 720;
};

struct RenderingConfig {
    // Linear filtering for textures and intermediate render targets. Enabled by
    // default so fractionally scaled painterly art does not shimmer in motion.
    bool smooth_textures = true;
};

struct SettingsDefaults {
    float music_volume = 1.0f;
    float sfx_volume = 1.0f;
};

/// One selectable UI-strings language (issue #72). `id` is the stable, ASCII id
/// stored in settings and matched by the selector; `name` is the display label,
/// written in its own language (e.g. "Español"); `strings_path` is the logical
/// path to that language's UI-strings file. The MVP ships one entry (Spanish);
/// the list is design-ready for more Western languages.
struct LanguageEntry {
    std::string id;
    std::string name;
    std::string strings_path;
};

struct CursorBlinkConfig {
    /// Seconds for one dark-to-light or light-to-dark transition. Zero disables
    /// the pulse; steps controls how many preloaded hardware-cursor tones it uses.
    float interval = 0.0f;
    unsigned steps = 12;
    sf::Color dark{64, 64, 64};
    sf::Color light{255, 255, 255};

    [[nodiscard]] bool enabled() const { return interval > 0.0f; }
};

/// Custom point-and-click cursor (issue #73). `image` is the resting cursor;
/// `interact` (optional) is shown over an interactive hotspot. `hotspot` is the
/// pointer's active pixel (the click point) within both images. An optional blink
/// alternates the resting cursor between two solid RGB tones while preserving
/// the source image's alpha. When `image` is empty the OS cursor is kept.
struct CursorConfig {
    std::string image;
    std::string interact;
    sf::Vector2u hotspot{0, 0};
    CursorBlinkConfig blink;
};

struct SceneDesc {
    std::string id;
    std::string type;
    SceneParams parameters;
};

/// One independently testable chapter in the campaign's linear sequence.
/// `scene` is the RoomScene used for new-game entry and save restoration;
/// `facts_path` is rebound when that scene starts so vocabularies do not grow
/// across chapter boundaries.
struct ChapterDesc {
    std::string id;
    std::string scene;
    std::string facts_path;
};

/// The single source of game-level configuration (`game.yaml`).
struct Manifest {
    /// Stable, filesystem-friendly id for the game. Used as the directory
    /// name under the per-user data path (e.g. `~/.local/share/<id>/saves/`),
    /// so two games using this engine get separate save folders. Required;
    /// allowed chars are `[a-z0-9_-]`.
    std::string id;
    int version = 1;
    sf::Vector2u resolution{1280, 720};
    WindowConfig window;
    RenderingConfig rendering;
    std::string resources_src; // resolved to a host root by load_manifest
    // Optional declared-facts registry. Kept configurable so an independently
    // runnable chapter can own its fact vocabulary below its content directory.
    std::string facts_path = "facts.yaml";
    // Logical path to the default language's UI-strings file. Always set: it
    // mirrors `languages[default_language].strings_path` and is the resource the
    // engine loads when no user language preference is stored.
    std::string strings_path;
    // Available UI-strings languages (issue #72). Always non-empty: a single
    // `strings:` scalar yields one entry; a `languages:` list yields many.
    std::vector<LanguageEntry> languages;
    // Id of the default language (the first declared, unless `languages.default`
    // overrides it). The active language is the stored user preference if valid,
    // else this default.
    std::string default_language;
    SettingsDefaults settings;
    CursorConfig cursor;
    SpeechConfig speech;
    DevFlags development;
    std::string entry;
    std::vector<SceneDesc> scenes;
    std::vector<ChapterDesc> chapters;

    const SceneDesc* find_scene(const std::string& id) const;
    const ChapterDesc* find_chapter(const std::string& id) const;
    const ChapterDesc* chapter_for_scene(const std::string& scene_id) const;
    const ChapterDesc* next_chapter(const std::string& id) const;
};

/// Parse + validate a manifest from YAML text. Throws ManifestError on any
/// structural problem (missing required fields, duplicate scene ids, entry not
/// found, ...). `resources_src` is kept verbatim.
Manifest parse_manifest(const std::string& yaml_text);

/// Read a manifest file, parse it, and resolve `resources_src` relative to the
/// manifest's directory. Throws ManifestError if the file cannot be read.
Manifest load_manifest(const std::string& file_path);

} // namespace pac::core
