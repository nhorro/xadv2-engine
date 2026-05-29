#pragma once

#include "engine/geom/geometry.hpp"
#include "engine/gfx/shader_effect.hpp"

#include <SFML/Graphics/Color.hpp>
#include <SFML/System/Vector2.hpp>

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace pac::pnc {

struct BackgroundLayer {
    std::string id;
    std::string image; // logical path, relative to the room file's directory
    float z = 0.0f;
    bool interactive = false;
    // Room-space top-left where the layer's image is drawn. Layers may differ in
    // size and be placed freely (e.g. a foreground occluder); the room's world
    // bounds are derived from the union of all layer rects (see
    // compute_room_bounds). Defaults to the world origin (0,0).
    geom::Point origin{0, 0};
    // Uniform render scale applied about the top-left `origin`. Aspect ratio is
    // always preserved (layers are never distorted); 1.0 draws at native pixel
    // size. Mainly a development aid for sizing furniture-style occluder layers;
    // in production layers should ship at their correct native size.
    float scale = 1.0f;
    // Initial visibility. Toggled at runtime via set_layer_visible (Lua), which
    // requires the layer to carry an `id`. Persisted per room like object/region
    // state. World bounds are still derived from all layers, hidden or not.
    bool visible = true;
    // Shaders applied when drawing this layer (design 03 §Shaders). MVP applies
    // the first; the vector is a design-for ordered stack (multi-pass needs
    // render-to-texture, not yet built).
    std::vector<gfx::ShaderEffect> shaders;
};

struct RoomHotspot {
    std::string id;
    std::string name;                     // localized noun
    geom::Polygon area;                   // hit-test polygon (M3: required)
    std::optional<geom::Point> approach;  // resolved approach point, if any
    std::vector<std::string> affordances; // verbs the UI may offer
    std::string default_verb = "look_at";
    std::string
        bind; // "object:<id>" / "region:<id>" / "npc:<id>" hit source (RoomRuntime::hotspot_at)
    bool enabled = true;
    // When true (the default), a command on this hotspot only fires once the
    // player has walked to `approach`; until then input is blocked. Set false for
    // the rare act-from-a-distance interaction (e.g. talking to something across
    // the room), where the action fires immediately even if the player is far.
    bool requires_approach = true;
};

struct RoomAvatarPlacement {
    std::string id;
    std::string start; // point name
    std::string orientation = "down";
    bool player = false;
};

/// Trigger polygon for room exits / scripted events (fires on_zone_enter/exit).
struct Zone {
    std::string id;
    geom::Polygon polygon;
};

/// A polygon subtracted from the walkable area for pathfinding. `id` is optional
/// — a named obstacle can be toggled from Lua (enable_obstacle/disable_obstacle).
/// `enabled` is the initial state; runtime toggles mutate it and are persisted per
/// room (like object/hotspot state). A disabled obstacle no longer blocks walking.
struct Obstacle {
    std::string id;
    geom::Polygon area;
    bool enabled = true;
};

/// A changeable part of the background with named states (e.g. drawer shut/open),
/// swapped with set_region_state. A state image may be empty (draws nothing).
struct Region {
    std::string id;
    geom::Polygon area; // footprint (also a hit source)
    float z = 0.0f;
    std::string over; // optional: draw at the z of this layer id
    // Optional explicit sort line (world Y), in avatar-feet space, exactly like a
    // RoomObject's `baseline`: the region occludes characters whose feet are above
    // the line (smaller y) and is occluded by those below it. Set it for a
    // perspective-drawn region (e.g. a swappable cart state) that the player must
    // pass in front of / behind. Overrides `z` / `over` (design 04 §Z-order).
    std::optional<float> baseline;
    std::map<std::string, std::string> states; // state id -> image logical path
    std::string initial;
    std::vector<gfx::ShaderEffect> shaders; // applied when drawing (design 03 §Shaders)
};

/// An active sprite placed in the room (visual only; interactivity via a hotspot).
struct RoomObject {
    std::string id;
    // Logical path. A static texture (e.g. *.png), or an animation (*.anim.yml /
    // *.yaml) — the latter makes this an *animated* object (an AnimatedSprite,
    // like an avatar) that can play sequences and be moved from script (#142).
    std::string sprite;
    // Initial animation sequence to play (looping) for an animated object. Empty
    // for static objects, or when the object only animates on a scripted play().
    std::string sequence;
    geom::Point position{0, 0}; // top-left for a static texture; pivot for animated
    bool z_auto = true;         // auto: depth = position.y; else explicit z
    float z = 0.0f;
    // Uniform render scale about the top-left `position`, aspect always preserved
    // (like a layer's `scale`). 1.0 = native pixel size. The room editor sets this
    // when resizing an object; `z: auto` uses the scaled bottom edge (#147).
    float scale = 1.0f;
    bool visible = true;
    // Optional explicit sort line (world Y), in the same space as an avatar's
    // walking-pivot y. When set, the object sorts at this depth against avatars:
    // it occludes characters whose feet are above the line (smaller y) and is
    // occluded by those below it (larger y) — the SCUMM/AGS "baseline". Overrides
    // `z` / `z_auto`. Use it for a perspective object's foreground piece so the
    // character passes correctly in front of / behind it (design 04 §Z-order).
    std::optional<float> baseline;
    std::vector<gfx::ShaderEffect> shaders; // applied when drawing (design 03 §Shaders)
};

/// A "walk-behind" mask: a polygon patch of a background `layer`, redrawn on top
/// at `baseline` (a world-Y sort line). An avatar passes in front of it when its
/// feet are below the line (larger y) and behind it when above — without
/// duplicating the art, since the pixels are sampled from the layer itself
/// (design 04 §Walk-behind). MVP: the polygon must be convex; decompose a concave
/// occluder into several walk-behind areas.
struct WalkBehind {
    std::string id;
    std::string layer;  // source background layer id (its image is sampled)
    geom::Polygon area; // mask polygon (convex)
    float baseline = 0.0f;
};

/// Optional avatar-scale-by-depth, simulating perspective. Two floor lines, each
/// a `(y, scale)`: the engine linearly interpolates an avatar's render scale from
/// its walking-pivot y between `top` (far/back, usually smaller) and `bottom`
/// (near/front, usually larger), clamped to the band's endpoints outside it.
struct Perspective {
    float top_y = 0.0f;
    float top_scale = 1.0f;
    float bottom_y = 0.0f;
    float bottom_scale = 1.0f;
};

/// Parsed static room definition (`rooms/<id>.yaml`). Headless and testable; the
/// behavior lives in `rooms/<id>.lua`.
struct RoomData {
    int version = 1;
    std::string id;
    sf::Color background_color = sf::Color::Black;
    std::vector<BackgroundLayer> layers;
    geom::Polygon walkable;
    std::vector<Obstacle> obstacles;
    std::map<std::string, geom::Point> points;
    std::vector<Zone> zones;
    std::map<std::string, Region> regions;
    std::map<std::string, RoomObject> objects;
    std::map<std::string, RoomHotspot> hotspots;
    std::vector<WalkBehind> walkbehinds;
    std::vector<RoomAvatarPlacement> avatars;
    std::optional<Perspective> perspective;

    const geom::Point* point(const std::string& name) const;
    bool is_walkable(geom::Point p) const;
    /// Areas of the currently-enabled obstacles, for the pathfinder (geom takes
    /// plain polygons and must not know about obstacle ids / enabled flags).
    [[nodiscard]] std::vector<geom::Polygon> active_obstacles() const;

    /// Avatar render scale at walking-pivot `y`. Returns `fallback` when the room
    /// has no `perspective`; otherwise the interpolated (clamped) perspective scale.
    float avatar_scale_at(float y, float fallback = 1.0f) const;
};

/// Parse + validate room YAML. Throws a `pac::core::LoadError` (source
/// `room-loader`) on malformed input. Hotspot `approach` given as a point name is
/// resolved against `points`. When `expected_id` is non-empty it must match the
/// YAML `id` (the filename-vs-id check); pass it empty to skip that check (e.g.
/// headless tests that feed raw text without a filename).
RoomData parse_room(const std::string& yaml_text, const std::string& expected_id = {});

} // namespace pac::pnc
