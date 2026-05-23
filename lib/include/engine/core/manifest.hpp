#pragma once

#include "engine/core/dev_flags.hpp"
#include "engine/core/load_error.hpp"
#include "engine/core/scene_params.hpp"

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

struct SettingsDefaults {
    float music_volume = 1.0f;
    float sfx_volume = 1.0f;
};

struct SceneDesc {
    std::string id;
    std::string type;
    SceneParams parameters;
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
    std::string resources_src; // resolved to a host root by load_manifest
    std::string strings_path;  // logical path
    SettingsDefaults settings;
    DevFlags development;
    std::string entry;
    std::vector<SceneDesc> scenes;

    const SceneDesc* find_scene(const std::string& id) const;
};

/// Parse + validate a manifest from YAML text. Throws ManifestError on any
/// structural problem (missing required fields, duplicate scene ids, entry not
/// found, ...). `resources_src` is kept verbatim.
Manifest parse_manifest(const std::string& yaml_text);

/// Read a manifest file, parse it, and resolve `resources_src` relative to the
/// manifest's directory. Throws ManifestError if the file cannot be read.
Manifest load_manifest(const std::string& file_path);

} // namespace pac::core
