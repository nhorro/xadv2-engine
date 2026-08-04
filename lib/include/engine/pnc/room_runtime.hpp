#pragma once

#include "engine/core/scripting.hpp"
#include "engine/geom/geometry.hpp"
#include "engine/gfx/visual_sprite.hpp"
#include "engine/pnc/avatar.hpp"
#include "engine/pnc/room.hpp"

#include <SFML/Graphics/Rect.hpp>

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace pac::core {
class Diagnostics;
class ResourceCache;
} // namespace pac::core

namespace pac::pnc {

/// Outcome of a verb-handler lookup. `handled` is true when a matching handler
/// function existed and ran (regardless of what it returned); `caption` is its
/// returned line, if any. `in_flight` carries the task id when the handler is
/// auto-spawned (M9 #183) and yielded before completing — the dispatcher then
/// defers its caption + `finish_execution` until the task drains. This lets the
/// dispatcher distinguish "no handler → emit the default caption" from "a handler
/// ran (did an action, spoke, or returned nothing) → it took responsibility,
/// emit nothing" from "a handler is still running asynchronously."
struct VerbResult {
    bool handled = false;
    std::optional<std::string> caption;
    std::optional<pac::core::TaskId> in_flight;
};

/// Loaded room: its static data plus the Lua behavior table. The behavior (a
/// sol::table) is held opaquely (pimpl) so sol2 never leaks into a header. Lua
/// hooks/handlers are invoked through non-sol methods.
///
/// Owns the room's NPC avatars (per [design 04 §Avatars]): the player avatar
/// outlives the room, but NPC avatars are room-scoped and destroyed on unload.
class RoomRuntime {
public:
    explicit RoomRuntime(RoomData data);
    ~RoomRuntime();
    RoomRuntime(RoomRuntime&&) noexcept;
    RoomRuntime& operator=(RoomRuntime&&) noexcept;

    const RoomData& data() const { return data_; }

    /// First enabled hotspot that hits `world`, by the design 04 hit-test rule:
    /// per hotspot, in priority order — (1) explicit `area` polygon, (2)
    /// `bind: object:<id>` frame bounds (via `object_bounds`, render-side: needs
    /// the texture size), (3) `bind: region:<id>` area polygon (state-independent).
    /// The no-arg overload omits object-frame testing so it stays headless.
    const RoomHotspot* hotspot_at(geom::Point world) const;
    const RoomHotspot* hotspot_at(
        geom::Point world,
        const std::function<std::optional<sf::FloatRect>(const std::string&)>& object_bounds) const;

    /// First zone whose polygon contains `world`, or nullptr.
    const Zone* zone_at(geom::Point world) const;

    // --- runtime state (live; persistence is mediated by RoomScene) ---
    void set_region_state(const std::string& region_id, const std::string& state);
    [[nodiscard]] std::string region_state(const std::string& region_id) const;

    void set_object_visible(const std::string& object_id, bool visible);
    [[nodiscard]] bool object_visible(const std::string& object_id) const;

    // Scripted object transform + movement (#142). Position and scale start from
    // the RoomObject def and can be changed from script; `move_to` is a free
    // linear move (NOT walkable-gated — objects are not characters). This runtime
    // pose is transient: re-derived from the def on room load, like an avatar's
    // pose (only object *visibility* persists). update_objects advances moves.
    [[nodiscard]] geom::Point object_position(const std::string& object_id) const;
    void set_object_position(const std::string& object_id, geom::Point p);
    [[nodiscard]] float object_scale(const std::string& object_id) const;
    void set_object_scale(const std::string& object_id, float scale);
    [[nodiscard]] float object_rotation(const std::string& object_id) const;
    void set_object_rotation(const std::string& object_id, float degrees);
    void object_move_to(const std::string& object_id, geom::Point target, float speed);
    [[nodiscard]] bool object_moving(const std::string& object_id) const;
    void update_objects(float dt);

    // Animated objects (#142): an object whose sprite is an animation owns an
    // AnimatedSprite (built render-side with resources and attached here, like an
    // NPC avatar). update_objects advances it and syncs its transform to the
    // runtime pose. `object_play` plays a sequence; pass track_until_end=true for
    // a one-shot whose completion `object_acting` reports (drives play_until_end).
    void set_object_sprite(const std::string& object_id, gfx::VisualSprite sprite);
    [[nodiscard]] bool object_animated(const std::string& object_id) const;
    [[nodiscard]] const gfx::VisualSprite* object_sprite(const std::string& object_id) const;
    bool
    object_play(const std::string& object_id, const std::string& sequence, bool track_until_end);
    [[nodiscard]] bool object_acting(const std::string& object_id) const;

    void set_layer_visible(const std::string& layer_id, bool visible);
    [[nodiscard]] bool layer_visible(const std::string& layer_id) const;

