#include "engine/gfx/animated_sprite.hpp"

#include "engine/core/resource_cache.hpp"
#include "engine/core/resource_source.hpp"

#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/Graphics/Sprite.hpp>

#include <utility>

namespace pac::gfx {

AnimatedSprite::AnimatedSprite(Spritesheet sheet, Animation anim)
    : sheet_(std::move(sheet)), pivot_(anim.pivot), player_(std::move(anim)) {}

void AnimatedSprite::draw(sf::RenderTarget& target, sf::RenderStates states) const {
    const std::string frame_id = player_.current_frame_id();
    if (frame_id.empty()) {
        return;
    }
    const Frame* frame = sheet_.frame(frame_id);
    if (!frame) {
        return;
    }

    sf::Sprite sprite(sheet_.texture());
    sprite.setTextureRect(frame->rect);
    if (const sf::Vector2f* pivot = frame->anchor(pivot_)) {
        sprite.setOrigin(*pivot); // place pivot at this sprite's position
    }
    sprite.setColor(color_);

    states.transform *= getTransform();
    target.draw(sprite, states);
}

AnimatedSprite load_animated_sprite(pac::core::ResourceCache& res,
                                    const std::string& anim_logical) {
    Animation anim = parse_animation(res.read_text(anim_logical));
    const std::string sheet_logical =
        pac::core::logical_join(pac::core::logical_dir(anim_logical), anim.spritesheet);
    Spritesheet sheet = load_spritesheet(res, sheet_logical);
    return AnimatedSprite(std::move(sheet), std::move(anim));
}

} // namespace pac::gfx
