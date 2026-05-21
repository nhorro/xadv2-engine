#pragma once

namespace pac::core {

class Diagnostics;
class Display;
class ResourceCache;
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
};

} // namespace pac::core
