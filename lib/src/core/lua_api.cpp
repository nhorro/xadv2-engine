#include "engine/core/lua_api.hpp"

#include "engine/core/audio.hpp"
#include "engine/core/diagnostics.hpp"
#include "engine/core/engine_context.hpp"
#include "engine/core/facts.hpp"
#include "engine/core/information_overlay.hpp"
#include "engine/core/localization.hpp"
#include "engine/core/manifest.hpp"
#include "engine/core/resource_cache.hpp"
#include "engine/core/resource_source.hpp"
#include "engine/core/scripting.hpp"
#include "engine/core/state_store.hpp"
#include "engine/core/text_id.hpp"
#include "engine/geom/geometry.hpp"

#include <sol/sol.hpp>

#include <exception>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace pac::core {

void bind_core_api(EngineContext& ctx, const std::string& facts_path) {
    sol::state& lua = ctx.scripting.lua();

    // --- resources ---
    lua.set_function("resource_path", [&ctx](const std::string& rel) -> sol::object {
        sol::state& L = ctx.scripting.lua();
        if (!is_valid_logical_path(rel)) {
            ctx.log.error("resource_path: invalid logical path '" + rel + "'");
            return sol::make_object(L, sol::lua_nil);
        }
        return sol::make_object(L, rel);
    });

    // --- script composition ---
    // include(logical): load and run a Lua resource in the shared state, returning
    // its value (typically a table). Lets a script be split across files — the
    // sandbox has no `require`, and `dofile` would bypass the resource layer (and
    // the release .pak). Not cached: each call re-runs the file. Errors are logged
    // and yield nil so a typo is visible without aborting the caller.
    lua.set_function("include", [&ctx](const std::string& logical) -> sol::object {
        sol::state& L = ctx.scripting.lua();
        if (!is_valid_logical_path(logical)) {
            ctx.log.error("include: invalid logical path '" + logical + "'");
            return sol::make_object(L, sol::lua_nil);
        }
        std::string code;
        try {
            code = ctx.resources.read_text(logical);
        } catch (const std::exception& e) {
            ctx.log.error("include('" + logical + "'): " + e.what());
            return sol::make_object(L, sol::lua_nil);
        }
        sol::load_result chunk = L.load(code, "@" + logical);
        if (!chunk.valid()) {
            const sol::error err = chunk;
            ctx.log.error("include('" + logical + "') load error: " + err.what());
            return sol::make_object(L, sol::lua_nil);
        }
        const sol::protected_function_result r = sol::protected_function(chunk)();
        if (!r.valid()) {
            const sol::error err = r;
            ctx.log.error("include('" + logical + "') error: " + err.what());
            return sol::make_object(L, sol::lua_nil);
        }
        return r.get<sol::object>();
    });

    // --- audio ---
    lua.set_function(
        "play_music",
        [&ctx](const std::string& path, sol::optional<bool> loop, sol::optional<float> gain) {
            ctx.audio.music.play(path, loop.value_or(true), gain.value_or(1.0f));
        });
    lua.set_function("crossfade_music",
                     [&ctx](const std::string& path,
                            sol::optional<float> seconds,
                            sol::optional<bool> preserve_offset,
                            sol::optional<bool> loop,
                            sol::optional<float> gain) {
                         return ctx.audio.music.crossfade(path,
                                                          seconds.value_or(2.5f),
                                                          preserve_offset.value_or(false),
                                                          loop.value_or(true),
                                                          gain.value_or(1.0f));
                     });
    lua.set_function("stop_music", [&ctx](sol::optional<float> seconds) {
        if (seconds.value_or(0.0f) > 0.0f) {
            ctx.audio.music.fade_out(*seconds);
        } else {
            ctx.audio.music.stop();
        }
    });
    lua.set_function(
        "play_sound",
        [&ctx](const std::string& path, sol::optional<float> volume, sol::optional<float> pan) {
            ctx.audio.sfx.play(path, volume.value_or(1.0f), pan.value_or(0.0f));
        });
    lua.set_function("stop_sound", [&ctx](const std::string& path, sol::optional<float> seconds) {
        ctx.audio.sfx.stop(path, seconds.value_or(0.0f));
    });
    lua.set_function("stop_sounds", [&ctx](sol::optional<float> seconds) {
        ctx.audio.sfx.stop_all(seconds.value_or(0.0f));
    });
    lua.set_function(
        "set_ambience",
        [&ctx](const std::string& path, sol::optional<float> volume, sol::optional<float> seconds) {
            ctx.audio.ambience.set_base(path, volume.value_or(1.0f), seconds.value_or(2.5f));
        });
    lua.set_function("set_ambience_volume", [&ctx](float volume, sol::optional<float> seconds) {
        ctx.audio.ambience.set_base_volume(volume, seconds.value_or(1.0f));
    });
    lua.set_function("stop_ambience", [&ctx](sol::optional<float> seconds) {
        ctx.audio.ambience.stop(seconds.value_or(2.5f));
    });
    lua.set_function("set_ambience_layer_enabled", [&ctx](const std::string& id, bool enabled) {
        const bool found = ctx.audio.ambience.set_random_layer_enabled(id, enabled);
        if (!found) {
            ctx.log.warn("set_ambience_layer_enabled: unknown layer '" + id + "'");
        }
        return found;
    });
    lua.set_function("set_ambience_layer_volume", [&ctx](const std::string& id, float volume) {
        const bool found = ctx.audio.ambience.set_random_layer_volume(id, volume);
        if (!found) {
            ctx.log.warn("set_ambience_layer_volume: unknown layer '" + id + "'");
        }
        return found;
    });

    // --- localization (any genre; not P&C-only) ---
    lua.set_function("tr", [&ctx](const std::string& id, const std::string& source) {
        return ctx.localization.text(id, source);
    });
    lua.set_function("set_language", [&ctx](const std::string& id) {
        return ctx.localization.set_language(id);
    });
    lua.set_function("language", [&ctx]() { return ctx.localization.active(); });
    lua.set_function("_translate_text",
                     [&ctx](const std::string& source, sol::optional<std::string> explicit_id) {
                         const std::string id =
                             text_id(explicit_id.value_or(std::string()), source);
                         return ctx.localization.text(id, source);
                     });

    // --- geometry (headless primitives; pathfinding stays kit-policy) ---
    lua.set_function("distance", [](float x1, float y1, float x2, float y2) {
        return pac::geom::distance({x1, y1}, {x2, y2});
    });
    lua.set_function("point_in_polygon", [](float x, float y, const sol::table& points) {
        pac::geom::Polygon poly;
        const std::size_t n = points.size();
        poly.reserve(n);
        for (std::size_t i = 1; i <= n; ++i) {
            const sol::table p = points[i];
            poly.push_back({p.get<float>("x"), p.get<float>("y")});
        }
        return pac::geom::point_in_polygon({x, y}, poly);
    });

    // --- reusable cross-scene information / target guidance ----------------
    if (ctx.information) {
        lua.set_function("_show_information", [&ctx](const sol::table& options) {
            InformationPage page;
            page.text = options.get_or("text", std::string());
            page.image = options.get_or("image", std::string());
            page.dismiss_text = options.get_or("dismiss_text", std::string());
            const sol::object indicator_object = options["indicator"];
            if (indicator_object.is<sol::table>()) {
                const sol::table indicator = indicator_object.as<sol::table>();
                InformationIndicator target;
                target.position = {indicator.get_or("x", 0.0f), indicator.get_or("y", 0.0f)};
                const std::string direction = indicator.get_or("direction", std::string("down"));
                if (direction == "up") {
                    target.direction = IndicatorDirection::Up;
                } else if (direction == "left") {
                    target.direction = IndicatorDirection::Left;
                } else if (direction == "right") {
                    target.direction = IndicatorDirection::Right;
                } else {
                    target.direction = IndicatorDirection::Down;
                }
                page.indicator = target;
            }
            return ctx.information->show(std::move(page), ctx.scripting.current_scope());
        });
        lua.set_function("hide_information", [&ctx]() { ctx.information->dismiss(); });
        lua.set_function("information_visible",
                         [&ctx]() { return ctx.information->modal_active(); });
        lua.set_function("show_indicator", [&ctx](const sol::table& options) {
            InformationIndicator indicator;
            indicator.position = {options.get_or("x", 0.0f), options.get_or("y", 0.0f)};
            const std::string direction = options.get_or("direction", std::string("down"));
            if (direction == "up") {
                indicator.direction = IndicatorDirection::Up;
            } else if (direction == "left") {
                indicator.direction = IndicatorDirection::Left;
            } else if (direction == "right") {
                indicator.direction = IndicatorDirection::Right;
            }
            ctx.information->show_indicator(indicator);
        });
        lua.set_function("hide_indicator", [&ctx]() { ctx.information->hide_indicator(); });
        ctx.scripting.run_string(R"LUA(
function show_information(options)
  local dismissed = _show_information(options or {})
  if dismissed ~= nil then
    wait_event(dismissed)
  end
end
)LUA",
                                 "=information-overlay-api");
    }

    // --- global state (scalars only) ---
    lua.set_function("get_state", [&ctx](const std::string& key) -> sol::object {
        sol::state& L = ctx.scripting.lua();
        const auto value = ctx.state.get(key);
        if (!value) {
            return sol::make_object(L, sol::lua_nil);
        }
        return std::visit([&L](const auto& v) { return sol::make_object(L, v); }, *value);
    });
    lua.set_function("set_state", [&ctx](const std::string& key, sol::object value) {
        if (value.is<bool>()) {
            ctx.state.set(key, value.as<bool>());
        } else if (value.is<double>()) {
            ctx.state.set(key, value.as<double>());
        } else if (value.is<std::string>()) {
            ctx.state.set(key, value.as<std::string>());
        } else {
            ctx.log.error("set_state('" + key + "'): only bool, number, or string is allowed");
        }
    });

    // --- discovered case terms ---------------------------------------------
    // The reserved state keys are the live representation. RoomScene folds
    // them into GameState::case_terms when saving and restores them on load.
    lua.set_function("add_case_term", [&ctx](const std::string& id) {
        if (id.empty()) {
            ctx.log.error("add_case_term: term id must not be empty");
            return false;
        }
        const std::string key = "__case_term." + id;
        const bool added = !ctx.state.has(key);
        ctx.state.set(key, true);
        if (added) {
            sol::protected_function hook = ctx.scripting.lua()["__on_content_added"];
            if (hook.valid()) {
                sol::protected_function_result result = hook("term", id);
                if (!result.valid()) {
                    const sol::error error = result;
                    ctx.log.error(std::string("__on_content_added: ") + error.what());
                }
            }
        }
        return added;
    });
    lua.set_function("has_case_term", [&ctx](const std::string& id) {
        return !id.empty() && ctx.state.has("__case_term." + id);
    });
    lua.set_function("remove_case_term", [&ctx](const std::string& id) {
        if (id.empty()) {
            ctx.log.error("remove_case_term: term id must not be empty");
            return false;
        }
        return ctx.state.erase("__case_term." + id);
    });
    lua.set_function("clear_case_terms",
                     [&ctx]() { return ctx.state.erase_prefix("__case_term."); });

    // --- declared facts (#188): the `facts.<ns>.<name>` proxy over state ---
    // The configured facts resource is optional: a missing file leaves an empty registry (guard
    // off); a malformed one is a loud authoring error but still degrades to the
    // guard-off proxy so content can run. Bound last so get_state/set_state exist.
    bind_facts_resource(ctx, facts_path);
}

