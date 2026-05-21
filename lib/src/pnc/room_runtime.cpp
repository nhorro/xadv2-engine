#include "engine/pnc/room_runtime.hpp"

#include "engine/core/diagnostics.hpp"
#include "engine/core/resource_cache.hpp"
#include "engine/core/scripting.hpp"

#include <sol/sol.hpp>

#include <utility>

namespace pac::pnc {

struct RoomRuntime::Behavior {
    sol::table table;
    sol::state* state = nullptr;
    pac::core::Diagnostics* log = nullptr;
    bool valid = false;
};

RoomRuntime::RoomRuntime(RoomData data) : data_(std::move(data)) {}
RoomRuntime::~RoomRuntime() = default;
RoomRuntime::RoomRuntime(RoomRuntime&&) noexcept = default;
RoomRuntime& RoomRuntime::operator=(RoomRuntime&&) noexcept = default;

const RoomHotspot* RoomRuntime::hotspot_at(geom::Point world) const {
    for (const auto& [id, hs] : data_.hotspots) {
        if (hs.enabled && !hs.area.empty() && geom::point_in_polygon(world, hs.area)) {
            return &hs;
        }
    }
    return nullptr;
}

void RoomRuntime::load_behavior(pac::core::Scripting& scripting,
                                pac::core::ResourceCache& resources,
                                const std::string& logical,
                                pac::core::Diagnostics& log) {
    behavior_ = std::make_unique<Behavior>();
    behavior_->state = &scripting.lua();
    behavior_->log = &log;

    std::string code;
    try {
        code = resources.read_text(logical);
    } catch (const std::exception&) {
        log.warn("room: no behavior script '" + logical + "'");
        return;
    }

    sol::state& lua = scripting.lua();
    sol::load_result chunk = lua.load(code, "@" + logical);
    if (!chunk.valid()) {
        const sol::error err = chunk;
        log.error(std::string("room behavior load error: ") + err.what());
        return;
    }
    const sol::protected_function_result r = sol::protected_function(chunk)();
    if (!r.valid()) {
        const sol::error err = r;
        log.error(std::string("room behavior error: ") + err.what());
        return;
    }
    sol::optional<sol::table> table = r;
    if (!table) {
        log.error("room behavior '" + logical + "' did not return a table");
        return;
    }
    behavior_->table = *table;
    behavior_->valid = true;
}

void RoomRuntime::call_hook(const std::string& name) {
    if (!behavior_ || !behavior_->valid) {
        return;
    }
    sol::optional<sol::protected_function> hook = behavior_->table[name];
    if (!hook) {
        return;
    }
    const sol::protected_function_result r = (*hook)();
    if (!r.valid()) {
        const sol::error err = r;
        behavior_->log->error(std::string("room hook '" + name + "' error: ") + err.what());
    }
}

std::optional<std::string> RoomRuntime::call_hotspot(const std::string& hotspot_id,
                                                     const std::string& verb) {
    if (!behavior_ || !behavior_->valid) {
        return std::nullopt;
    }
    sol::optional<sol::table> hotspots = behavior_->table["hotspots"];
    if (!hotspots) {
        return std::nullopt;
    }
    sol::optional<sol::table> hs = (*hotspots)[hotspot_id];
    if (!hs) {
        return std::nullopt;
    }
    sol::optional<sol::protected_function> fn = (*hs)[verb];
    if (!fn) {
        return std::nullopt;
    }
    const sol::protected_function_result r = (*fn)();
    if (!r.valid()) {
        const sol::error err = r;
        behavior_->log->error(std::string("hotspot '" + hotspot_id + "." + verb + "' error: ") +
                              err.what());
        return std::nullopt;
    }
    sol::optional<std::string> caption = r;
    if (caption) {
        return *caption;
    }
    return std::nullopt;
}

} // namespace pac::pnc
