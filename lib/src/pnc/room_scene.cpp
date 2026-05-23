#include "engine/pnc/room_scene.hpp"

#include "engine/core/diagnostics.hpp"
#include "engine/core/display.hpp"
#include "engine/core/engine_context.hpp"
#include "engine/core/resource_cache.hpp"
#include "engine/core/resource_source.hpp"
#include "engine/core/save_service.hpp"
#include "engine/core/scene_manager.hpp"
#include "engine/core/scene_params.hpp"
#include "engine/core/scripting.hpp"
#include "engine/core/strings.hpp"
#include "engine/gfx/animated_sprite.hpp"
#include "engine/pnc/data_error.hpp"
#include "engine/pnc/room.hpp"
#include "pnc/dialog_internal.hpp"

#include <SFML/Graphics/Font.hpp>
#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/Graphics/Text.hpp>
#include <SFML/Graphics/View.hpp>
#include <SFML/Window/Event.hpp>
#include <sol/sol.hpp>

#include <algorithm>
#include <optional>
#include <utility>
#include <variant>

namespace pac::pnc {

namespace {
constexpr float kScenerFraction = 0.85f;
constexpr float kAvatarScale = 1.1f;
constexpr float kSpeechRise = 250.0f;

/// Whether `verb` is offered for an operand with these affordances. `look_at` is
/// always allowed; every other verb must be listed (design 04 §Affordances).
bool affordance_ok(const std::vector<std::string>* affordances, Verb verb) {
    if (verb == Verb::LOOK_AT) {
        return true;
    }
    if (!affordances) {
        return false;
    }
    const std::string vid(verb_id(verb));
    return std::find(affordances->begin(), affordances->end(), vid) != affordances->end();
}

std::optional<pac::core::StateValue> to_state_value(const sol::object& v) {
    if (v.is<bool>()) {
        return pac::core::StateValue{v.as<bool>()};
    }
    if (v.is<double>()) {
        return pac::core::StateValue{v.as<double>()};
    }
    if (v.is<std::string>()) {
        return pac::core::StateValue{v.as<std::string>()};
    }
    return std::nullopt;
}
} // namespace

/// inventory.lua + game.lua behavior tables (sol kept out of the header).
struct RoomScene::Lua {
    sol::table inventory_table;
    bool inventory_valid = false;
    sol::table game_table;
    bool game_valid = false;
    pac::core::Diagnostics* log = nullptr;

    std::optional<std::string> call_inventory(const std::string& item,
                                              const std::string& verb,
                                              std::optional<std::string> operand) {
        if (!inventory_valid) {
            return std::nullopt;
        }
        sol::optional<sol::table> t = inventory_table[item];
        if (!t) {
            return std::nullopt;
        }
        sol::optional<sol::protected_function> fn = (*t)[verb];
        if (!fn) {
            return std::nullopt;
        }
        const sol::protected_function_result r = operand ? (*fn)(*operand) : (*fn)();
        if (!r.valid()) {
            const sol::error e = r;
            log->error(std::string("inventory '" + item + "." + verb + "' error: ") + e.what());
            return std::nullopt;
        }
        sol::optional<std::string> cap = r;
        return cap ? std::optional<std::string>(*cap) : std::nullopt;
    }

