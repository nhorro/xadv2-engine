#pragma once

#include "engine/geom/geometry.hpp"
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
class Scripting;
} // namespace pac::core

namespace pac::pnc {

/// Outcome of a verb-handler lookup. `handled` is true when a matching handler
/// function existed and ran (regardless of what it returned); `caption` is its
/// returned line, if any. This lets the dispatcher distinguish "no handler →
/// emit the default caption" from "a handler ran (did an action, spoke, or
/// returned nothing) → it took responsibility, emit nothing".
struct VerbResult {
    bool handled = false;
    std::optional<std::string> caption;
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
    void object_move_to(const std::string& object_id, geom::Point target, float speed);
    [[nodiscard]] bool object_moving(const std::string& object_id) const;
    void update_objects(float dt);

    // Animated objects (#142): an object whose sprite is an animation owns an
    // AnimatedSprite (built render-side with resources and attached here, like an
    // NPC avatar). update_objects advances it and syncs its transform to the
    // runtime pose. `object_play` plays a sequence; pass track_until_end=true for
    // a one-shot whose completion `object_acting` reports (drives play_until_end).
    void set_object_sprite(const std::string& object_id, gfx::AnimatedSprite sprite);
    [[nodiscard]] bool object_animated(const std::string& object_id) const;
    [[nodiscard]] const gfx::AnimatedSprite* object_sprite(const std::string& object_id) const;
    bool
    object_play(const std::string& object_id, const std::string& sequence, bool track_until_end);
    [[nodiscard]] bool object_acting(const std::string& object_id) const;

    void set_layer_visible(const std::string& layer_id, bool visible);
    [[nodiscard]] bool layer_visible(const std::string& layer_id) const;

    void set_hotspot_enabled(const std::string& hotspot_id, bool enabled);
    [[nodiscard]] bool hotspot_enabled(const std::string& hotspot_id) const;

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

private:
    struct Behavior;
    void seed_runtime_state();

    RoomData data_;
    std::unique_ptr<Behavior> behavior_;
    std::map<std::string, std::string> region_states_;
    std::map<std::string, bool> object_visible_;
    std::map<std::string, bool> layer_visible_;
    std::map<std::string, bool> hotspot_enabled_;
    std::map<std::string, Avatar> npcs_;

    // Transient per-object runtime pose (#142), seeded from each RoomObject def.
    struct ObjectRuntime {
        geom::Point position{0.0f, 0.0f};
        float scale = 1.0f;
        geom::Point target{0.0f, 0.0f};
        float speed = 240.0f;
        bool moving = false;
        std::string acting; // one-shot sequence in progress (for play_until_end)
    };
    std::map<std::string, ObjectRuntime> object_rt_;
    std::map<std::string, gfx::AnimatedSprite> object_sprites_; // animated objects (#142)
};

} // namespace pac::pnc
