#include "engine/pnc/room_runtime.hpp"

#include "engine/core/diagnostics.hpp"
#include "engine/core/resource_cache.hpp"
#include "engine/core/scripting.hpp"
#include "engine/core/scripting_sol.hpp"

#include <sol/sol.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

namespace pac::pnc {

struct RoomRuntime::Behavior {
    sol::table table;
    sol::state* state = nullptr;
    pac::core::Scripting* scripting = nullptr; // for auto-spawn (M9 #183)
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
        rt.rotation = object.rotation;
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
    if (data_.dynamic_lighting) {
        for (const RoomLight& light : data_.dynamic_lighting->lights) {
            light_enabled_[light.id] = light.enabled;
            light_rt_[light.id] = {light.intensity, light.intensity, light.intensity, 0.0f, 0.0f};
        }
        for (const LightOccluder& occluder : data_.dynamic_lighting->occluders) {
            light_occluder_enabled_[occluder.id] = occluder.enabled;
        }
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

float RoomRuntime::object_rotation(const std::string& object_id) const {
    const auto it = object_rt_.find(object_id);
    if (it != object_rt_.end()) {
        return it->second.rotation;
    }
    const auto d = data_.objects.find(object_id);
    return d != data_.objects.end() ? d->second.rotation : 0.0f;
}

void RoomRuntime::set_object_rotation(const std::string& object_id, float degrees) {
    if (std::isfinite(degrees)) {
        object_rt_[object_id].rotation = degrees;
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
    // Advance animated objects and sync each sprite's transform to its runtime
    // pose (position = pivot, like an avatar). A finished one-shot clears `acting`.
    for (auto& [id, sprite] : object_sprites_) {
        ObjectRuntime& rt = object_rt_[id];
        sprite.setPosition(rt.position.x, rt.position.y);
        sprite.setScale(rt.scale, rt.scale);
        sprite.setRotation(rt.rotation);
        sprite.update(dt);
        if (!rt.acting.empty() && sprite.finished()) {
            rt.acting.clear();
        }
    }
}

void RoomRuntime::set_object_sprite(const std::string& object_id, gfx::VisualSprite sprite) {
    object_sprites_.insert_or_assign(object_id, std::move(sprite));
}

bool RoomRuntime::object_animated(const std::string& object_id) const {
    return object_sprites_.count(object_id) > 0;
}

const gfx::VisualSprite* RoomRuntime::object_sprite(const std::string& object_id) const {
    const auto it = object_sprites_.find(object_id);
    return it != object_sprites_.end() ? &it->second : nullptr;
}

bool RoomRuntime::object_play(const std::string& object_id,
                              const std::string& sequence,
                              bool track_until_end) {
    const auto it = object_sprites_.find(object_id);
    if (it == object_sprites_.end() || !it->second.has(sequence)) {
        return false; // not animated, or unknown sequence
    }
    it->second.play(sequence, true);
    object_rt_[object_id].acting = track_until_end ? sequence : std::string();
    return true;
}

bool RoomRuntime::object_acting(const std::string& object_id) const {
    const auto it = object_rt_.find(object_id);
    return it != object_rt_.end() && !it->second.acting.empty();
}

void RoomRuntime::set_hotspot_enabled(const std::string& hotspot_id, bool enabled) {
    hotspot_enabled_[hotspot_id] = enabled;
}

bool RoomRuntime::hotspot_enabled(const std::string& hotspot_id) const {
    const auto it = hotspot_enabled_.find(hotspot_id);
    return it != hotspot_enabled_.end() ? it->second : true;
}

bool RoomRuntime::has_light(const std::string& light_id) const {
    return light_enabled_.count(light_id) > 0;
}

void RoomRuntime::set_light_enabled(const std::string& light_id, bool enabled) {
    const auto it = light_enabled_.find(light_id);
    if (it != light_enabled_.end()) {
        it->second = enabled;
    }
}

bool RoomRuntime::light_enabled(const std::string& light_id) const {
    const auto it = light_enabled_.find(light_id);
    return it != light_enabled_.end() && it->second;
}

void RoomRuntime::set_light_intensity(const std::string& light_id,
                                      float intensity,
                                      float transition_seconds) {
    const auto it = light_rt_.find(light_id);
    if (it == light_rt_.end() || !std::isfinite(intensity) ||
        !std::isfinite(transition_seconds)) {
        return;
    }
    LightRuntime& rt = it->second;
    const float target = std::clamp(intensity, 0.0f, 4.0f);
    const float duration = std::max(transition_seconds, 0.0f);
    if (duration <= 0.0f || target == rt.intensity) {
        rt.intensity = target;
        rt.start = target;
        rt.target = target;
        rt.elapsed = 0.0f;
        rt.duration = 0.0f;
        return;
    }
    rt.start = rt.intensity;
    rt.target = target;
    rt.elapsed = 0.0f;
    rt.duration = duration;
}

float RoomRuntime::light_intensity(const std::string& light_id) const {
    const auto it = light_rt_.find(light_id);
    return it != light_rt_.end() ? it->second.intensity : 0.0f;
}

void RoomRuntime::update_lights(float dt) {
    for (auto& [id, rt] : light_rt_) {
        if (rt.duration <= 0.0f) {
            continue;
        }
        rt.elapsed += std::max(dt, 0.0f);
        const float progress = std::clamp(rt.elapsed / rt.duration, 0.0f, 1.0f);
        // Smoothstep avoids a visibly mechanical start/stop while preserving
        // deterministic duration and exact endpoints.
        const float eased = progress * progress * (3.0f - 2.0f * progress);
        rt.intensity = rt.start + (rt.target - rt.start) * eased;
        if (progress >= 1.0f) {
            rt.intensity = rt.target;
            rt.duration = 0.0f;
        }
    }
}

bool RoomRuntime::has_light_occluder(const std::string& occluder_id) const {
    return light_occluder_enabled_.count(occluder_id) > 0;
}

void RoomRuntime::set_light_occluder_enabled(const std::string& occluder_id, bool enabled) {
    const auto it = light_occluder_enabled_.find(occluder_id);
    if (it != light_occluder_enabled_.end()) {
        it->second = enabled;
    }
}

bool RoomRuntime::light_occluder_enabled(const std::string& occluder_id) const {
    const auto it = light_occluder_enabled_.find(occluder_id);
    return it != light_occluder_enabled_.end() && it->second;
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
    behavior_->scripting = &scripting;
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

VerbResult RoomRuntime::call_hotspot(const std::string& hotspot_id,
                                     const std::string& verb,
                                     std::optional<std::string> operand) {
    if (!behavior_ || !behavior_->valid || !behavior_->scripting) {
        return {};
    }
    sol::optional<sol::table> hotspots = behavior_->table["hotspots"];
    if (!hotspots) {
        return {};
    }
    sol::optional<sol::table> hs = (*hotspots)[hotspot_id];
    if (!hs) {
        return {};
    }
    sol::optional<sol::function> fn = (*hs)[verb];
    if (!fn) {
        return {}; // no handler for this verb -> caller may show the default caption
    }
    // A handler exists: from here it is responsible. Auto-spawn (M9 #183) so the
    // handler can `talk` / `move_to` / `wait` without an explicit `spawn()`
    // wrapper. `spawn_call` resumes inline to the first yield or completion: if
    // it completes synchronously, its return value is the caption (today's
    // semantics); if it yielded, `in_flight` carries the task id and the
    // dispatcher (`RoomScene::dispatch_and_feedback`) defers finish_execution
    // until the task drains.
    const pac::core::SpawnCallResult call =
        pac::core::spawn_call(*behavior_->scripting, *fn, operand);
    VerbResult res;
    res.handled = true;
    if (call.done) {
        res.caption = call.string_return;
    } else {
        res.in_flight = call.task_id;
    }
    return res;
}

VerbResult RoomRuntime::call_config_beat(const std::string& config_id, const std::string& hook) {
    if (!behavior_ || !behavior_->valid || !behavior_->scripting) {
        return {};
    }
    sol::optional<sol::table> configs = behavior_->table["configs"];
    if (!configs) {
        return {}; // room .lua declares no config beats
    }
    sol::optional<sol::table> cfg = (*configs)[config_id];
    if (!cfg) {
        return {}; // presence-only config (declared in YAML, no Lua beats)
    }
    sol::optional<sol::function> fn = (*cfg)[hook];
    if (!fn) {
        return {}; // this config has no beat for this hook
    }
    // Auto-spawn like a verb handler (#183): a `cutscene { ... }` beat arms the
    // BLOCKED drain itself; a plain blocking beat yields and hands back its task id.
    const pac::core::SpawnCallResult call = pac::core::spawn_call(*behavior_->scripting, *fn);
    VerbResult res;
    res.handled = true;
    if (!call.done) {
        res.in_flight = call.task_id;
    }
    return res;
}

} // namespace pac::pnc
