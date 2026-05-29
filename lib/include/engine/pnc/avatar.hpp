#pragma once

#include "engine/geom/geometry.hpp"
#include "engine/gfx/animated_sprite.hpp"
#include "engine/gfx/shader_effect.hpp"
#include "engine/pnc/cast.hpp" // Shadow
#include "engine/pnc/mover.hpp"

#include <optional>
#include <string>
#include <vector>

namespace sf {
class RenderTarget;
}

namespace pac::core {
class ResourceCache;
}

namespace pac::gfx {
class ShaderChain;
}

namespace pac::pnc {

struct RoomData;

/// The in-room visual instance of a character: an AnimatedSprite driven by a
/// headless `Mover`. The Mover owns all movement state and logic; the Avatar only
/// syncs the sprite (walking-pivot position + directional animation) from it, so
/// movement stays testable without a graphics context.
class Avatar {
public:
    explicit Avatar(gfx::AnimatedSprite sprite,
                    float scale = 1.0f,
                    std::optional<Shadow> shadow = std::nullopt);

    void set_position(geom::Point p);
    geom::Point position() const { return mover_.position(); }
    void face(const std::string& direction) { face(direction_from_string(direction)); }
    void face(Direction direction);
    std::string facing() const { return to_string(mover_.facing()); }

    /// Walk straight to a single point, gated by the walkable area.
    void move_to(geom::Point target);
    /// Walk an ordered list of waypoints (e.g. a `geom::find_path` result),
    /// turning at each corner. An empty path stops the avatar.
    void follow_path(std::vector<geom::Point> path);
    void stop();
    bool moving() const { return mover_.moving(); }

    void update(float dt, const RoomData& room);

    /// Render the avatar (shadow blob + animated sprite). The renderer supplies
    /// `resources` and `time` so the sprite can apply its shader stack (design 03
    /// §Shaders, issue #106); `chain` is the per-render multi-pass pool, may be
    /// null (single-shader and unshaded paths still work).
    void draw(sf::RenderTarget& target,
              pac::core::ResourceCache& resources,
              float time = 0.0f,
              pac::gfx::ShaderChain* chain = nullptr) const;

    /// Set the shader stack carried by the avatar's animated sprite (mirrors the
    /// cast appearance's `shaders:`). Cleared with an empty vector.
    void set_shaders(std::vector<pac::gfx::ShaderEffect> shaders) {
        sprite_.set_shaders(std::move(shaders));
    }

    float z() const { return mover_.position().y; } // depth key = walking-pivot y

    /// World-space bounds of the current animation frame (position + perspective
    /// scale applied). Hit-tests a hotspot bound to this avatar (#141).
    [[nodiscard]] sf::FloatRect bounds() const { return sprite_.global_bounds(); }

private:
    /// Mirror the mover's position and facing/action onto the sprite.
    void sync_sprite();
    /// Play the sequence for the current action + facing, falling back from
    /// `<action>_<direction>` to the bare `<action>` and finally to `stand`.
    void apply_animation();

    /// Draw the ground shadow blob (if any) centered on the walking pivot, sized
    /// by the shadow's unit size times the avatar's current render scale.
    void draw_shadow(sf::RenderTarget& target) const;

    gfx::AnimatedSprite sprite_;
    Mover mover_;
    float scale_ = 1.0f;      // base scale; the fallback when the room has no perspective
    float draw_scale_ = 1.0f; // last applied render scale (perspective), for the shadow
    std::optional<Shadow> shadow_;
};

} // namespace pac::pnc
