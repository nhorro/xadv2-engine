#include "engine/core/lua_api.hpp"

#include "engine/core/audio.hpp"
#include "engine/core/diagnostics.hpp"
#include "engine/core/engine_context.hpp"
#include "engine/core/facts.hpp"
#include "engine/core/manifest.hpp"
#include "engine/core/resource_cache.hpp"
#include "engine/core/resource_source.hpp"
#include "engine/core/scripting.hpp"
#include "engine/core/state_store.hpp"

#include <sol/sol.hpp>

#include <exception>
#include <functional>
#include <string>
#include <utility>

namespace pac::core {

void bind_core_api(EngineContext& ctx) {
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
    lua.set_function("play_music", [&ctx](const std::string& path, sol::optional<bool> loop) {
        ctx.audio.music.play(path, loop.value_or(true));
    });
    lua.set_function("stop_music", [&ctx]() { ctx.audio.music.stop(); });
    lua.set_function(
        "play_sound",
        [&ctx](const std::string& path, sol::optional<float> volume, sol::optional<float> pan) {
            ctx.audio.sfx.play(path, volume.value_or(1.0f), pan.value_or(0.0f));
        });
    lua.set_function("stop_sounds", [&ctx]() { ctx.audio.sfx.stop_all(); });

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

    // --- declared facts (#188): the `facts.<ns>.<name>` proxy over state ---
    // `facts.yaml` is optional: a missing file leaves an empty registry (guard
    // off); a malformed one is a loud authoring error but still degrades to the
    // guard-off proxy so content can run. Bound last so get_state/set_state exist.
    FactsRegistry facts;
    std::string facts_yaml;
    bool have_facts = true;
    try {
        facts_yaml = ctx.resources.read_text("facts.yaml");
    } catch (const std::exception&) {
        have_facts = false; // optional resource
    }
    if (have_facts) {
        try {
            facts = FactsRegistry::parse(facts_yaml);
        } catch (const std::exception& e) {
            ctx.log.error(std::string("facts.yaml: ") + e.what());
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
