// A game-specific scene, written in C++ and plugged into the engine — the
// escape hatch for the one screen a game needs that the engine doesn't ship.
// (A standard game needs none of this: rooms, dialogs, close-ups and cutscenes
// are all data. Reach for C++ only when the *interaction model itself* is new.)
//
// Three pieces, and they are the whole contract:
//
//   1. a `pac::core::Scene` subclass  — the screen itself,
//   2. a scene TYPE registered with the `SceneFactory` — so `type: FieldNotes`
//      in the manifest builds it, exactly like a built-in scene,
//   3. Lua bindings installed through `ApplicationHooks::configure` — so the
//      game's scripts can drive it (`discover_note("...")`).
//
// This is the pattern the Ingreso Urgente prototype used for its notebook,
// reduced to the smallest thing that still shows all three.
#pragma once

#include "engine/core/scene.hpp"

#include <string>
#include <vector>

namespace sf {
class Font;
}

namespace pac::core {
struct EngineContext;
struct Manifest;
class SceneFactory;
class SceneParams;
} // namespace pac::core

namespace example::notes {

/// One note, as declared in data/notes.yaml. Whether the player has FOUND it is
/// not stored here — that lives in the engine's StateStore under `notes.<id>`,
/// because it is persistent game state and must survive save/load. (A member
/// `bool found` here would be silently lost on reload.)
struct Note {
    std::string id;
    std::string title;
    std::string body;
};

std::vector<Note> parse_notes(const std::string& yaml);

/// The screen: a full-screen list of the notes the player has discovered.
/// Esc or a click on BACK pops it off the stack, revealing whatever was beneath.
class FieldNotesScene : public pac::core::Scene {
public:
    FieldNotesScene(pac::core::EngineContext& ctx,
                    const pac::core::SceneParams& params,
                    std::vector<Note> notes);

    void handle_event(const sf::Event& event) override;
    void draw(sf::RenderTarget& target) const override;

private:
    bool found(const Note& note) const;

    pac::core::EngineContext& ctx_;
    std::vector<Note> notes_;
    const sf::Font* font_ = nullptr;
};

/// Owns the notes and wires them into the engine: `register_scenes` teaches the
/// factory the `FieldNotes` type, `configure` reads the manifest and installs the
/// Lua API. Hand `configure` to `ApplicationHooks` in main().
class FieldNotesModule {
public:
    explicit FieldNotesModule(std::string scene_id);

    void register_scenes(pac::core::SceneFactory& factory);
    void configure(pac::core::EngineContext& ctx, const pac::core::Manifest& manifest);

private:
    std::string scene_id_;
    std::vector<Note> notes_;
};

} // namespace example::notes
