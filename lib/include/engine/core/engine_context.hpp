#pragma once

#include <string>

namespace pac::core {

class Diagnostics;
class Display;
class ResourceCache;
class SaveService;
class SceneManager;
class Scripting;
class Settings;
class StateStore;
class Strings;
struct AudioServices;
struct DevFlags;

/// Borrowed, app-owned service references handed to every scene at construction.
/// Scenes never own or extend the lifetime of these services.
struct EngineContext {
    Display& display;
    ResourceCache& resources;
    AudioServices& audio;
    Scripting& scripting;
    StateStore& state;
    Settings& settings;
    SceneManager& scenes;
    const Strings& strings;
    Diagnostics& log;
    const DevFlags& dev;
    /// Manifest `id` — stable, filesystem-friendly. Routes per-game data
    /// (save files, settings overrides, ...) into a game-specific subtree
    /// of the per-user data path, so distinct games using this engine don't
    /// trample each other.
    const std::string& game_id;
    /// Save/load slots, autosave, latest-slot lookup, and the staged-restore
    /// hand-off between TitleScreen's Continue and RoomScene::enter().
    SaveService& saves;
};

} // namespace pac::core
