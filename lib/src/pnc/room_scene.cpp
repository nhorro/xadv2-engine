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

#include <SFML/Graphics/Font.hpp>
#include <SFML/Graphics/RenderTarget.hpp>
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
    // `to = END` sentinel used by dialog Lua tables; matches kEndSentinel in dialog.cpp.
    L["END"] = std::string("__END__");
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
    // and let restore()'s pending change_pending_ drive it.
    if (auto staged = ctx_.saves.take_pending_restore()) {
        restore(*staged);
        return;
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

    // Restore persisted region states (else the YAML initial seeded by RoomRuntime).
    const auto persisted = region_state_persist_.find(id);
    if (persisted != region_state_persist_.end()) {
        for (const auto& [region_id, state] : persisted->second) {
            room_->set_region_state(region_id, state);
        }
    }

    const sf::Vector2u vres = ctx_.display.virtual_resolution();
    camera_.emplace(sf::Vector2f(static_cast<float>(vres.x), scenery_height()), room_->data().size);

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
        // Snapshot region states so they persist across the change.
        for (const auto& [region_id, region] : room_->data().regions) {
            region_state_persist_[current_room_id_][region_id] = room_->region_state(region_id);
        }
    }
    ctx_.scripting.cancel_scope(room_scope_);
    // An in-progress dialog references the outgoing room's NPC avatars; the
    // room change kills both.
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
    if (event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::Escape) {
        ctx_.scenes.quit();
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
    } else if (builder_.state() == CommandBuilder::State::IDLE && player_ &&
               room_->data().is_walkable(world)) {
        player_->move_to(world);
    }
}

void RoomScene::object_clicked(const ObjectRef& object) {
    // Resolve default verb, affordances, and combinable flag from the object's data.
    Verb default_verb = Verb::LOOK_AT;
    bool combinable = false;
    const std::vector<std::string>* affordances = nullptr;
    if (object.kind == ObjectKind::ROOM_OBJECT && room_) {
        const auto it = room_->data().hotspots.find(object.id);
        if (it != room_->data().hotspots.end()) {
            affordances = &it->second.affordances;
            if (auto v = verb_from_id(it->second.default_verb)) {
                default_verb = *v;
            }
        }
    } else if (object.kind == ObjectKind::INVENTORY_OBJECT) {
        if (const InventoryItem* item = inventory_.item(object.id)) {
            affordances = &item->affordances;
            combinable = item->combinable;
            if (auto v = verb_from_id(item->default_verb)) {
                default_verb = *v;
            }
        }
    }

    if (builder_.state() == CommandBuilder::State::IDLE) {
        builder_.select_verb(default_verb);
    }
    const std::optional<Verb> verb = builder_.verb();
    if (!verb) {
        return;
    }
    // look_at is always allowed; otherwise the affordance must be listed.
    const std::string vid(verb_id(*verb));
    bool affordance_ok = (*verb == Verb::LOOK_AT);
    if (!affordance_ok && affordances) {
        affordance_ok =
            std::find(affordances->begin(), affordances->end(), vid) != affordances->end();
    }
    builder_.provide_object(object, affordance_ok, combinable);
    execute_ready_command();
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
    auto name_of = [this](const ObjectRef& o) -> std::string {
        if (o.kind == ObjectKind::ROOM_OBJECT && room_) {
            const auto it = room_->data().hotspots.find(o.id);
            if (it != room_->data().hotspots.end()) {
                return it->second.name;
            }
        } else if (o.kind == ObjectKind::INVENTORY_OBJECT) {
            if (const InventoryItem* item = inventory_.item(o.id)) {
                return item->name;
            }
        }
        return o.id;
    };
    std::string s = strings.verb_label(std::string(verb_id(*verb)));
    if (builder_.param1()) {
        s += " " + name_of(*builder_.param1());
        if (*verb == Verb::USE || *verb == Verb::GIVE) {
            s += " " + strings.connector(std::string(verb_id(*verb)));
        }
    }
    if (builder_.param2()) {
        s += " " + name_of(*builder_.param2());
    }
    return s;
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
        if (!was_restore) {
            ctx_.saves.save(pac::core::SaveService::kAutosaveSlot, snap());
        }
        return;
    }
    if (player_ && room_) {
        player_->update(dt, room_->data());
        room_->update_npcs(dt);
        if (camera_) {
            const sf::Vector2u vres = ctx_.display.virtual_resolution();
            const sf::Vector2f dead_zone{static_cast<float>(vres.x) * 0.18f,
                                         scenery_height() * 0.22f};
            camera_->follow(player_->position(), dead_zone);
        }
        check_zones();
    }
    speech_.update(dt);
    if (dialog_) {
        dialog_->update();
        if (dialog_->ended()) {
            dialog_.reset();
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
            panel_->draw(target, ctx_.strings, inventory_, command_preview(), builder_.verb());
        }
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

    auto rt =
        DialogRuntime::start(ctx_.scripting, ctx_.resources, ctx_.log, npc_id, std::move(host));
    if (!rt) {
        return;
    }
    dialog_.emplace(std::move(*rt));
    view_state_ = ViewState::DIALOG;
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
    // Region states: snapshot persisted across rooms, then overlay the live
    // values from the currently loaded room (which may differ from `_persist`
    // if Lua mutated them this session).
    s.region_states = region_state_persist_;
    if (room_) {
        auto& room_map = s.region_states[current_room_id_];
        for (const auto& [region_id, region] : room_->data().regions) {
            room_map[region_id] = room_->region_state(region_id);
        }
    }
    return s;
}

void RoomScene::restore(const pac::core::GameState& state) {
    if (state.save_version != 1) {
        ctx_.log.error("RoomScene::restore: unsupported save_version " +
                       std::to_string(state.save_version));
        return;
    }
    // Replace stores before the room load so on_load observes restored state.
    ctx_.state.replace_all(state.global_state);
    room_state_ = state.room_state;
    region_state_persist_ = state.region_states;
    inventory_.replace_all(state.inventory);

    // Kill transient runtime — none of it is part of GameState.
    dialog_.reset();
    view_state_ = ViewState::COMMAND;
    builder_.cancel();
    speech_.skip();

    // Schedule the room load; update() will reseat the player at the saved
    // position after load_room finishes.
    change_pending_ = true;
    pending_room_ = state.room_view.current_room_id;
    pending_entry_.clear();
    pending_restore_player_ = state.room_view.player;
}

} // namespace pac::pnc
