#include "engine/pnc/title_screen.hpp"

#include "engine/core/audio.hpp"
#include "engine/core/cursor.hpp"
#include "engine/core/diagnostics.hpp"
#include "engine/core/display.hpp"
#include "engine/core/engine_context.hpp"
#include "engine/core/resource_cache.hpp"
#include "engine/core/save_service.hpp"
#include "engine/core/scene_manager.hpp"
#include "engine/core/scene_params.hpp"
#include "engine/core/state_store.hpp"
#include "engine/core/strings.hpp"
#include "engine/core/text_encoding.hpp"

#include <SFML/Graphics/Font.hpp>
#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/Graphics/Sprite.hpp>
#include <SFML/Graphics/Text.hpp>
#include <SFML/Graphics/Texture.hpp>
#include <SFML/Window/Event.hpp>

#include <exception>

namespace pac::pnc {

namespace {

// Vertical spacing between menu entries as a multiple of the font size.
constexpr float kLineSpacing = 1.6f;
// Horizontal padding added around an entry's text for mouse hit-testing.
constexpr float kHitPadX = 16.0f;
// Hit width used for entries when no font is available to measure labels.
constexpr float kNoFontHitWidth = 240.0f;

float parse_fraction(const pac::core::SceneParams& params, const std::string& key, float fallback) {
    const auto v = params.get(key);
    if (!v) {
        return fallback;
    }
    try {
        return std::stof(*v);
    } catch (const std::exception&) {
        return fallback;
    }
}

} // namespace

TitleScreen::TitleScreen(pac::core::EngineContext& ctx, const pac::core::SceneParams& params)
    : ctx_(ctx) {
    background_path_ = params.get_or("background", "");
    music_path_ = params.get_or("music", "");
    new_game_target_ = params.get_or("menu.options.new_game", "");
    continue_target_ = params.get_or("menu.options.continue", "");
    load_game_target_ = params.get_or("menu.options.load_game", "");
    exit_target_ = params.get_or("menu.options.exit", "QUIT");

    const std::string continue_fallback = params.get_or("menu.continue_fallback", "");
    continue_starts_new_game_ = continue_fallback == "new_game";
    if (!continue_fallback.empty() && !continue_starts_new_game_) {
        ctx_.log.warn("title: menu.continue_fallback must be 'new_game' (got '" +
                      continue_fallback + "'); ignoring");
    }

    menu_anchor_.x = parse_fraction(params, "menu.position.x", 0.5f);
    menu_anchor_.y = parse_fraction(params, "menu.position.y", 0.5f);

    if (const auto fs = params.get("font_size")) {
        try {
            font_size_ = static_cast<unsigned>(std::stoul(*fs));
        } catch (const std::exception&) {
            ctx_.log.warn("title: invalid font_size '" + *fs + "'; using default");
        }
    }

    const std::string font_path = params.get_or("font", "");
    if (!font_path.empty()) {
        font_ = ctx_.resources.try_font(font_path);
        if (!font_) {
            ctx_.log.warn("title: no font '" + font_path + "'; menu labels will not be drawn");
        }
    }

    rebuild_entries();
}

void TitleScreen::enter() {
    // Every time we return to the title (e.g. quit-to-title), re-check whether
    // a save exists so Continue shows/hides accordingly.
    rebuild_entries();
    hovered_ = -1;
    if (!music_path_.empty()) {
        ctx_.audio.music.play(music_path_, /*loop=*/true);
    }
}

void TitleScreen::leave() {
    if (!music_path_.empty()) {
        ctx_.audio.music.stop();
    }
}

void TitleScreen::rebuild_entries() {
    entries_.clear();
    entries_.push_back({ctx_.strings.ui_label("new_game"), Action::NEW_GAME, 0.0f});
    // Continue resumes the most recent save across the autosave and the manual
    // slots. With `menu.continue_fallback: new_game` it is always listed and falls
    // through to a new game when nothing is saved yet.
    if (!continue_target_.empty() &&
        (ctx_.saves.latest_slot().has_value() || continue_starts_new_game_)) {
        entries_.push_back({ctx_.strings.ui_label("continue"), Action::CONTINUE, 0.0f});
    }
    // Optional "Load game" entry (#108) — shows when the manifest wires it
    // (`menu.options.load_game`) and a save exists. The action pushes the load
    // scene via SceneManager::open_load(); `load_game_target_` is reserved for
    // a future opt-out routing override and otherwise unused.
    if (!load_game_target_.empty() && !ctx_.scenes.load_scene_id().empty() &&
        ctx_.saves.latest_slot().has_value()) {
        entries_.push_back({ctx_.strings.ui_label("load_game"), Action::LOAD_GAME, 0.0f});
    }
    entries_.push_back({ctx_.strings.ui_label("settings"), Action::SETTINGS, 0.0f});
    entries_.push_back({ctx_.strings.ui_label("quit_to_os"), Action::EXIT, 0.0f});

    if (font_ != nullptr) {
        for (Entry& e : entries_) {
            sf::Text text(pac::core::utf8(e.label), *font_, font_size_);
            e.width = text.getLocalBounds().width;
        }
    }
}

sf::Vector2f TitleScreen::entry_center(int index, int count) const {
    const sf::Vector2u vres = ctx_.display.virtual_resolution();
    const float line_h = static_cast<float>(font_size_) * kLineSpacing;
    const float block_h = static_cast<float>(count) * line_h;
    const float anchor_x = menu_anchor_.x * static_cast<float>(vres.x);
    const float anchor_y = menu_anchor_.y * static_cast<float>(vres.y);
    const float top = anchor_y - block_h / 2.0f;
    return {anchor_x, top + (static_cast<float>(index) + 0.5f) * line_h};
}

int TitleScreen::entry_at(float vx, float vy) const {
    const int count = static_cast<int>(entries_.size());
    const float line_h = static_cast<float>(font_size_) * kLineSpacing;
    for (int i = 0; i < count; ++i) {
        const sf::Vector2f c = entry_center(i, count);
        const Entry& e = entries_[static_cast<std::size_t>(i)];
        const float half_w = (e.width > 0.0f ? e.width / 2.0f + kHitPadX : kNoFontHitWidth / 2.0f);
        const float half_h = line_h / 2.0f;
        if (vx >= c.x - half_w && vx <= c.x + half_w && vy >= c.y - half_h && vy <= c.y + half_h) {
            return i;
        }
    }
    return -1;
}

void TitleScreen::start_new_game() {
    if (new_game_target_.empty()) {
        ctx_.log.warn("title: 'new_game' outcome is not wired in the manifest");
        return;
    }
    ctx_.state.clear();
    ctx_.saves.clear_staged();
    ctx_.scenes.goto_scene(new_game_target_);
}

void TitleScreen::trigger(Action action) {
    switch (action) {
    case Action::NEW_GAME:
        start_new_game();
        break;
    case Action::CONTINUE: {
        const auto slot = ctx_.saves.latest_slot();
        if (!slot) {
            // Only reachable with `menu.continue_fallback: new_game` (otherwise the
            // entry isn't listed without a save): Continue doubles as "just play".
            if (continue_starts_new_game_) {
                start_new_game();
            } else {
                ctx_.log.warn("title: 'Continue' clicked with no save on disk");
            }
            break;
        }
        auto state = ctx_.saves.load(*slot);
        if (!state) {
            // load() already logged. The save is unreadable — stay on the title
            // rather than silently starting over: 'New game' is right there, and
            // the player should get to make that call themselves.
            break;
        }
        ctx_.saves.stage_restore(std::move(*state));
        ctx_.scenes.goto_scene(continue_target_);
        break;
    }
    case Action::LOAD_GAME:
        ctx_.scenes.open_load();
        break;
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

void TitleScreen::update(float dt) {
    (void) dt;
    // Use the same hover affordance as the rest of the game: the custom game
    // cursor switches to its INTERACT variant over a clickable menu entry.
    if (hovered_ >= 0) {
        ctx_.cursor.want(pac::core::CursorKind::INTERACT);
    }
}

void TitleScreen::draw(sf::RenderTarget& target) const {
    const sf::Vector2u vres = ctx_.display.virtual_resolution();
    const auto vw = static_cast<float>(vres.x);
    const auto vh = static_cast<float>(vres.y);

    // Background: scaled full-screen image, or solid black when none is set.
    sf::RectangleShape bg(sf::Vector2f(vw, vh));
    bg.setFillColor(sf::Color::Black);
    target.draw(bg);

    if (!background_path_.empty()) {
        try {
            const sf::Texture& tex = ctx_.resources.texture(background_path_);
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

    if (font_ == nullptr) {
        return;
    }

    const int count = static_cast<int>(entries_.size());
    for (int i = 0; i < count; ++i) {
        const bool hot = (i == hovered_);
        const sf::Vector2f c = entry_center(i, count);

        sf::Text text(pac::core::utf8(entries_[static_cast<std::size_t>(i)].label),
                      *font_,
                      font_size_);
        text.setFillColor(hot ? sf::Color::White : sf::Color(200, 205, 220));
        text.setOutlineColor(sf::Color(0, 0, 0, 200));
        text.setOutlineThickness(2.0f);
        const sf::FloatRect b = text.getLocalBounds();
        text.setPosition(c.x - b.width / 2.0f - b.left, c.y - b.height / 2.0f - b.top);
        target.draw(text);
    }
}

} // namespace pac::pnc
