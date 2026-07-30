#include "engine/gfx/animated_sprite.hpp"

#include "engine/core/resource_cache.hpp"
#include "engine/core/resource_source.hpp"
#include "engine/gfx/shader_chain.hpp"

#include <SFML/Graphics/Glsl.hpp>
#include <SFML/Graphics/RenderStates.hpp>
#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/Graphics/Shader.hpp>
#include <SFML/Graphics/Sprite.hpp>
#include <SFML/Graphics/Texture.hpp>

#include <utility>

namespace pac::gfx {

namespace {

// Build the sf::Sprite for the current frame: textureRect from the atlas, origin
// at the named pivot, tint color. The caller then applies the transform.
bool build_current_sprite(const Spritesheet& sheet,
                          const SequencePlayer& player,
                          const std::string& pivot,
                          sf::Color color,
                          sf::Sprite& out_sprite,
                          sf::IntRect& out_rect,
                          sf::Vector2f& out_origin) {
    const std::string frame_id = player.current_frame_id();
    if (frame_id.empty()) {
        return false;
    }
    const Frame* frame = sheet.frame(frame_id);
    if (!frame) {
        return false;
    }
    out_sprite.setTexture(sheet.texture(), false);
    out_sprite.setTextureRect(frame->rect);
    out_rect = frame->rect;
    out_origin = sf::Vector2f(0.0f, 0.0f);
    if (const sf::Vector2f* p = frame->anchor(pivot)) {
        out_sprite.setOrigin(*p);
        out_origin = *p;
    }
    if (player.current_h_mirror()) {
        out_sprite.setScale(-1.0f, 1.0f);
    }
    out_sprite.setColor(color);
    return true;
}

} // namespace

AnimatedSprite::AnimatedSprite(Spritesheet sheet, Animation anim)
    : sheet_(std::move(sheet)), pivot_(anim.pivot), player_(std::move(anim)) {}

void AnimatedSprite::draw(sf::RenderTarget& target, sf::RenderStates states) const {
    sf::Sprite sprite;
    sf::IntRect rect;
    sf::Vector2f origin;
    if (!build_current_sprite(sheet_, player_, pivot_, color_, sprite, rect, origin)) {
        return;
    }
    states.transform *= getTransform();
    target.draw(sprite, states);
}

void AnimatedSprite::draw(sf::RenderTarget& target,
                          pac::core::ResourceCache& resources,
                          float time,
                          ShaderChain* chain) const {
    sf::Sprite sprite;
    sf::IntRect rect;
    sf::Vector2f origin;
    if (!build_current_sprite(sheet_, player_, pivot_, color_, sprite, rect, origin)) {
        return;
    }

    // Count effects we can apply right now (enabled, no controller). A controller
    // is design-for and the drawable falls back to unshaded for it.
    std::size_t applicable = 0;
    const ShaderEffect* single = nullptr;
    for (const ShaderEffect& fx : shaders_) {
        if (fx.enabled && fx.controller.empty()) {
            ++applicable;
            if (!single) {
                single = &fx;
            }
        }
    }

    sf::RenderStates states;
    states.transform *= getTransform();

    if (applicable <= 1) {
        if (applicable == 1) {
            pac::core::ShaderProgram* program = resources.shader(single->source);
            if (program) {
                if (program->uses_time) {
                    program->shader.setUniform("u_time", time);
                }
                if (program->uses_resolution) {
                    program->shader.setUniform("u_resolution",
                                               sf::Glsl::Vec2(static_cast<float>(rect.width),
                                                              static_cast<float>(rect.height)));
                }
                if (program->uses_texture) {
                    program->shader.setUniform("texture", sf::Shader::CurrentTexture);
                }
                apply_shader_params(program->shader, single->params);
                states.shader = &program->shader;
            }
        }
        target.draw(sprite, states);
        return;
    }

    // Multi-pass: bake the current frame into the chain's RT, then blit the
    // result with the same origin/transform so the avatar's pivot still lands at
    // the avatar's world position.
    if (!chain) {
        target.draw(sprite, states);
        return;
    }
    const sf::Texture* result = chain->apply(resources, sheet_.texture(), rect, shaders_, time);
    if (!result) {
        target.draw(sprite, states);
        return;
    }
    sf::Sprite blit(*result, sf::IntRect(0, 0, rect.width, rect.height));
    blit.setOrigin(origin);
    if (player_.current_h_mirror()) {
        blit.setScale(-1.0f, 1.0f);
    }
    blit.setColor(color_);
    target.draw(blit, states);
}

sf::FloatRect AnimatedSprite::global_bounds() const {
    const std::string frame_id = player_.current_frame_id();
    const Frame* frame = frame_id.empty() ? nullptr : sheet_.frame(frame_id);
    if (!frame) {
        const sf::Vector2f p = getPosition();
        return sf::FloatRect(p.x, p.y, 0.0f, 0.0f);
    }
    // The frame is drawn with its pivot anchor at the origin (see draw()), so in
    // local space it spans [-pivot, size - pivot). getTransform() then applies
    // position + scale to give world-space bounds.
    sf::Vector2f pivot(0.0f, 0.0f);
    if (const sf::Vector2f* p = frame->anchor(pivot_)) {
        pivot = *p;
    }
    const float left =
        player_.current_h_mirror() ? pivot.x - static_cast<float>(frame->rect.width) : -pivot.x;
    const sf::FloatRect local(left,
                              -pivot.y,
                              static_cast<float>(frame->rect.width),
                              static_cast<float>(frame->rect.height));
    return getTransform().transformRect(local);
}

std::optional<sf::Vector2f> AnimatedSprite::anchor_world(const std::string& name) const {
    const std::string frame_id = player_.current_frame_id();
    const Frame* frame = frame_id.empty() ? nullptr : sheet_.frame(frame_id);
    if (!frame) {
        return std::nullopt;
    }
    const sf::Vector2f* a = frame->anchor(name);
    if (!a) {
        return std::nullopt;
    }
    // Anchors are frame-local (top-left origin); the sprite is drawn with its
    // pivot at the transform position (origin = pivot), so subtract the pivot
    // before applying the transform — same convention as global_bounds().
    sf::Vector2f pivot(0.0f, 0.0f);
    if (const sf::Vector2f* p = frame->anchor(pivot_)) {
        pivot = *p;
    }
    const float local_x = player_.current_h_mirror() ? pivot.x - a->x : a->x - pivot.x;
    return getTransform().transformPoint(local_x, a->y - pivot.y);
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
