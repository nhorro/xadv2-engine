#pragma once

#include "engine/geom/geometry.hpp"
#include "engine/pnc/avatar.hpp"
#include "engine/pnc/room.hpp"

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

    /// First enabled hotspot whose area contains `world`, or nullptr.
    const RoomHotspot* hotspot_at(geom::Point world) const;

    /// First zone whose polygon contains `world`, or nullptr.
    const Zone* zone_at(geom::Point world) const;

    // --- runtime state (live; persistence is mediated by RoomScene) ---
    void set_region_state(const std::string& region_id, const std::string& state);
    [[nodiscard]] std::string region_state(const std::string& region_id) const;

    void set_object_visible(const std::string& object_id, bool visible);
    [[nodiscard]] bool object_visible(const std::string& object_id) const;

    void set_hotspot_enabled(const std::string& hotspot_id, bool enabled);
    [[nodiscard]] bool hotspot_enabled(const std::string& hotspot_id) const;

    // --- NPC avatars (room-scoped) ---
    void add_npc(const std::string& id, Avatar avatar);
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

    /// Call `hotspots.<id>.<verb>(operand?)` if defined; returns its string result
    /// (a caption), or nullopt when there is no handler / no string result. When
    /// `operand` is set it is passed as the handler argument (two-operand verbs).
    std::optional<std::string> call_hotspot(const std::string& hotspot_id,
                                            const std::string& verb,
                                            std::optional<std::string> operand = std::nullopt);

private:
    struct Behavior;
    void seed_runtime_state();

    RoomData data_;
    std::unique_ptr<Behavior> behavior_;
    std::map<std::string, std::string> region_states_;
    std::map<std::string, bool> object_visible_;
    std::map<std::string, bool> hotspot_enabled_;
    std::map<std::string, Avatar> npcs_;
};

} // namespace pac::pnc
