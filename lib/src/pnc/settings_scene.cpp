#include "engine/pnc/settings_scene.hpp"

#include "engine/core/audio.hpp"
#include "engine/core/display.hpp"
#include "engine/core/engine_context.hpp"
#include "engine/core/resource_cache.hpp"
#include "engine/core/scene_manager.hpp"
#include "engine/core/scene_params.hpp"
#include "engine/core/settings.hpp"
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
} // namespace

SettingsScene::SettingsScene(pac::core::EngineContext& ctx, const pac::core::SceneParams& params)
    : ctx_(ctx) {
    const std::string font_path = params.get_or("font", "");
    if (!font_path.empty()) {
        font_ = ctx_.resources.try_font(font_path);
    }

    const sf::VideoMode desktop = sf::VideoMode::getDesktopMode();
    sizes_ = pac::core::windowed_size_options({desktop.width, desktop.height},
                                              ctx_.display.virtual_resolution());
    // Select the entry matching the current windowed size, if present.
    for (std::size_t i = 0; i < sizes_.size(); ++i) {
        if (sizes_[i].x == ctx_.settings.window_width &&
            sizes_[i].y == ctx_.settings.window_height) {
            size_idx_ = static_cast<int>(i);
            break;
        }
    }
}

void SettingsScene::request_current_mode() {
    ctx_.display.request_mode(
        {{ctx_.settings.window_width, ctx_.settings.window_height}, ctx_.settings.fullscreen});
}

void SettingsScene::adjust(int dir) {
    switch (row_) {
    case ROW_RESOLUTION: {
        if (sizes_.empty()) {
            break;
        }
        const int next = std::clamp(size_idx_ + dir, 0, static_cast<int>(sizes_.size()) - 1);
        // No change unless the size moved or we're leaving fullscreen.
        if (next == size_idx_ && !ctx_.settings.fullscreen) {
            break;
        }
        size_idx_ = next;
        ctx_.settings.window_width = sizes_[size_idx_].x;
        ctx_.settings.window_height = sizes_[size_idx_].y;
        // Picking a windowed resolution implies windowed mode.
        ctx_.settings.fullscreen = false;
        request_current_mode();
        break;
    }
    case ROW_FULLSCREEN: {
        const bool want = dir > 0;
        if (want == ctx_.settings.fullscreen) {
            break; // already in this mode; don't recreate the window
        }
        ctx_.settings.fullscreen = want;
        request_current_mode();
        break;
    }
    case ROW_MUSIC:
        ctx_.settings.audio.music_volume += static_cast<float>(dir) * kVolumeStep;
        ctx_.settings.clamp();
        ctx_.audio.apply_settings(ctx_.settings); // live preview
        break;
    case ROW_SFX:
        ctx_.settings.audio.sfx_volume += static_cast<float>(dir) * kVolumeStep;
        ctx_.settings.clamp();
        ctx_.audio.apply_settings(ctx_.settings); // live preview
        break;
    default:
        break;
    }
}

void SettingsScene::handle_event(const sf::Event& event) {
    if (event.type != sf::Event::KeyPressed) {
        return;
    }
    switch (event.key.code) {
    case sf::Keyboard::Escape:
        ctx_.scenes.pop_scene();
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
        // Enter toggles the boolean row; for the others it nudges right.
        adjust(row_ == ROW_FULLSCREEN && ctx_.settings.fullscreen ? -1 : +1);
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
             static_cast<float>(vres.y) * 0.18f,
             40,
             sf::Color::White);

    std::string res_value = "-";
    if (!sizes_.empty()) {
        const sf::Vector2u s = sizes_[static_cast<std::size_t>(size_idx_)];
        res_value = std::to_string(s.x) + " x " + std::to_string(s.y);
    }
    const std::string fs_value = strings.ui_label(ctx_.settings.fullscreen ? "on" : "off");
    const int music_pct = static_cast<int>(ctx_.settings.audio.music_volume * 100.0f + 0.5f);
    const int sfx_pct = static_cast<int>(ctx_.settings.audio.sfx_volume * 100.0f + 0.5f);

    const std::string labels[ROW_COUNT] = {
        strings.ui_label("resolution"),
        strings.ui_label("fullscreen"),
        strings.ui_label("music"),
        strings.ui_label("sfx"),
    };
    const std::string values[ROW_COUNT] = {
        res_value,
        fs_value,
        std::to_string(music_pct) + "%",
        std::to_string(sfx_pct) + "%",
    };

    const float row_y0 = static_cast<float>(vres.y) * 0.40f;
    const float row_dy = static_cast<float>(vres.y) * 0.10f;
    for (int i = 0; i < ROW_COUNT; ++i) {
        const bool selected = (i == row_);
        const sf::Color color = selected ? sf::Color(255, 240, 180) : sf::Color(180, 185, 200);
        const std::string text = (selected ? "> " : "  ") + labels[i] + ":  < " + values[i] + " >";
        centered(text, row_y0 + row_dy * static_cast<float>(i), 28, color);
    }

    centered("[Esc] " + strings.ui_label("back"),
             static_cast<float>(vres.y) * 0.80f,
             22,
             sf::Color(150, 155, 170));
}

} // namespace pac::pnc
