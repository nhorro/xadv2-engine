#pragma once

#include "engine/core/scene.hpp"
#include "engine/pnc/ui_sound_cues.hpp"

#include <SFML/Graphics/Rect.hpp>

namespace sf {
class Font;
}

namespace pac::core {
struct EngineContext;
class SceneParams;
} // namespace pac::core

namespace pac::pnc {

/// Transparent, localized modal used for destructive navigation. SceneManager
/// owns the pending action; this scene only presents it and accepts/cancels it.
/// Escape and the initially selected "No" choice both favor staying in-game.
class ConfirmationScene final : public pac::core::Scene {
public:
    ConfirmationScene(pac::core::EngineContext& ctx, const pac::core::SceneParams& params);

    void enter() override;
    void handle_event(const sf::Event& event) override;
    void update(float dt) override;
    void draw(sf::RenderTarget& target) const override;

private:
    struct Layout {
        sf::FloatRect panel;
        sf::FloatRect yes;
        sf::FloatRect no;
    };

    [[nodiscard]] Layout layout() const;
    [[nodiscard]] int button_at(float x, float y) const;
    void activate(int button);

    pac::core::EngineContext& ctx_;
    UiSoundCues ui_sounds_;
    const sf::Font* font_ = nullptr;
    unsigned font_size_ = 28;
    unsigned button_font_size_ = 24;
    int selected_ = 1; // 0 = yes, 1 = no; default is deliberately safe
    int hovered_ = -1;
};

} // namespace pac::pnc
