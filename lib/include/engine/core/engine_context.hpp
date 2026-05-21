#pragma once

namespace pac::core {

class Diagnostics;
class Display;
class ResourceSource;
class SceneManager;
class Settings;
class Strings;
struct DevFlags;

/// Borrowed, app-owned service references handed to every scene at construction.
/// Scenes never own or extend the lifetime of these services.
///
/// Audio (M1) and scripting (M2) services join this struct in their milestones.
struct EngineContext {
    Display& display;
    ResourceSource& resources;
    Settings& settings;
    SceneManager& scenes;
    const Strings& strings;
    Diagnostics& log;
    const DevFlags& dev;
};

} // namespace pac::core
