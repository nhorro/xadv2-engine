#include "engine/pnc/story_text_scene.hpp"

#include "engine/core/diagnostics.hpp"
#include "engine/core/display.hpp"
#include "engine/core/engine_context.hpp"
#include "engine/core/resource_cache.hpp"
#include "engine/core/scene_manager.hpp"
#include "engine/core/scene_params.hpp"
#include "engine/core/text_layout.hpp" // core::draw_text_block (wrap + center pages)

#include <SFML/Graphics/Font.hpp>
#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/Window/Event.hpp>

namespace pac::pnc {

StoryTextScene::StoryTextScene(pac::core::EngineContext& ctx, const pac::core::SceneParams& params)
    : ctx_(ctx) {
    script_ = params.get_or("script", "");
    on_finish_ = params.get_or("on_finish", "");
    const std::string font_path = params.get_or("font", "");
    if (!font_path.empty()) {
        font_ = ctx_.resources.try_font(font_path);
    }
}

void StoryTextScene::enter() {
    scope_ = ctx_.scripting.open_scope();
    ctx_.scripting.set_current_scope(scope_);
    if (!script_.empty()) {
        ctx_.scripting.spawn_resource(ctx_.resources, script_);
    } else {
        ctx_.log.warn("StoryText: no 'script' parameter");
    }
    ctx_.scripting.set_current_scope(ctx_.scripting.global_scope());
}

void StoryTextScene::leave() {
    ctx_.scripting.cancel_scope(scope_);
}

void StoryTextScene::finish() {
    if (finished_) {
        return;
    }
    finished_ = true;
    ctx_.scripting.cancel_scope(scope_);
    if (on_finish_.empty()) {
        ctx_.log.warn("StoryText: no 'on_finish' outcome; quitting");
        ctx_.scenes.quit();
    } else {
        ctx_.scenes.goto_scene(on_finish_);
    }
}

void StoryTextScene::handle_event(const sf::Event& event) {
    const bool skip_key =
        event.type == sf::Event::KeyPressed &&
        (event.key.code == sf::Keyboard::Enter || event.key.code == sf::Keyboard::Space ||
         event.key.code == sf::Keyboard::Escape);
    const bool skip_click =
        event.type == sf::Event::MouseButtonReleased && event.mouseButton.button == sf::Mouse::Left;
    if (skip_key || skip_click) {
        finish();
    }
}

void StoryTextScene::update(float /*dt*/) {
    // The application loop advances the scheduler; when this scene's scope has no
    // tasks left, the script has finished.
    if (!finished_ && ctx_.scripting.active_task_count(scope_) == 0) {
        finish();
    }
}

void StoryTextScene::draw(sf::RenderTarget& target) const {
    const sf::Vector2u vres = ctx_.display.virtual_resolution();

    sf::RectangleShape bg(sf::Vector2f(static_cast<float>(vres.x), static_cast<float>(vres.y)));
    bg.setFillColor(sf::Color::Black);
    target.draw(bg);

    const std::string& page = ctx_.scripting.current_text();
    if (font_ && !page.empty()) {
        // Wrap the page within a margin of the screen and center the block — long
        // pages and explicit '\n' both lay out instead of overflowing a single line.
        constexpr float kTextWidthFraction = 0.8f;
        pac::core::TextStyle style;
        style.size = 30;
        style.color = sf::Color(225, 225, 230);
        const sf::Vector2f center(static_cast<float>(vres.x) / 2.0f,
                                  static_cast<float>(vres.y) / 2.0f);
        pac::core::draw_text_block(target,
                                   *font_,
                                   page,
                                   style,
                                   center,
                                   static_cast<float>(vres.x) * kTextWidthFraction,
                                   pac::core::HAlign::Center,
                                   pac::core::VAnchor::Center);
    }
}

} // namespace pac::pnc
