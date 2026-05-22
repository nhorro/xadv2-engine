#pragma once

#include "engine/gfx/animation.hpp"
#include "engine/gfx/sequence_player.hpp"
#include "engine/gfx/spritesheet.hpp"

#include <SFML/Graphics/Color.hpp>
#include <SFML/Graphics/Drawable.hpp>
#include <SFML/Graphics/Transformable.hpp>

#include <functional>
#include <string>

namespace pac::core {
class ResourceCache;
}

namespace pac::gfx {

/// An animated sprite: a spritesheet + animation driven by a SequencePlayer.
/// Inherits sf::Transformable, so setPosition places the current frame's pivot
/// anchor at that point (feet/hands stay stable across frames).
class AnimatedSprite : public sf::Drawable, public sf::Transformable {
public:
    AnimatedSprite(Spritesheet sheet, Animation anim);

    void play(const std::string& sequence, bool restart = true) { player_.play(sequence, restart); }
    void update(float dt) { player_.update(dt); }
    bool has(const std::string& sequence) const { return player_.has(sequence); }
    bool finished() const { return player_.finished(); }
    void set_on_finished(std::function<void()> cb) { player_.set_on_finished(std::move(cb)); }
    const std::string& current_sequence() const { return player_.current_sequence(); }

    void set_color(sf::Color color) { color_ = color; }

    void draw(sf::RenderTarget& target, sf::RenderStates states) const override;

private:
    Spritesheet sheet_;
    std::string pivot_;
    SequencePlayer player_;
    sf::Color color_ = sf::Color::White;
};

/// Build an AnimatedSprite from an `*.anim.yaml` logical path, loading the
/// spritesheet it references (resolved relative to the anim file's directory).
AnimatedSprite load_animated_sprite(pac::core::ResourceCache& res, const std::string& anim_logical);

} // namespace pac::gfx
