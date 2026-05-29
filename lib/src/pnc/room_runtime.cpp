#include "engine/pnc/room_runtime.hpp"

#include "engine/core/diagnostics.hpp"
#include "engine/core/resource_cache.hpp"
#include "engine/core/scripting.hpp"

#include <sol/sol.hpp>

#include <cmath>
#include <utility>

namespace pac::pnc {

struct RoomRuntime::Behavior {
    sol::table table;
    sol::state* state = nullptr;
    pac::core::Diagnostics* log = nullptr;
    bool valid = false;
};

RoomRuntime::RoomRuntime(RoomData data) : data_(std::move(data)) {
    seed_runtime_state();
}
RoomRuntime::~RoomRuntime() = default;
RoomRuntime::RoomRuntime(RoomRuntime&&) noexcept = default;
RoomRuntime& RoomRuntime::operator=(RoomRuntime&&) noexcept = default;

void RoomRuntime::seed_runtime_state() {
    for (const auto& [id, region] : data_.regions) {
        region_states_[id] = region.initial;
    }
    for (const auto& [id, object] : data_.objects) {
        object_visible_[id] = object.visible;
        ObjectRuntime rt;
        rt.position = object.position;
        rt.scale = object.scale;
        object_rt_[id] = rt;
    }
    for (const BackgroundLayer& layer : data_.layers) {
        if (!layer.id.empty()) {
            layer_visible_[layer.id] = layer.visible;
        }
    }
    for (const auto& [id, hs] : data_.hotspots) {
        hotspot_enabled_[id] = hs.enabled;
    }
}

namespace {
// Split a hotspot bind ("object:<id>" / "region:<id>") into {kind, ref}.
std::pair<std::string, std::string> split_bind(const std::string& bind) {
    const auto pos = bind.find(':');
    if (pos == std::string::npos) {
        return {bind, std::string()};
    }
    return {bind.substr(0, pos), bind.substr(pos + 1)};
}
} // namespace

const RoomHotspot* RoomRuntime::hotspot_at(geom::Point world) const {
    return hotspot_at(world, {});
}

const RoomHotspot* RoomRuntime::hotspot_at(
    geom::Point world,
    const std::function<std::optional<sf::FloatRect>(const std::string&)>& object_bounds) const {
    for (const auto& [id, hs] : data_.hotspots) {
        if (!hotspot_enabled(id)) {
            continue;
        }
        // (1) explicit area polygon
        if (!hs.area.empty() && geom::point_in_polygon(world, hs.area)) {
            return &hs;
        }
        if (hs.bind.empty()) {
            continue;
        }
        const auto [kind, ref] = split_bind(hs.bind);
        if (kind == "object" && object_bounds) {
            // (2) object frame bounds (render-side; absent in the headless overload)
            if (const auto rect = object_bounds(ref); rect && rect->contains(world)) {
                return &hs;
            }
        } else if (kind == "region") {
            // (3) the bound region's area polygon — constant, independent of the
            // current state image (design 04 §Hotspot hit-test rule 3).
            const auto it = data_.regions.find(ref);
            if (it != data_.regions.end() && geom::point_in_polygon(world, it->second.area)) {
                return &hs;
            }
        } else if (kind == "npc") {
            // (4) the bound NPC's *current* world bounds (it moves). Resolved from
            // the live avatar, so the hotspot follows the NPC and is inactive when
            // the NPC is absent (not spawned / despawned) (#141).
            const auto it = npcs_.find(ref);
            if (it != npcs_.end() && it->second.bounds().contains(world)) {
                return &hs;
            }
        }
    }
    return nullptr;
}

const Zone* RoomRuntime::zone_at(geom::Point world) const {
    for (const Zone& zone : data_.zones) {
        if (!zone.polygon.empty() && geom::point_in_polygon(world, zone.polygon)) {
            return &zone;
        }
    }
    return nullptr;
}

void RoomRuntime::set_region_state(const std::string& region_id, const std::string& state) {
    region_states_[region_id] = state;
}

std::string RoomRuntime::region_state(const std::string& region_id) const {
    const auto it = region_states_.find(region_id);
    return it != region_states_.end() ? it->second : std::string();
}

void RoomRuntime::set_object_visible(const std::string& object_id, bool visible) {
    object_visible_[object_id] = visible;
}

void RoomRuntime::set_layer_visible(const std::string& layer_id, bool visible) {
    layer_visible_[layer_id] = visible;
}

bool RoomRuntime::layer_visible(const std::string& layer_id) const {
    const auto it = layer_visible_.find(layer_id);
    return it != layer_visible_.end() ? it->second : true;
}

bool RoomRuntime::object_visible(const std::string& object_id) const {
    const auto it = object_visible_.find(object_id);
    return it != object_visible_.end() ? it->second : true;
}

geom::Point RoomRuntime::object_position(const std::string& object_id) const {
    const auto it = object_rt_.find(object_id);
    if (it != object_rt_.end()) {
        return it->second.position;
    }
    const auto d = data_.objects.find(object_id);
    return d != data_.objects.end() ? d->second.position : geom::Point{0.0f, 0.0f};
}

void RoomRuntime::set_object_position(const std::string& object_id, geom::Point p) {
    auto& rt = object_rt_[object_id];
    rt.position = p;
    rt.moving = false; // an explicit placement cancels an in-progress move
}

float RoomRuntime::object_scale(const std::string& object_id) const {
    const auto it = object_rt_.find(object_id);
    if (it != object_rt_.end()) {
        return it->second.scale;
    }
    const auto d = data_.objects.find(object_id);
    return d != data_.objects.end() ? d->second.scale : 1.0f;
}

void RoomRuntime::set_object_scale(const std::string& object_id, float scale) {
    if (scale > 0.0f) {
        object_rt_[object_id].scale = scale;
    }
}

void RoomRuntime::object_move_to(const std::string& object_id, geom::Point target, float speed) {
    auto& rt = object_rt_[object_id];
    rt.target = target;
    rt.speed = speed > 0.0f ? speed : 240.0f;
    rt.moving = true;
}

bool RoomRuntime::object_moving(const std::string& object_id) const {
    const auto it = object_rt_.find(object_id);
    return it != object_rt_.end() && it->second.moving;
}

void RoomRuntime::update_objects(float dt) {
    for (auto& [id, rt] : object_rt_) {
        if (!rt.moving) {
            continue;
        }
        const geom::Point d{rt.target.x - rt.position.x, rt.target.y - rt.position.y};
        const float dist = std::sqrt(d.x * d.x + d.y * d.y);
        const float step = rt.speed * dt;
        if (dist <= step || dist < 1e-3f) {
            rt.position = rt.target; // arrived (or close enough): snap and stop
            rt.moving = false;
        } else {
            rt.position.x += d.x / dist * step;
            rt.position.y += d.y / dist * step;
        }
    }
}

void RoomRuntime::set_hotspot_enabled(const std::string& hotspot_id, bool enabled) {
    hotspot_enabled_[hotspot_id] = enabled;
}

bool RoomRuntime::hotspot_enabled(const std::string& hotspot_id) const {
    const auto it = hotspot_enabled_.find(hotspot_id);
    return it != hotspot_enabled_.end() ? it->second : true;
}

void RoomRuntime::set_obstacle_enabled(const std::string& obstacle_id, bool enabled) {
    if (obstacle_id.empty()) {
        return;
    }
    for (Obstacle& o : data_.obstacles) {
        if (o.id == obstacle_id) {
            o.enabled = enabled;
        }
    }
}

bool RoomRuntime::obstacle_enabled(const std::string& obstacle_id) const {
    for (const Obstacle& o : data_.obstacles) {
        if (o.id == obstacle_id) {
            return o.enabled;
        }
    }
    return true;
}

void RoomRuntime::add_npc(const std::string& id, Avatar avatar) {
    npcs_.insert_or_assign(id, std::move(avatar));
}

void RoomRuntime::remove_npc(const std::string& id) {
    npcs_.erase(id);
}

Avatar* RoomRuntime::npc(const std::string& id) {
    const auto it = npcs_.find(id);
    return it != npcs_.end() ? &it->second : nullptr;
}

const Avatar* RoomRuntime::npc(const std::string& id) const {
    const auto it = npcs_.find(id);
    return it != npcs_.end() ? &it->second : nullptr;
}

std::vector<const Avatar*> RoomRuntime::npcs() const {
    std::vector<const Avatar*> out;
    out.reserve(npcs_.size());
    for (const auto& [id, avatar] : npcs_) {
        out.push_back(&avatar);
    }
    return out;
}

void RoomRuntime::update_npcs(float dt) {
    for (auto& [id, avatar] : npcs_) {
        avatar.update(dt, data_);
    }
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

void RoomRuntime::call_zone_hook(const std::string& hook, const std::string& zone_id) {
    if (!behavior_ || !behavior_->valid) {
        return;
    }
    sol::optional<sol::protected_function> fn = behavior_->table[hook];
    if (!fn) {
        return;
    }
    const sol::protected_function_result r = (*fn)(zone_id);
    if (!r.valid()) {
        const sol::error err = r;
        behavior_->log->error(std::string("zone hook '" + hook + "' error: ") + err.what());
    }
}

std::optional<std::string> RoomRuntime::call_hotspot(const std::string& hotspot_id,
                                                     const std::string& verb,
                                                     std::optional<std::string> operand) {
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
    const sol::protected_function_result r = operand ? (*fn)(*operand) : (*fn)();
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
