#include "engine/pnc/room_scene.hpp"

#include "engine/core/cursor.hpp"
#include "engine/core/dev_flags.hpp"
#include "engine/core/diagnostics.hpp"
#include "engine/core/display.hpp"
#include "engine/core/engine_context.hpp"
#include "engine/core/load_error.hpp"
#include "engine/core/render_stats.hpp"
#include "engine/core/resource_cache.hpp"
#include "engine/core/resource_source.hpp"
#include "engine/core/save_service.hpp"
#include "engine/core/scene_manager.hpp"
#include "engine/core/scene_params.hpp"
#include "engine/core/scripting.hpp"
#include "engine/core/scripting_sol.hpp"
#include "engine/core/strings.hpp"
#include "engine/core/text_encoding.hpp"
#include "engine/core/thumbnail.hpp"
#include "engine/gfx/animated_sprite.hpp"
#include "engine/pnc/approach_follow.hpp"
#include "engine/pnc/data_error.hpp"
#include "engine/pnc/dev_actions.hpp"
#include "engine/pnc/pause_overlay.hpp"
#include "engine/pnc/room.hpp"
#include "pnc/dialog_internal.hpp"
#include "pnc/room_lighting.hpp"
#include "pnc/room_tuning_overlay.hpp"

#include <SFML/Graphics/Font.hpp>
#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/Graphics/RenderTexture.hpp>
#include <SFML/Graphics/Sprite.hpp>
#include <SFML/Graphics/Text.hpp>
#include <SFML/Graphics/Texture.hpp>
#include <SFML/Graphics/View.hpp>
#include <SFML/Window/Event.hpp>
#include <sol/sol.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <utility>
#include <variant>

namespace pac::pnc {

namespace {
constexpr float kScenerFraction = 0.85f;
constexpr float kAvatarScale = 1.1f;
// Distance (world px) within which the avatar is considered "at" an approach
// point: closer than this we don't bother walking / waiting.
constexpr float kApproachReached = 8.0f;
constexpr float kRoomFadeDefault = 0.3f; // change_room fade-out/in seconds

// A hotspot bound to one of these kinds tracks a *moving* target, so a
// `requires_approach` walk-then-act can follow its live position (#158). Region
// binds are static and use the normal (area/approach) path.
bool is_moving_bind(const std::string& bind) {
    return bind.starts_with("npc:") || bind.starts_with("object:");
}

// Engine-reserved global-state keys backing declarative configs (#185): the live
// config id per room and a per-(room,config) "seen" flag for first-enter tracking.
// Folded into GameState's global store like the `__dialog.*` once-flags, so they
// persist across save/load with no save-format change. Authors never touch them —
// they use `set_room_config` / `current_room_config` instead.
std::string config_cur_key(const std::string& room) {
    return "__config." + room + ".cur";
}
std::string config_seen_key(const std::string& room, const std::string& config) {
    return "__config." + room + "." + config + ".seen";
}

sf::FloatRect tuning_panel_region(const ScummPanel& panel, sf::Vector2u runtime_size) {
    const ScummPanelLayout& layout = panel.config().layout;
    const float sx = layout.design_size.x > 0.0f
                         ? static_cast<float>(runtime_size.x) / layout.design_size.x
                         : 1.0f;
    const float sy = layout.design_size.y > 0.0f
                         ? static_cast<float>(runtime_size.y) / layout.design_size.y
                         : 1.0f;
    return {layout.panel_rect.left * sx,
            layout.panel_rect.top * sy,
            layout.panel_rect.width * sx,
            layout.panel_rect.height * sy};
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
    sol::table development_table;
    bool development_valid = false;
    pac::core::Scripting* scripting = nullptr; // for auto-spawn (M9 #183)
    pac::core::Diagnostics* log = nullptr;

    // M9 #186: deferred room beats registered via `on_room_resume(fn)`, keyed by
    // the room they were registered for. Fired once (via the same spawn_call seam
    // as #183/#184) when that room is the live, ticking scene again — the bridge
    // a close-up uses to hand a blocking beat back to its frozen room. Held as a
    // main_protected_function: the value may arrive from a short-lived coroutine.
    // A plain sol::function remembers that coroutine's lua_State; after its task
    // drains and GC collects the thread, merely destroying the reference can call
    // luaL_unref through a dangling state. Rebinding to Lua's main state keeps the
    // registry anchor valid through close-up/task teardown. Transient — it fires
    // within a frame of the close-up closing, so it stays out of GameState.
    std::map<std::string, sol::main_protected_function> pending_resume;

    VerbResult call_inventory(const std::string& item,
                              const std::string& verb,
                              std::optional<std::string> operand) {
        if (!inventory_valid || !scripting) {
            return {};
        }
        sol::optional<sol::table> t = inventory_table[item];
        if (!t) {
            return {};
        }
        sol::optional<sol::function> fn = (*t)[verb];
        if (!fn) {
            return {};
        }
        // Auto-spawn (M9 #183) so an inventory `use(target)` can `talk` /
        // `move_to` / `wait` without an explicit `spawn()` wrapper.
        const pac::core::SpawnCallResult call = pac::core::spawn_call(*scripting, *fn, operand);
        VerbResult res;
        res.handled = true;
        if (call.done) {
            res.caption = call.string_return;
        } else {
            res.in_flight = call.task_id;
        }
        return res;
    }

    VerbResult
    call_game(const std::string& verb, const std::string& a, std::optional<std::string> b) {
        if (!game_valid || !scripting) {
            return {};
        }
        sol::optional<sol::table> fallbacks = game_table["fallbacks"];
        if (!fallbacks) {
            return {};
        }
        sol::optional<sol::function> fn = (*fallbacks)[verb];
        if (!fn) {
            return {};
        }
        // Auto-spawn (M9 #183). game.fallbacks take 1-2 operands (a / a+b).
        const pac::core::SpawnCallResult call = pac::core::spawn_call(*scripting, *fn, a, b);
        VerbResult res;
        res.handled = true;
        if (call.done) {
            res.caption = call.string_return;
        } else {
            res.in_flight = call.task_id;
        }
        return res;
    }

    /// Run the normal and optional development `on_start()` hooks once per new
    /// game. A hook may return a room id to override the manifest's start_room;
    /// the development hook runs last so a removable debug sidecar can select a
    /// scenario without mutating production game logic.
    std::optional<std::string> call_on_start() {
        std::optional<std::string> room_override;
        const auto call =
            [this, &room_override](const sol::table& table, bool valid, const std::string& label) {
                if (!valid) {
                    return;
                }
                sol::optional<sol::protected_function> fn = table["on_start"];
                if (!fn) {
                    return;
                }
                const sol::protected_function_result r = (*fn)();
                if (!r.valid()) {
                    const sol::error e = r;
                    log->error(label + " on_start error: " + e.what());
                    return;
                }
                if (r.return_count() == 0) {
                    return;
                }
                const sol::object value(r[0]);
                if (value.is<std::string>() && !value.as<std::string>().empty()) {
                    room_override = value.as<std::string>();
                }
            };

        call(game_table, game_valid, "game");
        call(development_table, development_valid, "development logic");
        return room_override;
    }
};

RoomScene::RoomScene(pac::core::EngineContext& ctx, const pac::core::SceneParams& params)
    : ctx_(ctx), command_controller_(*this) {
    cast_path_ = params.get_or("cast", "cast.yaml");
    rooms_dir_ = params.get_or("rooms", "rooms");
    start_room_ = params.get_or("start_room", "");
    player_char_ = params.get_or("player", "");
    font_path_ = params.get_or("font", "");
    scumm_panel_path_ = params.get_or("scumm_panel", "");
    inventory_path_ = params.get_or("inventory", "");
    inventory_logic_ = params.get_or("inventory_logic", "");
    logic_path_ = params.get_or("logic", "");
    development_logic_path_ = params.get_or("development_logic", "");
    pause_overlays_ = parse_pause_overlays(params, ctx_.log);
    fade_duration_ = kRoomFadeDefault;
    if (const auto v = params.get("fade_duration")) {
        try {
            fade_duration_ = std::stof(*v);
        } catch (const std::exception&) {
            // keep the default on a malformed value
        }
    }
}

RoomScene::~RoomScene() {
    if (post_process_rt_bytes_ != 0) {
        pac::core::add_shader_rt_bytes(-static_cast<std::ptrdiff_t>(post_process_rt_bytes_));
    }
}

bool RoomScene::tuning_overlay_active() const {
    return tuning_overlay_ && tuning_overlay_->active();
}

float RoomScene::scenery_height() const {
    // Set from the panel config in enter(); the fraction is the pre-panel fallback
    // (and the value enter() itself uses to place a default panel).
    if (scenery_height_ > 0.0f) {
        return scenery_height_;
    }
    return static_cast<float>(ctx_.display.virtual_resolution().y) * kScenerFraction;
}

void RoomScene::enter() {
    // Seed the debug overlay layers from the manifest dev flags (#37). They only
    // render / respond to F1-F4 when ctx_.dev.edit_mode is set (gated in draw()
    // and handle_event()).
    debug_flags_.walkboxes = ctx_.dev.show_walkboxes;
    debug_flags_.hotspots = ctx_.dev.show_hotspots;
    debug_flags_.anchors = ctx_.dev.show_anchors;
    debug_flags_.hud = ctx_.dev.show_state;

    if (!font_path_.empty()) {
        font_ = ctx_.resources.try_font(font_path_);
    }
    speech_font_ = font_;
    if (!ctx_.speech.font.empty()) {
        if (const sf::Font* configured = ctx_.resources.try_font(ctx_.speech.font)) {
            speech_font_ = configured;
        }
    }
    speech_.set_font_size(ctx_.speech.font_size);
    try {
        cast_ = parse_cast(ctx_.resources.read_text(cast_path_));
    } catch (const std::exception& e) {
        ctx_.log.error(std::string("RoomScene: cast: ") + e.what());
    }
    if (!inventory_path_.empty()) {
        try {
            const std::string text = ctx_.resources.read_text(inventory_path_);
            inventory_.set_definitions(parse_inventory(text));
            inventory_.set_icon_sheet(parse_inventory_icons(text));
        } catch (const std::exception& e) {
            ctx_.log.error(std::string("RoomScene: inventory: ") + e.what());
        }
    }

    const sf::Vector2u vres = ctx_.display.virtual_resolution();
    const float scenery = scenery_height();
    const sf::FloatRect default_panel_rect(0.0f,
                                           scenery,
                                           static_cast<float>(vres.x),
                                           static_cast<float>(vres.y) - scenery);
    ScummPanelConfig panel_config = default_scumm_panel_config(default_panel_rect);
    if (!scumm_panel_path_.empty()) {
        try {
            panel_config = parse_scumm_panel_config(ctx_.resources.read_text(scumm_panel_path_),
                                                    scumm_panel_path_);
        } catch (pac::core::LoadError& e) {
            ctx_.log.error(std::string("RoomScene: scumm_panel: ") +
                           e.with_file(scumm_panel_path_).what());
        } catch (const std::exception& e) {
            ctx_.log.error(std::string("RoomScene: scumm_panel: ") + e.what());
        }
    }
    // The scene owns everything above the panel. Derive the split from the panel's
    // own top edge (converted from its design space to the virtual resolution) so a
    // game that resizes the panel doesn't leave the scenery viewport, hotspot
    // hit-testing and walk clamping stranded at the built-in fraction. Panels that
    // keep the default rect land back on exactly kScenerFraction.
    const sf::Vector2f design = panel_config.layout.design_size;
    if (design.y > 0.0f) {
        scenery_height_ =
            panel_config.layout.panel_rect.top * (static_cast<float>(vres.y) / design.y);
    }
    panel_.emplace(std::move(panel_config), vres, font_, &ctx_.resources);

    lua_ = std::make_unique<Lua>();
    lua_->log = &ctx_.log;
    lua_->scripting = &ctx_.scripting;

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
    if (auto t = load_table(development_logic_path_)) {
        lua_->development_table = *t;
        lua_->development_valid = true;
    }

    // --- bind the genre Lua API (captures this; one RoomScene is active) ---
    L.set_function("change_room", [this](std::string id, sol::optional<std::string> e) {
        api_change_room(id, e.value_or(""));
    });
    L.set_function("current_room", [this]() { return api_current_room(); });
    // Declarative room configs (#185): the transition primitive + the read accessor.
    L.set_function("set_room_config", [this](std::string room_id, std::string config_id) {
        api_set_room_config(room_id, config_id);
    });
    L.set_function("current_room_config", [this](sol::optional<std::string> room_id) {
        return api_current_room_config(room_id.value_or(current_room_id_));
    });
    L.set_function("set_region_state",
                   [this](std::string id, std::string s) { api_set_region_state(id, s); });
    L.set_function("get_region_state", [this](std::string id) { return api_get_region_state(id); });
    L.set_function("show_object", [this](std::string id) { api_show_object(id, true); });
    L.set_function("hide_object", [this](std::string id) { api_show_object(id, false); });
    L.set_function("set_layer_visible",
                   [this](std::string id, bool visible) { api_set_layer_visible(id, visible); });
    L.set_function("enable_hotspot", [this](std::string id) { api_set_hotspot_enabled(id, true); });
    L.set_function("disable_hotspot",
                   [this](std::string id) { api_set_hotspot_enabled(id, false); });
    // Toggle a named obstacle (#143): a disabled obstacle stops blocking the
    // walkable area, so the player/NPCs can path through where it was.
    L.set_function("enable_obstacle", [this](std::string id) {
        if (room_) {
            room_->set_obstacle_enabled(id, true);
        }
    });
    L.set_function("disable_obstacle", [this](std::string id) {
        if (room_) {
            room_->set_obstacle_enabled(id, false);
        }
    });
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
    L.set_function("_talk_start",
                   [this](std::string speaker,
                          std::string text,
                          bool continue_action,
                          sol::optional<std::string> face_target) {
                       const std::optional<std::string> target =
                           face_target ? std::optional<std::string>(*face_target) : std::nullopt;
                       return api_talk(speaker, text, continue_action, target);
                   });
    // talk(speaker, text): show the line, then (when run inside a coroutine task)
    // yield until it is dismissed so a cutscene's lines play one after another
    // instead of overwriting each other (design 05: talk "yields until done"). On
    // the main thread — a plain on_load / verb handler that is not a coroutine — it
    // stays fire-and-forget so those existing call sites keep working.
    //
    // M9 #184 also defines the screenplay-shaped aliases (`say` / `move` / `face`)
    // and the `cutscene { ... }` block here, in the same prelude — they all rely on
    // `talk` / `spawn` / `avatar(id):method` being already bound, and on the
    // C++-side `_cutscene_arm` being set above. `cutscene(body)` returns a wrapper
    // that, when called, spawns the body in the current scope, captures its task
    // id, and arms the drain seam. The wrapper itself returns immediately, so a
    // hotspot handler `use = cutscene(function() ... end)` behaves like any other
    // auto-spawned handler that yielded — the dispatcher (#183) defers the SCUMM
    // panel's finish_execution until update() sees the cutscene task drain.
    ctx_.scripting.run_string(R"LUA(
function talk(speaker, text, opts)
  opts = opts or {}
  local ev = _talk_start(speaker, text, opts.continue_action == true, opts.face)
  local _, ismain = coroutine.running()
  if ev and ev ~= "" and not ismain then wait_event(ev) end
end
function say(speaker, text, opts) return talk(speaker, text, opts) end
function remark(speaker, text)
  return talk(speaker, text, { continue_action = true })
end
function move(id, target) return avatar(id):move_to(target) end
function face(id, dir) return avatar(id):face(dir) end
function cutscene(body, on_skip)
  return function(...)
    local args = { ... }
    local tid = spawn(function() body(table.unpack(args)) end)
    _cutscene_arm(tid, on_skip)
  end
end
function skippable_cutscene(body, on_skip)
  assert(type(on_skip) == "function", "skippable_cutscene needs an on_skip finalizer")
  return cutscene(body, on_skip)
end
)LUA",
                              "=room_lua_prelude");
    L.set_function("start_dialog",
                   [this](std::string dialog_id, sol::optional<std::string> speaker) {
                       api_start_dialog(dialog_id, speaker.value_or(dialog_id));
                   });
    // Open a close-up / examine view (issue #76) as an overlay over the room; the
    // close-up pops back here on Esc / right-click, so the room state is preserved.
    L.set_function("open_closeup",
                   [this](const std::string& scene_id) { ctx_.scenes.push_scene(scene_id); });
    // Case-resolution templates use the same overlay lifetime as close-ups, but
    // have their own verb so story scripts do not need to describe them as one.
    L.set_function("open_case_resolution",
                   [this](const std::string& scene_id) { ctx_.scenes.push_scene(scene_id); });
    // Leave the room for a manifest scene (typically a Cutscene / StoryText) — e.g.
    // an act-closing cutscene triggered from a verb handler. Unlike open_closeup
    // this REPLACES the room (goto_scene), so the scene's own `on_finish` decides
    // where to go next (often back to room_view). Persistent state in GameState
    // survives; the live room is unloaded.
    L.set_function("start_cutscene",
                   [this](const std::string& scene_id) { ctx_.scenes.goto_scene(scene_id); });
    // Ambient floating text (non-blocking): onomatopoeia and background NPC
    // chatter, independent of the single speech line. `where` is a point name,
    // "npc:id"/"object:id" (follows the moving thing), or {x=, y=}. `opts` =
    // { duration = seconds, color = { r=, g=, b= } }.
    L.set_function(
        "float_text",
        [this](const std::string& text, sol::object where, sol::optional<sol::table> opts) {
            AmbientLabel::Anchor anchor = AmbientLabel::Anchor::POINT;
            geom::Point fixed{0.0f, 0.0f};
            std::string ref;
            if (where.is<std::string>()) {
                const std::string w = where.as<std::string>();
                if (w.rfind("npc:", 0) == 0) {
                    anchor = AmbientLabel::Anchor::NPC;
                    ref = w.substr(4);
                } else if (w.rfind("object:", 0) == 0) {
                    anchor = AmbientLabel::Anchor::OBJECT;
                    ref = w.substr(7);
                } else if (const geom::Point* p = room_ ? room_->data().point(w) : nullptr) {
                    fixed = *p;
                } else {
                    ctx_.log.warn("float_text: unknown point '" + w + "'");
                    return;
                }
            } else if (where.is<sol::table>()) {
                sol::table t = where.as<sol::table>();
                fixed = {t["x"].get_or(0.0f), t["y"].get_or(0.0f)};
            } else {
                ctx_.log.warn(
                    "float_text: anchor must be a point name, 'npc:'/'object:' id, or {x, y}");
                return;
            }
            sf::Color color(245, 245, 250);
            float duration = std::clamp(0.6f + 0.05f * static_cast<float>(text.size()), 1.0f, 5.0f);
            if (opts) {
                if (sol::optional<sol::table> c = (*opts)["color"]) {
                    color = sf::Color(static_cast<sf::Uint8>((*c)["r"].get_or(255)),
                                      static_cast<sf::Uint8>((*c)["g"].get_or(255)),
                                      static_cast<sf::Uint8>((*c)["b"].get_or(255)));
                }
                if (sol::optional<double> d = (*opts)["duration"]) {
                    duration = static_cast<float>(*d);
                }
            }
            api_float_text(text, anchor, fixed, std::move(ref), color, duration);
        });
    // `to = END` is injected per-dialog by DialogRuntime::start as a unique
    // sentinel table — no engine-wide binding needed here.

