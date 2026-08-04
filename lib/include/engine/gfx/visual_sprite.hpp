#pragma once

#include "engine/gfx/animated_sprite.hpp"
#include "engine/gfx/composite_sprite.hpp"

#include <variant>

namespace pac::gfx {

/// Room-object visual with one stable surface for a single AnimatedSprite or a
/// CompositeSprite. RoomRuntime and Lua do not need type-specific branches.
class VisualSprite {
public:
    explicit VisualSprite(AnimatedSprite sprite) : value_(std::move(sprite)) {}
    explicit VisualSprite(CompositeSprite sprite) : value_(std::move(sprite)) {}

    void play(const std::string& sequence, bool restart = true);
    void update(float dt);
    [[nodiscard]] bool has(const std::string& sequence) const;
    [[nodiscard]] bool finished() const;
    [[nodiscard]] const std::string& current_sequence() const;

    void setPosition(float x, float y);
    void setScale(float x, float y);
    void setRotation(float degrees);
    void set_shaders(std::vector<ShaderEffect> shaders);

    [[nodiscard]] sf::FloatRect global_bounds() const;
    [[nodiscard]] std::optional<sf::Vector2f> anchor_world(const std::string& name) const;
    void draw(sf::RenderTarget& target,
              pac::core::ResourceCache& resources,
              float time,
              ShaderChain* chain) const;

private:
    std::variant<AnimatedSprite, CompositeSprite> value_;
};

/// A `.composite.yml` / `.composite.yaml` path loads a CompositeSprite; every
/// other YAML visual keeps the established AnimatedSprite loader behavior.
VisualSprite load_visual_sprite(pac::core::ResourceCache& resources, const std::string& logical);

} // namespace pac::gfx
