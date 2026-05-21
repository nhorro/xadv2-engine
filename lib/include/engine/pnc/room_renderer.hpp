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

struct RoomData;
class Avatar;

/// Draws a room's scenery: the background fill, the background layers (textures
/// resolved relative to `room_dir`, scaled to the room size), and the player
/// avatar interleaved by z. Speech/UI are drawn by the scene over the top.
class RoomRenderer {
public:
    void draw(sf::RenderTarget& target,
              const RoomData& room,
              const std::string& room_dir,
              pac::core::ResourceCache& resources,
              const Avatar* player,
              pac::core::Diagnostics& log) const;
};

} // namespace pac::pnc