    void set_hotspot_enabled(const std::string& hotspot_id, bool enabled);
    [[nodiscard]] bool hotspot_enabled(const std::string& hotspot_id) const;

    // Dynamic-light overrides are transient and reset from YAML on room load.
    // Unknown ids are ignored; callers can use has_light() to report a useful
    // script error without accidentally creating runtime state.
    [[nodiscard]] bool has_light(const std::string& light_id) const;
    void set_light_enabled(const std::string& light_id, bool enabled);
    [[nodiscard]] bool light_enabled(const std::string& light_id) const;
    void set_light_intensity(const std::string& light_id,
                             float intensity,
                             float transition_seconds = 0.0f);
    [[nodiscard]] float light_intensity(const std::string& light_id) const;
    void update_lights(float dt);
    [[nodiscard]] bool has_light_occluder(const std::string& occluder_id) const;
    void set_light_occluder_enabled(const std::string& occluder_id, bool enabled);
    [[nodiscard]] bool light_occluder_enabled(const std::string& occluder_id) const;

    // Named obstacles (#143): enable/disable a walkable blocker by id. The flag
    // lives on the obstacle in `data_` so the pathfinder (RoomData::is_walkable /
    // active_obstacles) sees it. Unknown / unnamed obstacles are ignored.
    void set_obstacle_enabled(const std::string& obstacle_id, bool enabled);
    [[nodiscard]] bool obstacle_enabled(const std::string& obstacle_id) const;

    // --- NPC avatars (room-scoped) ---
    void add_npc(const std::string& id, Avatar avatar);
    /// Remove a room NPC if present (scripted despawn, #140). No-op when absent.
    void remove_npc(const std::string& id);
    [[nodiscard]] Avatar* npc(const std::string& id);
    [[nodiscard]] const Avatar* npc(const std::string& id) const;
    [[nodiscard]] std::vector<const Avatar*> npcs() const;
    void update_npcs(float dt);

    /// Load `rooms/<id>.lua` and keep its returned table as this room's behavior.
    void load_behavior(pac::core::Scripting& scripting,
                       pac::core::ResourceCache& resources,
                       const std::string& logical,
                       pac::core::Diagnostics& log);

    /// Call a room lifecycle hook (e.g. on_load / on_unload) if defined.
    void call_hook(const std::string& name);

    /// Call a zone hook (on_zone_enter / on_zone_exit) with the zone id, if defined.
    void call_zone_hook(const std::string& hook, const std::string& zone_id);

    /// Call `hotspots.<id>.<verb>(operand?)` if defined. The result's `handled`
    /// is true when the handler function existed and ran (so the caller suppresses
    /// the default caption even if it returned nothing); `caption` carries a
    /// returned string. When `operand` is set it is passed as the handler argument
    /// (two-operand verbs).
    VerbResult call_hotspot(const std::string& hotspot_id,
                            const std::string& verb,
                            std::optional<std::string> operand = std::nullopt);

    /// Spawn the current config's optional beat — `configs.<config_id>.<hook>`,
    /// where `hook` is "on_first_enter" / "on_reenter" — as an auto-spawned task
    /// (#183), so it may block (talk/move_to/wait) like a verb handler. Returns
    /// `in_flight` when it yields (the caller blocks input until it drains), or an
    /// empty result when the config/hook is absent or it ran synchronously. (#185)
    VerbResult call_config_beat(const std::string& config_id, const std::string& hook);

private:
    struct Behavior;
    void seed_runtime_state();

    RoomData data_;
    std::unique_ptr<Behavior> behavior_;
    std::map<std::string, std::string> region_states_;
    std::map<std::string, bool> object_visible_;
    std::map<std::string, bool> layer_visible_;
    std::map<std::string, bool> hotspot_enabled_;
    std::map<std::string, bool> light_enabled_;
    struct LightRuntime {
        float intensity = 1.0f;
        float start = 1.0f;
        float target = 1.0f;
        float elapsed = 0.0f;
        float duration = 0.0f;
    };
    std::map<std::string, LightRuntime> light_rt_;
    std::map<std::string, bool> light_occluder_enabled_;
    std::map<std::string, Avatar> npcs_;

    // Transient per-object runtime pose (#142), seeded from each RoomObject def.
    struct ObjectRuntime {
        geom::Point position{0.0f, 0.0f};
        float scale = 1.0f;
        float rotation = 0.0f;
        geom::Point target{0.0f, 0.0f};
        float speed = 240.0f;
        bool moving = false;
        std::string acting; // one-shot sequence in progress (for play_until_end)
    };
    std::map<std::string, ObjectRuntime> object_rt_;
    std::map<std::string, gfx::VisualSprite> object_sprites_; // animated/composite objects
};

} // namespace pac::pnc
