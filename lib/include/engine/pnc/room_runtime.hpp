#pragma once

#include "engine/geom/geometry.hpp"
#include "engine/pnc/room.hpp"

#include <memory>
#include <optional>
#include <string>

namespace pac::core {
class Diagnostics;
class ResourceCache;
class Scripting;
} // namespace pac::core

namespace pac::pnc {

/// Loaded room: its static data plus the Lua behavior table. The behavior (a
/// sol::table) is held opaquely (pimpl) so sol2 never leaks into a header. Lua
/// hooks/handlers are invoked through non-sol methods.
class RoomRuntime {
public:
    explicit RoomRuntime(RoomData data);
    ~RoomRuntime();
    RoomRuntime(RoomRuntime&&) noexcept;
    RoomRuntime& operator=(RoomRuntime&&) noexcept;

    const RoomData& data() const { return data_; }

    /// First enabled hotspot whose area contains `world`, or nullptr.
    const RoomHotspot* hotspot_at(geom::Point world) const;

    /// Load `rooms/<id>.lua` and keep its returned table as this room's behavior.
    void load_behavior(pac::core::Scripting& scripting,
                       pac::core::ResourceCache& resources,
                       const std::string& logical,
                       pac::core::Diagnostics& log);

    /// Call a room lifecycle hook (e.g. on_load / on_unload) if defined.
    void call_hook(const std::string& name);

    /// Call `hotspots.<id>.<verb>()` if defined; returns its string result (a
    /// caption), or nullopt when there is no handler / no string result.
    std::optional<std::string> call_hotspot(const std::string& hotspot_id, const std::string& verb);

private:
    struct Behavior;
    RoomData data_;
    std::unique_ptr<Behavior> behavior_;
};

} // namespace pac::pnc
