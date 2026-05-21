#include "engine/pnc/room_scene.hpp"

#include "engine/core/diagnostics.hpp"
#include "engine/core/display.hpp"
#include "engine/core/engine_context.hpp"
#include "engine/core/resource_cache.hpp"
#include "engine/core/resource_source.hpp"
#include "engine/core/scene_manager.hpp"
#include "engine/core/scene_params.hpp"
#include "engine/core/scripting.hpp"
#include "engine/gfx/animated_sprite.hpp"
#include "engine/pnc/data_error.hpp"
#include "engine/pnc/room.hpp"

#include <SFML/Window/Event.hpp>

#include <algorithm>
#include <utility>

namespace pac::pnc {

namespace {
constexpr float kAvatarScale = 1.1f;
constexpr float kSpeechRise = 250.0f; // px above the avatar's feet
} // namespace

RoomScene::RoomScene(pac::core::EngineContext& ctx, const pac::core::SceneParams& params)
    : ctx_(ctx) {
    cast_path_ = params.get_or("cast", "cast.yaml");
    rooms_dir_ = params.get_or("rooms", "rooms");
    start_room_ = params.get_or("start_room", "");
    player_char_ = params.get_or("player", "");
    font_path_ = params.get_or("font", "");
}

void RoomScene::enter() {
    if (!font_path_.empty()) {
        font_ = ctx_.resources.try_font(font_path_);
    }
    try {
        cast_ = parse_cast(ctx_.resources.read_text(cast_path_));
    } catch (const std::exception& e) {
        ctx_.log.error(std::string("RoomScene: cast: ") + e.what());
    }
    if (start_room_.empty()) {
        ctx_.log.error("RoomScene: no 'start_room'");
        return;
    }
    load_room(start_room_);
}

void RoomScene::leave() {
    unload_room();
}

void RoomScene::load_room(const std::string& id) {
    const std::string room_logical = rooms_dir_ + "/" + id + ".yaml";
    const std::string lua_logical = rooms_dir_ + "/" + id + ".lua";
    room_dir_ = pac::core::logical_dir(room_logical);

    RoomData data;
    try {
        data = parse_room(ctx_.resources.read_text(room_logical));
    } catch (const std::exception& e) {
        ctx_.log.error(std::string("RoomScene: room '" + id + "': ") + e.what());
        return;
    }

    room_.emplace(std::move(data));

    room_scope_ = ctx_.scripting.open_scope();
    ctx_.scripting.set_current_scope(room_scope_);
    room_->load_behavior(ctx_.scripting, ctx_.resources, lua_logical, ctx_.log);

    // Build the persistent player avatar once, from the cast appearance.
    if (!player_) {
        const Character* character = cast_.character(player_char_);
        if (!character) {
            ctx_.log.error("RoomScene: player character '" + player_char_ + "' not in cast");
        } else if (const Appearance* app = cast_.appearance(character->appearance)) {
            if (app->type == "animated_sprite" && !app->sprite.empty()) {
                try {
                    gfx::AnimatedSprite sprite =
                        gfx::load_animated_sprite(ctx_.resources, app->sprite);
                    player_.emplace(std::move(sprite), kAvatarScale);
                } catch (const std::exception& e) {
                    ctx_.log.error(std::string("RoomScene: player appearance: ") + e.what());
                }
            } else {
                ctx_.log.error("RoomScene: appearance '" + character->appearance +
                               "' is not a usable animated_sprite (M3)");
            }
        }
    }

    // Resolve the player's placement: the room's player:true start, else
    // 'player_start'. (Full entry-point resolution arrives with change_room in M4.)
    if (player_) {
        const geom::Point* start = nullptr;
        for (const RoomAvatarPlacement& a : room_->data().avatars) {
            if (a.player) {
                start = room_->data().point(a.start);
                if (start) {
                    player_->face(a.orientation);
                }
                break;
            }
        }
        if (!start) {
            start = room_->data().point("player_start");
        }
        if (start) {
            player_->set_position(*start);
        } else {
            ctx_.log.error("RoomScene: room '" + id + "' has no player start point");
        }
    }

    room_->call_hook("on_load");
    ctx_.scripting.set_current_scope(ctx_.scripting.global_scope());
}

void RoomScene::unload_room() {
    if (room_) {
        room_->call_hook("on_unload");
    }
    ctx_.scripting.cancel_scope(room_scope_);
}

void RoomScene::say(const std::string& text) {
    if (text.empty()) {
        return;
    }
    geom::Point pos{640.0f, 360.0f};
    if (player_) {
        pos = player_->position();
        pos.y -= kSpeechRise;
    }
    sf::Color color(230, 230, 230);
    if (const Character* c = cast_.character(player_char_)) {
        color = c->speech_color;
    }
    float duration = 0.5f + 0.06f * static_cast<float>(text.size());
    duration = std::clamp(duration, 1.0f, 7.0f);
    speech_.show(text, pos, color, duration);
}

void RoomScene::handle_event(const sf::Event& event) {
    if (event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::Escape) {
        ctx_.scenes.quit();
        return;
    }
    if (event.type != sf::Event::MouseButtonReleased ||
        event.mouseButton.button != sf::Mouse::Left) {
        return;
    }
    if (speech_.active()) {
        speech_.skip(); // a click first dismisses active speech
        return;
    }
    if (!room_) {
        return;
    }
    const geom::Point world{static_cast<float>(event.mouseButton.x),
                            static_cast<float>(event.mouseButton.y)};

    if (const RoomHotspot* hs = room_->hotspot_at(world)) {
        const std::string verb = hs->default_verb.empty() ? "look_at" : hs->default_verb;
        const std::optional<std::string> caption = room_->call_hotspot(hs->id, verb);
        say(caption.value_or("No pasa nada."));
        return;
    }
    if (player_ && room_->data().is_walkable(world)) {
        player_->move_to(world);
    }
}

void RoomScene::update(float dt) {
    if (player_ && room_) {
        player_->update(dt, room_->data());
    }
    speech_.update(dt);
}

void RoomScene::draw(sf::RenderTarget& target) const {
    if (room_) {
        renderer_.draw(target,
                       room_->data(),
                       room_dir_,
                       ctx_.resources,
                       player_ ? &*player_ : nullptr,
                       ctx_.log);
    }
    speech_.draw(target, font_);
}

} // namespace pac::pnc
