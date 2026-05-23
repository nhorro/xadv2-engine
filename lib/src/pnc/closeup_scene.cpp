#include "engine/pnc/closeup_scene.hpp"

#include "engine/core/cursor.hpp"
#include "engine/core/diagnostics.hpp"
#include "engine/core/display.hpp"
#include "engine/core/engine_context.hpp"
#include "engine/core/resource_cache.hpp"
#include "engine/core/scene_manager.hpp"
#include "engine/core/scene_params.hpp"
#include "engine/core/strings.hpp"
#include "engine/core/text_encoding.hpp"

#include <SFML/Graphics/Font.hpp>
#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/Graphics/Sprite.hpp>
#include <SFML/Graphics/Text.hpp>
#include <SFML/Graphics/Texture.hpp>
#include <SFML/Window/Event.hpp>

#include <algorithm>
#include <exception>

namespace pac::pnc {

namespace {
const sf::Color kCaptionColor(235, 235, 240);
} // namespace

CloseUpScene::CloseUpScene(pac::core::EngineContext& ctx, const pac::core::SceneParams& params)
    : ctx_(ctx) {
    data_path_ = params.get_or("data", "");
    on_exit_ = params.get_or("on_exit", "");
    const std::string font_path = params.get_or("font", "");
    if (!font_path.empty()) {
        font_ = ctx_.resources.try_font(font_path);
    }
    // A close-up is a HUD-like full-screen view that fully covers the scene it is
    // pushed over, so it stays opaque (the default) and the manager skips drawing
    // the room beneath it.
}

void CloseUpScene::enter() {
    if (data_path_.empty()) {
        ctx_.log.error("CloseUp: no 'data' parameter");
        return;
    }
    try {
        data_ = parse_closeup(ctx_.resources.read_text(data_path_));
        loaded_ = true;
    } catch (const std::exception& e) {
        ctx_.log.error(std::string("CloseUp: ") + e.what());
    }
}

void CloseUpScene::exit() {
    if (on_exit_.empty()) {
        ctx_.scenes.pop_scene(); // back to the scene we overlaid
    } else {
        ctx_.scenes.goto_scene(on_exit_);
    }
}

void CloseUpScene::activate(const CloseUpHotspot& hs) {
    if (!hs.goto_scene.empty()) {
        ctx_.scenes.goto_scene(hs.goto_scene);
        return;
    }
    // The single look action: show the hotspot's name as a caption at the cursor.
    float duration = 0.5f + 0.06f * static_cast<float>(hs.name.size());
    duration = std::clamp(duration, 1.0f, 7.0f);
    speech_.show(hs.name, hover_, kCaptionColor, duration);
}

void CloseUpScene::handle_event(const sf::Event& event) {
    if (event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::Escape) {
        exit();
        return;
    }
    if (event.type == sf::Event::MouseMoved) {
        hover_ = {static_cast<float>(event.mouseMove.x), static_cast<float>(event.mouseMove.y)};
        return;
    }
    if (event.type == sf::Event::MouseButtonReleased) {
        if (event.mouseButton.button == sf::Mouse::Right) {
            exit();
            return;
        }
        if (event.mouseButton.button == sf::Mouse::Left) {
            // A click dismisses a showing caption first (like in-room speech),
            // otherwise it acts on the hotspot under the cursor.
            if (speech_.active()) {
                speech_.skip();
                return;
            }
            if (loaded_) {
                if (const CloseUpHotspot* hs = data_.hotspot_at(hover_)) {
                    activate(*hs);
                }
            }
        }
    }
}

void CloseUpScene::update(float dt) {
    speech_.update(dt);
    hovered_ = loaded_ ? data_.hotspot_at(hover_) : nullptr;
    if (hovered_ != nullptr) {
        ctx_.cursor.want(pac::core::CursorKind::INTERACT);
    }
}

void CloseUpScene::draw(sf::RenderTarget& target) const {
    const sf::Vector2u vres = ctx_.display.virtual_resolution();
    const auto vw = static_cast<float>(vres.x);
    const auto vh = static_cast<float>(vres.y);

    sf::RectangleShape fill(sf::Vector2f(vw, vh));
    fill.setFillColor(data_.background_color);
    target.draw(fill);

    if (loaded_ && !data_.background.empty()) {
        try {
            const sf::Texture& tex = ctx_.resources.texture(data_.background);
            sf::Sprite sprite(tex);
            const sf::Vector2u ts = tex.getSize();
            if (ts.x > 0 && ts.y > 0) {
                sprite.setScale(vw / static_cast<float>(ts.x), vh / static_cast<float>(ts.y));
            }
            target.draw(sprite);
        } catch (const std::exception& e) {
            ctx_.log.error(e.what());
        }
    }

    if (font_ != nullptr) {
        // Hover affordance: the examined thing's name across the top, unless a
        // caption is already showing there.
        if (hovered_ != nullptr && !speech_.active()) {
            sf::Text label(pac::core::utf8(hovered_->name), *font_, 24);
            label.setFillColor(kCaptionColor);
            label.setOutlineColor(sf::Color(0, 0, 0, 200));
            label.setOutlineThickness(2.0f);
            const sf::FloatRect b = label.getLocalBounds();
            label.setPosition((vw - b.width) / 2.0f - b.left, vh * 0.06f);
            target.draw(label);
        }

        sf::Text hint(pac::core::utf8("[Esc] " + ctx_.strings.ui_label("back")), *font_, 18);
        hint.setFillColor(sf::Color(200, 200, 210, 180));
        hint.setOutlineColor(sf::Color(0, 0, 0, 180));
        hint.setOutlineThickness(1.0f);
        hint.setPosition(16.0f, vh - 32.0f);
        target.draw(hint);
    }

    speech_.draw(target, font_);
}

} // namespace pac::pnc
