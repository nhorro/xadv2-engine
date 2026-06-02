#include "engine/core/lua_api.hpp"

#include "engine/core/audio.hpp"
#include "engine/core/diagnostics.hpp"
#include "engine/core/engine_context.hpp"
#include "engine/core/resource_cache.hpp"
#include "engine/core/resource_source.hpp"
#include "engine/core/scripting.hpp"
#include "engine/core/state_store.hpp"

#include <sol/sol.hpp>

#include <exception>
#include <string>

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
}

} // namespace pac::core