    std::optional<std::string>
    call_game(const std::string& verb, const std::string& a, std::optional<std::string> b) {
        if (!game_valid) {
            return std::nullopt;
        }
        sol::optional<sol::table> fallbacks = game_table["fallbacks"];
        if (!fallbacks) {
            return std::nullopt;
        }
        sol::optional<sol::protected_function> fn = (*fallbacks)[verb];
        if (!fn) {
            return std::nullopt;
        }
        const sol::protected_function_result r = b ? (*fn)(a, *b) : (*fn)(a);
        if (!r.valid()) {
            const sol::error e = r;
            log->error(std::string("game fallback '" + verb + "' error: ") + e.what());
            return std::nullopt;
        }
        sol::optional<std::string> cap = r;
        return cap ? std::optional<std::string>(*cap) : std::nullopt;
    }
};

RoomScene::RoomScene(pac::core::EngineContext& ctx, const pac::core::SceneParams& params)
    : ctx_(ctx) {
    cast_path_ = params.get_or("cast", "cast.yaml");
    rooms_dir_ = params.get_or("rooms", "rooms");
    start_room_ = params.get_or("start_room", "");
    player_char_ = params.get_or("player", "");
    font_path_ = params.get_or("font", "");
    inventory_path_ = params.get_or("inventory", "");
    inventory_logic_ = params.get_or("inventory_logic", "");
    logic_path_ = params.get_or("logic", "");
}

RoomScene::~RoomScene() = default;

float RoomScene::scenery_height() const {
    return static_cast<float>(ctx_.display.virtual_resolution().y) * kScenerFraction;
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
    if (!inventory_path_.empty()) {
        try {
            inventory_.set_definitions(parse_inventory(ctx_.resources.read_text(inventory_path_)));
        } catch (const std::exception& e) {
            ctx_.log.error(std::string("RoomScene: inventory: ") + e.what());
        }
    }

    const sf::Vector2u vres = ctx_.display.virtual_resolution();
    const float scenery = scenery_height();
    panel_.emplace(sf::FloatRect(0.0f,
                                 scenery,
                                 static_cast<float>(vres.x),
                                 static_cast<float>(vres.y) - scenery),
                   font_);

    lua_ = std::make_unique<Lua>();
    lua_->log = &ctx_.log;

    sol::state& L = ctx_.scripting.lua();
    auto load_table = [&](const std::string& logical) -> std::optional<sol::table> {
        if (logical.empty()) {
            return std::nullopt;
        }
        try {
            sol::load_result chunk = L.load(ctx_.resources.read_text(logical), "@" + logical);
            if (!chunk.valid()) {
                const sol::error e = chunk;
                ctx_.log.error(std::string("RoomScene: ") + e.what());
                return std::nullopt;
            }
            const sol::protected_function_result r = sol::protected_function(chunk)();
            if (!r.valid()) {
                const sol::error e = r;
                ctx_.log.error(std::string("RoomScene: ") + e.what());
                return std::nullopt;
            }
            sol::optional<sol::table> t = r;
            return t ? std::optional<sol::table>(*t) : std::nullopt;
        } catch (const std::exception& e) {
            ctx_.log.error(std::string("RoomScene: ") + e.what());
            return std::nullopt;
        }
    };
    if (auto t = load_table(inventory_logic_)) {
        lua_->inventory_table = *t;
        lua_->inventory_valid = true;
    }
    if (auto t = load_table(logic_path_)) {
        lua_->game_table = *t;
        lua_->game_valid = true;
    }

    // --- bind the genre Lua API (captures this; one RoomScene is active) ---
    L.set_function("change_room", [this](std::string id, sol::optional<std::string> e) {
        api_change_room(id, e.value_or(""));
    });
    L.set_function("current_room", [this]() { return api_current_room(); });
    L.set_function("set_region_state",
                   [this](std::string id, std::string s) { api_set_region_state(id, s); });
    L.set_function("get_region_state", [this](std::string id) { return api_get_region_state(id); });
    L.set_function("show_object", [this](std::string id) { api_show_object(id, true); });
    L.set_function("hide_object", [this](std::string id) { api_show_object(id, false); });
    L.set_function("enable_hotspot", [this](std::string id) { api_set_hotspot_enabled(id, true); });
    L.set_function("disable_hotspot",
                   [this](std::string id) { api_set_hotspot_enabled(id, false); });
    L.set_function("has_item", [this](std::string id) { return inventory_.has(id); });
    L.set_function("add_item", [this](std::string id) { inventory_.add(id); });
    L.set_function("remove_item", [this](std::string id) { inventory_.remove(id); });
    L.set_function("list_items", [this]() {
        sol::state& s = ctx_.scripting.lua();
        sol::table out = s.create_table();
        int i = 1;
        for (const std::string& id : inventory_.list()) {
            out[i++] = id;
        }
        return out;
    });
    L.set_function("talk",
                   [this](std::string speaker, std::string text) { api_talk(speaker, text); });
    L.set_function("start_dialog", [this](std::string npc_id) { api_start_dialog(npc_id); });
    // `to = END` is injected per-dialog by DialogRuntime::start as a unique
    // sentinel table — no engine-wide binding needed here.

    // Room-view-state controls (issue #32). `block_input` gates clicks during
    // cutscene-like sections; `unblock_input` restores normal play.
    // `set_room_view_state(name)` is the general-purpose setter for the same
    // states. DIALOG and MENU stay engine-managed: a script asking to enter
    // them is a logic error and produces a warning rather than a transition.
    L.set_function("block_input", [this]() {
        if (view_state_ == ViewState::COMMAND) {
            view_state_ = ViewState::BLOCKED;
        }
    });
    L.set_function("unblock_input", [this]() {
        if (view_state_ == ViewState::BLOCKED) {
            view_state_ = ViewState::COMMAND;
        }
    });
    L.set_function("set_room_view_state", [this](std::string name) {
        if (name == "command") {
            view_state_ = ViewState::COMMAND;
        } else if (name == "blocked") {
            view_state_ = ViewState::BLOCKED;
        } else if (name == "dialog" || name == "menu") {
            ctx_.log.warn("set_room_view_state: '" + name +
                          "' is engine-managed and not script-settable");
        } else {
            ctx_.log.warn("set_room_view_state: unknown state '" + name + "'");
        }
    });
    L.set_function("set_room_state", [this](std::string key, sol::object v) {
        if (auto value = to_state_value(v)) {
            api_set_room_state(key, *value);
        } else {
            ctx_.log.error("set_room_state('" + key + "'): only bool/number/string allowed");
        }
    });
    L.set_function("get_room_state", [this](std::string key) -> sol::object {
        sol::state& s = ctx_.scripting.lua();
        const auto v = api_get_room_state(key);
        if (!v) {
            return sol::make_object(s, sol::lua_nil);
        }
        return std::visit([&s](const auto& x) { return sol::make_object(s, x); }, *v);
    });

    // Continue: TitleScreen stages a loaded GameState that we apply in place
    // of the manifest's default start. The staged state already names the
    // room to load and the player pose to seat at, so we skip load_room here
    // and let restore()'s pending change_pending_ drive it. If restore()
    // rejects (corrupted scene id, unsupported save_version, ...) we fall
    // back to the manifest's start_room so the player at least ends up in
    // a playable state.
    if (auto staged = ctx_.saves.take_pending_restore()) {
        if (restore(*staged)) {
            return;
        }
        ctx_.log.warn("RoomScene: staged restore was rejected; falling back to start_room");
    }

    if (start_room_.empty()) {
        ctx_.log.error("RoomScene: no 'start_room'");
        return;
    }
    load_room(start_room_, "");
}

void RoomScene::leave() {
    unload_room();
}

void RoomScene::load_room(const std::string& id, const std::string& entry_point) {
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
    current_room_id_ = id;
    current_zone_.clear();
    builder_.cancel();

    room_scope_ = ctx_.scripting.open_scope();
    ctx_.scripting.set_current_scope(room_scope_);
    room_->load_behavior(ctx_.scripting, ctx_.resources, lua_logical, ctx_.log);

    // Restore persisted runtime state (else RoomRuntime's defaults from YAML
    // win). Same lookup pattern for regions, hotspot_enabled, object_visible.
    if (const auto it = region_state_persist_.find(id); it != region_state_persist_.end()) {
        for (const auto& [region_id, state] : it->second) {
            room_->set_region_state(region_id, state);
        }
    }
    if (const auto it = hotspot_enabled_persist_.find(id); it != hotspot_enabled_persist_.end()) {
        for (const auto& [hs_id, enabled] : it->second) {
            room_->set_hotspot_enabled(hs_id, enabled);
        }
    }
    if (const auto it = object_visible_persist_.find(id); it != object_visible_persist_.end()) {
        for (const auto& [obj_id, visible] : it->second) {
            room_->set_object_visible(obj_id, visible);
        }
    }

    const sf::Vector2u vres = ctx_.display.virtual_resolution();
    const sf::Vector2f viewport(static_cast<float>(vres.x), scenery_height());
    const sf::Vector2u room_size =
        compute_room_bounds(room_->data(), room_dir_, ctx_.resources, viewport, ctx_.log);
    camera_.emplace(viewport, room_size);
    // The player can only reach the walkable area, so map that span onto the
    // full scroll range — otherwise the parts of the background above/around the
    // walkable polygon would never come into view (issue #28).
    if (!room_->data().walkable.empty()) {
        camera_->set_follow_bounds(geom::polygon_bounds(room_->data().walkable));
    }

    if (!player_) {
        if (auto avatar = make_avatar(player_char_)) {
            player_.emplace(std::move(*avatar));
        }
    }
    seat_player(entry_point);
    if (player_ && camera_) {
        camera_->snap_to(player_->position());
    }
    spawn_room_npcs();

    room_->call_hook("on_load");
    ctx_.scripting.set_current_scope(ctx_.scripting.global_scope());
}

std::optional<Avatar> RoomScene::make_avatar(const std::string& character_id) {
    const Character* character = cast_.character(character_id);
    if (!character) {
        ctx_.log.error("RoomScene: character '" + character_id + "' not in cast");
        return std::nullopt;
    }
    const Appearance* app = cast_.appearance(character->appearance);
    if (!app || app->type != "animated_sprite" || app->sprite.empty()) {
        ctx_.log.error("RoomScene: character '" + character_id +
                       "' has no usable animated_sprite appearance");
        return std::nullopt;
    }
    try {
        return Avatar(gfx::load_animated_sprite(ctx_.resources, app->sprite), kAvatarScale);
    } catch (const std::exception& e) {
        ctx_.log.error(std::string("RoomScene: appearance for '" + character_id + "': ") +
                       e.what());
        return std::nullopt;
    }
}

void RoomScene::spawn_room_npcs() {
    if (!room_) {
        return;
    }
    const RoomData& data = room_->data();
    for (const RoomAvatarPlacement& placement : data.avatars) {
        if (placement.player) {
            continue;
        }
        auto avatar = make_avatar(placement.id);
        if (!avatar) {
            continue;
        }
        const geom::Point* start = data.point(placement.start);
        if (!start) {
            ctx_.log.error("RoomScene: NPC '" + placement.id + "' has no start point '" +
                           placement.start + "'");
            continue;
        }
        avatar->set_position(*start);
        avatar->face(placement.orientation);
        room_->add_npc(placement.id, std::move(*avatar));
    }
}

void RoomScene::seat_player(const std::string& entry_point) {
    if (!player_ || !room_) {
        return;
    }
    const RoomData& data = room_->data();
    const geom::Point* start = nullptr;
    std::string orientation = "down";
    if (!entry_point.empty()) {
        start = data.point(entry_point);
    }
    if (!start) {
        for (const RoomAvatarPlacement& a : data.avatars) {
            if (a.player) {
                start = data.point(a.start);
                orientation = a.orientation;
                break;
            }
        }
    }
    if (!start) {
        start = data.point("player_start");
    }
    if (start) {
        player_->set_position(*start);
        player_->face(orientation);
    } else {
        ctx_.log.error("RoomScene: room '" + current_room_id_ + "' has no player start");
    }
}

void RoomScene::unload_room() {
    if (room_) {
        room_->call_hook("on_unload");
        // Snapshot live runtime state so per-room flags persist across the
        // change (they're folded back in by load_room or by GameState restore).
        for (const auto& [region_id, region] : room_->data().regions) {
            region_state_persist_[current_room_id_][region_id] = room_->region_state(region_id);
        }
        for (const auto& [hs_id, hs] : room_->data().hotspots) {
            hotspot_enabled_persist_[current_room_id_][hs_id] = room_->hotspot_enabled(hs_id);
        }
        for (const auto& [obj_id, obj] : room_->data().objects) {
            object_visible_persist_[current_room_id_][obj_id] = room_->object_visible(obj_id);
        }
    }
    ctx_.scripting.cancel_scope(room_scope_);
    // An in-progress dialog references the outgoing room's NPC avatars; the
    // room change kills both. Cancelling the dialog scope also reaps any
    // `run`-task spawned from the option (and anything that task spawned).
    if (dialog_scope_ != 0) {
        ctx_.scripting.cancel_scope(dialog_scope_);
        dialog_scope_ = 0;
        run_task_ = 0;
    }
    dialog_.reset();
    view_state_ = ViewState::COMMAND;
}

void RoomScene::say(const std::string& text, sf::Color color) {
    geom::Point pos{640.0f, 360.0f};
    if (player_) {
        pos = player_->position();
    }
    say_at(text, color, pos);
}

void RoomScene::say_at(const std::string& text, sf::Color color, geom::Point world) {
    if (text.empty()) {
        return;
    }
    world.y -= kSpeechRise;
    float duration = 0.5f + 0.06f * static_cast<float>(text.size());
    duration = std::clamp(duration, 1.0f, 7.0f);
    speech_.show(text, world, color, duration);
}

geom::Point RoomScene::virtual_to_world(sf::Vector2f vp) const {
    if (!camera_) {
        return {vp.x, vp.y};
    }
    const sf::Vector2f tl = camera_->top_left();
    return {tl.x + vp.x, tl.y + vp.y};
}

void RoomScene::handle_event(const sf::Event& event) {
    // Track the pointer (virtual coords) so the top bar can preview the element
    // under the cursor each frame (issue #28). Coordinates are already mapped to
    // virtual space by the application's event rewrite.
    if (event.type == sf::Event::MouseMoved) {
        hover_vp_ = {static_cast<float>(event.mouseMove.x), static_cast<float>(event.mouseMove.y)};
        return;
    }
    if (event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::Escape) {
        // ESC toggles the in-game pause/save/load menu from COMMAND, and
        // closes it again from MENU. DIALOG / BLOCKED ignore ESC so the
        // player can't accidentally lose a dialog mid-conversation; in MVP
        // the dialog must run to its end (or be skipped via clicks).
        if (view_state_ == ViewState::COMMAND) {
            view_state_ = ViewState::MENU;
        } else if (view_state_ == ViewState::MENU) {
            view_state_ = ViewState::COMMAND;
        }
        return;
    }
    if (view_state_ == ViewState::MENU) {
        handle_menu_event(event);
        return;
    }
    if (event.type != sf::Event::MouseButtonReleased ||
        event.mouseButton.button != sf::Mouse::Left) {
        return;
    }
    const sf::Vector2f vp{static_cast<float>(event.mouseButton.x),
                          static_cast<float>(event.mouseButton.y)};
    if (speech_.active()) {
        speech_.skip();
        return;
    }
    if (view_state_ == ViewState::BLOCKED) {
        return;
    }
    if (view_state_ == ViewState::DIALOG && dialog_) {
        if (panel_ && panel_->contains(vp)) {
            const int idx = panel_->click_option(vp, dialog_->options().size());
            if (idx >= 0) {
                dialog_->choose(idx);
            }
        }
        return;
    }

    if (panel_ && panel_->contains(vp)) {
        const PanelIntent intent = panel_->click(vp, inventory_);
        if (intent.kind == PanelIntent::Kind::SELECT_VERB) {
            builder_.select_verb(intent.verb);
        } else if (intent.kind == PanelIntent::Kind::CLICK_INVENTORY) {
            object_clicked({ObjectKind::INVENTORY_OBJECT, intent.item_id});
        }
        return;
    }

    if (!room_) {
        return;
    }
    const geom::Point world = virtual_to_world(vp);
    if (const RoomHotspot* hs = room_->hotspot_at(world)) {
        object_clicked({ObjectKind::ROOM_OBJECT, hs->id});
    } else if (builder_.state() != CommandBuilder::State::IDLE) {
        // Clicking empty scenery while a command is being built cancels it and
        // returns to IDLE (issue #28).
        builder_.cancel();
    } else if (player_) {
        // IDLE: walk there. A click outside the walkable area routes to the
        // nearest reachable point instead of doing nothing (issue #28).
        const geom::Polygon& walk = room_->data().walkable;
        const geom::Point target =
            walk.empty() ? world : geom::closest_point_in_polygon(world, walk);
        player_->move_to(target);
    }
}

void RoomScene::object_clicked(const ObjectRef& object) {
    const Operand info = resolve_operand(object);
    if (builder_.state() == CommandBuilder::State::IDLE) {
        builder_.select_verb(info.default_verb);
    }
    const std::optional<Verb> verb = builder_.verb();
    if (!verb) {
        return;
    }
    builder_.provide_object(object, affordance_ok(info.affordances, *verb), info.combinable);
    execute_ready_command();
}

RoomScene::Operand RoomScene::resolve_operand(const ObjectRef& object) const {
    Operand info;
    info.name = object.id; // fallback when the id is unknown
    if (object.kind == ObjectKind::ROOM_OBJECT && room_) {
        const auto it = room_->data().hotspots.find(object.id);
        if (it != room_->data().hotspots.end()) {
            info.found = true;
            info.name = it->second.name;
            info.affordances = &it->second.affordances;
            if (auto v = verb_from_id(it->second.default_verb)) {
                info.default_verb = *v;
            }
        }
    } else if (object.kind == ObjectKind::INVENTORY_OBJECT) {
        if (const InventoryItem* item = inventory_.item(object.id)) {
            info.found = true;
            info.name = item->name;
            info.affordances = &item->affordances;
            info.combinable = item->combinable;
            if (auto v = verb_from_id(item->default_verb)) {
                info.default_verb = *v;
            }
        }
    }
    return info;
}

void RoomScene::execute_ready_command() {
    if (builder_.state() != CommandBuilder::State::COMMAND_READY) {
        return;
    }
    const std::optional<Command> cmd = builder_.take_ready();
    if (!cmd) {
        return;
    }
    // Walk to the room operand's approach point, if any (caption shows immediately).
    const ObjectRef* room_target = nullptr;
    if (cmd->param2 && cmd->param2->kind == ObjectKind::ROOM_OBJECT) {
        room_target = &*cmd->param2;
    } else if (cmd->param1.kind == ObjectKind::ROOM_OBJECT) {
        room_target = &cmd->param1;
    }
    if (room_target && room_ && player_) {
        const auto it = room_->data().hotspots.find(room_target->id);
        if (it != room_->data().hotspots.end() && it->second.approach) {
            player_->move_to(*it->second.approach);
        }
    }

    const std::optional<std::string> caption = dispatch(*cmd);
    // If dispatch flipped us into a non-command state (e.g. a `talk_to` handler
    // called `start_dialog`), the dialog's first NPC line is already on screen;
    // suppress the fallback caption that would otherwise overwrite it.
    if (view_state_ != ViewState::COMMAND) {
        builder_.finish_execution();
        return;
    }
    sf::Color color(230, 230, 230);
    if (const Character* c = cast_.character(player_char_)) {
        color = c->speech_color;
    }
    say(caption.value_or("No pasa nada."), color);
    builder_.finish_execution();
}

std::optional<std::string> RoomScene::dispatch(const Command& cmd) {
    const std::string verb(verb_id(cmd.verb));
    const ObjectRef& p1 = cmd.param1;
    if (cmd.param2) {
        const ObjectRef& p2 = *cmd.param2;
        if (p1.kind == ObjectKind::INVENTORY_OBJECT) {
            if (auto c = lua_->call_inventory(p1.id, verb, p2.id)) {
                return c;
            }
        }
        if (p2.kind == ObjectKind::ROOM_OBJECT && room_) {
            if (auto c = room_->call_hotspot(p2.id, verb, p1.id)) {
                return c;
            }
        }
        return lua_->call_game(verb, p1.id, p2.id);
    }
    if (p1.kind == ObjectKind::INVENTORY_OBJECT) {
        if (auto c = lua_->call_inventory(p1.id, verb, std::nullopt)) {
            return c;
        }
    } else if (p1.kind == ObjectKind::ROOM_OBJECT && room_) {
        if (auto c = room_->call_hotspot(p1.id, verb, std::nullopt)) {
            return c;
        }
    }
    return lua_->call_game(verb, p1.id, std::nullopt);
}

std::string RoomScene::command_preview() const {
    const std::optional<Verb> verb = builder_.verb();
    if (!verb) {
        return {};
    }
    const pac::core::Strings& strings = ctx_.strings;
    std::string s = strings.verb_label(std::string(verb_id(*verb)));
    if (builder_.param1()) {
        s += " " + resolve_operand(*builder_.param1()).name;
        if (*verb == Verb::USE || *verb == Verb::GIVE) {
            s += " " + strings.connector(std::string(verb_id(*verb)));
        }
    }
    if (builder_.param2()) {
        s += " " + resolve_operand(*builder_.param2()).name;
    }
    return s;
}

std::string RoomScene::top_bar_text() const {
    // Only the command view shows a command bar (dialog/menu/blocked don't).
    if (view_state_ != ViewState::COMMAND) {
        return {};
    }
    const pac::core::Strings& strings = ctx_.strings;

    // Resolve what the pointer is over: a panel verb, an operand (room hotspot or
    // inventory item), a walkable floor tile, or nothing.
    enum class Hover { NONE, VERB, OPERAND, WALKABLE };
    Hover hover = Hover::NONE;
    Verb hover_verb = Verb::LOOK_AT;
    ObjectRef hover_obj;
    if (panel_ && panel_->contains(hover_vp_)) {
        const PanelIntent in = panel_->click(hover_vp_, inventory_);
        if (in.kind == PanelIntent::Kind::SELECT_VERB) {
            hover = Hover::VERB;
            hover_verb = in.verb;
        } else if (in.kind == PanelIntent::Kind::CLICK_INVENTORY) {
            hover = Hover::OPERAND;
            hover_obj = {ObjectKind::INVENTORY_OBJECT, in.item_id};
        }
    } else if (room_) {
        const geom::Point world = virtual_to_world(hover_vp_);
        if (const RoomHotspot* hs = room_->hotspot_at(world)) {
            hover = Hover::OPERAND;
            hover_obj = {ObjectKind::ROOM_OBJECT, hs->id};
        } else if (room_->data().is_walkable(world)) {
            hover = Hover::WALKABLE;
        }
    }

    // IDLE: the bar just names what's under the pointer (design 04 §Top bar).
    if (builder_.state() == CommandBuilder::State::IDLE) {
        switch (hover) {
        case Hover::VERB:
            return strings.verb_label(std::string(verb_id(hover_verb)));
        case Hover::OPERAND:
            return resolve_operand(hover_obj).name;
        case Hover::WALKABLE:
            return strings.ui_label("walk_to");
        case Hover::NONE:
            break;
        }
        return {};
    }

    // Building a command: show the committed preview, and append the hovered
    // operand only when it would be a valid next argument.
    std::string base = command_preview();
    if (hover == Hover::OPERAND) {
        const Operand info = resolve_operand(hover_obj);
        const std::optional<Verb> verb = builder_.verb();
        if (verb && builder_.would_accept(hover_obj, affordance_ok(info.affordances, *verb))) {
            base += " " + info.name;
        }
    }
    return base;
}

void RoomScene::update(float dt) {
    if (change_pending_) {
        change_pending_ = false;
        const std::string id = pending_room_;
        const std::string entry = pending_entry_;
        const bool was_restore = pending_restore_player_.has_value();
        unload_room();
        load_room(id, entry);
        // Restore overrides the default seat from load_room when restoring a save.
        if (pending_restore_player_ && player_) {
            player_->set_position({pending_restore_player_->x, pending_restore_player_->y});
            player_->face(pending_restore_player_->facing);
            if (camera_) {
                camera_->snap_to(player_->position());
            }
            pending_restore_player_.reset();
        }
        // Autosave on every room change *except* when the change was the
        // restoring of a save: that would round-trip the save we just loaded
        // (harmless, but a wasted write and a confusing log line).
        if (!was_restore && can_save()) {
            ctx_.saves.save(pac::core::SaveService::kAutosaveSlot, snap());
        }
        return;
    }
    if (player_ && room_) {
        player_->update(dt, room_->data());
        room_->update_npcs(dt);
        if (camera_) {
            camera_->follow(player_->position());
        }
        check_zones();
    }
    speech_.update(dt);
    if (dialog_) {
        dialog_->update();
        if (dialog_->ended()) {
            dialog_.reset();
            // Reap the dialog scope: cancel the run-task (if still around)
            // and anything else that was spawned within the dialog. No-op if
            // the scope was already cancelled by unload_room / restore.
            if (dialog_scope_ != 0) {
                ctx_.scripting.cancel_scope(dialog_scope_);
                dialog_scope_ = 0;
                run_task_ = 0;
            }
            view_state_ = ViewState::COMMAND;
        }
    }
}

void RoomScene::check_zones() {
    if (!player_ || !room_) {
        return;
    }
    const Zone* zone = room_->zone_at(player_->position());
    const std::string zone_id = zone ? zone->id : std::string();
    if (zone_id == current_zone_) {
        return;
    }
    if (!current_zone_.empty()) {
        room_->call_zone_hook("on_zone_exit", current_zone_);
    }
    current_zone_ = zone_id;
    if (!current_zone_.empty()) {
        ctx_.scripting.set_current_scope(room_scope_);
        room_->call_zone_hook("on_zone_enter", current_zone_);
        ctx_.scripting.set_current_scope(ctx_.scripting.global_scope());
    }
}

void RoomScene::draw(sf::RenderTarget& target) const {
    const sf::Vector2u vres = ctx_.display.virtual_resolution();
    if (room_ && camera_) {
        sf::View scenery(camera_->view_rect());
        scenery.setViewport(ctx_.display.viewport_for(
            sf::FloatRect(0.0f, 0.0f, static_cast<float>(vres.x), scenery_height())));
        target.setView(scenery);
        renderer_.draw(target,
                       *room_,
                       room_dir_,
                       ctx_.resources,
                       player_ ? &*player_ : nullptr,
                       room_->npcs(),
                       ctx_.log);
        speech_.draw(target, font_); // world coordinates, over the scenery
    }

    target.setView(ctx_.display.view());
    if (panel_) {
        if (view_state_ == ViewState::DIALOG && dialog_) {
            std::vector<std::string> labels;
            labels.reserve(dialog_->options().size());
            for (const DialogOption& opt : dialog_->options()) {
                labels.push_back(opt.text);
            }
            panel_->draw_options(target, labels);
        } else {
            panel_->draw(target, ctx_.strings, inventory_, top_bar_text(), builder_.verb());
        }
    }
    if (view_state_ == ViewState::MENU) {
        draw_menu(target);
    }
}

// --- genre Lua API targets ---

void RoomScene::api_change_room(const std::string& id, const std::string& entry_point) {
    change_pending_ = true;
    pending_room_ = id;
    pending_entry_ = entry_point;
}

void RoomScene::api_set_region_state(const std::string& region_id, const std::string& state) {
    if (room_) {
        room_->set_region_state(region_id, state);
    }
    region_state_persist_[current_room_id_][region_id] = state;
}

std::string RoomScene::api_get_region_state(const std::string& region_id) const {
    return room_ ? room_->region_state(region_id) : std::string();
}

void RoomScene::api_show_object(const std::string& object_id, bool visible) {
    if (room_) {
        room_->set_object_visible(object_id, visible);
    }
}

void RoomScene::api_set_hotspot_enabled(const std::string& hotspot_id, bool enabled) {
    if (room_) {
        room_->set_hotspot_enabled(hotspot_id, enabled);
    }
}

void RoomScene::api_set_room_state(const std::string& key, pac::core::StateValue value) {
    room_state_[current_room_id_][key] = std::move(value);
}

std::optional<pac::core::StateValue> RoomScene::api_get_room_state(const std::string& key) const {
    const auto room = room_state_.find(current_room_id_);
    if (room == room_state_.end()) {
        return std::nullopt;
    }
    const auto it = room->second.find(key);
    if (it == room->second.end()) {
        return std::nullopt;
    }
    return it->second;
}

void RoomScene::api_talk(const std::string& speaker_id, const std::string& text) {
    sf::Color color(230, 230, 230);
    if (const Character* c = cast_.character(speaker_id)) {
        color = c->speech_color;
    }
    say(text, color);
}

void RoomScene::api_start_dialog(const std::string& npc_id) {
    if (view_state_ != ViewState::COMMAND) {
        ctx_.log.error("start_dialog('" + npc_id + "'): a dialog is already running");
        return;
    }
    DialogHost host;
    host.speak_npc = [this, npc_id](const std::string& text) {
        sf::Color color(230, 230, 230);
        if (const Character* c = cast_.character(npc_id)) {
            color = c->speech_color;
        }
        geom::Point pos{640.0f, 360.0f};
        if (room_) {
            if (const Avatar* a = room_->npc(npc_id)) {
                pos = a->position();
            }
        }
        say_at(text, color, pos);
    };
    host.speak_player = [this](const std::string& text) {
        sf::Color color(230, 230, 230);
        if (const Character* c = cast_.character(player_char_)) {
            color = c->speech_color;
        }
        const geom::Point pos = player_ ? player_->position() : geom::Point{640.0f, 360.0f};
        say_at(text, color, pos);
    };
    host.is_speaking = [this]() { return speech_.active(); };
    // `once`-consumption persists in the global StateStore under the
    // engine-reserved `__dialog.<id>.<node>.<idx>` prefix, so it survives
    // dialog end + restart and folds into GameState on save.
    auto consumed_key = [npc_id](const std::string& node, int idx) {
        return "__dialog." + npc_id + "." + node + "." + std::to_string(idx);
    };
    host.is_option_consumed = [this, consumed_key](const std::string& node, int idx) {
        const auto v = ctx_.state.get(consumed_key(node, idx));
        return v && std::holds_alternative<bool>(*v) && std::get<bool>(*v);
    };
    host.mark_option_consumed = [this, consumed_key](const std::string& node, int idx) {
        ctx_.state.set(consumed_key(node, idx), true);
    };
    // Dialog scope: spawn run-callbacks (and anything they spawn) here so they
    // are bounded by the dialog's lifetime. Cancelling the scope on dialog end
    // / room change kills them deterministically — `spawn` from inside `run`
    // inherits this scope per the scheduler's `current_scope` discipline.
    dialog_scope_ = ctx_.scripting.open_scope();
    run_task_ = 0;
    host.spawn_run = [this](DialogRunFn& carrier) {
        // We don't have a C++ entry point for `spawn`, so go through the Lua
        // global that's bound to `Scripting::Impl::spawn(fn, current_scope)`.
        // Setting current_scope to dialog_scope_ first places the task (and
        // anything it spawns) under the dialog's lifetime.
        const pac::core::ScopeId prev = ctx_.scripting.current_scope();
        ctx_.scripting.set_current_scope(dialog_scope_);
        sol::state& L = ctx_.scripting.lua();
        sol::function spawn_fn = L["spawn"];
        const sol::protected_function_result r = sol::protected_function(spawn_fn)(carrier.fn);
        ctx_.scripting.set_current_scope(prev);
        if (!r.valid()) {
            const sol::error e = r;
            ctx_.log.error(std::string("dialog run spawn: ") + e.what());
            run_task_ = 0;
            return;
        }
        run_task_ = r.get<pac::core::TaskId>();
    };
    host.is_run_running = [this]() {
        return run_task_ != 0 && ctx_.scripting.is_task_alive(run_task_);
    };
    // The `run` callback may queue a room change via `change_room`; per design
    // 04, the dialog ends before the change is honored so we never speak a
    // line into the outgoing room. `unload_room` reaps the scope.
    host.should_end = [this]() { return change_pending_; };

    auto rt =
        DialogRuntime::start(ctx_.scripting, ctx_.resources, ctx_.log, npc_id, std::move(host));
    if (!rt) {
        ctx_.scripting.cancel_scope(dialog_scope_);
        dialog_scope_ = 0;
        return;
    }
    dialog_.emplace(std::move(*rt));
    view_state_ = ViewState::DIALOG;
}

// --- pause / save / load menu (M5c/2) ---

std::vector<RoomScene::MenuButton> RoomScene::menu_buttons() const {
    const sf::Vector2u vres = ctx_.display.virtual_resolution();
    const float w = 480.0f;
    const float row_h = 56.0f;
    const float gap = 12.0f;
    const float footer_y_gap = 32.0f;
    const float total_h = 3.0f * row_h + 2.0f * gap + footer_y_gap + row_h;
    const float left = (static_cast<float>(vres.x) - w) / 2.0f;
    const float top = (static_cast<float>(vres.y) - total_h) / 2.0f;
    const float btn_w = 100.0f;
    const float label_w = w - 2.0f * btn_w - 2.0f * gap;

    auto save_action = [](int slot) {
        return slot == 1   ? MenuAction::SAVE_SLOT_1
               : slot == 2 ? MenuAction::SAVE_SLOT_2
                           : MenuAction::SAVE_SLOT_3;
    };
    auto load_action = [](int slot) {
        return slot == 1   ? MenuAction::LOAD_SLOT_1
               : slot == 2 ? MenuAction::LOAD_SLOT_2
                           : MenuAction::LOAD_SLOT_3;
    };

    std::vector<MenuButton> out;
    for (int slot = 1; slot <= 3; ++slot) {
        const float y = top + static_cast<float>(slot - 1) * (row_h + gap);
        const float save_x = left + label_w + gap;
        const float load_x = save_x + btn_w + gap;
        out.push_back({{save_x, y, btn_w, row_h}, save_action(slot), true});
        out.push_back({{load_x, y, btn_w, row_h}, load_action(slot), ctx_.saves.slot_exists(slot)});
    }
    // Footer: Resume (left), Quit to title (right).
    const float footer_y = top + 3.0f * (row_h + gap) - gap + footer_y_gap;
    const float footer_btn_w = (w - gap) / 2.0f;
    out.push_back({{left, footer_y, footer_btn_w, row_h}, MenuAction::RESUME, true});
    out.push_back({{left + footer_btn_w + gap, footer_y, footer_btn_w, row_h},
                   MenuAction::QUIT_TO_TITLE,
                   true});
    return out;
}

void RoomScene::handle_menu_event(const sf::Event& event) {
    if (event.type != sf::Event::MouseButtonReleased ||
        event.mouseButton.button != sf::Mouse::Left) {
        return;
    }
    const sf::Vector2f vp{static_cast<float>(event.mouseButton.x),
                          static_cast<float>(event.mouseButton.y)};
    for (const MenuButton& b : menu_buttons()) {
        if (b.enabled && b.rect.contains(vp)) {
            trigger_menu(b.action);
            return;
        }
    }
}

void RoomScene::trigger_menu(MenuAction action) {
    auto save_to = [this](int slot) {
        if (!can_save()) {
            ctx_.log.warn("RoomScene: cannot save here (view state isn't COMMAND/MENU)");
            return;
        }
        ctx_.saves.save(slot, snap());
    };
    switch (action) {
    case MenuAction::SAVE_SLOT_1:
        save_to(1);
        break;
    case MenuAction::SAVE_SLOT_2:
        save_to(2);
        break;
    case MenuAction::SAVE_SLOT_3:
        save_to(3);
        break;
    case MenuAction::LOAD_SLOT_1:
    case MenuAction::LOAD_SLOT_2:
    case MenuAction::LOAD_SLOT_3: {
        const int slot = (action == MenuAction::LOAD_SLOT_1)   ? 1
                         : (action == MenuAction::LOAD_SLOT_2) ? 2
                                                               : 3;
        if (auto state = ctx_.saves.load(slot)) {
            // Same hand-off as Title's Continue: stage + scene change.
            // restore() would also work directly, but routing through
            // scene-change keeps the bootstrapping (cast/inventory/lua
            // bindings re-init) consistent with a fresh entry.
            ctx_.saves.stage_restore(std::move(*state));
            ctx_.scenes.goto_scene("room_view");
        }
        break;
    }
    case MenuAction::RESUME:
        view_state_ = ViewState::COMMAND;
        break;
    case MenuAction::QUIT_TO_TITLE:
        ctx_.scenes.goto_scene("title");
        break;
    }
}

void RoomScene::draw_menu(sf::RenderTarget& target) const {
    const sf::Vector2u vres = ctx_.display.virtual_resolution();

    // Dim the world behind the menu.
    sf::RectangleShape dim(sf::Vector2f(static_cast<float>(vres.x), static_cast<float>(vres.y)));
    dim.setFillColor(sf::Color(0, 0, 0, 180));
    target.draw(dim);

    if (!font_) {
        return;
    }

    // Heading.
    sf::Text title(ctx_.strings.ui_label("pause"), *font_, 36);
    title.setFillColor(sf::Color(255, 240, 180));
    const sf::FloatRect tb = title.getLocalBounds();
    title.setPosition((static_cast<float>(vres.x) - tb.width) / 2.0f - tb.left,
                      static_cast<float>(vres.y) * 0.18f);
    target.draw(title);

    // Slot rows (the buttons themselves come from menu_buttons; we still need
    // to draw a "Slot N — Saved/Empty" label next to each row).
    const auto buttons = menu_buttons();
    const float row_h = 56.0f;
    const float label_pad = 12.0f;
    for (int slot = 1; slot <= 3; ++slot) {
        // Save button for this slot is at buttons[(slot-1)*2]; its rect.top is
        // the row's vertical origin.
        const sf::FloatRect save_btn = buttons[static_cast<std::size_t>((slot - 1) * 2)].rect;
        const float row_y = save_btn.top;
        const float row_left = save_btn.left - 120.0f /* approximate label width */;

        const std::string label = "Slot " + std::to_string(slot) +
                                  (ctx_.saves.slot_exists(slot) ? " — Guardado" : " — Vacío");
        sf::Text txt(label, *font_, 22);
        txt.setFillColor(sf::Color(220, 224, 235));
        const sf::FloatRect b = txt.getLocalBounds();
        txt.setPosition(row_left - 200.0f + label_pad,
                        row_y + (row_h - b.height) / 2.0f - b.top - 1.0f);
        target.draw(txt);
    }

    // Buttons themselves.
    for (const MenuButton& bt : buttons) {
        sf::RectangleShape box(sf::Vector2f(bt.rect.width, bt.rect.height));
        box.setPosition(bt.rect.left, bt.rect.top);
        if (!bt.enabled) {
            box.setFillColor(sf::Color(24, 26, 36));
            box.setOutlineColor(sf::Color(50, 54, 70));
        } else {
            box.setFillColor(sf::Color(34, 38, 54));
            box.setOutlineColor(sf::Color(90, 100, 130));
        }
        box.setOutlineThickness(1.5f);
        target.draw(box);

        std::string label;
        switch (bt.action) {
        case MenuAction::SAVE_SLOT_1:
        case MenuAction::SAVE_SLOT_2:
        case MenuAction::SAVE_SLOT_3:
            label = "Guardar";
            break;
        case MenuAction::LOAD_SLOT_1:
        case MenuAction::LOAD_SLOT_2:
        case MenuAction::LOAD_SLOT_3:
            label = "Cargar";
            break;
        case MenuAction::RESUME:
            label = ctx_.strings.ui_label("resume");
            break;
        case MenuAction::QUIT_TO_TITLE:
            label = ctx_.strings.ui_label("quit_to_title");
            break;
        }
        sf::Text txt(label, *font_, 20);
        txt.setFillColor(bt.enabled ? sf::Color(220, 224, 235) : sf::Color(120, 128, 145));
        const sf::FloatRect b = txt.getLocalBounds();
        txt.setPosition(bt.rect.left + (bt.rect.width - b.width) / 2.0f - b.left,
                        bt.rect.top + (bt.rect.height - b.height) / 2.0f - b.top);
        target.draw(txt);
    }
}

bool RoomScene::can_save() const {
    if (change_pending_) {
        return false;
    }
    // MENU is reachable only from COMMAND (see handle_event routing), so its
    // underlying snapshot is the same as the moment ESC was pressed. DIALOG
    // and BLOCKED carry transient runtime that the save format doesn't
    // capture; refusing here makes the partial-snapshot bug impossible.
    return view_state_ == ViewState::COMMAND || view_state_ == ViewState::MENU;
}

pac::core::GameState RoomScene::snap() const {
    pac::core::GameState s;
    s.save_version = 1;
    // MVP only saves while RoomScene is active; hardcode the manifest scene id
    // for forward compat (see design 02 §"Make persistent state explicit").
    s.current_scene_id = "room_view";
    s.room_view.current_room_id = current_room_id_;
    if (player_) {
        s.room_view.player.x = player_->position().x;
        s.room_view.player.y = player_->position().y;
        s.room_view.player.facing = player_->facing();
    }
    if (const Character* c = cast_.character(player_char_)) {
        s.room_view.player.appearance_id = c->appearance;
    }
    s.inventory = inventory_.list();
    s.global_state = ctx_.state.entries();
    s.room_state = room_state_;
    // Per-room runtime flags: snapshot persisted history across rooms, then
    // overlay the live values from the currently loaded room (which may
    // differ from the persisted snapshot if Lua mutated them this session).
    s.region_states = region_state_persist_;
    s.hotspot_enabled = hotspot_enabled_persist_;
    s.object_visible = object_visible_persist_;
    if (room_) {
        auto& region_map = s.region_states[current_room_id_];
        for (const auto& [region_id, region] : room_->data().regions) {
            region_map[region_id] = room_->region_state(region_id);
        }
        auto& hs_map = s.hotspot_enabled[current_room_id_];
        for (const auto& [hs_id, hs] : room_->data().hotspots) {
            hs_map[hs_id] = room_->hotspot_enabled(hs_id);
        }
        auto& obj_map = s.object_visible[current_room_id_];
        for (const auto& [obj_id, obj] : room_->data().objects) {
            obj_map[obj_id] = room_->object_visible(obj_id);
        }
    }
    return s;
}

bool RoomScene::restore(const pac::core::GameState& state) {
    if (state.save_version != 1) {
        ctx_.log.error("RoomScene::restore: unsupported save_version " +
                       std::to_string(state.save_version));
        return false;
    }
    // MVP only supports restoring into a RoomScene. A save with a different
    // current_scene_id means either a corrupted file or a forward-compat
    // save written by a future engine version. Fail loud, don't trample
    // current state.
    if (state.current_scene_id != "room_view") {
        ctx_.log.error("RoomScene::restore: save targets scene '" + state.current_scene_id +
                       "', expected 'room_view'");
        return false;
    }
    if (state.room_view.current_room_id.empty()) {
        ctx_.log.error("RoomScene::restore: save has empty current_room_id");
        return false;
    }
    // Replace stores before the room load so on_load observes restored state.
    ctx_.state.replace_all(state.global_state);
    room_state_ = state.room_state;
    region_state_persist_ = state.region_states;
    hotspot_enabled_persist_ = state.hotspot_enabled;
    object_visible_persist_ = state.object_visible;
    inventory_.replace_all(state.inventory);

    // Kill transient runtime — none of it is part of GameState.
    dialog_.reset();
    if (dialog_scope_ != 0) {
        ctx_.scripting.cancel_scope(dialog_scope_);
        dialog_scope_ = 0;
        run_task_ = 0;
    }
    view_state_ = ViewState::COMMAND;
    builder_.cancel();
    speech_.skip();

    // Schedule the room load; update() will reseat the player at the saved
    // position after load_room finishes.
    change_pending_ = true;
    pending_room_ = state.room_view.current_room_id;
    pending_entry_.clear();
    pending_restore_player_ = state.room_view.player;
    return true;
}

} // namespace pac::pnc
