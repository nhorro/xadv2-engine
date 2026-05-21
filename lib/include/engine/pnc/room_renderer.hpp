#pragma once

#include <string>

namespace sf {
class RenderTarget;
}

namespace pac::core {
class Diagnostics;
class ResourceCache;
} // namespace pac::core

namespace pac::pnc {

class RoomRuntime;
class Avatar;

/// Draws a room's scenery in world space (the room view sets the camera/scenery
/// sf::View first): background fill + layers, visible regions (current-state
/// image) and objects, and the player avatar — all sorted by z (ZDrawable order).
/// Textures are resolved relative to `room_dir`. Speech/UI are drawn by the scene.
class RoomRenderer {
public:
    void draw(sf::RenderTarget& target,
              const RoomRuntime& room,
              const std::string& room_dir,
              pac::core::ResourceCache& resources,
              const Avatar* player,
              pac::core::Diagnostics& log) const;
};

} // namespace pac::pnc
