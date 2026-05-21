#include "engine/pnc/blank_scene.hpp"

#include "engine/core/display.hpp"
#include "engine/core/engine_context.hpp"
#include "engine/core/scene_manager.hpp"
#include "engine/core/scene_params.hpp"

#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/Window/Event.hpp>

namespace pac::pnc {

BlankScene::BlankScene(pac::core::EngineContext& ctx, const pac::core::SceneParams& params)
    : ctx_(ctx), color_(20, 24, 32) {
    (void) params;
}

void BlankScene::handle_event(const sf::Event& event) {
    if (event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::Escape) {
        ctx_.scenes.quit();
    }
}

void BlankScene::draw(sf::RenderTarget& target) const {
    const sf::Vector2u vres = ctx_.display.virtual_resolution();
    sf::RectangleShape bg(sf::Vector2f(static_cast<float>(vres.x), static_cast<float>(vres.y)));
    bg.setFillColor(color_);
    target.draw(bg);
}

} // namespace pac::pnc
