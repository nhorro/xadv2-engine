#pragma once

#include "engine/core/speech_config.hpp"

#include <string>

namespace pac::core {

class Diagnostics;
class Display;
class Localization;
class ResourceCache;
class SaveService;
class SceneManager;
class Scripting;
class Settings;
class SettingsStore;
class StateStore;
class Strings;
class Thumbnail;
struct AudioServices;
struct CursorState;
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
    /// Active UI strings. Bound to `localization`'s current resource: when the
    /// player switches language the underlying object is reloaded in place, so
    /// this reference transparently reflects the new language.
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
    /// Per-frame cursor appearance request channel (issue #73). Scenes call
    /// `cursor.want(...)`; the harness applies it and resets it each frame.
    CursorState& cursor;
    /// Available languages + the live language switch (issue #72). The settings
    /// scene reads `languages()` for the selector and calls `set_language()`,
    /// which swaps the resource `strings` points at.
    Localization& localization;
    /// Player settings persistence (issue #66). The settings scene calls
    /// `save()` when the player applies changes.
    SettingsStore& settings_store;
    /// Latest captured framebuffer thumbnail (issue #119). The app loop
    /// refreshes it on frames the active scene marks as good thumbnail
    /// candidates (`Scene::wants_thumbnail()`); the in-game pause menu hands
    /// it to `SaveService` when the player opens the save picker.
    Thumbnail& thumbnail;
    /// Game-wide spoken-line typography. Scene-specific fonts remain UI fonts.
    SpeechConfig speech;
};

} // namespace pac::core
