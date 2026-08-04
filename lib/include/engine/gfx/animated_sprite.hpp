#pragma once

#include "engine/gfx/animation.hpp"
#include "engine/gfx/sequence_player.hpp"
#include "engine/gfx/shader_effect.hpp"
#include "engine/gfx/spritesheet.hpp"

#include <SFML/Graphics/Color.hpp>
#include <SFML/Graphics/Drawable.hpp>
#include <SFML/Graphics/Transform.hpp>
#include <SFML/Graphics/Transformable.hpp>

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace pac::core {
class ResourceCache;
}

namespace pac::gfx {

class ShaderChain;

/// An animated sprite: a spritesheet + animation driven by a SequencePlayer.
/// Inherits sf::Transformable, so setPosition places the current frame's pivot
/// anchor at that point (feet/hands stay stable across frames).
///
/// Shaders (design 03 §Shaders) are an optional declarative stack — the same
/// `gfx::ShaderEffect`s used on background layers / regions / objects. The base
/// `draw(target, states)` overload (sf::Drawable) ignores them; the
/// shader-aware `draw(target, resources, time, chain)` overload routes through
/// `ShaderChain` (multi-pass) or sets a single shader directly. The owning
/// avatar wires its room's resources/time at draw time.
class AnimatedSprite : public sf::Drawable, public sf::Transformable {
public:
    AnimatedSprite(Spritesheet sheet, Animation anim);

    void play(const std::string& sequence, bool restart = true) { player_.play(sequence, restart); }
    void update(float dt) { player_.update(dt); }
    bool has(const std::string& sequence) const { return player_.has(sequence); }
    bool finished() const { return player_.finished(); }
    void set_on_finished(std::function<void()> cb) { player_.set_on_finished(std::move(cb)); }
    const std::string& current_sequence() const { return player_.current_sequence(); }

    /// World-space axis-aligned bounds of the current frame with this sprite's
    /// transform (position + scale) applied — the pivot anchor sits at the
    /// transform's position, like draw(). Empty (zero-size at the position) when
    /// there is no valid current frame. Used for hit-testing a moving avatar.
    [[nodiscard]] sf::FloatRect global_bounds() const;

    /// Current frame bounds in pivot-relative local coordinates. This is the
    /// geometry draw() transforms into world space and is also the primitive a
    /// CompositeSprite uses when unioning the bounds of its attached parts.
    [[nodiscard]] sf::FloatRect local_bounds() const;

    /// World position of a named anchor on the current frame (transform applied),
    /// or nullopt when the frame has no such anchor. Mirrors global_bounds()'s
    /// pivot/transform handling.
    [[nodiscard]] std::optional<sf::Vector2f> anchor_world(const std::string& name) const;

    /// Position of an anchor relative to this animation's pivot, including the
    /// current sequence's horizontal mirroring. Composite attachments use this
    /// without temporarily mutating the child's world transform.
    [[nodiscard]] std::optional<sf::Vector2f> anchor_local(const std::string& name) const;

    void set_color(sf::Color color) { color_ = color; }

    void set_shaders(std::vector<ShaderEffect> shaders) { shaders_ = std::move(shaders); }
    const std::vector<ShaderEffect>& shaders() const { return shaders_; }

    void draw(sf::RenderTarget& target, sf::RenderStates states) const override;

    /// Shader-aware draw: applies `shaders_` (single-pass fast path or
    /// `ShaderChain` for multi-pass). When `shaders_` is empty this reduces to
    /// the standard unshaded draw.
    void draw(sf::RenderTarget& target,
              pac::core::ResourceCache& resources,
              float time,
              ShaderChain* chain) const;

    /// Shader-aware draw with an explicit local-to-world transform. This is the
    /// normal rendering path for a part inside a CompositeSprite.
    void draw_transformed(sf::RenderTarget& target,
                          const sf::Transform& transform,
                          pac::core::ResourceCache& resources,
                          float time,
                          ShaderChain* chain) const;

    /// Draw the current frame with an explicit local-to-world transform and tint,
    /// bypassing this sprite's normal position/scale and shader stack. Used by
    /// the point-and-click renderer to reuse the live alpha silhouette for a
    /// projected ground shadow while preserving frame mirroring and pivot.
    void draw_transformed(sf::RenderTarget& target,
                          const sf::Transform& transform,
                          sf::Color tint) const;

private:
    Spritesheet sheet_;
    std::string pivot_;
    SequencePlayer player_;
    sf::Color color_ = sf::Color::White;
    std::vector<ShaderEffect> shaders_;
};

/// Build an AnimatedSprite from an `*.anim.yaml` logical path, loading the
/// spritesheet it references (resolved relative to the anim file's directory).
AnimatedSprite load_animated_sprite(pac::core::ResourceCache& res, const std::string& anim_logical);

} // namespace pac::gfx
