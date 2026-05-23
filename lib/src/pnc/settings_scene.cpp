#include "engine/pnc/settings_scene.hpp"

#include "engine/core/audio.hpp"
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
#include <SFML/Graphics/Text.hpp>
#include <SFML/Window/Event.hpp>
#include <SFML/Window/VideoMode.hpp>

#include <algorithm>
#include <string>

namespace pac::pnc {

namespace {
constexpr float kVolumeStep = 0.05f;

pac::core::DisplayMode mode_of(const pac::core::Settings& s) {
    return {{s.window_width, s.window_height}, s.fullscreen};
}
} // namespace

SettingsScene::SettingsScene(pac::core::EngineContext& ctx, const pac::core::SceneParams& params)
    : ctx_(ctx), working_(ctx.settings) {
    const std::string font_path = params.get_or("font", "");
    if (!font_path.empty()) {
        font_ = ctx_.resources.try_font(font_path);
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
        const int next = std::clamp(lang_idx_ + dir, 0, static_cast<int>(langs.size()) - 1);
        if (next == lang_idx_) {
            break;
        }
        lang_idx_ = next;
        working_.language = langs[lang_idx_].id;
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
    default:
        adjust(+1);
        break;
    }
}

void SettingsScene::apply() {
    const bool display_changed = mode_of(working_) != mode_of(ctx_.settings);
    const bool language_changed = working_.language != ctx_.settings.language;

    ctx_.settings = working_;
    ctx_.audio.apply_settings(ctx_.settings);
    if (language_changed) {
        ctx_.localization.set_language(ctx_.settings.language);
    }
    if (display_changed) {
        ctx_.display.request_mode(mode_of(ctx_.settings));
    }
    ctx_.settings_store.save(ctx_.settings);
    ctx_.scenes.pop_scene();
}

void SettingsScene::cancel() {
    // Undo any live audio preview by re-applying the committed (unchanged) values.
    ctx_.audio.apply_settings(ctx_.settings);
    ctx_.scenes.pop_scene();
}

void SettingsScene::handle_event(const sf::Event& event) {
    if (event.type != sf::Event::KeyPressed) {
        return;
    }
    switch (event.key.code) {
    case sf::Keyboard::Escape:
        cancel();
        break;
    case sf::Keyboard::Up:
        row_ = (row_ + ROW_COUNT - 1) % ROW_COUNT;
        break;
    case sf::Keyboard::Down:
        row_ = (row_ + 1) % ROW_COUNT;
        break;
    case sf::Keyboard::Left:
        adjust(-1);
        break;
    case sf::Keyboard::Right:
        adjust(+1);
        break;
    case sf::Keyboard::Return:
    case sf::Keyboard::Space:
        activate();
        break;
    default:
        break;
    }
}

void SettingsScene::draw(sf::RenderTarget& target) const {
    const sf::Vector2u vres = ctx_.display.virtual_resolution();

    sf::RectangleShape bg(sf::Vector2f(static_cast<float>(vres.x), static_cast<float>(vres.y)));
    bg.setFillColor(sf::Color(12, 14, 22));
    target.draw(bg);

    if (!font_) {
        return;
    }

    const pac::core::Strings& strings = ctx_.strings;
    const float cx = static_cast<float>(vres.x) / 2.0f;

    auto centered = [&](const std::string& s, float y, unsigned size, sf::Color color) {
        sf::Text text(pac::core::utf8(s), *font_, size);
        text.setFillColor(color);
        const sf::FloatRect b = text.getLocalBounds();
        text.setPosition(cx - b.width / 2.0f - b.left, y);
        target.draw(text);
    };

    centered(strings.ui_label("settings"),
             static_cast<float>(vres.y) * 0.14f,
             40,
             sf::Color::White);

    std::string res_value = "-";
    if (!sizes_.empty()) {
        const sf::Vector2u s = sizes_[static_cast<std::size_t>(size_idx_)];
        res_value = std::to_string(s.x) + " x " + std::to_string(s.y);
    }
    const std::string fs_value = strings.ui_label(working_.fullscreen ? "on" : "off");

    std::string lang_value = working_.language;
    const auto& langs = ctx_.localization.languages();
    if (!langs.empty()) {
        const pac::core::LanguageEntry& e = langs[static_cast<std::size_t>(lang_idx_)];
        lang_value = e.name.empty() ? e.id : e.name;
    }

    const int music_pct = static_cast<int>(working_.audio.music_volume * 100.0f + 0.5f);
    const int sfx_pct = static_cast<int>(working_.audio.sfx_volume * 100.0f + 0.5f);

    struct RowView {
        std::string label;
        std::string value;
        bool selectable_value; // draws the "< value >" chooser affordance
    };
    const RowView rows[ROW_COUNT] = {
        {strings.ui_label("resolution"), res_value, true},
        {strings.ui_label("fullscreen"), fs_value, true},
        {strings.ui_label("language"), lang_value, true},
        {strings.ui_label("music"), std::to_string(music_pct) + "%", true},
        {strings.ui_label("sfx"), std::to_string(sfx_pct) + "%", true},
        {strings.ui_label("apply"), "", false},
        {strings.ui_label("back"), "", false},
    };

    const float row_y0 = static_cast<float>(vres.y) * 0.34f;
    const float row_dy = static_cast<float>(vres.y) * 0.085f;
    for (int i = 0; i < ROW_COUNT; ++i) {
        const bool selected = (i == row_);
        const sf::Color color = selected ? sf::Color(255, 240, 180) : sf::Color(180, 185, 200);
        const std::string marker = selected ? "> " : "  ";
        std::string text;
        if (rows[i].selectable_value) {
            text = marker + rows[i].label + ":  < " + rows[i].value + " >";
        } else {
            text = marker + rows[i].label;
        }
        centered(text, row_y0 + row_dy * static_cast<float>(i), 28, color);
    }
}

} // namespace pac::pnc
