#include "field_notes.hpp"

#include "engine/core/diagnostics.hpp"
#include "engine/core/display.hpp"
#include "engine/core/engine_context.hpp"
#include "engine/core/manifest.hpp"
#include "engine/core/resource_cache.hpp"
#include "engine/core/scene_factory.hpp"
#include "engine/core/scene_manager.hpp"
#include "engine/core/scene_params.hpp"
#include "engine/core/scripting.hpp"
#include "engine/core/state_store.hpp"

#include <SFML/Graphics.hpp>
#include <yaml-cpp/yaml.h>

// sol2 is how a C++ scene reaches Lua. The engine keeps it out of its public
// headers (pimpl), but exposes the live sol::state through `Scripting::lua()`
// for exactly this: a game that wants to add its own Lua functions.
#include <sol/sol.hpp>

#include <stdexcept>
#include <utility>

namespace example::notes {
namespace {

// Where a note's "found" flag lives. Persistent state belongs to the engine's
// StateStore, which is what gets written to the save file.
std::string state_key(const std::string& id) {
    return "notes." + id;
}

} // namespace

std::vector<Note> parse_notes(const std::string& yaml) {
    const YAML::Node root = YAML::Load(yaml);
    std::vector<Note> notes;
    for (const YAML::Node& n : root["notes"] ? root["notes"] : YAML::Node()) {
        Note note;
        note.id = n["id"].as<std::string>();
        note.title = n["title"].as<std::string>("");
        note.body = n["body"].as<std::string>("");
        notes.push_back(std::move(note));
    }
    return notes;
}

FieldNotesScene::FieldNotesScene(pac::core::EngineContext& ctx,
                                 const pac::core::SceneParams& params,
                                 std::vector<Note> notes)
    : ctx_(ctx), notes_(std::move(notes)) {
    opaque_ = true; // covers the room underneath, so the manager skips drawing it
    font_ = ctx_.resources.try_font(params.get_or("font", ""));
}

bool FieldNotesScene::found(const Note& note) const {
    const auto value = ctx_.state.get(state_key(note.id));
    return value && std::holds_alternative<bool>(*value) && std::get<bool>(*value);
}

void FieldNotesScene::handle_event(const sf::Event& event) {
    const bool escape =
        event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::Escape;
    const bool click = event.type == sf::Event::MouseButtonPressed;
    if (escape || click) {
        // Pop ourselves off the stack; the scene beneath (the room, or the pause
        // menu that opened us) becomes the top again, untouched.
        ctx_.scenes.pop_scene();
    }
}

void FieldNotesScene::draw(sf::RenderTarget& target) const {
    const sf::Vector2u res = ctx_.display.virtual_resolution();
    const auto w = static_cast<float>(res.x);
    const auto h = static_cast<float>(res.y);

    sf::RectangleShape backdrop({w, h});
    backdrop.setFillColor(sf::Color(18, 20, 28));
    target.draw(backdrop);

    if (!font_) {
        return; // no font: an empty page beats a crash (fonts are optional)
    }

    sf::Text heading("FIELD NOTES", *font_, 34);
    heading.setPosition(90.f, 60.f);
    heading.setFillColor(sf::Color(240, 216, 122));
    target.draw(heading);

    float y = 150.f;
    int discovered = 0;
    for (const Note& note : notes_) {
        const bool have = found(note);
        if (have) {
            ++discovered;
        }

        sf::Text title(have ? note.title : std::string("? ? ?"), *font_, 22);
        title.setPosition(90.f, y);
        title.setFillColor(have ? sf::Color(231, 231, 233) : sf::Color(90, 92, 104));
        target.draw(title);

        if (have) {
            sf::Text body(note.body, *font_, 17);
            body.setPosition(110.f, y + 30.f);
            body.setFillColor(sf::Color(170, 178, 196));
            target.draw(body);
        }
        y += have ? 84.f : 44.f;
    }

    sf::Text footer(std::to_string(discovered) + "/" + std::to_string(notes_.size()) +
                        " found  —  [esc] or click to go back",
                    *font_,
                    16);
    footer.setPosition(90.f, h - 70.f);
    footer.setFillColor(sf::Color(120, 126, 142));
    target.draw(footer);
}

FieldNotesModule::FieldNotesModule(std::string scene_id) : scene_id_(std::move(scene_id)) {}

void FieldNotesModule::register_scenes(pac::core::SceneFactory& factory) {
    // From here on, `type: FieldNotes` in the manifest is as real as `type: RoomScene`.
    factory.register_type(
        "FieldNotes",
        [this](pac::core::EngineContext& ctx, const pac::core::SceneParams& params) {
            return std::make_unique<FieldNotesScene>(ctx, params, notes_);
        });
}

void FieldNotesModule::configure(pac::core::EngineContext& ctx,
                                 const pac::core::Manifest& manifest) {
    // Runs once at startup, after the engine's own Lua bindings are installed and
    // before the first scene is built.
    const pac::core::SceneDesc* scene = manifest.find_scene(scene_id_);
    if (!scene) {
        throw std::runtime_error("field notes: manifest has no scene '" + scene_id_ + "'");
    }
    const std::string data = scene->parameters.get_or("data", "");
    if (data.empty()) {
        throw std::runtime_error("field notes: scene '" + scene_id_ + "' needs parameters.data");
    }
    notes_ = parse_notes(ctx.resources.read_text(data));

    // The game's own Lua API, sitting next to the engine's. Flat snake_case
    // globals, scalar state — the same conventions the engine's own API follows.
    sol::state& lua = ctx.scripting.lua();
    pac::core::StateStore& state = ctx.state;

    lua.set_function("discover_note",
                     [&state, this](const std::string& id) { state.set(state_key(id), true); });
    lua.set_function("has_note", [&state](const std::string& id) {
        const auto value = state.get(state_key(id));
        return value && std::holds_alternative<bool>(*value) && std::get<bool>(*value);
    });
    lua.set_function("open_notes", [&ctx, this]() { ctx.scenes.push_scene(scene_id_); });

    ctx.log.info("field notes: " + std::to_string(notes_.size()) + " notes, Lua API installed");
}

} // namespace example::notes
