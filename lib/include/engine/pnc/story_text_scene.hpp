#pragma once

#include "engine/core/scene.hpp"
#include "engine/core/scripting.hpp" // ScopeId

#include <string>

namespace sf {
class Font;
}

namespace pac::core {
struct EngineContext;
class SceneParams;
} // namespace pac::core

namespace pac::pnc {

/// Text cutscene. Runs its `script` as a coroutine task in a dedicated scene
/// scope; the script shows centered pages with `show_text` and may `wait`. When
/// the script finishes (no tasks left in the scope) the `on_finish` outcome fires.
/// Skippable (Enter/Space/Esc/click): skipping cancels the running script and
/// fires `on_finish`.
class StoryTextScene : public pac::core::Scene {
public:
    StoryTextScene(pac::core::EngineContext& ctx, const pac::core::SceneParams& params);

    void enter() override;
    void leave() override;
    void handle_event(const sf::Event& event) override;
    void update(float dt) override;
    void draw(sf::RenderTarget& target) const override;

private:
    void finish();

    pac::core::EngineContext& ctx_;
    std::string script_;
    std::string on_finish_;
    const sf::Font* font_ = nullptr;
    pac::core::ScopeId scope_ = 0;
    bool finished_ = false;
};

} // namespace pac::pnc
