#include "engine/pnc/settings_scene.hpp"

#include "engine/core/display.hpp"
#include "engine/core/engine_context.hpp"
#include "engine/core/resource_cache.hpp"
#include "engine/core/scene_manager.hpp"
#include "engine/core/scene_params.hpp"
#include "engine/core/settings.hpp"
#include "engine/core/strings.hpp"

#include <SFML/Graphics/Font.hpp>
#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/Graphics/Text.hpp>
#include <SFML/Window/Event.hpp>

#include <algorithm>
#include <string>

namespace pac::pnc {

namespace {
constexpr float kVolumeStep = 0.05f;
}

SettingsScene::SettingsScene(pac::core::EngineContext& ctx, const pac::core::SceneParams& params)
    : ctx_(ctx) {
    const std::string font_path = params.get_or("font", "");
    if (!font_path.empty()) {
        font_ = ctx_.resources.try_font(font_path);
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
    case sf::Keyboard::Left:
        ctx_.settings.audio.music_volume -= kVolumeStep;
        ctx_.settings.clamp();
        break;
    case sf::Keyboard::Right:
        ctx_.settings.audio.music_volume += kVolumeStep;
        ctx_.settings.clamp();
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

    const float cx = static_cast<float>(vres.x) / 2.0f;
    const int volume_pct = static_cast<int>(ctx_.settings.audio.music_volume * 100.0f + 0.5f);

    auto centered = [&](const std::string& s, float y, unsigned size, sf::Color color) {
        sf::Text text(s, *font_, size);
        text.setFillColor(color);
        const sf::FloatRect b = text.getLocalBounds();
        text.setPosition(cx - b.width / 2.0f - b.left, y);
        target.draw(text);
    };

    centered(ctx_.strings.ui_label("settings"),
             static_cast<float>(vres.y) * 0.30f,
             40,
             sf::Color::White);
    centered("Musica  < " + std::to_string(volume_pct) + "% >",
             static_cast<float>(vres.y) * 0.48f,
             30,
             sf::Color(210, 215, 230));
    centered("[Esc] " + ctx_.strings.ui_label("settings"),
             static_cast<float>(vres.y) * 0.66f,
             22,
             sf::Color(150, 155, 170));
}

} // namespace pac::pnc
