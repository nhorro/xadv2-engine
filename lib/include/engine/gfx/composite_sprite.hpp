#pragma once

#include "engine/gfx/animated_sprite.hpp"

#include <SFML/Graphics/Transform.hpp>
#include <SFML/Graphics/Transformable.hpp>
#include <SFML/System/Vector2.hpp>

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace pac::core {
class ResourceCache;
}

namespace pac::gfx {

struct CompositeNodeDefinition {
    std::string id;
    std::string parent;
    std::string animation;
    std::string parent_anchor;
    std::string child_anchor;
    sf::Vector2f offset{0.0f, 0.0f};
    float scale = 1.0f;
    float rotation = 0.0f;
    int z = 0;
};

struct CompositeRotationTrack {
    float from = 0.0f;
    float to = 0.0f;
    float duration = 0.0f;
    bool loop = false;
};

struct CompositePartSequence {
    std::string sequence;
    std::optional<CompositeRotationTrack> rotation;
};

struct CompositeSequence {
    std::map<std::string, CompositePartSequence> parts;
};

/// Parsed, texture-free representation of a `*.composite.yml` hierarchy.
/// `nodes` is topologically ordered with the root first, so every parent appears
/// before its children. The parser rejects duplicate ids, unknown parents and
/// cycles before any GPU resource is loaded.
struct CompositeDefinition {
    std::vector<CompositeNodeDefinition> nodes;
    std::map<std::string, CompositeSequence> sequences;
};

CompositeDefinition parse_composite(const std::string& yaml_text);

/// A hierarchy of independently animated sprites. The object-level transform is
/// inherited by every node; child transforms are attached through frame anchors.
/// Each part is an AnimatedSprite: its setRotation and setScale are applied
/// around the attachment (parent anchor, child anchor). High-level sequences can
/// select each part's frame sequence and run a simple rotation track, which
/// covers wheels/propellers without a general timeline. A live set_part_rotation
/// cancels that track so the next update does not overwrite the angle.
class CompositeSprite : public sf::Transformable {
public:
    CompositeSprite(CompositeDefinition definition, std::vector<AnimatedSprite> sprites);

    void play(const std::string& sequence, bool restart = true);
    void update(float dt);
    [[nodiscard]] bool has(const std::string& sequence) const;
    [[nodiscard]] bool finished() const { return finished_; }
    [[nodiscard]] const std::string& current_sequence() const { return current_sequence_; }

    void set_shaders(std::vector<ShaderEffect> shaders);
    [[nodiscard]] sf::FloatRect global_bounds() const;
    [[nodiscard]] std::optional<sf::Vector2f> anchor_world(const std::string& name) const;

    /// Set a part's rotation in degrees around its attachment. Cancels a playing
    /// rotation track on that part. Returns false if `id` is unknown.
    bool set_part_rotation(const std::string& id, float degrees);

    /// Set a part's scale around the same attachment. (1, 1) is the authored
    /// size. Returns false if `id` is unknown or either component is not finite.
    bool set_part_scale(const std::string& id, float scale_x, float scale_y);

    void draw(sf::RenderTarget& target,
              pac::core::ResourceCache& resources,
              float time,
              ShaderChain* chain) const;

private:
    struct RotationPlayback {
        CompositeRotationTrack track;
        float elapsed = 0.0f;
        bool finished = false;
    };
    struct Node {
        CompositeNodeDefinition definition;
        std::size_t parent = 0;
        AnimatedSprite sprite;
        bool sequence_tracked = false;
        std::optional<RotationPlayback> rotation_track;
    };

    [[nodiscard]] std::vector<sf::Transform> node_transforms() const;
    [[nodiscard]] std::optional<std::size_t> node_index(const std::string& id) const;
    void refresh_finished();

    std::vector<Node> nodes_;
    std::map<std::string, CompositeSequence> sequences_;
    std::string current_sequence_;
    bool finished_ = true;
};

CompositeSprite load_composite_sprite(pac::core::ResourceCache& resources,
                                      const std::string& logical);

} // namespace pac::gfx
