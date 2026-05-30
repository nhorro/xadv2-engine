#pragma once

#include "engine/core/scene.hpp"
#include "engine/core/scripting.hpp" // ScopeId, TaskId
#include "engine/geom/geometry.hpp"
#include "engine/pnc/cast.hpp"
#include "engine/pnc/closeup.hpp"
#include "engine/pnc/closeup_runtime.hpp"
#include "engine/pnc/speech_manager.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace sf {
class Font;
}

namespace pac::core {
struct EngineContext;
class SceneParams;
} // namespace pac::core

namespace pac::pnc {

/// Full-screen close-up / examine view (issue #76, design 01 R2 / 04 §Genre
/// scenes): a background that fills the screen and hotspot hit-testing. Pushed as
/// an opaque overlay over the calling scene so backing out (Esc / right-click)
/// restores it exactly; an `on_exit` parameter overrides that with a full scene
/// switch.
///
/// A close-up may be **scripted** via an optional `logic:` Lua sidecar
/// (`closeups/<id>.lua`, see CloseUpRuntime): hotspot clicks run Lua handlers that
/// can make the character talk in steps, change state, and reach into the room
/// beneath (instant `spawn_npc`/`despawn_npc`/… — the room is frozen under the
/// overlay, so blocking room moves are not available). Unscripted close-ups keep
/// the simple per-hotspot `look` caption / `goto` behavior. Reuses the hotspot
/// polygons, the SpeechManager, and the custom cursor affordance.
class CloseUpScene : public pac::core::Scene {
public:
    CloseUpScene(pac::core::EngineContext& ctx, const pac::core::SceneParams& params);
    ~CloseUpScene() override;

    void enter() override;
    void leave() override;
    void handle_event(const sf::Event& event) override;
    void update(float dt) override;
    void draw(sf::RenderTarget& target) const override;

private:
    void exit();
    void activate(const CloseUpHotspot& hs);
    /// Close-up-local speech: show `text` for `speaker` (its cast speech color when a
    /// cast is loaded) at the close-up talk anchor, and return the event id the talk
    /// wrapper waits on so consecutive lines play in sequence. Empty text -> no event.
    std::string api_talk(const std::string& speaker, const std::string& text);

    pac::core::EngineContext& ctx_;
    std::string data_path_;
    std::string logic_path_;
    std::string cast_path_;
    std::string on_exit_; // scene id on back-out; empty -> pop the overlay
    const sf::Font* font_ = nullptr;
    CloseUpData data_;
    bool loaded_ = false;
    SpeechManager speech_;
    geom::Point hover_{-1.0f, -1.0f};
    const CloseUpHotspot* hovered_ = nullptr;

    // Scripting (optional `logic:` sidecar).
    CloseUpRuntime runtime_;
    bool scripted_ = false;
    pac::core::ScopeId closeup_scope_ = 0;
    Cast cast_;
    bool has_cast_ = false;
    pac::core::TaskId active_handler_ = 0; // the running hotspot-handler task, if any

    // Close-up `talk(...)` calls in flight: woken when speech clears so a scripted
    // sequence's lines play one after another (mirrors RoomScene::pending_speech_).
    struct PendingSpeech {
        pac::core::ScopeId scope;
        std::string event;
    };
    std::vector<PendingSpeech> pending_speech_;
    std::uint64_t talk_seq_ = 0;

    // Holds the saved `talk` global (a sol::object) while we shadow it, kept in a
    // pimpl so sol2 stays out of this header.
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace pac::pnc
