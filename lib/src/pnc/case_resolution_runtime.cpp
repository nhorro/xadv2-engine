#include "engine/pnc/case_resolution_runtime.hpp"

#include "engine/core/diagnostics.hpp"
#include "engine/core/scripting.hpp"

#include <sol/sol.hpp>

namespace pac::pnc {

struct CaseResolutionRuntime::Behavior {
    sol::table table;
    pac::core::Diagnostics* log = nullptr;
    bool valid = false;
};

CaseResolutionRuntime::CaseResolutionRuntime() = default;
CaseResolutionRuntime::~CaseResolutionRuntime() = default;
CaseResolutionRuntime::CaseResolutionRuntime(CaseResolutionRuntime&&) noexcept = default;
CaseResolutionRuntime& CaseResolutionRuntime::operator=(CaseResolutionRuntime&&) noexcept = default;

bool CaseResolutionRuntime::load(pac::core::Scripting& scripting,
                                 const std::string& lua_text,
                                 const std::string& logical,
                                 pac::core::Diagnostics& log) {
    behavior_ = std::make_unique<Behavior>();
    behavior_->log = &log;
    sol::load_result chunk = scripting.lua().load(lua_text, "@" + logical);
    if (!chunk.valid()) {
        const sol::error err = chunk;
        log.error(std::string("case-resolution behavior load error: ") + err.what());
        return false;
    }
    const sol::protected_function_result result = sol::protected_function(chunk)();
    if (!result.valid()) {
        const sol::error err = result;
        log.error(std::string("case-resolution behavior error: ") + err.what());
        return false;
    }
    sol::optional<sol::table> table = result;
    if (!table) {
        log.error("case-resolution behavior '" + logical + "' did not return a table");
        return false;
    }
    if (sol::object hook = (*table)["on_check"];
        hook.valid() && !hook.is<sol::protected_function>()) {
        log.error("case-resolution behavior '" + logical + "': `on_check` must be a function");
        return false;
    }
    if (sol::object hook = (*table)["on_exit"];
        hook.valid() && !hook.is<sol::protected_function>()) {
        log.error("case-resolution behavior '" + logical + "': `on_exit` must be a function");
        return false;
    }
    behavior_->table = *table;
    behavior_->valid = true;
    return true;
}

void CaseResolutionRuntime::run_on_exit(const std::string& status) {
    if (!valid())
        return;
    sol::optional<sol::protected_function> hook = behavior_->table["on_exit"];
    if (!hook)
        return;
    const sol::protected_function_result result = (*hook)(status);
    if (!result.valid()) {
        const sol::error err = result;
        behavior_->log->error(std::string("case-resolution hook 'on_exit' error: ") + err.what());
    }
}

bool CaseResolutionRuntime::valid() const {
    return behavior_ && behavior_->valid;
}

void CaseResolutionRuntime::run_on_check(std::size_t invalid_slots) {
    if (!valid())
        return;
    sol::optional<sol::protected_function> hook = behavior_->table["on_check"];
    if (!hook)
        return;
    const sol::protected_function_result result = (*hook)(invalid_slots);
    if (!result.valid()) {
        const sol::error err = result;
        behavior_->log->error(std::string("case-resolution hook 'on_check' error: ") + err.what());
    }
}

} // namespace pac::pnc
