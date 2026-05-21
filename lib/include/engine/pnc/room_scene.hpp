#pragma once

#include "engine/core/scene.hpp"
#include "engine/core/scripting.hpp" // ScopeId
#include "engine/pnc/avatar.hpp"
#include "engine/pnc/cast.hpp"
#include "engine/pnc/room_renderer.hpp"
#include "engine/pnc/room_runtime.hpp"
#include "engine/pnc/speech_manager.hpp"

#include <optional>
#include <string>

namespace sf {
class Font;
}

namespace pac::core {
struct EngineContext;
class SceneParams;
} // namespace pac::core

namespace pac::pnc {

/// SCUMM-style room gameplay (M3 slice): loads a room (YAML + Lua) and the cast,
/// creates the persistent player avatar, renders the scenery, supports
/// click-to-move (walkability-gated) and clicking a hotspot to run its default
/// verb, showing the returned caption as speech. Camera, SCUMM panel, inventory,
/// dialog, and room transitions arrive in M4–M5.
class RoomScene : public pac::core::Scene {
public:
    RoomScene(pac::core::EngineContext& ctx, const pac::core::SceneParams& params);

    void enter() override;
    void leave() override;
    void handle_event(const sf::Event& event) override;
    void update(float dt) override;
    void draw(sf::RenderTarget& target) const override;

private:
    void load_room(const std::string& id);
    void unload_room();
    void say(const std::string& text);

    pac::core::EngineContext& ctx_;
    std::string cast_path_;
    std::string rooms_dir_;
    std::string start_room_;
    std::string player_char_;
    std::string font_path_;

    const sf::Font* font_ = nullptr;
    Cast cast_;
    std::optional<RoomRuntime> room_;
    std::string room_dir_; // logical dir for resolving layer images
    std::optional<Avatar> player_;
    SpeechManager speech_;
    RoomRenderer renderer_;
    pac::core::ScopeId room_scope_ = 0;
};

} // namespace pac::pnc
