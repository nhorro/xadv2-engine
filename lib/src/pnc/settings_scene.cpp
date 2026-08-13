#include "engine/pnc/settings_scene.hpp"

#include "engine/core/audio.hpp"
#include "engine/core/cursor.hpp"
#include "engine/core/diagnostics.hpp"
#include "engine/core/display.hpp"
#include "engine/core/engine_context.hpp"
#include "engine/core/localization.hpp"
#include "engine/core/resource_cache.hpp"
#include "engine/core/scene_manager.hpp"
#include "engine/core/scene_params.hpp"
#include "engine/core/settings.hpp"
#include "engine/core/settings_store.hpp"
#include "engine/core/strings.hpp"
#include "engine/core/text_encoding.hpp"

#include <SFML/Graphics/Font.hpp>
#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/Graphics/Sprite.hpp>
#include <SFML/Graphics/Text.hpp>
#include <SFML/Graphics/Texture.hpp>
#include <SFML/Window/Event.hpp>
#include <SFML/Window/VideoMode.hpp>

#include <algorithm>
#include <exception>
#include <string>

namespace pac::pnc {

namespace {
constexpr float kVolumeStep = 0.05f;

// Menu layout as fractions of the virtual height, shared by draw + hit-testing.
constexpr float kRowsTopFrac = 0.28f; // top (y) of the first row's text
constexpr float kRowStepFrac = 0.08f; // vertical pitch between rows

pac::core::DisplayMode mode_of(const pac::core::Settings& s) {
    return {{s.window_width, s.window_height}, s.fullscreen};
}
} // namespace

int wrap_choice_index(int current, int direction, int count) {
    if (count <= 0) {
        return 0;
    }
    current = std::clamp(current, 0, count - 1);
    if (direction == 0) {
        return current;
    }
    const int step = direction < 0 ? -1 : 1;
    return (current + step + count) % count;
}

SettingsScene::SettingsScene(pac::core::EngineContext& ctx, const pac::core::SceneParams& params)
    : ctx_(ctx), ui_sounds_(params), working_(ctx.settings) {
    background_path_ = params.get_or("background", "");

    const std::string font_path = params.get_or("font", "");
    if (!font_path.empty()) {
        font_ = ctx_.resources.try_font(font_path);
    }
    if (const auto fs = params.get("font_size")) {
        try {
            font_size_ = static_cast<unsigned>(std::stoul(*fs));
        } catch (const std::exception&) {
            ctx_.log.warn("settings: invalid font_size '" + *fs + "'; using default");
        }
    }

    const sf::VideoMode desktop = sf::VideoMode::getDesktopMode();
    sizes_ = pac::core::windowed_size_options({desktop.width, desktop.height},
                                              ctx_.display.virtual_resolution());
    for (std::size_t i = 0; i < sizes_.size(); ++i) {
        if (sizes_[i].x == working_.window_width && sizes_[i].y == working_.window_height) {
            size_idx_ = static_cast<int>(i);
            break;
        }
    }

    const auto& langs = ctx_.localization.languages();
    for (std::size_t i = 0; i < langs.size(); ++i) {
        if (langs[i].id == working_.language) {
            lang_idx_ = static_cast<int>(i);
            break;
        }
    }
}

void SettingsScene::adjust(int dir) {
    switch (row_) {
    case ROW_RESOLUTION: {
        if (sizes_.empty()) {
            break;
        }
        const int next = std::clamp(size_idx_ + dir, 0, static_cast<int>(sizes_.size()) - 1);
        if (next == size_idx_ && !working_.fullscreen) {
            break;
        }
        size_idx_ = next;
        working_.window_width = sizes_[size_idx_].x;
        working_.window_height = sizes_[size_idx_].y;
        // Picking a windowed resolution implies windowed mode.
        working_.fullscreen = false;
        break;
    }
    case ROW_FULLSCREEN:
        working_.fullscreen = dir > 0;
        break;
    case ROW_LANGUAGE: {
        const auto& langs = ctx_.localization.languages();
        if (langs.empty()) {
            break;
        }
        const int next = wrap_choice_index(lang_idx_, dir, static_cast<int>(langs.size()));
        if (next == lang_idx_) {
            break;
        }
        // Language is a live preview, like audio volume: the settings screen
        // itself immediately demonstrates the selected UI catalog. Apply keeps
        // it; Back/Escape restores the committed language in cancel().
        if (ctx_.localization.set_language(langs[static_cast<std::size_t>(next)].id)) {
            lang_idx_ = next;
            working_.language = langs[static_cast<std::size_t>(lang_idx_)].id;
        }
        break;
    }
    case ROW_MUSIC:
        working_.audio.music_volume += static_cast<float>(dir) * kVolumeStep;
        working_.clamp();
        ctx_.audio.apply_settings(working_); // live preview
        break;
    case ROW_SFX:
        working_.audio.sfx_volume += static_cast<float>(dir) * kVolumeStep;
        working_.clamp();
        ctx_.audio.apply_settings(working_); // live preview
        break;
    case ROW_SPEECH:
        working_.audio.speech_enabled = dir > 0;
        ctx_.audio.apply_settings(working_);
        break;
    default:
        break;
    }
}

void SettingsScene::activate() {
    switch (row_) {
    case ROW_APPLY:
        apply();
        break;
    case ROW_BACK:
        cancel();
        break;
    case ROW_FULLSCREEN:
        adjust(working_.fullscreen ? -1 : +1); // Enter toggles
        break;
    case ROW_SPEECH:
        adjust(working_.audio.speech_enabled ? -1 : +1);
        break;
    default:
        adjust(+1);
        break;
    }
}

void SettingsScene::apply() {
    const bool display_changed = mode_of(working_) != mode_of(ctx_.settings);

    ctx_.settings = working_;
    ctx_.audio.apply_settings(ctx_.settings);
    // Normally already active through live preview. Retry here so Apply remains
    // correct if another caller changed working_ without going through adjust().
    if (ctx_.localization.active() != ctx_.settings.language) {
        ctx_.localization.set_language(ctx_.settings.language);
    }
    if (display_changed) {
        ctx_.display.request_mode(mode_of(ctx_.settings));
    }
    ctx_.settings_store.save(ctx_.settings);
    ctx_.scenes.pop_scene();
}

void SettingsScene::cancel() {
    // Undo live audio and language previews using the committed settings.
    ctx_.audio.apply_settings(ctx_.settings);
    if (ctx_.localization.active() != ctx_.settings.language) {
        ctx_.localization.set_language(ctx_.settings.language);
    }
    ctx_.scenes.pop_scene();
}

float SettingsScene::row_center_y(int i, const sf::Vector2u& vres) const {
    const float vh = static_cast<float>(vres.y);
    const float row_top = vh * kRowsTopFrac + vh * kRowStepFrac * static_cast<float>(i);
    return row_top + static_cast<float>(font_size_) * 0.5f;
}

int SettingsScene::row_at(float vx, float vy) const {
    const sf::Vector2u vres = ctx_.display.virtual_resolution();
    const float cx = static_cast<float>(vres.x) / 2.0f;
    const float half_w = static_cast<float>(vres.x) * 0.45f; // generous centered band
    const float half_h = static_cast<float>(vres.y) * kRowStepFrac * 0.5f;
    if (vx < cx - half_w || vx > cx + half_w) {
        return -1;
    }
    for (int i = 0; i < ROW_COUNT; ++i) {
        const float cy = row_center_y(i, vres);
        if (vy >= cy - half_h && vy <= cy + half_h) {
            return i;
        }
    }
    return -1;
}

SettingsScene::RowView SettingsScene::row_view(int row) const {
    const pac::core::Strings& strings = ctx_.strings;
    switch (row) {
    case ROW_RESOLUTION: {
        std::string value = "-";
        if (!sizes_.empty()) {
            const sf::Vector2u size = sizes_[static_cast<std::size_t>(size_idx_)];
            value = std::to_string(size.x) + " x " + std::to_string(size.y);
        }
        return {strings.ui_label("resolution"), value, true};
    }
    case ROW_FULLSCREEN:
        return {strings.ui_label("fullscreen"),
                strings.ui_label(working_.fullscreen ? "on" : "off"),
                true};
    case ROW_LANGUAGE: {
        std::string value = working_.language;
        const auto& languages = ctx_.localization.languages();
        if (!languages.empty()) {
            const pac::core::LanguageEntry& language =
                languages[static_cast<std::size_t>(lang_idx_)];
            value = language.name.empty() ? language.id : language.name;
        }
        return {strings.ui_label("language"), value, true};
    }
    case ROW_MUSIC: {
        const int percent = static_cast<int>(working_.audio.music_volume * 100.0f + 0.5f);
        return {strings.ui_label("music"), std::to_string(percent) + "%", true};
    }
    case ROW_SFX: {
        const int percent = static_cast<int>(working_.audio.sfx_volume * 100.0f + 0.5f);
        return {strings.ui_label("sfx"), std::to_string(percent) + "%", true};
    }
    case ROW_SPEECH:
        return {strings.ui_label("speech"),
                strings.ui_label(working_.audio.speech_enabled ? "on" : "off"),
                true};
    case ROW_APPLY:
        return {strings.ui_label("apply"), "", false};
    case ROW_BACK:
        return {strings.ui_label("back"), "", false};
    default:
        return {};
    }
}

float SettingsScene::chooser_value_center_x(int row) const {
    const float center = static_cast<float>(ctx_.display.virtual_resolution().x) / 2.0f;
    if (!font_) {
        return center;
    }
    const RowView view = row_view(row);
    if (!view.selectable_value) {
        return center;
    }
    const std::string prefix = "> " + view.label + ":  < ";
    const std::string through_value = prefix + view.value;
    const std::string full = through_value + " >";
    const auto width = [this](const std::string& text) {
        return sf::Text(pac::core::utf8(text), *font_, font_size_).getLocalBounds().width;
    };
    const float left = center - width(full) / 2.0f;
    return left + (width(prefix) + width(through_value)) / 2.0f;
}

void SettingsScene::handle_event(const sf::Event& event) {
    if (event.type == sf::Event::MouseMoved) {
        const int previous = row_;
        const bool was_hovered = hovered_;
        const int r =
            row_at(static_cast<float>(event.mouseMove.x), static_cast<float>(event.mouseMove.y));
        hovered_ = (r >= 0);
        if (r >= 0) {
            row_ = r; // hover selects the row, mirroring keyboard navigation
            if (!was_hovered || row_ != previous) {
                ui_sounds_.selection(ctx_);
            }
        }
        return;
    }
    if (event.type == sf::Event::MouseButtonReleased &&
        event.mouseButton.button == sf::Mouse::Left) {
        const auto x = static_cast<float>(event.mouseButton.x);
        const int r = row_at(x, static_cast<float>(event.mouseButton.y));
        if (r < 0) {
            return;
        }
        row_ = r;
        ui_sounds_.activate(ctx_);
        if (r == ROW_APPLY || r == ROW_BACK) {
            activate();
        } else {
            adjust(x < chooser_value_center_x(r) ? -1 : +1);
        }
        return;
    }
    if (event.type != sf::Event::KeyPressed) {
        return;
    }
    switch (event.key.code) {
    case sf::Keyboard::Escape:
        ui_sounds_.activate(ctx_);
        cancel();
        break;
    case sf::Keyboard::Up:
        row_ = (row_ + ROW_COUNT - 1) % ROW_COUNT;
        ui_sounds_.selection(ctx_);
        break;
    case sf::Keyboard::Down:
        row_ = (row_ + 1) % ROW_COUNT;
        ui_sounds_.selection(ctx_);
        break;
    case sf::Keyboard::Left:
        adjust(-1);
        break;
    case sf::Keyboard::Right:
        adjust(+1);
        break;
    case sf::Keyboard::Return:
    case sf::Keyboard::Space:
        ui_sounds_.activate(ctx_);
        activate();
        break;
    default:
        break;
    }
}

void SettingsScene::update(float dt) {
    (void) dt;
    // Same hover affordance as the rest of the game: the custom cursor switches
    // to its interact variant while pointing at a settings row.
    if (hovered_) {
        ctx_.cursor.want(pac::core::CursorKind::INTERACT);
    }
}

void SettingsScene::draw(sf::RenderTarget& target) const {
    const sf::Vector2u vres = ctx_.display.virtual_resolution();

    const auto vw = static_cast<float>(vres.x);
    const auto vh = static_cast<float>(vres.y);

    sf::RectangleShape bg(sf::Vector2f(vw, vh));
    bg.setFillColor(sf::Color(12, 14, 22));
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

    if (!font_) {
        return;
    }

    const pac::core::Strings& strings = ctx_.strings;
    const float cx = static_cast<float>(vres.x) / 2.0f;

    auto centered = [&](const std::string& s, float y, unsigned size, sf::Color color) {
        sf::Text text(pac::core::utf8(s), *font_, size);
        text.setFillColor(color);
        text.setOutlineColor(sf::Color(0, 0, 0, 200)); // legible over a background image
        text.setOutlineThickness(2.0f);
        const sf::FloatRect b = text.getLocalBounds();
        text.setPosition(cx - b.width / 2.0f - b.left, y);
        target.draw(text);
    };

    centered(strings.ui_label("settings"), vh * 0.14f, font_size_ + 12u, sf::Color::White);

    const float row_y0 = vh * kRowsTopFrac;
    const float row_dy = vh * kRowStepFrac;
    for (int i = 0; i < ROW_COUNT; ++i) {
        const RowView row = row_view(i);
        const bool selected = (i == row_);
        const sf::Color color = selected ? sf::Color(255, 240, 180) : sf::Color(180, 185, 200);
        const std::string marker = selected ? "> " : "  ";
        std::string text;
        if (row.selectable_value) {
            text = marker + row.label + ":  < " + row.value + " >";
        } else {
            text = marker + row.label;
        }
        centered(text, row_y0 + row_dy * static_cast<float>(i), font_size_, color);
    }
}

} // namespace pac::pnc
