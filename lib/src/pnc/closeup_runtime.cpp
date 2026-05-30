#include "engine/pnc/closeup_runtime.hpp"

#include "engine/core/diagnostics.hpp"
#include "engine/core/scripting.hpp"

#include <sol/sol.hpp>

namespace pac::pnc {

struct CloseUpRuntime::Behavior {
    sol::table table;
    pac::core::Diagnostics* log = nullptr;
    bool valid = false;
};

CloseUpRuntime::CloseUpRuntime() = default;
CloseUpRuntime::~CloseUpRuntime() = default;
CloseUpRuntime::CloseUpRuntime(CloseUpRuntime&&) noexcept = default;
CloseUpRuntime& CloseUpRuntime::operator=(CloseUpRuntime&&) noexcept = default;

namespace {
void call_hook(const sol::table& table, pac::core::Diagnostics& log, const char* name) {
    sol::optional<sol::protected_function> hook = table[name];
    if (!hook) {
        return;
    }
    const sol::protected_function_result r = (*hook)();
    if (!r.valid()) {
        const sol::error err = r;
        log.error(std::string("close-up hook '") + name + "' error: " + err.what());
    }
}
} // namespace

bool CloseUpRuntime::load(pac::core::Scripting& scripting,
                          const std::string& lua_text,
                          const std::string& logical,
                          pac::core::Diagnostics& log) {
    behavior_ = std::make_unique<Behavior>();
    behavior_->log = &log;

    sol::state& lua = scripting.lua();
    sol::load_result chunk = lua.load(lua_text, "@" + logical);
    if (!chunk.valid()) {
        const sol::error err = chunk;
        log.error(std::string("close-up behavior load error: ") + err.what());
        return false;
    }
    const sol::protected_function_result r = sol::protected_function(chunk)();
    if (!r.valid()) {
        const sol::error err = r;
        log.error(std::string("close-up behavior error: ") + err.what());
        return false;
    }
    sol::optional<sol::table> table = r;
    if (!table) {
        log.error("close-up behavior '" + logical + "' did not return a table");
        return false;
    }
    // Shape check: `hotspots`, if present, must be a table.
    if (sol::object h = (*table)["hotspots"]; h.valid() && !h.is<sol::table>()) {
        log.error("close-up behavior '" + logical + "': `hotspots` must be a table");
        return false;
    }
    behavior_->table = *table;
    behavior_->valid = true;
    return true;
}

bool CloseUpRuntime::valid() const {
    return behavior_ && behavior_->valid;
}

bool CloseUpRuntime::has_hotspot(const std::string& id) const {
    if (!valid()) {
        return false;
    }
    sol::optional<sol::table> hotspots = behavior_->table["hotspots"];
    if (!hotspots) {
        return false;
    }
    sol::optional<sol::protected_function> fn = (*hotspots)[id];
    return fn.has_value();
}

void CloseUpRuntime::run_on_enter() {
    if (valid()) {
        call_hook(behavior_->table, *behavior_->log, "on_enter");
    }
}

void CloseUpRuntime::run_on_exit() {
    if (valid()) {
        call_hook(behavior_->table, *behavior_->log, "on_exit");
    }
}

pac::core::TaskId CloseUpRuntime::spawn_hotspot(pac::core::Scripting& scripting,
                                                pac::core::ScopeId scope,
                                                const std::string& id) {
    if (!valid()) {
        return 0;
    }
    sol::optional<sol::table> hotspots = behavior_->table["hotspots"];
    if (!hotspots) {
        return 0;
    }
    sol::optional<sol::function> fn = (*hotspots)[id];
    if (!fn) {
        return 0;
    }
    // Run the handler as a coroutine task in the close-up scope, reusing the Lua
    // `spawn` global (which places the task in the current scope) so blocking
    // talk/wait inside the handler work.
    scripting.set_current_scope(scope);
    sol::protected_function spawn_fn = scripting.lua()["spawn"];
    sol::protected_function_result r = spawn_fn(*fn);
    if (!r.valid()) {
        const sol::error err = r;
        behavior_->log->error(std::string("close-up hotspot '" + id + "' spawn error: ") +
                              err.what());
        return 0;
    }
    return r.get<pac::core::TaskId>();
}

} // namespace pac::pnc
