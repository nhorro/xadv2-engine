#include "engine/gfx/visual_sprite.hpp"

#include <utility>

namespace pac::gfx {

namespace {

bool is_composite_path(const std::string& logical) {
    return logical.ends_with(".composite.yml") || logical.ends_with(".composite.yaml");
}

} // namespace

void VisualSprite::play(const std::string& sequence, bool restart) {
    std::visit([&](auto& sprite) { sprite.play(sequence, restart); }, value_);
}

void VisualSprite::update(float dt) {
    std::visit([&](auto& sprite) { sprite.update(dt); }, value_);
}

bool VisualSprite::has(const std::string& sequence) const {
    return std::visit([&](const auto& sprite) { return sprite.has(sequence); }, value_);
}

bool VisualSprite::finished() const {
    return std::visit([](const auto& sprite) { return sprite.finished(); }, value_);
}

const std::string& VisualSprite::current_sequence() const {
    return std::visit(
        [](const auto& sprite) -> const std::string& { return sprite.current_sequence(); },
        value_);
}

void VisualSprite::setPosition(float x, float y) {
    std::visit([&](auto& sprite) { sprite.setPosition(x, y); }, value_);
}

void VisualSprite::setScale(float x, float y) {
    std::visit([&](auto& sprite) { sprite.setScale(x, y); }, value_);
}

void VisualSprite::setRotation(float degrees) {
    std::visit([&](auto& sprite) { sprite.setRotation(degrees); }, value_);
}

void VisualSprite::set_shaders(std::vector<ShaderEffect> shaders) {
    std::visit([&](auto& sprite) { sprite.set_shaders(shaders); }, value_);
}

sf::FloatRect VisualSprite::global_bounds() const {
    return std::visit([](const auto& sprite) { return sprite.global_bounds(); }, value_);
}

std::optional<sf::Vector2f> VisualSprite::anchor_world(const std::string& name) const {
    return std::visit([&](const auto& sprite) { return sprite.anchor_world(name); }, value_);
}

void VisualSprite::draw(sf::RenderTarget& target,
                        pac::core::ResourceCache& resources,
                        float time,
                        ShaderChain* chain) const {
    std::visit([&](const auto& sprite) { sprite.draw(target, resources, time, chain); }, value_);
}

VisualSprite load_visual_sprite(pac::core::ResourceCache& resources, const std::string& logical) {
    if (is_composite_path(logical)) {
        return VisualSprite(load_composite_sprite(resources, logical));
    }
    return VisualSprite(load_animated_sprite(resources, logical));
}

} // namespace pac::gfx
