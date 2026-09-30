#include "engine/pnc/confirmation_scene.hpp"

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
#include <SFML/Graphics/Text.hpp>
#include <SFML/Graphics/View.hpp>
#include <SFML/Window/Event.hpp>

#include <algorithm>
#include <exception>

namespace pac::pnc {

namespace {

unsigned unsigned_param(const pac::core::SceneParams& params,
                        const std::string& key,
                        unsigned fallback,
                        unsigned low,
                        unsigned high) {
    const auto value = params.get(key);
    if (!value) {
        return fallback;
    }
    try {
        return std::clamp(static_cast<unsigned>(std::stoul(*value)), low, high);
    } catch (const std::exception&) {
        return fallback;
    }
}

void center_text(sf::Text& text, const sf::FloatRect& area) {
    const sf::FloatRect bounds = text.getLocalBounds();
    text.setPosition({area.position.x + (area.size.x - bounds.size.x) / 2.0f - bounds.position.x,
                      area.position.y + (area.size.y - bounds.size.y) / 2.0f - bounds.position.y});
}

} // namespace

ConfirmationScene::ConfirmationScene(pac::core::EngineContext& ctx,
                                     const pac::core::SceneParams& params)
    : ctx_(ctx), ui_sounds_(params) {
    opaque_ = false;
    font_size_ = unsigned_param(params, "font_size", 28, 14, 64);
    button_font_size_ = unsigned_param(params, "button_font_size", 24, 12, 48);

    const std::string font_path = params.get_or("font", "");
    if (!font_path.empty()) {
        font_ = ctx_.resources.try_font(font_path);
    }
}

void ConfirmationScene::enter() {
    selected_ = 1;
    hovered_ = -1;
}

ConfirmationScene::Layout ConfirmationScene::layout() const {
    const sf::Vector2u resolution = ctx_.display.virtual_resolution();
    const float width = std::min(620.0f, static_cast<float>(resolution.x) - 80.0f);
    const float height = 190.0f;
    const float left = (static_cast<float>(resolution.x) - width) / 2.0f;
    const float top = (static_cast<float>(resolution.y) - height) / 2.0f;
    constexpr float button_w = 150.0f;
    constexpr float button_h = 48.0f;
    constexpr float gap = 28.0f;
    const float buttons_left = left + (width - button_w * 2.0f - gap) / 2.0f;
    const float buttons_top = top + height - button_h - 24.0f;
    return {{{left, top}, {width, height}},
            {{buttons_left, buttons_top}, {button_w, button_h}},
            {{buttons_left + button_w + gap, buttons_top}, {button_w, button_h}}};
}

int ConfirmationScene::button_at(float x, float y) const {
    const Layout l = layout();
    if (l.yes.contains({x, y})) {
        return 0;
    }
    if (l.no.contains({x, y})) {
        return 1;
    }
    return -1;
}

void ConfirmationScene::activate(int button) {
    ui_sounds_.activate(ctx_);
    if (button == 0) {
        ctx_.scenes.accept_confirmation();
    } else {
        ctx_.scenes.cancel_confirmation();
    }
}

void ConfirmationScene::handle_event(const sf::Event& event) {
    if (event.is<sf::Event::MouseMoved>()) {
        const int next =
            button_at(static_cast<float>(event.getIf<sf::Event::MouseMoved>()->position.x),
                      static_cast<float>(event.getIf<sf::Event::MouseMoved>()->position.y));
        if (next >= 0 && next != hovered_) {
            ui_sounds_.selection(ctx_);
            selected_ = next;
        }
        hovered_ = next;
        return;
    }
    if (event.is<sf::Event::MouseButtonReleased>() &&
        event.getIf<sf::Event::MouseButtonReleased>()->button == sf::Mouse::Button::Left) {
        const int button = button_at(
            static_cast<float>(event.getIf<sf::Event::MouseButtonReleased>()->position.x),
            static_cast<float>(event.getIf<sf::Event::MouseButtonReleased>()->position.y));
        if (button >= 0) {
            activate(button);
        }
        return;
    }
    if (!event.is<sf::Event::KeyPressed>()) {
        return;
    }
    if (event.getIf<sf::Event::KeyPressed>()->code == sf::Keyboard::Key::Escape) {
        activate(1);
    } else if (event.getIf<sf::Event::KeyPressed>()->code == sf::Keyboard::Key::Left ||
               event.getIf<sf::Event::KeyPressed>()->code == sf::Keyboard::Key::Right) {
        selected_ = 1 - selected_;
        hovered_ = -1;
        ui_sounds_.selection(ctx_);
    } else if (event.getIf<sf::Event::KeyPressed>()->code == sf::Keyboard::Key::Enter ||
               event.getIf<sf::Event::KeyPressed>()->code == sf::Keyboard::Key::Space) {
        activate(selected_);
    }
}

void ConfirmationScene::update(float dt) {
    (void) dt;
}

void ConfirmationScene::draw(sf::RenderTarget& target) const {
    const sf::View previous = target.getView();
    target.setView(ctx_.display.view());
    const sf::Vector2u resolution = ctx_.display.virtual_resolution();
    const Layout l = layout();

    sf::RectangleShape dim({static_cast<float>(resolution.x), static_cast<float>(resolution.y)});
    dim.setFillColor(sf::Color(0, 0, 0, 170));
    target.draw(dim);

    sf::RectangleShape panel({l.panel.size.x, l.panel.size.y});
    panel.setPosition({l.panel.position.x, l.panel.position.y});
    panel.setFillColor(sf::Color(13, 11, 9, 242));
    panel.setOutlineColor(sf::Color(181, 139, 64, 220));
    panel.setOutlineThickness(1.5f);
    target.draw(panel);

    const auto draw_button = [&](const sf::FloatRect& rect, bool hot, const std::string& label) {
        sf::RectangleShape button({rect.size.x, rect.size.y});
        button.setPosition({rect.position.x, rect.position.y});
        button.setFillColor(hot ? sf::Color(181, 139, 64, 90) : sf::Color(26, 22, 18, 235));
        button.setOutlineColor(hot ? sf::Color(245, 224, 177) : sf::Color(125, 102, 67));
        button.setOutlineThickness(hot ? 2.0f : 1.0f);
        target.draw(button);
        if (font_) {
            sf::Text text(*font_, pac::core::utf8(label), button_font_size_);
            text.setFillColor(hot ? sf::Color(255, 242, 207) : sf::Color(220, 207, 177));
            center_text(text, rect);
            target.draw(text);
        }
    };

    const bool yes_hot = selected_ == 0;
    const bool no_hot = selected_ == 1;
    draw_button(l.yes, yes_hot, ctx_.strings.ui_label("confirm_yes"));
    draw_button(l.no, no_hot, ctx_.strings.ui_label("confirm_no"));

    if (font_) {
        const bool quitting = ctx_.scenes.confirmation_action() ==
                              pac::core::SceneManager::ConfirmationAction::QUIT_APPLICATION;
        const std::string message =
            ctx_.strings.ui_label(quitting ? "confirm_quit_message" : "confirm_title_message");
        sf::Text text(*font_, pac::core::utf8(message), font_size_);
        text.setFillColor(sf::Color(245, 224, 177));
        text.setOutlineColor(sf::Color(22, 17, 12));
        text.setOutlineThickness(1.25f);
        const sf::FloatRect message_area{{l.panel.position.x + 28.0f, l.panel.position.y + 22.0f},
                                         {l.panel.size.x - 56.0f, 72.0f}};
        center_text(text, message_area);
        target.draw(text);
    }

    target.setView(previous);
}

} // namespace pac::pnc
