#pragma once

#include <SFML/System/Vector2.hpp>

#include <string>
#include <vector>

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
struct RoomData;

/// Per-frame inputs the renderer feeds to a shader's reserved uniforms (design 03
/// §Shaders). `time` (seconds since the scene began) drives `u_time`;
/// `u_resolution` is set per drawable from its texture size, so it is not here.
struct ShaderEnv {
    float time = 0.0f;
};

/// Log a one-time warning (at room load) for shader features that parse but are
/// not yet applied: a multi-shader stack (only the first runs) and a `controller`
/// reference (declarative-only for now). Keeps the per-frame render path quiet.
void warn_unsupported_shader_features(const RoomData& data, pac::core::Diagnostics& log);

/// Derive a room's world bounds from its background layers: the bounding box of
/// every layer rect — a layer occupies [origin, origin + native texture size ×
/// scale), origin defaulting to the world origin (0,0) and scale to 1.0 — anchored
/// at (0,0) and floored to
/// `viewport` so the world is never smaller than the room view. Layers that fail
/// to load contribute nothing; uncovered world area shows `background_color`.
/// Texture sizes come from `resources`, so this is render-side, not headless.
[[nodiscard]] sf::Vector2u compute_room_bounds(const RoomData& data,
                                               const std::string& room_dir,
                                               pac::core::ResourceCache& resources,
                                               sf::Vector2f viewport,
                                               pac::core::Diagnostics& log);

/// Draws a room's scenery in world space (the room view sets the camera/scenery
/// sf::View first): background fill + layers, visible regions (current-state
/// image) and objects, and the player + NPC avatars — all sorted by z (ZDrawable
/// order). Textures are resolved relative to `room_dir`. Speech/UI are drawn by
/// the scene.
class RoomRenderer {
public:
    void draw(sf::RenderTarget& target,
              const RoomRuntime& room,
              const std::string& room_dir,
              pac::core::ResourceCache& resources,
              const Avatar* player,
              const std::vector<const Avatar*>& npcs,
              pac::core::Diagnostics& log,
              const ShaderEnv& shaders = {}) const;
};

} // namespace pac::pnc