    // Scripted camera overrides (issue #25). A target is a named point, an avatar
    // id, or `{ x = .., y = .. }` (design 05 §Camera). camera_go_to yields the
    // task for the engine-chosen tween duration via the core `wait` primitive, so
    // it reuses the scheduler rather than introducing a second blocking mechanism.
    auto resolve_target = [this](const sol::object& t) -> std::optional<geom::Point> {
        if (t.is<sol::table>()) {
            const sol::table tbl = t.as<sol::table>();
            if (tbl["x"].valid() && tbl["y"].valid()) {
                return geom::Point{tbl["x"].get<float>(), tbl["y"].get<float>()};
            }
            return std::nullopt;
        }
        if (t.is<std::string>()) {
            const std::string name = t.as<std::string>();
            if (room_) {
                if (const geom::Point* p = room_->data().point(name)) {
                    return *p;
                }
                if (const Avatar* npc = room_->npc(name)) {
                    return npc->position();
                }
            }
            if (player_ && name == player_char_) {
                return player_->position();
            }
            ctx_.log.warn("script target '" + name + "' is not a known point or avatar");
        }
        return std::nullopt;
    };
    L.set_function("camera_look_at", [this, resolve_target](const sol::object& t) {
        if (const auto p = resolve_target(t)) {
            api_camera_look_at(*p);
        }
    });
    L.set_function("_camera_go_to_start", [this, resolve_target](const sol::object& t) -> double {
        if (const auto p = resolve_target(t)) {
            return api_camera_go_to(*p);
        }
        return 0.0;
    });
    L.set_function("camera_follow_player", [this]() { api_camera_follow_player(); });
    // camera_go_to(target): start the tween, then yield for its duration so the
    // calling task blocks until the pan finishes (design 05: "yield until done").
    ctx_.scripting.run_string(
        "function camera_go_to(target) return wait(_camera_go_to_start(target)) end",
        "=camera_go_to");

    // Scripted avatar control: the `avatar(id)` handle (#139). The C++ helpers
    // resolve the id to the live player/NPC and do the work; the Lua prelude below
    // gives the `avatar(id):method` surface. `move_to` starts a pathfound walk and
    // returns the arrival event the wrapper waits on (empty -> no wait, so a bad id
    // never hangs the task); RoomScene::update emits it once the avatar stops.
    L.set_function(
        "_avatar_move_to",
        [this, resolve_target](const std::string& id, const sol::object& t) -> std::string {
            const auto p = resolve_target(t);
            if (!p) {
                ctx_.log.error("avatar('" + id + "'):move_to — target is not a known point");
                return std::string();
            }
            return api_avatar_move_to(id, *p);
        });
    L.set_function("_avatar_face",
                   [this](const std::string& id, const std::string& d) { api_avatar_face(id, d); });
    L.set_function("_avatar_look_at",
                   [this, resolve_target](const std::string& id, const sol::object& t) {
                       if (const auto p = resolve_target(t)) {
                           api_avatar_look_at(id, *p);
                       } else {
                           ctx_.log.error("avatar('" + id +
                                          "'):look_at — target is not a known point");
                       }
                   });
    L.set_function("_avatar_position", [this](const std::string& id) -> sol::object {
        sol::state& s = ctx_.scripting.lua();
        const auto p = api_avatar_position(id);
        if (!p) {
            return sol::lua_nil;
        }
        return sol::make_object(s, s.create_table_with("x", p->x, "y", p->y));
    });
    L.set_function("_avatar_play", [this](const std::string& id, const std::string& seq) {
        api_avatar_play(id, seq);
    });
    L.set_function("_avatar_set_visible", [this](const std::string& id, bool visible) {
        api_avatar_set_visible(id, visible);
    });
    L.set_function("_avatar_set_shadow_opacity",
                   [this](const std::string& id,
                          double opacity,
                          sol::optional<double> transition_seconds) {
                       api_avatar_set_shadow_opacity(
                           id,
                           static_cast<float>(opacity),
                           static_cast<float>(transition_seconds.value_or(0.0)));
                   });
    L.set_function("_avatar_play_until_end", [this](const std::string& id, const std::string& seq) {
        return api_avatar_play_until_end(id, seq);
    });
    L.set_function("_avatar_anchor",
                   [this](const std::string& id, const std::string& name) -> sol::object {
                       sol::state& s = ctx_.scripting.lua();
                       const auto p = api_avatar_anchor(id, name);
                       if (!p) {
                           return sol::lua_nil;
                       }
                       return sol::make_object(s, s.create_table_with("x", p->x, "y", p->y));
                   });
    // The handle: a light table carrying only the id, with movement/animation
    // blocking done in Lua via the scheduler's wait_event (so the yield stays on
    // the Lua side, like wait/camera_go_to). Re-resolution + fail-loud live in the
    // C++ helpers.
    ctx_.scripting.run_string(R"LUA(
do
  local M = {}
  M.__index = M
  function M:move_to(target)
    local ev = _avatar_move_to(self.id, target)
    if ev and ev ~= "" then wait_event(ev) end
    return self
  end
  function M:look_at(target) _avatar_look_at(self.id, target); return self end
  function M:face(direction) _avatar_face(self.id, direction); return self end
  function M:position() return _avatar_position(self.id) end
  function M:set_visible(visible) _avatar_set_visible(self.id, visible); return self end
  function M:show() return self:set_visible(true) end
  function M:hide() return self:set_visible(false) end
  function M:set_shadow_opacity(opacity, transition_seconds)
    _avatar_set_shadow_opacity(self.id, opacity, transition_seconds)
    return self
  end
  function M:play(sequence) _avatar_play(self.id, sequence); return self end
  function M:play_until_end(sequence)
    local ev = _avatar_play_until_end(self.id, sequence)
    if ev and ev ~= "" then wait_event(ev) end
    return self
  end
  function M:anchor(name) return _avatar_anchor(self.id, name) end
  function avatar(id) return setmetatable({ id = id }, M) end
end
)LUA",
                              "=avatar_handle");

    // Scripted NPC presence (#140): create/remove room NPCs from script so an NPC
    // can appear conditionally (e.g. checked against global state in on_load).
    // `start` is a named room point or { x, y }; orientation defaults to "down".
    L.set_function("spawn_npc",
                   [this, resolve_target](const std::string& id,
                                          const sol::object& start,
                                          sol::optional<std::string> orientation) {
                       const auto p = resolve_target(start);
                       if (!p) {
                           ctx_.log.error("spawn_npc('" + id + "'): start is not a known point");
                           return;
                       }
                       api_spawn_npc(id, *p, orientation.value_or(std::string("down")));
                   });
    L.set_function("despawn_npc", [this](const std::string& id) { api_despawn_npc(id); });

