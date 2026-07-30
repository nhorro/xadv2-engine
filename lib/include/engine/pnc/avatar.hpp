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

    /// Stop the current movement/action and play the talking loop for the current
    /// facing. Prefers `talk_<direction>`, then any directional talk sequence,
    /// then bare `talk`; when none exists the avatar simply stands.
    void talk();
    /// End a talk loop and return control to the mover-driven stand animation.
    void stop_talking();
    [[nodiscard]] bool talking() const { return talking_; }

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

    /// Play an explicit animation sequence, overriding the mover-driven
    /// stand/walk animation. A non-looping sequence plays once and then control
    /// returns to stand/walk; movement also cancels it. No-op if the sequence is
    /// absent (#149).
    void play(const std::string& sequence);
    /// True while a scripted `play` sequence is still running (not yet finished
    /// and not cancelled by movement).
    [[nodiscard]] bool acting() const { return !acting_.empty(); }
    /// The sequence currently selected on the underlying sprite. Useful to
    /// diagnostics and engine-level behavior tests.
    [[nodiscard]] const std::string& current_animation() const {
        return sprite_.current_sequence();
    }
    /// World position of a named sprite anchor on the current frame, if present.
    [[nodiscard]] std::optional<geom::Point> anchor(const std::string& name) const;

private:
    /// Mirror the mover's position and facing/action onto the sprite.
    void sync_sprite();
    /// Play the sequence for the current action + facing, falling back from
    /// `<action>_<direction>` to the bare `<action>` and finally to `stand`.
    void apply_animation();

    /// Draw the ground shadow blob (if any) centered on the walking pivot, sized
    /// by the shadow's unit size times the avatar's current render scale.
    void draw_shadow(sf::RenderTarget& target) const;

    // Scripted animation override (#149): the sequence set by play(). While
    // non-empty it supersedes the mover-driven stand/walk animation; a one-shot
    // sequence clears it on finish, and movement cancels it.
    std::string acting_;
    bool talking_ = false;

    gfx::AnimatedSprite sprite_;
    Mover mover_;
    float scale_ = 1.0f;      // base scale; the fallback when the room has no perspective
    float draw_scale_ = 1.0f; // last applied render scale (perspective), for the shadow
    std::optional<Shadow> shadow_;
};

} // namespace pac::pnc