void bind_facts_resource(EngineContext& ctx, const std::string& facts_path) {
    FactsRegistry facts;
    std::string facts_yaml;
    bool have_facts = true;
    try {
        facts_yaml = ctx.resources.read_text(facts_path);
    } catch (const std::exception&) {
        have_facts = false; // optional resource
    }
    if (have_facts) {
        try {
            facts = FactsRegistry::parse(facts_yaml);
        } catch (const std::exception& e) {
            ctx.log.error(facts_path + ": " + e.what());
        }
    }
    // The typo guard is a development aid (per the "fail loudly in development
    // builds" invariant): loud in a Debug build, and opt-in in a Release build via
    // `development.show_state` for authors who run an optimized build. A clean
    // release stays silent — the underlying state access works regardless.
#ifdef NDEBUG
    const bool dev_facts = ctx.dev.show_state;
#else
    const bool dev_facts = true;
#endif
    bind_facts(ctx.scripting, facts, dev_facts, [&ctx](const std::string& msg) {
        ctx.log.warn(msg);
    });
}

void bind_facts(Scripting& scripting,
                const FactsRegistry& reg,
                bool dev_warn,
                std::function<void(const std::string&)> warn) {
    sol::state& lua = scripting.lua();

    // Hand the declared sets + the dev flag + the warn sink to the Lua prelude as
    // temporaries; the prelude captures them as upvalues and clears the globals.
    sol::table ns_tab = lua.create_table();
    for (const std::string& ns : reg.namespaces()) {
        ns_tab[ns] = true;
    }
    sol::table key_tab = lua.create_table();
    for (const std::string& key : reg.keys()) {
        key_tab[key] = true;
    }
    lua["__facts_ns"] = ns_tab;
    lua["__facts_key"] = key_tab;
    lua["__facts_dev"] = dev_warn;
    lua.set_function("__fact_warn", std::move(warn));

    // `facts.<ns>.<name>` reads/writes route through get_state/set_state (so a
    // fact persists and interoperates with the dotted key). Each namespace is a
    // metatable proxy; an undeclared key/namespace warns when `dev` and a registry
    // was loaded (`guard`), but the read still returns false / the write still
    // happens, so release never blocks on a missing declaration.
    scripting.run_string(R"LUA(
do
  local ns_set  = __facts_ns
  local key_set = __facts_key
  local dev     = __facts_dev and true or false
  local warn    = __fact_warn
  local guard   = next(key_set) ~= nil   -- a facts.yaml was actually loaded
  __facts_ns, __facts_key, __facts_dev, __fact_warn = nil, nil, nil, nil

  local cache = {}
  local function namespace_proxy(ns)
    local p = cache[ns]
    if p then return p end
    p = setmetatable({}, {
      __index = function(_, name)
        local key = ns .. "." .. name
        if dev and guard and not key_set[key] then
          warn("fact '" .. key .. "' is not declared in facts.yaml")
        end
        local v = get_state(key)
        if v == nil then return false end
        return v
      end,
      __newindex = function(_, name, value)
        local key = ns .. "." .. name
        if dev and guard and not key_set[key] then
          warn("fact '" .. key .. "' is not declared in facts.yaml")
        end
        set_state(key, value)
      end,
    })
    cache[ns] = p
    return p
  end

  facts = setmetatable({}, {
    __index = function(_, ns)
      if dev and guard and not ns_set[ns] then
        warn("fact namespace 'facts." .. ns .. "' is not declared in facts.yaml")
      end
      return namespace_proxy(ns)
    end,
    __newindex = function(_, ns)
      error("facts: write facts." .. tostring(ns) .. ".<name> = value, not facts." ..
            tostring(ns), 2)
    end,
  })
end
)LUA",
                         "=facts_sugar");
}

} // namespace pac::core