    // Scripted object control — the `object(id)` handle (#142). Like the avatar
    // handle, blocking (move_to) yields on the Lua side via wait_event; the C++
    // helpers resolve the id each call. `target` is a named room point or {x,y};
    // `speed` is optional (world px/s).
    L.set_function("_object_move_to",
                   [this, resolve_target](const std::string& id,
                                          const sol::object& t,
                                          sol::optional<double> speed) -> std::string {
                       const auto p = resolve_target(t);
                       if (!p) {
                           ctx_.log.error("object('" + id +
                                          "'):move_to — target is not a known point");
                           return std::string();
                       }
                       return api_object_move_to(id, *p, static_cast<float>(speed.value_or(240.0)));
                   });
    L.set_function("_object_set_position", [this](const std::string& id, float x, float y) {
        api_object_set_position(id, {x, y});
    });
    L.set_function("_object_position", [this](const std::string& id) -> sol::object {
        sol::state& s = ctx_.scripting.lua();
        const auto p = api_object_position(id);
        if (!p) {
            return sol::lua_nil;
        }
        return sol::make_object(s, s.create_table_with("x", p->x, "y", p->y));
    });
    L.set_function("_object_set_scale", [this](const std::string& id, double s) {
        api_object_set_scale(id, static_cast<float>(s));
    });
    L.set_function("_object_set_rotation", [this](const std::string& id, double degrees) {
        api_object_set_rotation(id, static_cast<float>(degrees));
    });
    L.set_function("_object_rotation", [this](const std::string& id) -> sol::object {
        sol::state& s = ctx_.scripting.lua();
        const auto rotation = api_object_rotation(id);
        return rotation ? sol::make_object(s, *rotation) : sol::make_object(s, sol::lua_nil);
    });
    L.set_function("_object_play", [this](const std::string& id, const std::string& seq) {
        api_object_play(id, seq);
    });
    L.set_function("_object_play_until_end", [this](const std::string& id, const std::string& seq) {
        return api_object_play_until_end(id, seq);
    });
    ctx_.scripting.run_string(R"LUA(
do
  local O = {}
  O.__index = O
  function O:move_to(target, speed)
    local ev = _object_move_to(self.id, target, speed)
    if ev and ev ~= "" then wait_event(ev) end
    return self
  end
  function O:set_position(x, y) _object_set_position(self.id, x, y); return self end
  function O:position() return _object_position(self.id) end
  function O:set_scale(s) _object_set_scale(self.id, s); return self end
  function O:set_rotation(degrees) _object_set_rotation(self.id, degrees); return self end
  function O:rotation() return _object_rotation(self.id) end
  function O:play(sequence) _object_play(self.id, sequence); return self end
  function O:play_until_end(sequence)
    local ev = _object_play_until_end(self.id, sequence)
    if ev and ev ~= "" then wait_event(ev) end
    return self
  end
  function object(id) return setmetatable({ id = id }, O) end
end
)LUA",
                              "=object_handle");

    // Dynamic room-light control. The authored type, colour, radius/cone,
    // attachment, and modulation remain declarative; scripts control the two
    // properties most useful for story beats and switches.
    L.set_function("_light_set_enabled", [this](const std::string& id, bool enabled) {
        api_light_set_enabled(id, enabled);
    });
    L.set_function("_light_enabled", [this](const std::string& id) -> sol::object {
        sol::state& s = ctx_.scripting.lua();
        const auto enabled = api_light_enabled(id);
        return enabled ? sol::make_object(s, *enabled) : sol::make_object(s, sol::lua_nil);
    });
    L.set_function(
        "_light_set_intensity",
        [this](const std::string& id,
               double intensity,
               sol::optional<double> transition_seconds) {
            api_light_set_intensity(id,
                                    static_cast<float>(intensity),
                                    static_cast<float>(transition_seconds.value_or(0.0)));
        });
    L.set_function("_light_intensity", [this](const std::string& id) -> sol::object {
        sol::state& s = ctx_.scripting.lua();
        const auto intensity = api_light_intensity(id);
        return intensity ? sol::make_object(s, *intensity) : sol::make_object(s, sol::lua_nil);
    });
    ctx_.scripting.run_string(R"LUA(
do
  local L = {}
  L.__index = L
  function L:set_enabled(enabled) _light_set_enabled(self.id, enabled); return self end
  function L:enable() return self:set_enabled(true) end
  function L:disable() return self:set_enabled(false) end
  function L:enabled() return _light_enabled(self.id) end
  function L:set_intensity(intensity, transition_seconds)
    _light_set_intensity(self.id, intensity, transition_seconds)
    return self
  end
  function L:intensity() return _light_intensity(self.id) end
  function light(id) return setmetatable({ id = id }, L) end
end
)LUA",
                              "=light_handle");

    L.set_function("_light_occluder_set_enabled",
                   [this](const std::string& id, bool enabled) {
                       api_light_occluder_set_enabled(id, enabled);
                   });
    L.set_function("_light_occluder_enabled", [this](const std::string& id) -> sol::object {
        sol::state& s = ctx_.scripting.lua();
        const auto enabled = api_light_occluder_enabled(id);
        return enabled ? sol::make_object(s, *enabled) : sol::make_object(s, sol::lua_nil);
    });
    ctx_.scripting.run_string(R"LUA(
do
  local O = {}
  O.__index = O
  function O:set_enabled(enabled) _light_occluder_set_enabled(self.id, enabled); return self end
  function O:enable() return self:set_enabled(true) end
  function O:disable() return self:set_enabled(false) end
  function O:enabled() return _light_occluder_enabled(self.id) end
  function light_occluder(id) return setmetatable({ id = id }, O) end
end
)LUA",
                              "=light_occluder_handle");

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
    // M9 #184 cutscene { ... } block. The Lua-side `cutscene(body)` wrapper
    // (defined below) calls back into `_cutscene_arm` with the spawned task id;
    // we set BLOCKED and reuse the auto-spawn drain seam (#183) so when the
    // task ends — whether by completion OR scope cancellation (change_room,
    // unload_room) — update() restores COMMAND from C++. The restoration MUST
    // be C++-side because scope cancellation does not run Lua cleanup (per
    // design 05 §Coroutine rules), so a Lua `finally` is not an option.
    L.set_function("_cutscene_arm",
                   [this](pac::core::TaskId tid, sol::optional<sol::function> on_skip) {
        if (view_state_ == ViewState::COMMAND) {
            view_state_ = ViewState::BLOCKED;
        }
        awaiting_handler_task_ = tid;
        cutscene_skip_ = {};
        if (on_skip) {
            // `_cutscene_arm` normally runs inside the wrapper's short-lived
            // coroutine. Anchor the finalizer against the main Lua state before
            // that coroutine drains; otherwise the later std::function cleanup
            // dereferences a collected lua_State (SEGV in luaL_unref/lua_rawgeti).
            sol::main_protected_function finalizer(
                ctx_.scripting.lua().lua_state(), *on_skip);
            cutscene_skip_ = [this, finalizer = std::move(finalizer)]() mutable {
                const pac::core::ScopeId previous = ctx_.scripting.current_scope();
                ctx_.scripting.set_current_scope(room_scope_);
                const sol::protected_function_result result = finalizer();
                ctx_.scripting.set_current_scope(previous);
                if (!result.valid()) {
                    const sol::error error = result;
                    ctx_.log.error(std::string("cutscene skip finalizer: ") + error.what());
                }
            };
        }
    });
    // M9 #186 on_room_resume(fn): defer a room beat until this room is the live,
    // ticking scene again. The intended caller is a close-up's on_exit — it runs
    // while the room is frozen beneath the overlay and so cannot play blocking
    // choreography itself; it hands `fn` (typically a `cutscene { ... }`) back to
    // the room. The room beneath is still alive (just covered), so `this` and
    // `current_room_id_` are valid; we record `fn` against that room and fire it
    // from update() once the room ticks again (see the fire block there). The fn
    // is rebound to the main Lua state, so the close-up scope teardown — which
    // can collect the coroutine that supplied it — leaves the reference intact.
    L.set_function("on_room_resume", [this](sol::function fn) {
        if (!fn.valid()) {
            ctx_.log.error("on_room_resume: expected a function");
            return;
        }
        if (lua_->pending_resume.count(current_room_id_)) {
            ctx_.log.warn("on_room_resume: overwriting a pending beat for room '" +
                          current_room_id_ + "'");
        }
        lua_->pending_resume[current_room_id_] = sol::main_protected_function(
            ctx_.scripting.lua().lua_state(), fn);
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
    // New game (no staged restore reached this point): run the normal and optional
    // development on_start() hooks for one-time world-state initialization in
    // the global scope, BEFORE the selected room's on_load so it can read the
    // state they seed. Either hook may override the manifest start_room.
    // Continue/Load returns above, so on_start never clobbers a restored save.
    const pac::core::ScopeId prev_scope = ctx_.scripting.current_scope();
    ctx_.scripting.set_current_scope(ctx_.scripting.global_scope());
    const std::optional<std::string> start_override = lua_->call_on_start();
    ctx_.scripting.set_current_scope(prev_scope);

    load_room(start_override.value_or(start_room_), "");

    // Checkpoint the fresh start. Otherwise the only autosave is on a room change,
    // so a game with a single room would never have a save at all and the title's
    // Continue would have nothing to resume. Only reached on a new game — a staged
    // restore returns above, so this never round-trips a save we just loaded.
    // Skipped when the start room opened a cutscene (can_save() is false while
    // BLOCKED): restoring into a scene the player cannot act in is worse than no
    // checkpoint. No thumbnail yet — nothing has been rendered at enter() time.
    if (can_save()) {
        ctx_.saves.save(pac::core::SaveService::kAutosaveSlot, snap());
    }
}

void RoomScene::leave() {
    unload_room();
}

void RoomScene::load_room(const std::string& id, const std::string& entry_point) {
    if (tuning_overlay_) {
        tuning_overlay_->close();
    }
    const std::string room_logical = rooms_dir_ + "/" + id + ".yaml";
    const std::string lua_logical = rooms_dir_ + "/" + id + ".lua";
    room_dir_ = pac::core::logical_dir(room_logical);

    RoomData data;
    try {
        data = parse_room(ctx_.resources.read_text(room_logical), id);
    } catch (pac::core::LoadError& e) {
        ctx_.log.error(std::string("RoomScene: ") + e.with_file(room_logical).what());
        return;
    } catch (const std::exception& e) {
        ctx_.log.error(std::string("RoomScene: room '" + id + "': ") + e.what());
        return;
    }

    room_.emplace(std::move(data));
    current_room_id_ = id;
    current_zone_.clear();
    command_controller_.reset();
    warn_unsupported_shader_features(room_->data(), ctx_.log);

    // Apply the room's declarative ambience before on_load. AmbiencePlayer lives
    // outside RoomRuntime, so an identical base continues at its current offset;
    // on_load may then make story-dependent adjustments through the Lua API.
    ctx_.audio.ambience.configure(room_->data().ambience.value_or(pac::core::AmbienceDefinition{}));

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
    if (const auto it = layer_visible_persist_.find(id); it != layer_visible_persist_.end()) {
        for (const auto& [layer_id, visible] : it->second) {
            room_->set_layer_visible(layer_id, visible);
        }
    }
    if (const auto it = obstacle_enabled_persist_.find(id); it != obstacle_enabled_persist_.end()) {
        for (const auto& [obstacle_id, enabled] : it->second) {
            room_->set_obstacle_enabled(obstacle_id, enabled);
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
        } else {
            ctx_.log.error("RoomScene: player character '" + player_char_ +
                           "' has no usable avatar; the player will not appear (see error above)");
        }
    }
    const bool allow_entry_walk = entry_point.empty() && !pending_restore_player_.has_value();
    seat_player(entry_point, allow_entry_walk);
    if (player_ && camera_) {
        // Frame the authored destination, not the off-screen source. Otherwise
        // a scrolling room would chase the player outside before the entrance
        // has even begun.
        const geom::Point camera_target =
            pending_player_entry_ ? pending_player_entry_->target : player_->position();
        camera_->snap_to(camera_target);
    }
    spawn_room_npcs();
    build_object_sprites();

    // Declarative configs (#185): reconcile presence to the room's effective config
    // BEFORE on_load (so the hook sees the resolved room), then run on_load, then
    // the config's first-enter / re-enter beat. Both run while the room scope is
    // current, so the spawned beat is room-scoped. A room without `configs:` skips
    // all of this (apply_room_config returns "").
    const std::string config_id = apply_room_config();
    room_->call_hook("on_load");
    run_config_beat(config_id);
    ctx_.scripting.set_current_scope(ctx_.scripting.global_scope());
}

void RoomScene::dev_reload_room() {
    if (!room_ || !ctx_.dev.allow_room_reload) {
        return;
    }
    // Script-only hot reload (design 02 §Script task ownership): run on_unload,
    // cancel the room scope (reaping its tasks), reopen it, re-evaluate the
    // behavior table, then on_load. RoomData, the player, and persistent state
    // are untouched — only the Lua behavior is refreshed.
    room_->call_hook("on_unload");
    ctx_.scripting.cancel_scope(room_scope_);
    room_scope_ = ctx_.scripting.open_scope();
    ctx_.scripting.set_current_scope(room_scope_);
    const std::string lua_logical = rooms_dir_ + "/" + current_room_id_ + ".lua";
    room_->load_behavior(ctx_.scripting, ctx_.resources, lua_logical, ctx_.log);
    room_->call_hook("on_load");
    ctx_.scripting.set_current_scope(ctx_.scripting.global_scope());
    ctx_.log.info("dev: reloaded room script '" + current_room_id_ + "'");
}

void RoomScene::dev_jump_to_next_room() {
    const std::vector<std::string> ids = room_ids_in_dir(ctx_.resources.source(), rooms_dir_);
    if (ids.size() < 2) {
        ctx_.log.info("dev: no other room to jump to");
        return;
    }
    const auto it = std::find(ids.begin(), ids.end(), current_room_id_);
    const std::size_t next =
        (it == ids.end()) ? 0 : (static_cast<std::size_t>(it - ids.begin()) + 1) % ids.size();
    pending_room_ = ids[next];
    pending_entry_.clear();
    change_pending_ = true; // update() commits the unload+load next frame
    ctx_.log.info("dev: jump to room '" + ids[next] + "'");
}

void RoomScene::dev_give_next_item() {
    for (const auto& [id, def] : inventory_.definitions()) {
        if (!inventory_.has(id)) {
            inventory_.add(id);
            ctx_.log.info("dev: added item '" + id + "'");
            return;
        }
    }
    ctx_.log.info("dev: all defined items already held");
}

void RoomScene::dev_remove_last_item() {
    const std::vector<std::string>& held = inventory_.list();
    if (held.empty()) {
        ctx_.log.info("dev: inventory is empty");
        return;
    }
    const std::string id = held.back();
    inventory_.remove(id);
    ctx_.log.info("dev: removed item '" + id + "'");
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
        Avatar avatar(gfx::load_animated_sprite(ctx_.resources, app->sprite),
                      kAvatarScale,
                      app->shadow);
        if (!app->shaders.empty()) {
            avatar.set_shaders(app->shaders);
        }
        return avatar;
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
            ctx_.log.error("RoomScene: NPC '" + placement.id + "' in room '" + data.id +
                           "' will not appear (could not build its avatar; see error above)");
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

void RoomScene::seat_player(const std::string& entry_point, bool allow_entry_walk) {
    if (!player_ || !room_) {
        return;
    }
    const RoomData& data = room_->data();
    const geom::Point* start = nullptr;
    const RoomAvatarPlacement* player_placement = nullptr;
    std::string orientation = "down";
    if (!entry_point.empty()) {
        start = data.point(entry_point);
    }
    if (!start) {
        for (const RoomAvatarPlacement& a : data.avatars) {
            if (a.player) {
                player_placement = &a;
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
        pending_player_entry_.reset();
        if (allow_entry_walk && player_placement && !player_placement->enter_from.empty()) {
            const geom::Point* from = data.point(player_placement->enter_from);
            if (!from) {
                ctx_.log.error("RoomScene: player entry point '" + player_placement->enter_from +
                               "' does not exist");
            } else {
                player_->set_position(*from);
                player_->follow_path(
                    geom::find_path(*from, *start, data.walkable, data.active_obstacles()));
                pending_player_entry_ = PendingPlayerEntry{*start, orientation};
                view_state_ = ViewState::BLOCKED;
            }
        }
    } else {
        ctx_.log.error("RoomScene: room '" + current_room_id_ + "' has no player start");
    }
}

void RoomScene::unload_room() {
    if (tuning_overlay_) {
        tuning_overlay_->close();
    }
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
        for (const BackgroundLayer& layer : room_->data().layers) {
            if (!layer.id.empty()) {
                layer_visible_persist_[current_room_id_][layer.id] = room_->layer_visible(layer.id);
            }
        }
        for (const Obstacle& o : room_->data().obstacles) {
            if (!o.id.empty()) {
                obstacle_enabled_persist_[current_room_id_][o.id] = room_->obstacle_enabled(o.id);
            }
        }
    }
    ctx_.scripting.cancel_scope(room_scope_);
    // Scripted moves/animations belong to tasks in the room scope just cancelled;
    // drop the bookkeeping so update() never emits for an unloaded room (#139/#149).
    pending_moves_.clear();
    pending_anim_.clear();
    pending_obj_moves_.clear();
    pending_obj_anim_.clear();
    pending_speech_.clear();
    end_talk_animation();
    ambient_.clear(); // transient float_text labels do not survive a room change
    // An in-progress dialog references the outgoing room's NPC avatars; the
    // room change kills both. Cancelling the dialog scope also reaps any
    // `run`-task spawned from the option (and anything that task spawned).
    if (dialog_scope_ != 0) {
        ctx_.scripting.cancel_scope(dialog_scope_);
        dialog_scope_ = 0;
        run_task_ = 0;
    }
    dialog_.reset();
    dialog_text_anchor_.reset();
    // A command deferred for approach belongs to the outgoing room; drop it so it
    // can't fire into the new room (e.g. a zone hook changed rooms mid-walk).
    pending_approach_.reset();
    pending_moving_approach_.reset();
    // An auto-spawned handler task (M9 #183) was cancelled by the room scope
    // teardown above. The next room's enter() resets the command controller,
    // so just drop the awaiting id (otherwise update() in the new room would
    // see a stale task id and call finish_execution on the freshly-reset
    // builder).
    awaiting_handler_task_.reset();
    cutscene_skip_ = {};
    // A deferred on_room_resume beat (M9 #186) is scoped to the room it was
    // registered against; a room change drops it (it does not survive into the
    // next room — close-ups, its only caller, never change rooms).
    if (lua_) {
        lua_->pending_resume.clear();
    }
    view_state_ = ViewState::COMMAND;
}

geom::Point RoomScene::speech_anchor(const Avatar& a) const {
    // Prefer an authored "head_pivot" sprite anchor (sibling of "walking_pivot");
    // otherwise estimate the head as the top-centre of the current animation frame
    // (comic-balloon speech placement).
    if (const auto p = a.anchor("head_pivot")) {
        return *p;
    }
    const sf::FloatRect b = a.bounds();
    return {a.position().x, b.top};
}

void RoomScene::say(const std::string& text, sf::Color color, float gap) {
    const geom::Point pos = player_ ? speech_anchor(*player_) : geom::Point{640.0f, 360.0f};
    say_at(text, color, pos, gap);
}

void RoomScene::say_at(const std::string& text, sf::Color color, geom::Point world, float gap) {
    if (text.empty()) {
        return;
    }
    // `world` is the head anchor; the balloon floats above it (see place_speech).
    float duration = 0.5f + 0.06f * static_cast<float>(text.size());
    duration = std::clamp(duration, 1.0f, 7.0f);
    speech_.show(text, world, color, duration, gap);
    spoke_during_command_ = true;
}

void RoomScene::api_float_text(const std::string& text,
                               AmbientLabel::Anchor anchor,
                               geom::Point fixed,
                               std::string ref,
                               sf::Color color,
                               float duration) {
    if (text.empty() || duration <= 0.0f) {
        return;
    }
    AmbientLabel label;
    label.text = text;
    label.color = color;
    label.remaining = duration;
    label.anchor = anchor;
    label.fixed = fixed;
    label.ref = std::move(ref);
    ambient_.push_back(std::move(label));
}

geom::Point RoomScene::ambient_anchor_point(const AmbientLabel& label) const {
    if (label.anchor == AmbientLabel::Anchor::NPC && room_) {
        if (const Avatar* a = room_->npc(label.ref)) {
            return speech_anchor(*a); // float above the (moving) NPC's head
        }
    } else if (label.anchor == AmbientLabel::Anchor::OBJECT) {
        if (const std::optional<sf::FloatRect> b = object_frame_bounds(label.ref)) {
            return {b->left + b->width / 2.0f, b->top};
        }
    }
    return label.fixed;
}

void RoomScene::draw_ambient(sf::RenderTarget& target) const {
    if (!font_ || ambient_.empty()) {
        return;
    }
    for (const AmbientLabel& label : ambient_) {
        const geom::Point at = ambient_anchor_point(label);
        sf::Text text(pac::core::utf8(label.text), *font_, 22);
        text.setFillColor(label.color);
        text.setOutlineColor(sf::Color(0, 0, 0, 200));
        text.setOutlineThickness(2.0f);
        const sf::FloatRect b = text.getLocalBounds();
        // Centred just above the anchor (the NPC head / object top / point).
        text.setPosition(at.x - b.width / 2.0f - b.left, at.y - b.height - 12.0f - b.top);
        target.draw(text);
    }
}

geom::Point RoomScene::virtual_to_world(sf::Vector2f vp) const {
    if (!camera_) {
        return {vp.x, vp.y};
    }
    const sf::Vector2f tl = camera_->top_left();
    return {tl.x + vp.x, tl.y + vp.y};
}

void RoomScene::skip_active_cutscene() {
    if (!awaiting_handler_task_ || !cutscene_skip_) {
        return;
    }
    const pac::core::TaskId task = *awaiting_handler_task_;
    std::function<void()> finalizer = std::move(cutscene_skip_);
    cutscene_skip_ = {};
    awaiting_handler_task_.reset();

    // Cancel first: the coroutine must not resume after the finalizer places the
    // room in its canonical post-cutscene state. The finalizer is synchronous by
    // contract (no waits/talk/move_to).
    ctx_.scripting.cancel_task(task);
    speech_.skip();
    finalizer();

    if (view_state_ == ViewState::BLOCKED) {
        view_state_ = ViewState::COMMAND;
    }
    command_controller_.finish_execution();
    if (camera_) {
        camera_->follow_player();
    }
    sync_command_hover();
}

void RoomScene::handle_event(const sf::Event& event) {
    // F9 opens a dev-only render tuning panel over the SCUMM controls. It is a
    // separate input layer rather than ViewState::BLOCKED: the room keeps
    // rendering and updating while every player-facing input is consumed here.
    if (ctx_.dev.edit_mode && event.type == sf::Event::KeyPressed &&
        event.key.code == sf::Keyboard::F9) {
        if (tuning_overlay_active()) {
            tuning_overlay_->close();
        } else if (view_state_ == ViewState::COMMAND && room_ && panel_) {
            if (!tuning_overlay_) {
                tuning_overlay_ = std::make_unique<RoomTuningOverlay>();
            }
            tuning_overlay_->open(room_->data(),
                                  tuning_panel_region(*panel_, ctx_.display.virtual_resolution()),
                                  font_);
        }
        return;
    }
    if (tuning_overlay_active()) {
        tuning_overlay_->handle_event(event);
        return;
    }
    // Track the pointer (virtual coords) so the top bar can preview the element
    // under the cursor each frame (issue #28). Coordinates are already mapped to
    // virtual space by the application's event rewrite.
    if (event.type == sf::Event::MouseMoved) {
        hover_vp_ = {static_cast<float>(event.mouseMove.x), static_cast<float>(event.mouseMove.y)};
        sync_command_hover();
        return;
    }
    // Debug overlay toggles (#37): F1-F4 flip a layer. Only in dev (edit_mode);
    // a shipped game never reacts to these keys. Handled before all view-state
    // routing so overlays can be toggled during dialogs and the pause menu too.
    if (ctx_.dev.edit_mode && event.type == sf::Event::KeyPressed &&
        debug_flags_.toggle(event.key.code)) {
        return;
    }
    // Dev actions (#38): F5-F8 authoring helpers. Dev-only, and only from COMMAND
    // so they can't disrupt a running dialog or the pause menu.
    if (ctx_.dev.edit_mode && event.type == sf::Event::KeyPressed &&
        view_state_ == ViewState::COMMAND) {
        switch (event.key.code) {
        case sf::Keyboard::F5:
            dev_reload_room();
            return;
        case sf::Keyboard::F6:
            dev_jump_to_next_room();
            return;
        case sf::Keyboard::F7:
            dev_give_next_item();
            return;
        case sf::Keyboard::F8:
            dev_remove_last_item();
            return;
        default:
            break;
        }
    }
    if (change_armed_) {
        return; // a change_room is committing (fading out); ignore input
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
        } else if (view_state_ == ViewState::BLOCKED && cutscene_skip_) {
            skip_active_cutscene();
        }
        return;
    }
    if (view_state_ == ViewState::MENU) {
        handle_menu_event(event);
        return;
    }
    // The secondary button clears the command being composed (design 04 §cancel):
    // the player drops the verb and any operand and starts over, anywhere on screen.
    if (event.type == sf::Event::MouseButtonReleased &&
        event.mouseButton.button == sf::Mouse::Right) {
        if (view_state_ == ViewState::COMMAND) {
            command_controller_.cancel();
            sync_command_hover();
        }
        return;
    }
    if (event.type != sf::Event::MouseButtonReleased ||
        event.mouseButton.button != sf::Mouse::Left) {
        return;
    }
    const sf::Vector2f vp{static_cast<float>(event.mouseButton.x),
                          static_cast<float>(event.mouseButton.y)};
    // Systemic panel buttons belong to command mode. Dialog mode owns the whole
    // panel for its choices; opening settings/menu there would interrupt the
    // dialog lifecycle and leave both interfaces active at once.
    auto handle_system_button_if_clicked = [&]() {
        if (!panel_ || !panel_->contains(vp)) {
            return false;
        }
        const PanelIntent intent = panel_->click(vp, inventory_, command_controller_.state());
        switch (intent.kind) {
        case PanelIntent::Kind::OPEN_SETTINGS:
            ctx_.scenes.open_settings();
            return true;
        case PanelIntent::Kind::OPEN_MENU:
            view_state_ = ViewState::MENU;
            return true;
        case PanelIntent::Kind::PUSH_SCENE:
            ctx_.scenes.push_scene(intent.scene);
            return true;
        default:
            return false;
        }
    };
    if (view_state_ == ViewState::COMMAND && handle_system_button_if_clicked()) {
        return;
    }
    if (speech_.active()) {
        speech_.skip();
        return;
    }
    if (view_state_ == ViewState::BLOCKED) {
        // A cutscene-style block ignores input. But a block that is only the
        // walk-to-approach wait (a queued command in pending_approach_, or a
        // moving-target chase in pending_moving_approach_) is redirectable: a fresh
        // click drops the queued command and re-routes to the new target (classic
        // SCUMM redirect, issue #70). Stop the current walk and fall through to the
        // normal click handling below, which issues the new movement/command.
        if (!pending_approach_ && !pending_moving_approach_) {
            return;
        }
        pending_approach_.reset();
        pending_moving_approach_.reset();
        command_controller_.cancel();
        if (player_) {
            player_->stop();
        }
        view_state_ = ViewState::COMMAND;
    }
    if (view_state_ == ViewState::DIALOG && dialog_) {
        const std::vector<DialogOption> opts = dialog_->options();
        if (panel_ && !opts.empty() && panel_->contains(vp)) {
            std::vector<std::string> labels;
            labels.reserve(opts.size());
            for (const DialogOption& opt : opts) {
                labels.push_back(opt.text);
            }
            const DialogClick click = panel_->click_dialog(vp, labels, dialog_page_);
            if (click.kind == DialogClick::Kind::PAGE) {
                dialog_page_ = click.page_index;
            } else if (click.kind == DialogClick::Kind::OPTION) {
                dialog_page_ = 0;
                dialog_->choose(click.option_index);
            }
        }
        return;
    }

    if (panel_ && panel_->contains(vp)) {
        const PanelIntent intent = panel_->click(vp, inventory_, command_controller_.state());
        if (intent.kind == PanelIntent::Kind::SELECT_VERB) {
            // Clicking the already-selected verb un-selects it, so the verb row is
            // its own cancel affordance (design 04 §cancel).
            const CommandState& state = command_controller_.state();
            if (state.selected_verb && *state.selected_verb == intent.verb) {
                command_controller_.cancel();
            } else {
                command_controller_.on_verb_selected({intent.verb});
            }
        } else if (intent.kind == PanelIntent::Kind::CLICK_INVENTORY) {
            if (const auto cmd = command_controller_.on_inventory_item_selected({intent.item_id})) {
                execute_command(*cmd);
            }
        } else if (intent.kind == PanelIntent::Kind::CHANGE_INVENTORY_PAGE) {
            command_controller_.on_inventory_page_changed({intent.page_index});
        } else if (intent.kind == PanelIntent::Kind::OPEN_SETTINGS) {
            ctx_.scenes.open_settings();
        } else if (intent.kind == PanelIntent::Kind::OPEN_NOTEBOOK) {
            const ScummNotebookConfig& nb = panel_->config().notebook;
            if (!nb.tab_state.empty()) {
                ctx_.state.set(nb.tab_state, intent.tab);
            }
            if (!nb.scene.empty()) {
                ctx_.scenes.push_scene(nb.scene);
            }
        }
        sync_command_hover();
        return;
    }

    if (!room_) {
        return;
    }
    const geom::Point world = virtual_to_world(vp);
    if (const RoomHotspot* hs = hotspot_under(world)) {
        if (const auto cmd = command_controller_.on_hotspot_clicked({hs->id})) {
            execute_command(*cmd);
        }
    } else if (command_controller_.state().builder_state != CommandBuilder::State::IDLE) {
        // Clicking empty scenery while a command is being built cancels it and
        // returns to IDLE (issue #28).
        command_controller_.cancel();
    } else if (player_) {
        // IDLE: walk there. find_path clamps a click outside the walkable area to
        // the nearest reachable point (issue #28) and routes around obstacles; the
        // avatar walks the returned waypoints in order.
        const RoomData& d = room_->data();
        const auto path =
            geom::find_path(player_->position(), world, d.walkable, d.active_obstacles());
        player_->follow_path(path);
    }
    sync_command_hover();
}

CommandOperandInfo RoomScene::resolve_command_operand(const ObjectRef& object) const {
    CommandOperandInfo info;
    info.name = object.id; // fallback when the id is unknown
    if (object.kind == ObjectKind::ROOM_OBJECT && room_) {
        const auto it = room_->data().hotspots.find(object.id);
        if (it != room_->data().hotspots.end()) {
            info.found = true;
            info.name = it->second.name;
            info.affordances = it->second.affordances;
            if (auto v = verb_from_id(it->second.default_verb)) {
                info.default_verb = *v;
            }
        }
    } else if (object.kind == ObjectKind::INVENTORY_OBJECT) {
        if (const InventoryItem* item = inventory_.item(object.id)) {
            info.found = true;
            info.name = item->name;
            info.affordances = item->affordances;
            info.combinable = item->combinable;
            if (auto v = verb_from_id(item->default_verb)) {
                info.default_verb = *v;
            }
        }
    }
    return info;
}

void RoomScene::execute_command(const Command& cmd) {
    // Resolve the room operand the command targets (if any) and its approach point.
    const ObjectRef* room_target = nullptr;
    if (cmd.param2 && cmd.param2->kind == ObjectKind::ROOM_OBJECT) {
        room_target = &*cmd.param2;
    } else if (cmd.param1.kind == ObjectKind::ROOM_OBJECT) {
        room_target = &cmd.param1;
    }
    const RoomHotspot* hs = nullptr;
    if (room_target && room_) {
        const auto it = room_->data().hotspots.find(room_target->id);
        if (it != room_->data().hotspots.end()) {
            hs = &it->second;
        }
    }

    if (hs && hs->approach && player_ && room_) {
        // Always start walking toward the approach point. The (clamped-to-walkable)
        // distance then decides whether we act now or wait to arrive.
        const RoomData& d = room_->data();
        geom::Point ap = *hs->approach;
        if (!d.walkable.empty() && !d.is_walkable(ap)) {
            ctx_.log.debug("approach point for hotspot '" + room_target->id +
                           "' is outside the walkable area; clamped to the nearest walkable point");
            ap = geom::closest_point_in_polygon(ap, d.walkable);
        }
        const bool far = geom::distance(player_->position(), ap) > kApproachReached;
        walk_to_approach(*hs->approach, room_target->id);
        if (hs->requires_approach && far) {
            // Defer: block input and run the command on arrival (update() polls
            // for the avatar to stop). The builder stays in COMMAND_EXECUTING.
            pending_approach_ = PendingApproach{cmd, ap, room_target->id};
            view_state_ = ViewState::BLOCKED;
            return;
        }
    }

    // #158: a bind-only hotspot (no static area, no approach point) bound to a
    // *moving* NPC/object still honors `requires_approach` — walk toward the
    // target's live position and re-target it as it moves, firing once in range
    // (update() drives the chase).
    if (hs && hs->area.empty() && !hs->approach && hs->requires_approach &&
        is_moving_bind(hs->bind) && player_ && room_) {
        if (const std::optional<geom::Point> target = live_bind_target(hs->bind)) {
            const ChaseParams params; // interaction range / re-path / give-up tunables
            if (geom::distance(player_->position(), *target) > params.interaction_range) {
                pending_approach_.reset(); // mutually exclusive with the static path
                const geom::Point dest = route_to(*target);
                pending_moving_approach_ = PendingMovingApproach{cmd, room_target->id, dest, 0.0f};
                view_state_ = ViewState::BLOCKED;
                return;
            }
        }
    }

    dispatch_and_feedback(cmd);
}

void RoomScene::face_target(const Command& cmd) {
    if (!player_ || !room_) {
        return;
    }
    const ObjectRef* target = nullptr;
    if (cmd.param2 && cmd.param2->kind == ObjectKind::ROOM_OBJECT) {
        target = &*cmd.param2;
    } else if (cmd.param1.kind == ObjectKind::ROOM_OBJECT) {
        target = &cmd.param1;
    }
    if (target == nullptr) {
        return;
    }
    const auto it = room_->data().hotspots.find(target->id);
    if (it == room_->data().hotspots.end()) {
        return;
    }
    if (const std::optional<geom::Point> focus = hotspot_focus(it->second)) {
        player_->face(nearest_direction(*focus - player_->position()));
    }
}

std::optional<geom::Point> RoomScene::hotspot_focus(const RoomHotspot& hs) const {
    const auto centre = [](const sf::FloatRect& b) {
        return geom::Point{b.left + (b.width / 2.0f), b.top + (b.height / 2.0f)};
    };
    if (!hs.area.empty()) {
        return centre(geom::polygon_bounds(hs.area));
    }
    if (!room_) {
        return std::nullopt;
    }
    if (hs.bind.starts_with("region:")) {
        const auto it = room_->data().regions.find(hs.bind.substr(std::string("region:").size()));
        if (it != room_->data().regions.end() && !it->second.area.empty()) {
            return centre(geom::polygon_bounds(it->second.area));
        }
    } else if (hs.bind.starts_with("object:")) {
        if (const auto bounds =
                object_frame_bounds(hs.bind.substr(std::string("object:").size()))) {
            return centre(*bounds);
        }
    } else if (hs.bind.starts_with("npc:")) {
        // The bound NPC's current bounds, so the player turns to face a
        // (possibly moving) NPC — e.g. talk_to on an `npc:`-bound hotspot.
        if (const Avatar* a = room_->npc(hs.bind.substr(std::string("npc:").size()))) {
            return centre(a->bounds());
        }
    }
    return std::nullopt;
}

void RoomScene::dispatch_and_feedback(const Command& cmd) {
    spoke_during_command_ = false;
    const bool player_was_moving = player_ && player_->moving();
    const std::string movement_facing = player_ ? player_->facing() : std::string();
    // Turn to face what we're about to act on / talk to, before dispatch (so the
    // avatar already looks at an NPC when its dialog opens).
    face_target(cmd);
    const VerbResult result = dispatch(cmd);
    // If dispatch flipped us into a non-command state (e.g. a `talk_to` handler
    // called `start_dialog`), the dialog's first NPC line is already on screen;
    // suppress the fallback caption that would otherwise overwrite it.
    if (view_state_ != ViewState::COMMAND) {
        command_controller_.finish_execution();
        return;
    }
    // Auto-spawned handler that yielded (M9 #183): it took responsibility, so
    // suppress the fallback caption, block input until it drains, and defer
    // finish_execution to update() — which polls is_task_alive. The task is
    // scoped to the room, so a `change_room` mid-handler cancels it cleanly
    // (the room-scope teardown also clears awaiting_handler_task_).
    if (result.in_flight) {
        awaiting_handler_task_ = *result.in_flight;
        view_state_ = ViewState::BLOCKED;
        return;
    }
    // M9 #184: a synchronously-completed handler may still have armed a
    // separate in-flight task via `cutscene(...)` (its wrapper calls
    // _cutscene_arm before returning). Treat it like the yielded case —
    // defer finish_execution to the drain logic below.
    if (awaiting_handler_task_) {
        view_state_ = ViewState::BLOCKED;
        return;
    }
    sf::Color color(230, 230, 230);
    float gap = 48.0f;
    if (const Character* c = cast_.character(player_char_)) {
        color = c->speech_color;
        gap = c->speech_gap;
    }
    // Show a returned caption. Otherwise fall back to "nothing happens" ONLY when
    // no handler took responsibility — a handler that ran (opened a close-up,
    // changed state, spoke via talk(), or just returned nothing) suppresses the
    // default, since it already decided how to react.
    // An immediate observation can be made mid-stride. `face_target` above is
    // still useful for stationary interactions, but restore the route facing
    // when this command did not stop the walk.
    if (player_was_moving && player_ && player_->moving()) {
        player_->face(movement_facing);
    }
    if (result.caption) {
        say(*result.caption, color, gap);
        // Command captions are Julia reacting while normal play continues. Let
        // an in-flight walk or scripted gesture keep its animation; when she is
        // already idle, use the talk loop.
        if (speech_.active()) {
            begin_talk_animation(player_char_, player_ && (player_->moving() || player_->acting()));
        }
    } else if (!result.handled && !spoke_during_command_) {
        say(ctx_.strings.caption("nothing_happens"), color, gap);
        if (speech_.active()) {
            begin_talk_animation(player_char_, player_ && (player_->moving() || player_->acting()));
        }
    }
    command_controller_.finish_execution();
    sync_command_hover();
}

std::optional<sf::FloatRect> RoomScene::object_frame_bounds(const std::string& object_id) const {
    if (!room_ || !room_->object_visible(object_id)) {
        return std::nullopt;
    }
    const auto it = room_->data().objects.find(object_id);
    if (it == room_->data().objects.end() || it->second.sprite.empty()) {
        return std::nullopt;
    }
    // Animated object: its current frame bounds (transform already synced) (#142).
    if (const gfx::VisualSprite* spr = room_->object_sprite(object_id)) {
        return spr->global_bounds();
    }
    try {
        const sf::Texture& tex =
            ctx_.resources.texture(pac::core::logical_join(room_dir_, it->second.sprite));
        const sf::Vector2u sz = tex.getSize();
        const float s = room_->object_scale(object_id);            // runtime scale (#142)
        const geom::Point pos = room_->object_position(object_id); // runtime position (#142)
        return sf::FloatRect(pos.x,
                             pos.y,
                             static_cast<float>(sz.x) * s,
                             static_cast<float>(sz.y) * s);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

const RoomHotspot* RoomScene::hotspot_under(geom::Point world) const {
    if (!room_) {
        return nullptr;
    }
    return room_->hotspot_at(world,
                             [this](const std::string& id) { return object_frame_bounds(id); });
}

void RoomScene::walk_to_approach(geom::Point approach, const std::string& hotspot_id) {
    if (!player_ || !room_) {
        return;
    }
    const RoomData& d = room_->data();
    geom::Point ap = approach;
    if (!d.walkable.empty() && !d.is_walkable(ap)) {
        ap = geom::closest_point_in_polygon(ap, d.walkable);
        ctx_.log.warn(
            "hotspot '" + hotspot_id +
            "': approach point outside walkable area; routing to nearest reachable point");
    }
    // Near-enough short-circuit: don't shuffle in place when already there.
    if (geom::distance(player_->position(), ap) <= kApproachReached) {
        return;
    }
    const auto path = geom::find_path(player_->position(), ap, d.walkable, d.active_obstacles());
    player_->follow_path(path);
}

geom::Point RoomScene::route_to(geom::Point target) {
    const RoomData& d = room_->data();
    geom::Point dest = target;
    if (!d.walkable.empty() && !d.is_walkable(dest)) {
        dest = geom::closest_point_in_polygon(dest, d.walkable);
    }
    player_->follow_path(
        geom::find_path(player_->position(), dest, d.walkable, d.active_obstacles()));
    return dest;
}

std::optional<geom::Point> RoomScene::live_bind_target(const std::string& bind) const {
    if (!room_) {
        return std::nullopt;
    }
    if (bind.starts_with("npc:")) {
        if (const Avatar* a = room_->npc(bind.substr(std::string("npc:").size()))) {
            return a->position();
        }
        return std::nullopt;
    }
    if (bind.starts_with("object:")) {
        return api_object_position(bind.substr(std::string("object:").size()));
    }
    return std::nullopt;
}

VerbResult RoomScene::dispatch(const Command& cmd) {
    const std::string verb(verb_id(cmd.verb));
    const ObjectRef& p1 = cmd.param1;
    // Most specific handler that EXISTS wins and takes responsibility: stop as soon
    // as one is `handled` (even if it returned no caption), so a silent action
    // handler doesn't fall through to the default. Only when no handler exists at
    // any level does the (last) game-fallback result — typically unhandled — flow
    // back, and the caller shows the default caption.
    if (cmd.param2) {
        const ObjectRef& p2 = *cmd.param2;
        if (p1.kind == ObjectKind::INVENTORY_OBJECT) {
            if (VerbResult r = lua_->call_inventory(p1.id, verb, p2.id); r.handled) {
                return r;
            }
        }
        if (p2.kind == ObjectKind::ROOM_OBJECT && room_) {
            if (VerbResult r = room_->call_hotspot(p2.id, verb, p1.id); r.handled) {
                return r;
            }
        }
        return lua_->call_game(verb, p1.id, p2.id);
    }
    if (p1.kind == ObjectKind::INVENTORY_OBJECT) {
        if (VerbResult r = lua_->call_inventory(p1.id, verb, std::nullopt); r.handled) {
            return r;
        }
    } else if (p1.kind == ObjectKind::ROOM_OBJECT && room_) {
        if (VerbResult r = room_->call_hotspot(p1.id, verb, std::nullopt); r.handled) {
            return r;
        }
    }
    return lua_->call_game(verb, p1.id, std::nullopt);
}

std::string RoomScene::command_verb_label(Verb verb) const {
    return ctx_.strings.verb_label(std::string(verb_id(verb)));
}

std::string RoomScene::command_connector_label(Verb verb) const {
    return ctx_.strings.connector(std::string(verb_id(verb)));
}

std::string RoomScene::command_walk_label() const {
    return ctx_.strings.ui_label("walk_to");
}

void RoomScene::sync_command_hover() {
    if (view_state_ != ViewState::COMMAND) {
        command_controller_.clear_hover();
        return;
    }
    if (panel_ && panel_->contains(hover_vp_)) {
        const PanelIntent in = panel_->click(hover_vp_, inventory_, command_controller_.state());
        if (in.kind == PanelIntent::Kind::SELECT_VERB) {
            command_controller_.on_verb_hovered(in.verb);
        } else if (in.kind == PanelIntent::Kind::CLICK_INVENTORY) {
            command_controller_.on_inventory_item_hovered(in.item_id);
        } else {
            command_controller_.clear_hover();
        }
        return;
    }
    if (room_) {
        const geom::Point world = virtual_to_world(hover_vp_);
        if (const RoomHotspot* hs = hotspot_under(world)) {
            command_controller_.on_hotspot_hovered({hs->id});
        } else if (room_->data().is_walkable(world)) {
            command_controller_.on_walkable_hovered();
        } else {
            command_controller_.clear_hover();
        }
        return;
    }
    command_controller_.clear_hover();
}

void RoomScene::update(float dt) {
    shader_time_ += dt; // drives shaders' u_time uniform
    // Advance the change_room fade; once fully black, commit the deferred load.
    room_fade_.update(dt);
    if (change_armed_ && room_fade_.opaque()) {
        change_armed_ = false;
        change_pending_ = true;
    }
    if (change_pending_) {
        change_pending_ = false;
        const std::string id = pending_room_;
        const std::string entry = pending_entry_;
        const bool was_restore = pending_restore_player_.has_value();
        unload_room();
        load_room(id, entry);
        // If we got here via a faded change_room (screen is black), fade back in.
        // A restore() path leaves the screen clear, so this is a no-op there.
        if (room_fade_.opaque()) {
            room_fade_.fade_in(fade_duration_);
        }
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
            // Include a thumbnail so the load picker shows the autosave too (#119);
            // manual saves stage one, but the autosave path was passing none. This
            // is the latest gameplay frame (the room we are leaving) — good enough
            // for the autosave preview.
            if (ctx_.thumbnail.valid()) {
                const sf::Image thumb = ctx_.thumbnail.image();
                ctx_.saves.save(pac::core::SaveService::kAutosaveSlot, snap(), &thumb);
            } else {
                ctx_.saves.save(pac::core::SaveService::kAutosaveSlot, snap());
            }
        }
        return;
    }
    if (room_) {
        room_->update_lights(dt);
    }
    if (player_ && room_) {
        player_->update(dt, room_->data());
        room_->update_npcs(dt);
        // Wake any scripted avatar(id):move_to whose avatar has stopped (#139). A
        // null avatar (vanished, e.g. NPC despawn) emits too so the waiter doesn't
        // hang; its scope may already be cancelled, which is harmless.
        for (auto it = pending_moves_.begin(); it != pending_moves_.end();) {
            const Avatar* a = resolve_avatar(it->avatar_id);
            if (a == nullptr || !a->moving()) {
                ctx_.scripting.emit(it->scope, it->event);
                it = pending_moves_.erase(it);
            } else {
                ++it;
            }
        }
        // Wake scripted play_until_end whose one-shot sequence has finished (#149).
        for (auto it = pending_anim_.begin(); it != pending_anim_.end();) {
            const Avatar* a = resolve_avatar(it->avatar_id);
            if (a == nullptr || !a->acting()) {
                ctx_.scripting.emit(it->scope, it->event);
                it = pending_anim_.erase(it);
            } else {
                ++it;
            }
        }
        // Advance scripted object moves/animations and wake any that have stopped
        // moving / finished a one-shot sequence (#142).
        room_->update_objects(dt);
        for (auto it = pending_obj_moves_.begin(); it != pending_obj_moves_.end();) {
            if (!object_exists(it->avatar_id) || !room_->object_moving(it->avatar_id)) {
                ctx_.scripting.emit(it->scope, it->event);
                it = pending_obj_moves_.erase(it);
            } else {
                ++it;
            }
        }
        for (auto it = pending_obj_anim_.begin(); it != pending_obj_anim_.end();) {
            if (!object_exists(it->avatar_id) || !room_->object_acting(it->avatar_id)) {
                ctx_.scripting.emit(it->scope, it->event);
                it = pending_obj_anim_.erase(it);
            } else {
                ++it;
            }
        }
        // Wake any scripted talk(...) once its line has been dismissed (duration
        // elapsed or skipped), so the next line in a cutscene runs (#talk blocking).
        // speech_.update(dt) runs later this frame; checking here means we wake on
        // the frame after the bubble clears, which is fine for sequencing.
        if (!speech_.active() && !pending_speech_.empty()) {
            for (const PendingMove& p : pending_speech_) {
                ctx_.scripting.emit(p.scope, p.event);
            }
            pending_speech_.clear();
        }
        if (camera_) {
            camera_->update(dt); // advance a scripted go_to tween, if any
            if (camera_->following()) {
                camera_->follow(player_->position());
            }
        }
        check_zones();
    }
    // Declarative walk-on entrance. Unblock before the completion hook so the
    // hook may deliberately replace COMMAND with a cutscene or dialog.
    if (pending_player_entry_ && player_ && !player_->moving()) {
        const std::string orientation = pending_player_entry_->final_orientation;
        pending_player_entry_.reset();
        player_->face(orientation);
        view_state_ = ViewState::COMMAND;
        if (room_) {
            const pac::core::ScopeId previous_scope = ctx_.scripting.current_scope();
            ctx_.scripting.set_current_scope(room_scope_);
            room_->call_hook("on_player_entered");
            ctx_.scripting.set_current_scope(previous_scope);
        }
    }
    // A command deferred until the player reaches a `requires_approach` hotspot's
    // approach point fires once the avatar stops (path complete, or as close as
    // pathing allowed). We unblock first so dispatch runs in the COMMAND view.
    if (pending_approach_ && player_ && !player_->moving()) {
        const Command cmd = pending_approach_->cmd;
        pending_approach_.reset();
        view_state_ = ViewState::COMMAND;
        dispatch_and_feedback(cmd);
    }
    // #158: a command deferred behind a moving bound target. Each frame re-resolve
    // the target's live position and decide whether to keep walking, re-route, or
    // fire. A reset target (NPC despawned / object removed) fires from here so the
    // block can't outlive its target.
    if (pending_moving_approach_ && player_ && room_) {
        PendingMovingApproach& pend = *pending_moving_approach_;
        pend.elapsed += dt;
        const RoomHotspot* hs = nullptr;
        if (const auto it = room_->data().hotspots.find(pend.hotspot_id);
            it != room_->data().hotspots.end()) {
            hs = &it->second;
        }
        const std::optional<geom::Point> target = hs ? live_bind_target(hs->bind) : std::nullopt;
        const ChaseParams params;
        const ChaseDecision dec = target ? evaluate_chase(player_->position(),
                                                          *target,
                                                          pend.last_dest,
                                                          player_->moving(),
                                                          pend.elapsed,
                                                          params)
                                         : ChaseDecision{ChaseAction::Fire, {}};
        if (dec.action == ChaseAction::Fire) {
            const Command cmd = pend.cmd;
            pending_moving_approach_.reset();
            view_state_ = ViewState::COMMAND;
            dispatch_and_feedback(cmd);
        } else if (dec.action == ChaseAction::Repath) {
            pend.last_dest = route_to(dec.repath_to);
        }
        // Wait: keep walking the current path.
    }
    // M9 #186: a deferred on_room_resume(fn) beat fires now that this room is the
    // live, ticking scene again (update() only runs on the top scene, so reaching
    // here means the close-up that registered it has popped). Fire only from a
    // settled COMMAND view — not mid room-change and not while another auto-spawned
    // beat is still draining — so beats don't stack. We route through the same
    // spawn_call seam as a verb handler (#183/#184): a `cutscene { ... }` wrapper
    // arms awaiting_handler_task_ itself via _cutscene_arm; a plain blocking fn
    // yields and hands us its task id. Either way the room blocks until it drains.
    if (lua_ && view_state_ == ViewState::COMMAND && !dialog_ && !change_pending_ &&
        !change_armed_ && !awaiting_handler_task_) {
        if (const auto it = lua_->pending_resume.find(current_room_id_);
            it != lua_->pending_resume.end()) {
            const sol::function fn = it->second;
            lua_->pending_resume.erase(it);
            const pac::core::ScopeId prev = ctx_.scripting.current_scope();
            ctx_.scripting.set_current_scope(room_scope_);
            const pac::core::SpawnCallResult call = pac::core::spawn_call(ctx_.scripting, fn);
            ctx_.scripting.set_current_scope(prev);
            if (!call.done) {
                awaiting_handler_task_ = call.task_id;
            }
            if (awaiting_handler_task_) {
                view_state_ = ViewState::BLOCKED;
            }
        }
    }
    // Auto-spawned handler (M9 #183) drained: restore COMMAND, finish the
    // command, sync hover. The task may have changed scene (start_dialog,
    // change_room) — in that case view_state_ is already non-COMMAND, the
    // room scope was reaped, and the awaited id no longer resolves; clear it
    // either way. Also clears if the room scope was cancelled out from under
    // us (room change while running).
    if (awaiting_handler_task_ && !ctx_.scripting.is_task_alive(*awaiting_handler_task_)) {
        awaiting_handler_task_.reset();
        cutscene_skip_ = {};
        if (view_state_ == ViewState::BLOCKED) {
            view_state_ = ViewState::COMMAND;
        }
        command_controller_.finish_execution();
    }
    // Returning to COMMAND (e.g. a cutscene unblocking) resumes camera follow
    // after a scripted override (issue #25).
    if (camera_ && view_state_ == ViewState::COMMAND && prev_view_state_ != ViewState::COMMAND) {
        camera_->follow_player();
    }
    sync_command_hover();
    // Cursor affordance (#73): request the INTERACT cursor when an interactive
    // hotspot is under the pointer in COMMAND. hover_vp_ is offscreen (-1,-1)
    // until the first MouseMoved, so this stays DEFAULT until the mouse moves.
    if (view_state_ == ViewState::COMMAND && room_ && hover_vp_.y >= 0.0f &&
        hover_vp_.y < scenery_height() && hotspot_under(virtual_to_world(hover_vp_)) != nullptr) {
        ctx_.cursor.want(pac::core::CursorKind::INTERACT);
    }
    prev_view_state_ = view_state_;
    speech_.update(dt);
    if (!speech_.active()) {
        end_talk_animation();
    }
    // Age out ambient float_text labels.
    if (!ambient_.empty()) {
        for (AmbientLabel& label : ambient_) {
            label.remaining -= dt;
        }
        ambient_.erase(std::remove_if(ambient_.begin(),
                                      ambient_.end(),
                                      [](const AmbientLabel& l) { return l.remaining <= 0.0f; }),
                       ambient_.end());
    }
    if (dialog_) {
        dialog_->update();
        if (dialog_->ended()) {
            dialog_.reset();
            end_talk_animation();
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

std::string RoomScene::debug_hud_text() const {
    const auto value_str = [](const pac::core::StateValue& v) -> std::string {
        if (const auto* b = std::get_if<bool>(&v)) {
            return *b ? "true" : "false";
        }
        if (const auto* d = std::get_if<double>(&v)) {
            return std::to_string(*d);
        }
        if (const auto* s = std::get_if<std::string>(&v)) {
            return *s;
        }
        return "?";
    };
    const char* view = "?";
    switch (view_state_) {
    case ViewState::COMMAND:
        view = "COMMAND";
        break;
    case ViewState::DIALOG:
        view = "DIALOG";
        break;
    case ViewState::BLOCKED:
        view = "BLOCKED";
        break;
    case ViewState::MENU:
        view = "MENU";
        break;
    }
    const CommandState& command_state = command_controller_.state();
    const char* builder = "?";
    switch (command_state.builder_state) {
    case CommandBuilder::State::IDLE:
        builder = "IDLE";
        break;
    case CommandBuilder::State::EXPECTING_PARAM1_ROOM_OBJECT:
        builder = "WANT_P1_ROOM";
        break;
    case CommandBuilder::State::EXPECTING_PARAM1_INVENTORY_OBJECT:
        builder = "WANT_P1_INV";
        break;
    case CommandBuilder::State::EXPECTING_PARAM1_ANY_OBJECT:
        builder = "WANT_P1_ANY";
        break;
    case CommandBuilder::State::EXPECTING_PARAM2_ROOM_OBJECT:
        builder = "WANT_P2_ROOM";
        break;
    case CommandBuilder::State::EXPECTING_PARAM2_INVENTORY_OBJECT:
        builder = "WANT_P2_INV";
        break;
    case CommandBuilder::State::EXPECTING_PARAM2_ANY_OBJECT:
        builder = "WANT_P2_ANY";
        break;
    case CommandBuilder::State::COMMAND_READY:
        builder = "READY";
        break;
    case CommandBuilder::State::COMMAND_EXECUTING:
        builder = "EXECUTING";
        break;
    }

    std::string out = "ROOM: " + current_room_id_;
    if (!current_zone_.empty()) {
        out += "  ZONE: " + current_zone_;
    }
    out += "  VIEW: ";
    out += view;
    out += "\nCMD: ";
    out += builder;
    if (command_state.selected_verb) {
        out += " verb=";
        out += verb_id(*command_state.selected_verb);
    }
    if (command_state.param1 && command_state.param1->valid()) {
        out += " p1=" + command_state.param1->id;
    }
    if (command_state.param2 && command_state.param2->valid()) {
        out += " p2=" + command_state.param2->id;
    }

    out += "\n-- world state --";
    if (ctx_.state.entries().empty()) {
        out += "\n  (empty)";
    }
    for (const auto& [key, value] : ctx_.state.entries()) {
        out += "\n  " + key + " = " + value_str(value);
    }

    if (const auto it = room_state_.find(current_room_id_);
        it != room_state_.end() && !it->second.empty()) {
        out += "\n-- room state --";
        for (const auto& [key, value] : it->second) {
            out += "\n  " + key + " = " + value_str(value);
        }
    }

    if (room_ && !room_->data().regions.empty()) {
        out += "\n-- regions --";
        for (const auto& [id, region] : room_->data().regions) {
            out += "\n  " + id + " = " + room_->region_state(id);
        }
    }
    return out;
}

void RoomScene::draw(sf::RenderTarget& target) const {
    const sf::Vector2u vres = ctx_.display.virtual_resolution();
    if (room_ && camera_) {
        sf::View scenery(camera_->view_rect());
        scenery.setViewport(ctx_.display.viewport_for(
            sf::FloatRect(0.0f, 0.0f, static_cast<float>(vres.x), scenery_height())));

        const RoomPostProcess* post =
            tuning_overlay_active()
                ? tuning_overlay_->effective_post_process(room_->data())
                : (room_->data().post_process ? &*room_->data().post_process : nullptr);
        const bool post_active =
            post && post->enabled &&
            std::any_of(post->shaders.begin(), post->shaders.end(), [](const auto& fx) {
                return fx.enabled && fx.controller.empty();
            });
        const RoomLighting* lighting =
            tuning_overlay_active()
                ? tuning_overlay_->effective_lighting(room_->data())
                : (room_->data().dynamic_lighting ? &*room_->data().dynamic_lighting : nullptr);
        std::vector<ResolvedRoomLight> resolved_lights;
        if (lighting) {
            resolved_lights.reserve(lighting->lights.size());
            for (const RoomLight& light : lighting->lights) {
                geom::Point position = light.at;
                const Avatar* attached_avatar = nullptr;
                bool resolved_attachment = true;
                if (light.attach == "player") {
                    attached_avatar = player_ ? &*player_ : nullptr;
                    resolved_attachment = attached_avatar != nullptr;
                } else if (light.attach.starts_with("avatar:")) {
                    attached_avatar = resolve_avatar(light.attach.substr(7));
                    resolved_attachment = attached_avatar != nullptr;
                } else if (light.attach.starts_with("object:")) {
                    const std::string id = light.attach.substr(7);
                    resolved_attachment =
                        room_->data().objects.count(id) > 0 && room_->object_visible(id);
                    if (resolved_attachment) {
                        position = room_->object_position(id);
                    }
                }
                if (!resolved_attachment) {
                    continue;
                }
                if (attached_avatar) {
                    position = attached_avatar->position();
                }
                position = position + light.offset;

                float direction = light.direction;
                if (light.follow_facing && attached_avatar) {
                    const std::string facing = attached_avatar->facing();
                    if (facing == "down") {
                        direction += 90.0f;
                    } else if (facing == "left") {
                        direction += 180.0f;
                    } else if (facing == "up") {
                        direction += 270.0f;
                    }
                }
                const bool tuning_values =
                    tuning_overlay_active() && tuning_overlay_->using_working_values();
                resolved_lights.push_back(
                    {&light,
                     position,
                     direction,
                     tuning_values ? light.enabled : room_->light_enabled(light.id),
                     tuning_values ? light.intensity : room_->light_intensity(light.id)});
            }
        }
        std::vector<const LightOccluder*> resolved_occluders;
        if (lighting) {
            resolved_occluders.reserve(lighting->occluders.size());
            for (const LightOccluder& occluder : lighting->occluders) {
                if (room_->light_occluder_enabled(occluder.id)) {
                    resolved_occluders.push_back(&occluder);
                }
            }
        }

        std::optional<ProjectedShadow> resolved_projected_shadow;
        if (room_->data().projected_shadow &&
            !room_->data().projected_shadow->source.empty()) {
            const ProjectedShadow& authored = *room_->data().projected_shadow;
            const auto source = std::find_if(
                resolved_lights.begin(),
                resolved_lights.end(),
                [&authored](const ResolvedRoomLight& light) {
                    return light.light && light.light->id == authored.source;
                });
            if (source != resolved_lights.end() && source->enabled && source->intensity > 0.0f) {
                resolved_projected_shadow = authored;
                resolved_projected_shadow->light = source->position;
                const float effective = source->intensity *
                                        evaluate_light_modulation(source->light->modulation,
                                                                  shader_time_);
                resolved_projected_shadow->opacity *= std::clamp(effective, 0.0f, 1.0f);
            }
        }
        const ProjectedShadow* projected_shadow_override =
            resolved_projected_shadow ? &*resolved_projected_shadow : nullptr;
        const bool scenery_effects_active = post_active || lighting;
        bool scenery_composited = false;

        if (scenery_effects_active) {
            const sf::Vector2u post_size{vres.x,
                                         static_cast<unsigned>(std::ceil(scenery_height()))};
            if (!post_process_target_ || post_process_target_->getSize() != post_size) {
                auto next = std::make_unique<sf::RenderTexture>();
                if (next->create(post_size.x, post_size.y)) {
                    next->setSmooth(ctx_.resources.smooth_textures());
                    post_process_target_ = std::move(next);
                    const std::size_t next_bytes =
                        static_cast<std::size_t>(post_size.x) * post_size.y * 4;
                    pac::core::add_shader_rt_bytes(
                        static_cast<std::ptrdiff_t>(next_bytes) -
                        static_cast<std::ptrdiff_t>(post_process_rt_bytes_));
                    post_process_rt_bytes_ = next_bytes;
                } else {
                    post_process_target_.reset();
                    if (post_process_rt_bytes_ != 0) {
                        pac::core::add_shader_rt_bytes(
                            -static_cast<std::ptrdiff_t>(post_process_rt_bytes_));
                        post_process_rt_bytes_ = 0;
                    }
                    ctx_.log.error("room '" + room_->data().id +
                                   "': could not create the scenery post-process target");
                }
            }

            if (post_process_target_) {
                // Render the camera's world rectangle into a viewport-sized
                // texture. The room renderer includes every scenery drawable;
                // overlays are deliberately drawn later on the main target.
                post_process_target_->setView(sf::View(camera_->view_rect()));
                post_process_target_->clear(sf::Color::Transparent);
                renderer_.draw(*post_process_target_,
                               *room_,
                               room_dir_,
                               ctx_.resources,
                               player_ ? &*player_ : nullptr,
                               room_->npcs(),
                               ctx_.log,
                               ShaderEnv{shader_time_},
                               projected_shadow_override);
                post_process_target_->display();

                const sf::IntRect full(0,
                                       0,
                                       static_cast<int>(post_size.x),
                                       static_cast<int>(post_size.y));
                gfx::RuntimeShaderPass lighting_pass;
                const gfx::RuntimeShaderPass* lighting_prefix = nullptr;
                if (lighting) {
                    if (!lighting_renderer_) {
                        lighting_renderer_ = std::make_unique<RoomLightingRenderer>();
                    }
                    if (lighting_renderer_->make_pass(*lighting,
                                                      resolved_lights,
                                                      resolved_occluders,
                                                      camera_->view_rect(),
                                                      shader_time_,
                                                      room_dir_,
                                                      ctx_.resources,
                                                      ctx_.log,
                                                      lighting_pass)) {
                        lighting_prefix = &lighting_pass;
                    }
                }

                static const std::vector<gfx::ShaderEffect> kNoPostEffects;
                const std::vector<gfx::ShaderEffect>& post_effects =
                    post_active ? post->shaders : kNoPostEffects;
                const sf::Texture* processed =
                    post_process_chain_.apply(ctx_.resources,
                                              post_process_target_->getTexture(),
                                              full,
                                              post_effects,
                                              shader_time_,
                                              lighting_prefix);
                const sf::Texture& output =
                    processed ? *processed : post_process_target_->getTexture();
                target.setView(ctx_.display.view());
                target.draw(sf::Sprite(output, full));
                scenery_composited = true;
            }
        }

        if (!scenery_composited) {
            target.setView(scenery);
            renderer_.draw(target,
                           *room_,
                           room_dir_,
                           ctx_.resources,
                           player_ ? &*player_ : nullptr,
                           room_->npcs(),
                           ctx_.log,
                           ShaderEnv{shader_time_},
                           projected_shadow_override);
        }

        // World-space overlays are intentionally outside the post-process so
        // authoring guides and text retain their exact UI colours.
        target.setView(scenery);
        if (ctx_.dev.edit_mode) {
            debug_overlay_.draw_world(target,
                                      debug_flags_,
                                      room_->data(),
                                      player_ ? &*player_ : nullptr,
                                      room_->npcs(),
                                      font_);
        }
        speech_.draw(target, speech_font_); // world coordinates, over the scenery
        draw_ambient(target);               // float_text labels, world coordinates
    }

    target.setView(ctx_.display.view());
    // The SCUMM panel is hidden in BLOCKED (cutscene-like) state — design 04 §Room
    // view states: a blocked room shows a black/hidden panel. The window clears to
    // black, so not drawing it leaves a clean black bar under the scenery.
    if (tuning_overlay_active()) {
        tuning_overlay_->draw(target);
    } else if (panel_ && view_state_ != ViewState::BLOCKED) {
        if (view_state_ == ViewState::DIALOG) {
            // Options show only while awaiting a choice; while a line is being
            // spoken the option list is empty, so we draw nothing and leave a
            // clean bar under the scenery (the speech bubble floats over it).
            const std::vector<DialogOption> opts =
                dialog_ ? dialog_->options() : std::vector<DialogOption>{};
            if (!opts.empty()) {
                std::vector<std::string> labels;
                labels.reserve(opts.size());
                for (const DialogOption& opt : opts) {
                    labels.push_back(opt.text);
                }
                panel_->draw_options(target, ctx_.strings, labels, dialog_page_, hover_vp_);
            }
        } else {
            EvidenceProgress evidence;
            const ScummEvidenceIndicator& ev = panel_->config().evidence_indicator;
            if (ev.enabled) {
                const auto read_count = [this](const std::string& key) -> int {
                    if (key.empty()) {
                        return 0;
                    }
                    const auto v = ctx_.state.get(key);
                    if (!v) {
                        return 0;
                    }
                    if (const double* d = std::get_if<double>(&*v)) {
                        return static_cast<int>(*d);
                    }
                    if (const bool* b = std::get_if<bool>(&*v)) {
                        return *b ? 1 : 0;
                    }
                    return 0;
                };
                evidence.collected = read_count(ev.collected_state);
                evidence.total = read_count(ev.total_state);
            }
            panel_->draw(target,
                         ctx_.strings,
                         inventory_,
                         command_controller_.state(),
                         hover_vp_,
                         evidence,
                         [this](const std::string& item_id) {
                             const auto value =
                                 ctx_.state.get("inventory.notification." + item_id);
                             return value && std::holds_alternative<bool>(*value) &&
                                    std::get<bool>(*value);
                         });
        }
    }
    if (view_state_ == ViewState::MENU) {
        draw_menu(target);
    }

    if (ctx_.dev.edit_mode && debug_flags_.hud) {
        target.setView(ctx_.display.view());
        debug_overlay_.draw_hud(target, font_, debug_hud_text());
    }

    // change_room fade overlay: a black quad over the whole window (bars too),
    // drawn last so it covers the scenery and panel during the transition.
    const sf::Uint8 fade_a = room_fade_.alpha255();
    if (fade_a > 0) {
        const sf::View prev = target.getView();
        const sf::Vector2f size(static_cast<float>(target.getSize().x),
                                static_cast<float>(target.getSize().y));
        target.setView(sf::View(sf::FloatRect(0.0f, 0.0f, size.x, size.y)));
        sf::RectangleShape quad(size);
        quad.setFillColor(sf::Color(0, 0, 0, fade_a));
        target.draw(quad);
        target.setView(prev);
    }
}

// --- genre Lua API targets ---

void RoomScene::api_change_room(const std::string& id, const std::string& entry_point) {
    pending_room_ = id;
    pending_entry_ = entry_point;
    if (fade_duration_ > 0.0f) {
        // Fade the scenery to black; update() does the load once fully black.
        room_fade_.fade_out(fade_duration_);
        change_armed_ = true;
    } else {
        change_pending_ = true;
    }
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

void RoomScene::api_set_layer_visible(const std::string& layer_id, bool visible) {
    if (room_) {
        room_->set_layer_visible(layer_id, visible);
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

std::string RoomScene::api_talk(const std::string& speaker_id,
                                const std::string& text,
                                bool continue_action,
                                const std::optional<std::string>& face_target) {
    sf::Color color(230, 230, 230);
    float gap = 48.0f;
    if (const Character* c = cast_.character(speaker_id)) {
        color = c->speech_color;
        gap = c->speech_gap;
    }
    // Anchor at the speaking NPC's avatar when there is one, so `talk("npc", ...)`
    // appears over that NPC instead of the player. Player speech and speakers
    // without an in-room avatar fall back to the player position.
    if (room_) {
        if (const Avatar* npc = room_->npc(speaker_id)) {
            say_at(text, color, speech_anchor(*npc), gap);
        } else {
            say(text, color, gap);
        }
    } else {
        say(text, color, gap);
    }
    // Nothing shown (empty text) -> no event, so the Lua wrapper never waits.
    if (!speech_.active()) {
        end_talk_animation();
        return std::string();
    }
    begin_talk_animation(speaker_id, continue_action, face_target);
    const std::string event = "__spoke." + speaker_id + "." + std::to_string(++talk_seq_);
    pending_speech_.push_back({speaker_id, ctx_.scripting.current_scope(), event});
    return event;
}

void RoomScene::begin_talk_animation(const std::string& speaker_id,
                                     bool continue_action,
                                     const std::optional<std::string>& face_target) {
    end_talk_animation();
    if (continue_action) {
        return;
    }
    Avatar* speaker = resolve_avatar(speaker_id);
    if (!speaker) {
        return; // cast-only/close-up speakers can still show their line
    }
    speaker->stop();
    if (face_target) {
        if (const Avatar* target = resolve_avatar(*face_target)) {
            speaker->face(nearest_direction(target->position() - speaker->position()));
        } else if (room_) {
            if (const geom::Point* target = room_->data().point(*face_target)) {
                speaker->face(nearest_direction(*target - speaker->position()));
            }
        }
    }
    speaker->talk();
    if (speaker->talking()) {
        talking_avatar_id_ = speaker_id;
    }
}

void RoomScene::end_talk_animation() {
    if (talking_avatar_id_.empty()) {
        return;
    }
    if (Avatar* speaker = resolve_avatar(talking_avatar_id_)) {
        speaker->stop_talking();
    }
    talking_avatar_id_.clear();
}

void RoomScene::prepare_dialog_participants(const std::string& npc_id) {
    Avatar* npc = resolve_avatar(npc_id);
    if (!player_ || !npc || npc == &*player_) {
        return;
    }
    player_->stop();
    npc->stop();
    player_->face(nearest_direction(npc->position() - player_->position()));
    npc->face(nearest_direction(player_->position() - npc->position()));
}

void RoomScene::api_camera_look_at(geom::Point target) {
    if (camera_) {
        camera_->look_at(target);
    }
}

float RoomScene::api_camera_go_to(geom::Point target) {
    if (!camera_) {
        return 0.0f;
    }
    // Engine-chosen pan duration: distance / pan speed, clamped to a comfortable
    // range (the API takes no explicit duration).
    constexpr float kPanSpeed = 700.0f; // world px/s
    constexpr float kMinDuration = 0.2f;
    constexpr float kMaxDuration = 2.0f;
    const float dist = geom::distance(camera_->center(), target);
    const float duration = std::clamp(dist / kPanSpeed, kMinDuration, kMaxDuration);
    camera_->go_to(target, duration);
    return duration;
}

void RoomScene::api_camera_follow_player() {
    if (camera_) {
        camera_->follow_player();
        if (player_) {
            camera_->follow(player_->position());
        }
    }
}

Avatar* RoomScene::resolve_avatar(const std::string& id) {
    if (player_ && id == player_char_) {
        return &*player_;
    }
    return room_ ? room_->npc(id) : nullptr;
}

const Avatar* RoomScene::resolve_avatar(const std::string& id) const {
    if (player_ && id == player_char_) {
        return &*player_;
    }
    return room_ ? room_->npc(id) : nullptr;
}

std::string RoomScene::api_avatar_move_to(const std::string& id, geom::Point target) {
    Avatar* a = resolve_avatar(id);
    if (!a || !room_) {
        ctx_.log.error("avatar('" + id + "'):move_to — no such avatar in the room");
        return std::string();
    }
    const RoomData& d = room_->data();
    // Flag a scripted move whose target sits outside the walkable area — likely an
    // authoring slip (a stale point, or a point dropped off the floor). The path
    // still routes to the nearest reachable point, but the avatar won't reach the
    // requested spot, which is confusing without this hint.
    if (!d.walkable.empty() && !d.is_walkable(target)) {
        ctx_.log.warn("avatar('" + id + "'):move_to target (" + std::to_string(target.x) + ", " +
                      std::to_string(target.y) +
                      ") is outside the walkable area; routing to the nearest reachable point");
    }
    a->follow_path(geom::find_path(a->position(), target, d.walkable, d.active_obstacles()));
    const std::string event = "__avatar_arrived." + id + "." + std::to_string(++move_seq_);
    pending_moves_.push_back({id, ctx_.scripting.current_scope(), event});
    return event;
}

void RoomScene::api_avatar_face(const std::string& id, const std::string& direction) {
    if (Avatar* a = resolve_avatar(id)) {
        a->face(direction);
    } else {
        ctx_.log.error("avatar('" + id + "'):face — no such avatar in the room");
    }
}

void RoomScene::api_avatar_look_at(const std::string& id, geom::Point target) {
    if (Avatar* a = resolve_avatar(id)) {
        a->face(nearest_direction(target - a->position()));
    } else {
        ctx_.log.error("avatar('" + id + "'):look_at — no such avatar in the room");
    }
}

std::optional<geom::Point> RoomScene::api_avatar_position(const std::string& id) const {
    const Avatar* a = resolve_avatar(id);
    if (!a) {
        return std::nullopt;
    }
    return a->position();
}

void RoomScene::api_avatar_play(const std::string& id, const std::string& sequence) {
    if (Avatar* a = resolve_avatar(id)) {
        a->play(sequence);
    } else {
        ctx_.log.error("avatar('" + id + "'):play — no such avatar in the room");
    }
}

std::string RoomScene::api_avatar_play_until_end(const std::string& id,
                                                 const std::string& sequence) {
    Avatar* a = resolve_avatar(id);
    if (!a) {
        ctx_.log.error("avatar('" + id + "'):play_until_end — no such avatar in the room");
        return std::string();
    }
    a->play(sequence);
    if (!a->acting()) {
        // play() was a no-op (unknown sequence) — do not yield, or the task hangs.
        ctx_.log.warn("avatar('" + id + "'):play_until_end — unknown sequence '" + sequence + "'");
        return std::string();
    }
    const std::string event = "__avatar_anim." + id + "." + std::to_string(++anim_seq_);
    pending_anim_.push_back({id, ctx_.scripting.current_scope(), event});
    return event;
}

std::optional<geom::Point> RoomScene::api_avatar_anchor(const std::string& id,
                                                        const std::string& name) const {
    const Avatar* a = resolve_avatar(id);
    if (!a) {
        return std::nullopt;
    }
    return a->anchor(name);
}

void RoomScene::api_avatar_set_visible(const std::string& id, bool visible) {
    if (Avatar* avatar = resolve_avatar(id)) {
        avatar->set_visible(visible);
    } else {
        ctx_.log.error("avatar('" + id + "'):set_visible — no such avatar in the room");
    }
}

void RoomScene::api_avatar_set_shadow_opacity(const std::string& id,
                                              float opacity,
                                              float transition_seconds) {
    if (Avatar* avatar = resolve_avatar(id)) {
        avatar->set_shadow_opacity(opacity, transition_seconds);
    } else {
        ctx_.log.error("avatar('" + id +
                       "'):set_shadow_opacity — no such avatar in the room");
    }
}

bool RoomScene::object_exists(const std::string& id) const {
    return room_ && room_->data().objects.count(id) > 0;
}

std::string RoomScene::api_object_move_to(const std::string& id, geom::Point target, float speed) {
    if (!object_exists(id)) {
        ctx_.log.error("object('" + id + "'):move_to — no such object in the room");
        return std::string();
    }
    room_->object_move_to(id, target, speed);
    const std::string event = "__object_arrived." + id + "." + std::to_string(++obj_move_seq_);
    pending_obj_moves_.push_back({id, ctx_.scripting.current_scope(), event});
    return event;
}

void RoomScene::api_object_set_position(const std::string& id, geom::Point p) {
    if (object_exists(id)) {
        room_->set_object_position(id, p);
    } else {
        ctx_.log.error("object('" + id + "'):set_position — no such object in the room");
    }
}

std::optional<geom::Point> RoomScene::api_object_position(const std::string& id) const {
    if (!object_exists(id)) {
        return std::nullopt;
    }
    return room_->object_position(id);
}

void RoomScene::api_object_set_scale(const std::string& id, float scale) {
    if (!object_exists(id)) {
        ctx_.log.error("object('" + id + "'):set_scale — no such object in the room");
        return;
    }
    if (scale > 0.0f) {
        room_->set_object_scale(id, scale);
    }
}

void RoomScene::api_object_set_rotation(const std::string& id, float degrees) {
    if (!object_exists(id)) {
        ctx_.log.error("object('" + id + "'):set_rotation — no such object in the room");
        return;
    }
    room_->set_object_rotation(id, degrees);
}

std::optional<float> RoomScene::api_object_rotation(const std::string& id) const {
    if (!object_exists(id)) {
        return std::nullopt;
    }
    return room_->object_rotation(id);
}

void RoomScene::api_light_set_enabled(const std::string& id, bool enabled) {
    if (!room_ || !room_->has_light(id)) {
        ctx_.log.error("light('" + id + "'):set_enabled — no such light in the room");
        return;
    }
    room_->set_light_enabled(id, enabled);
}

std::optional<bool> RoomScene::api_light_enabled(const std::string& id) const {
    if (!room_ || !room_->has_light(id)) {
        return std::nullopt;
    }
    return room_->light_enabled(id);
}

void RoomScene::api_light_set_intensity(const std::string& id,
                                        float intensity,
                                        float transition_seconds) {
    if (!room_ || !room_->has_light(id)) {
        ctx_.log.error("light('" + id + "'):set_intensity — no such light in the room");
        return;
    }
    if (!std::isfinite(intensity) || !std::isfinite(transition_seconds)) {
        ctx_.log.error(
            "light('" + id + "'):set_intensity — intensity and transition must be finite");
        return;
    }
    room_->set_light_intensity(id, intensity, transition_seconds);
}

std::optional<float> RoomScene::api_light_intensity(const std::string& id) const {
    if (!room_ || !room_->has_light(id)) {
        return std::nullopt;
    }
    return room_->light_intensity(id);
}

void RoomScene::api_light_occluder_set_enabled(const std::string& id, bool enabled) {
    if (!room_ || !room_->has_light_occluder(id)) {
        ctx_.log.error("light_occluder('" + id +
                       "'):set_enabled — no such light occluder in the room");
        return;
    }
    room_->set_light_occluder_enabled(id, enabled);
}

std::optional<bool> RoomScene::api_light_occluder_enabled(const std::string& id) const {
    if (!room_ || !room_->has_light_occluder(id)) {
        return std::nullopt;
    }
    return room_->light_occluder_enabled(id);
}

void RoomScene::api_object_play(const std::string& id, const std::string& sequence) {
    if (!object_exists(id)) {
        ctx_.log.error("object('" + id + "'):play — no such object in the room");
        return;
    }
    if (!room_->object_play(id, sequence, false)) {
        ctx_.log.warn("object('" + id + "'):play — '" + id +
                      "' is not animated or has no sequence '" + sequence + "'");
    }
}

std::string RoomScene::api_object_play_until_end(const std::string& id,
                                                 const std::string& sequence) {
    if (!object_exists(id)) {
        ctx_.log.error("object('" + id + "'):play_until_end — no such object in the room");
        return std::string();
    }
    if (!room_->object_play(id, sequence, true)) {
        ctx_.log.warn("object('" + id + "'):play_until_end — '" + id +
                      "' is not animated or has no sequence '" + sequence + "'");
        return std::string(); // do not yield, or the task hangs
    }
    const std::string event = "__object_anim." + id + "." + std::to_string(++obj_anim_seq_);
    pending_obj_anim_.push_back({id, ctx_.scripting.current_scope(), event});
    return event;
}

void RoomScene::build_object_sprites() {
    if (!room_) {
        return;
    }
    for (const auto& [id, object] : room_->data().objects) {
        // An animation file (*.yml / *.yaml) makes this an animated object; a
        // static texture (e.g. *.png) is drawn directly by the renderer.
        const bool is_anim = object.sprite.size() >= 4 &&
                             (object.sprite.ends_with(".yml") || object.sprite.ends_with(".yaml"));
        if (!is_anim) {
            continue;
        }
        try {
            gfx::VisualSprite sprite =
                gfx::load_visual_sprite(ctx_.resources,
                                        pac::core::logical_join(room_dir_, object.sprite));
            if (!object.shaders.empty()) {
                sprite.set_shaders(object.shaders);
            }
            room_->set_object_sprite(id, std::move(sprite));
        } catch (const std::exception& e) {
            ctx_.log.error("object '" + id + "': could not load animation '" + object.sprite +
                           "': " + e.what());
            continue;
        }
        // Seat the initial looping sequence so the object renders something.
        if (!object.sequence.empty()) {
            if (!room_->object_play(id, object.sequence, false)) {
                ctx_.log.warn("object '" + id + "': initial sequence '" + object.sequence +
                              "' not found in its animation");
            }
        } else {
            ctx_.log.warn("object '" + id +
                          "' is animated but has no 'sequence'; it will not show a frame until "
                          "object('" +
                          id + "'):play(...) is called");
        }
    }
}

void RoomScene::api_spawn_npc(const std::string& id,
                              geom::Point start,
                              const std::string& orientation) {
    if (!room_) {
        ctx_.log.error("spawn_npc('" + id + "'): no room is loaded");
        return;
    }
    if (player_ && id == player_char_) {
        ctx_.log.error("spawn_npc('" + id + "'): '" + id +
                       "' is the player character and is not spawnable");
        return;
    }
    // Already present: reposition + reface rather than rebuild, so spawn_npc is
    // idempotent when called each on_load against global state.
    if (Avatar* existing = room_->npc(id)) {
        existing->set_position(start);
        existing->face(orientation);
        return;
    }
    auto avatar = make_avatar(id);
    if (!avatar) {
        return; // make_avatar logged the cause (unknown id / bad appearance)
    }
    avatar->set_position(start);
    avatar->face(orientation);
    room_->add_npc(id, std::move(*avatar));
}

void RoomScene::api_despawn_npc(const std::string& id) {
    if (room_) {
        room_->remove_npc(id);
    }
}

// --- declarative room configs (#185) ---

std::string RoomScene::apply_room_config() {
    if (!room_ || !room_->data().configs) {
        return ""; // a room without `configs:` behaves exactly as before
    }
    const RoomConfigs& cfgs = *room_->data().configs;
    // Effective config: the persisted live value, else the room's `start`. A
    // stale/unknown persisted id (e.g. a renamed config in a newer build) falls
    // back to `start` rather than leaving the room un-reconciled.
    std::string cur;
    if (const auto v = ctx_.state.get(config_cur_key(current_room_id_));
        v && std::holds_alternative<std::string>(*v)) {
        cur = std::get<std::string>(*v);
    }
    if (cur.empty() || !cfgs.find(cur)) {
        cur = cfgs.start;
    }
    ctx_.state.set(config_cur_key(current_room_id_), pac::core::StateValue{cur});
    reconcile_to_config(cur);
    return cur;
}

void RoomScene::reconcile_to_config(const std::string& config_id) {
    if (!room_ || !room_->data().configs) {
        return;
    }
    const RoomConfigs& cfgs = *room_->data().configs;
    const RoomConfig* c = cfgs.find(config_id);
    if (!c) {
        return;
    }
    const RoomData& d = room_->data();
    // NPCs: spawn those this config names (only if absent, so an actor a beat
    // already walked in isn't teleported), despawn every other managed NPC.
    for (const std::string& id : cfgs.managed_npcs) {
        const auto it = c->npcs.find(id);
        if (it != c->npcs.end()) {
            if (!room_->npc(id)) {
                if (const geom::Point* p = d.point(it->second.at)) {
                    api_spawn_npc(id, *p, it->second.facing);
                }
            }
        } else {
            api_despawn_npc(id);
        }
    }
    // Objects / obstacles: exhaustive show/enable iff named in this config.
    for (const std::string& id : cfgs.managed_objects) {
        const bool show = std::find(c->objects.begin(), c->objects.end(), id) != c->objects.end();
        api_show_object(id, show);
    }
    for (const std::string& id : cfgs.managed_obstacles) {
        const bool enabled =
            std::find(c->obstacles.begin(), c->obstacles.end(), id) != c->obstacles.end();
        room_->set_obstacle_enabled(id, enabled);
    }
}

void RoomScene::run_config_beat(const std::string& config_id) {
    if (config_id.empty() || !room_ || !room_->data().configs) {
        return;
    }
    const std::string seen_key = config_seen_key(current_room_id_, config_id);
    bool seen = false;
    if (const auto v = ctx_.state.get(seen_key); v && std::holds_alternative<bool>(*v)) {
        seen = std::get<bool>(*v);
    }
    // First load in this config runs on_first_enter (once); later loads run
    // on_reenter. Mark seen now so a beat that itself changes room can't re-trigger
    // the first-enter on the way back.
    if (!seen) {
        ctx_.state.set(seen_key, pac::core::StateValue{true});
    }
    const VerbResult beat =
        room_->call_config_beat(config_id, seen ? "on_reenter" : "on_first_enter");
    if (beat.in_flight) {
        awaiting_handler_task_ = *beat.in_flight;
    }
    // A cutscene-wrapped beat arms awaiting_handler_task_ via _cutscene_arm even
    // when its wrapper completes synchronously; either way, block until it drains.
    if (awaiting_handler_task_) {
        view_state_ = ViewState::BLOCKED;
    }
}

void RoomScene::api_set_room_config(const std::string& room_id, const std::string& config_id) {
    const bool live = room_id == current_room_id_ && room_ && room_->data().configs.has_value();
    if (live && !room_->data().configs->find(config_id)) {
        ctx_.log.error("set_room_config('" + room_id + "', '" + config_id +
                       "'): no such config in room '" + room_id + "'");
        return;
    }
    ctx_.state.set(config_cur_key(room_id), pac::core::StateValue{config_id});
    if (live) {
        // The live room: reconcile presence now. Count this in-room transition as
        // "seen" — the beat that called set_room_config IS the config's reveal, so
        // a later re-entry should run on_reenter, not on_first_enter.
        ctx_.state.set(config_seen_key(room_id, config_id), pac::core::StateValue{true});
        reconcile_to_config(config_id);
    }
    // For another room the value is recorded only; apply_room_config reconciles it
    // when that room next loads (so on_first_enter still fires there the first time).
}

std::string RoomScene::api_current_room_config(const std::string& room_id) const {
    if (const auto v = ctx_.state.get(config_cur_key(room_id));
        v && std::holds_alternative<std::string>(*v)) {
        return std::get<std::string>(*v);
    }
    // Not set yet: for the live room fall back to its declared start config.
    if (room_id == current_room_id_ && room_ && room_->data().configs) {
        return room_->data().configs->start;
    }
    return "";
}

void RoomScene::api_start_dialog(const std::string& dialog_id, const std::string& speaker_id) {
    // BLOCKED is a valid origin: a cutscene or deferred on_room_resume beat may
    // finish its choreography by handing control to a dialog. The drain seam
    // only restores BLOCKED -> COMMAND, so the DIALOG state set below survives
    // after that task exits. Test actual ownership instead of treating every
    // non-COMMAND state as a nested dialog.
    if (dialog_) {
        ctx_.log.error("start_dialog('" + dialog_id + "'): dialog '" + dialog_->npc_id() +
                       "' is already running");
        return;
    }
    if (view_state_ == ViewState::MENU) {
        ctx_.log.error("start_dialog('" + dialog_id + "'): cannot start while the menu is open");
        return;
    }

    // Optional game-level subscriber for presentation that must react to every
    // successfully started dialog without coupling story scripts to it.
    sol::protected_function hook = ctx_.scripting.lua()["__on_dialog_started"];
    if (hook.valid()) {
        sol::protected_function_result result = hook(dialog_id);
        if (!result.valid()) {
            const sol::error error = result;
            ctx_.log.error(std::string("__on_dialog_started: ") + error.what());
        }
    }

    prepare_dialog_participants(speaker_id);
    dialog_text_anchor_.reset();
    DialogHost host;
    host.set_text_anchor = [this](const std::string& point_name) {
        if (room_) {
            if (const geom::Point* p = room_->data().point(point_name)) {
                dialog_text_anchor_ = *p;
                return;
            }
        }
        ctx_.log.warn("dialog text_anchor '" + point_name + "' is not a known room point");
    };
    host.speak_npc = [this, speaker_id](const std::string& text) {
        sf::Color color(230, 230, 230);
        float gap = 48.0f;
        if (const Character* c = cast_.character(speaker_id)) {
            color = c->speech_color;
            gap = c->speech_gap;
        }
        // A declared text_anchor wins (used as the float-above anchor); otherwise
        // the balloon follows the speaker NPC avatar's head.
        geom::Point pos{640.0f, 360.0f};
        if (dialog_text_anchor_) {
            pos = *dialog_text_anchor_;
        } else if (room_) {
            if (const Avatar* a = room_->npc(speaker_id)) {
                pos = speech_anchor(*a);
            }
        }
        say_at(text, color, pos, gap);
        if (speech_.active()) {
            begin_talk_animation(speaker_id, false, player_char_);
        }
    };
    host.speak_player = [this, speaker_id](const std::string& text) {
        sf::Color color(230, 230, 230);
        float gap = 48.0f;
        if (const Character* c = cast_.character(player_char_)) {
            color = c->speech_color;
            gap = c->speech_gap;
        }
        const geom::Point pos = player_ ? speech_anchor(*player_) : geom::Point{640.0f, 360.0f};
        say_at(text, color, pos, gap);
        if (speech_.active()) {
            begin_talk_animation(player_char_, false, speaker_id);
        }
    };
    host.is_speaking = [this]() { return speech_.active(); };
    // `once`-consumption persists in the global StateStore under the
    // engine-reserved `__dialog.<id>.<node>.<idx>` prefix, so it survives
    // dialog end + restart and folds into GameState on save.
    // Once-consumption is namespaced by the dialog id (the file), so a speaker with
    // several dialogs keeps each dialog's `once` flags separate.
    auto consumed_key = [dialog_id](const std::string& node, int idx) {
        return "__dialog." + dialog_id + "." + node + "." + std::to_string(idx);
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
        DialogRuntime::start(ctx_.scripting, ctx_.resources, ctx_.log, dialog_id, std::move(host));
    if (!rt) {
        ctx_.scripting.cancel_scope(dialog_scope_);
        dialog_scope_ = 0;
        return;
    }
    dialog_.emplace(std::move(*rt));
    dialog_page_ = 0;
    view_state_ = ViewState::DIALOG;
}

// --- pause / save / load / settings menu (M5c/2) ---

std::vector<RoomScene::MenuButton> RoomScene::menu_buttons() const {
    // Single vertical column. The picker UI for save/load
    // (slots, thumbnails, descriptions) lives in the SaveLoadScene (issue #108)
    // and is pushed by `trigger_menu`; settings is another scene overlay. The
    // Save action is disabled when the engine can't take a coherent snapshot or
    // no save scene is configured; the Load action is disabled when no slot
    // exists or no load scene is configured.
    auto any_save_exists = [this]() {
        for (int s = 0; s < pac::core::SaveService::kSlotCount; ++s) {
            if (ctx_.saves.slot_exists(s)) {
                return true;
            }
        }
        return false;
    };
    const bool save_enabled = can_save() && !ctx_.scenes.save_scene_id().empty();
    const bool load_enabled = any_save_exists() && !ctx_.scenes.load_scene_id().empty();

    std::vector<MenuButton> out;
    out.push_back({{}, MenuAction::RESUME, ctx_.strings.ui_label("resume"), {}, 0, true});
    out.push_back(
        {{}, MenuAction::OPEN_SAVE, ctx_.strings.ui_label("save_game"), {}, 10, save_enabled});
    out.push_back(
        {{}, MenuAction::OPEN_LOAD, ctx_.strings.ui_label("load_game"), {}, 20, load_enabled});
    for (const PauseOverlayAction& overlay : pause_overlays_) {
        out.push_back({{},
                       MenuAction::PUSH_OVERLAY,
                       ctx_.strings.ui_label(overlay.label_key),
                       overlay.scene,
                       overlay.order,
                       true});
    }
    out.push_back({{}, MenuAction::OPEN_SETTINGS, ctx_.strings.ui_label("settings"), {}, 90, true});
    out.push_back(
        {{}, MenuAction::QUIT_TO_TITLE, ctx_.strings.ui_label("quit_to_title"), {}, 100, true});
    std::stable_sort(out.begin(), out.end(), [](const MenuButton& a, const MenuButton& b) {
        return a.order < b.order;
    });

    const sf::Vector2u vres = ctx_.display.virtual_resolution();
    const float w = 360.0f;
    const float row_h = 56.0f;
    const float gap = 14.0f;
    const int count = static_cast<int>(out.size());
    const float total_h = count * row_h + (count - 1) * gap;
    const float left = (static_cast<float>(vres.x) - w) / 2.0f;
    const float top = (static_cast<float>(vres.y) - total_h) / 2.0f;
    for (int i = 0; i < count; ++i) {
        const float y = top + static_cast<float>(i) * (row_h + gap);
        out[static_cast<std::size_t>(i)].rect = {left, y, w, row_h};
    }
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
            trigger_menu(b);
            return;
        }
    }
}

void RoomScene::trigger_menu(const MenuButton& button) {
    switch (button.action) {
    case MenuAction::RESUME:
        view_state_ = ViewState::COMMAND;
        break;
    case MenuAction::OPEN_SAVE:
        if (!can_save()) {
            ctx_.log.warn("RoomScene: cannot save here (view state isn't COMMAND/MENU)");
            break;
        }
        // Stage the snapshot for the picker to write. The picker pops itself
        // after a save or back, returning the player to the pause menu.
        ctx_.saves.stage_pending_snap(snap());
        // Stage the most recent gameplay thumbnail (#119). The MENU state stops
        // refreshing it, so the staged frame is the last COMMAND frame — what
        // the player remembers as "the moment they paused". An empty image
        // (no capture yet, e.g. first frame after a restore) is fine; the save
        // just skips the sidecar PNG.
        if (ctx_.thumbnail.valid()) {
            ctx_.saves.stage_pending_thumbnail(ctx_.thumbnail.image());
        }
        ctx_.scenes.open_save();
        break;
    case MenuAction::OPEN_LOAD:
        // The picker handles the load itself (slot pick → stage_restore →
        // goto_scene), so nothing to stage here. If no load scene is configured
        // the helper is a no-op; the button is disabled in that state anyway.
        ctx_.scenes.open_load();
        break;
    case MenuAction::PUSH_OVERLAY:
        push_pause_overlay({button.overlay_scene, button.label, button.order}, ctx_.scenes);
        break;
    case MenuAction::OPEN_SETTINGS:
        ctx_.scenes.open_settings();
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
    sf::Text title(pac::core::utf8(ctx_.strings.ui_label("pause")), *font_, 36);
    title.setFillColor(sf::Color(255, 240, 180));
    const sf::FloatRect tb = title.getLocalBounds();
    title.setPosition((static_cast<float>(vres.x) - tb.width) / 2.0f - tb.left,
                      static_cast<float>(vres.y) * 0.18f);
    target.draw(title);

    // Single column of five buttons; the actual save/load picker is the
    // SaveLoadScene (issue #108) opened by OPEN_SAVE / OPEN_LOAD.
    for (const MenuButton& bt : menu_buttons()) {
        const bool hot = bt.enabled && bt.rect.contains(hover_vp_);
        sf::RectangleShape box(sf::Vector2f(bt.rect.width, bt.rect.height));
        box.setPosition(bt.rect.left, bt.rect.top);
        if (!bt.enabled) {
            box.setFillColor(sf::Color(24, 26, 36));
            box.setOutlineColor(sf::Color(50, 54, 70));
        } else {
            box.setFillColor(hot ? sf::Color(70, 90, 140) : sf::Color(34, 38, 54));
            box.setOutlineColor(sf::Color(90, 100, 130));
        }
        box.setOutlineThickness(1.5f);
        target.draw(box);

        sf::Text txt(pac::core::utf8(bt.label), *font_, 20);
        txt.setFillColor(!bt.enabled ? sf::Color(120, 128, 145)
                                     : (hot ? sf::Color::White : sf::Color(220, 224, 235)));
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
    constexpr std::string_view case_term_prefix = "__case_term.";
    for (auto it = s.global_state.begin(); it != s.global_state.end();) {
        if (it->first.starts_with(case_term_prefix)) {
            s.case_terms.push_back(it->first.substr(case_term_prefix.size()));
            it = s.global_state.erase(it);
        } else {
            ++it;
        }
    }
    s.room_state = room_state_;
    // Per-room runtime flags: snapshot persisted history across rooms, then
    // overlay the live values from the currently loaded room (which may
    // differ from the persisted snapshot if Lua mutated them this session).
    s.region_states = region_state_persist_;
    s.hotspot_enabled = hotspot_enabled_persist_;
    s.object_visible = object_visible_persist_;
    s.layer_visible = layer_visible_persist_;
    s.obstacle_enabled = obstacle_enabled_persist_;
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
        auto& layer_map = s.layer_visible[current_room_id_];
        for (const BackgroundLayer& layer : room_->data().layers) {
            if (!layer.id.empty()) {
                layer_map[layer.id] = room_->layer_visible(layer.id);
            }
        }
        auto& obstacle_map = s.obstacle_enabled[current_room_id_];
        for (const Obstacle& o : room_->data().obstacles) {
            if (!o.id.empty()) {
                obstacle_map[o.id] = room_->obstacle_enabled(o.id);
            }
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
    for (const std::string& id : state.case_terms) {
        if (!id.empty())
            ctx_.state.set("__case_term." + id, true);
    }
    room_state_ = state.room_state;
    region_state_persist_ = state.region_states;
    hotspot_enabled_persist_ = state.hotspot_enabled;
    object_visible_persist_ = state.object_visible;
    layer_visible_persist_ = state.layer_visible;
    obstacle_enabled_persist_ = state.obstacle_enabled;
    inventory_.replace_all(state.inventory);

    // Kill transient runtime — none of it is part of GameState.
    dialog_.reset();
    if (dialog_scope_ != 0) {
        ctx_.scripting.cancel_scope(dialog_scope_);
        dialog_scope_ = 0;
        run_task_ = 0;
    }
    view_state_ = ViewState::COMMAND;
    // A handler task awaited from the previous session is dead post-restore (its
    // room scope is gone); drop the id so update() doesn't try to drain it. Any
    // deferred on_room_resume beat (M9 #186) is transient too — restored saves
    // never carry one (it fires within a frame of registration) — so clear it.
    awaiting_handler_task_.reset();
    cutscene_skip_ = {};
    if (lua_) {
        lua_->pending_resume.clear();
    }
    command_controller_.reset();
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
