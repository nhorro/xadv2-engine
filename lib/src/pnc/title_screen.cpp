#include "engine/pnc/title_screen.hpp"

#include "engine/core/diagnostics.hpp"
#include "engine/core/display.hpp"
#include "engine/core/engine_context.hpp"
#include "engine/core/resource_cache.hpp"
#include "engine/core/save_service.hpp"
#include "engine/core/scene_manager.hpp"
#include "engine/core/scene_params.hpp"
#include "engine/core/strings.hpp"

#include <SFML/Graphics/Font.hpp>
#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/Graphics/Text.hpp>
#include <SFML/Window/Event.hpp>

namespace pac::pnc {

namespace {

// Menu entry geometry in virtual space.
constexpr float kEntryWidth = 360.0f;
constexpr float kEntryHeight = 56.0f;
constexpr float kEntryGap = 16.0f;
constexpr unsigned kTextSize = 28;

sf::Vector2f entry_top_left(const sf::Vector2u& vres, int index, int count) {
    const float block_h = count * kEntryHeight + (count - 1) * kEntryGap;
    const float x = (static_cast<float>(vres.x) - kEntryWidth) / 2.0f;
    const float y0 = (static_cast<float>(vres.y) - block_h) / 2.0f;
    return {x, y0 + index * (kEntryHeight + kEntryGap)};
}

} // namespace

TitleScreen::TitleScreen(pac::core::EngineContext& ctx, const pac::core::SceneParams& params)
    : ctx_(ctx) {
    new_game_target_ = params.get_or("new_game", "");
    continue_target_ = params.get_or("continue", "");
    exit_target_ = params.get_or("exit", "QUIT");

    rebuild_entries();

    const std::string font_path = params.get_or("font", "");
    if (!font_path.empty()) {
        font_ = ctx_.resources.try_font(font_path);
        if (!font_) {
            ctx_.log.warn("title: no font '" + font_path + "'; drawing labels as bars");
        }
    }
}

void TitleScreen::enter() {
    // Every time we return to the title (e.g. quit-to-title), re-check whether
    // a save exists so Continue shows/hides accordingly.
    rebuild_entries();
    hovered_ = -1;
}

void TitleScreen::rebuild_entries() {
    entries_.clear();
    entries_.push_back({ctx_.strings.ui_label("new_game"), Action::NEW_GAME});
    if (!continue_target_.empty() && ctx_.saves.latest_slot().has_value()) {
        entries_.push_back({ctx_.strings.ui_label("continue"), Action::CONTINUE});
    }
    entries_.push_back({ctx_.strings.ui_label("settings"), Action::SETTINGS});
    entries_.push_back({ctx_.strings.ui_label("quit"), Action::EXIT});
}

int TitleScreen::entry_at(float vx, float vy) const {
    const sf::Vector2u vres = ctx_.display.virtual_resolution();
    const int count = static_cast<int>(entries_.size());
    for (int i = 0; i < count; ++i) {
        const sf::Vector2f tl = entry_top_left(vres, i, count);
        if (vx >= tl.x && vx <= tl.x + kEntryWidth && vy >= tl.y && vy <= tl.y + kEntryHeight) {
            return i;
        }
    }
    return -1;
}

void TitleScreen::trigger(Action action) {
    switch (action) {
    case Action::NEW_GAME:
        if (new_game_target_.empty()) {
            ctx_.log.warn("title: 'new_game' outcome is not wired in the manifest");
        } else {
            ctx_.scenes.goto_scene(new_game_target_);
        }
        break;
    case Action::CONTINUE: {
        const auto slot = ctx_.saves.latest_slot();
        if (!slot) {
            ctx_.log.warn("title: 'Continue' clicked with no save on disk");
            break;
        }
        auto state = ctx_.saves.load(*slot);
        if (!state) {
            // load() already logged. The save is unreadable; surface a clear
            // user-facing fallback by simply staying on the title.
            break;
        }
        ctx_.saves.stage_restore(std::move(*state));
        ctx_.scenes.goto_scene(continue_target_);
        break;
    }
    case Action::SETTINGS:
        ctx_.scenes.open_settings();
        break;
    case Action::EXIT:
        ctx_.scenes.goto_scene(exit_target_);
        break;
    }
}

void TitleScreen::handle_event(const sf::Event& event) {
    if (event.type == sf::Event::MouseMoved) {
        hovered_ =
            entry_at(static_cast<float>(event.mouseMove.x), static_cast<float>(event.mouseMove.y));
    } else if (event.type == sf::Event::MouseButtonReleased &&
               event.mouseButton.button == sf::Mouse::Left) {
        const int idx = entry_at(static_cast<float>(event.mouseButton.x),
                                 static_cast<float>(event.mouseButton.y));
        if (idx >= 0) {
            trigger(entries_[static_cast<std::size_t>(idx)].action);
        }
    } else if (event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::Escape) {
        trigger(Action::EXIT);
    }
}

void TitleScreen::draw(sf::RenderTarget& target) const {
    const sf::Vector2u vres = ctx_.display.virtual_resolution();

    sf::RectangleShape bg(sf::Vector2f(static_cast<float>(vres.x), static_cast<float>(vres.y)));
    bg.setFillColor(sf::Color(16, 18, 28));
    target.draw(bg);

    const int count = static_cast<int>(entries_.size());
    for (int i = 0; i < count; ++i) {
        const sf::Vector2f tl = entry_top_left(vres, i, count);
        const bool hot = (i == hovered_);

        sf::RectangleShape box(sf::Vector2f(kEntryWidth, kEntryHeight));
        box.setPosition(tl);
        box.setFillColor(hot ? sf::Color(70, 90, 140) : sf::Color(40, 46, 66));
        box.setOutlineThickness(2.0f);
        box.setOutlineColor(sf::Color(90, 100, 130));
        target.draw(box);

        if (font_) {
            sf::Text text(entries_[static_cast<std::size_t>(i)].label, *font_, kTextSize);
            text.setFillColor(hot ? sf::Color::White : sf::Color(210, 215, 230));
            const sf::FloatRect b = text.getLocalBounds();
            text.setPosition(tl.x + (kEntryWidth - b.width) / 2.0f - b.left,
                             tl.y + (kEntryHeight - b.height) / 2.0f - b.top);
            target.draw(text);
        }
    }
}

} // namespace pac::pnc
