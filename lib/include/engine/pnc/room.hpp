#pragma once

#include "engine/core/audio.hpp"
#include "engine/geom/geometry.hpp"
#include "engine/gfx/shader_effect.hpp"

#include <SFML/Graphics/Color.hpp>
#include <SFML/System/Vector2.hpp>

#include <array>
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
    // size and be placed freely (e.g. a foreground occluder); by default the
    // room's world bounds include this rect (see `extend_bounds` and
    // compute_room_bounds). Defaults to the world origin (0,0).
    geom::Point origin{0, 0};
    // Uniform render scale applied about the top-left `origin`. Aspect ratio is
    // always preserved (layers are never distorted); 1.0 draws at native pixel
    // size. Mainly a development aid for sizing furniture-style occluder layers;
    // in production layers should ship at their correct native size.
    float scale = 1.0f;
    // Initial visibility. Toggled at runtime via set_layer_visible (Lua), which
    // requires the layer to carry an `id`. Persisted per room like object/region
    // state. Visibility does not affect whether a layer extends world bounds.
    bool visible = true;
    // Whether this layer may enlarge the room's world bounds. Foreground art can
    // opt out so pixels beyond the base scenery canvas are simply clipped.
    bool extend_bounds = true;
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
    // Optional point from which the player walks into the room on a normal
    // entry. Usually just beyond a screen edge; `start` remains the destination.
    // Save restores and change_room(..., explicit_entry_point) skip this walk.
    std::string enter_from;
    std::string orientation = "down";
    bool player = false;
};

/// Optional post-process over the fully composed scenery viewport. Unlike
/// per-drawable shaders, this runs once after layers, regions, objects, shadows,
/// walk-behinds, and avatars have been drawn; speech, debug overlays, and the
/// room UI remain unprocessed.
struct RoomPostProcess {
    bool enabled = true;
    std::vector<gfx::ShaderEffect> shaders;
};

/// Time-varying multiplier for a dynamic light. Modulation changes the light's
/// intensity inside the lighting pass, so fire/faulty-lamp animation does not add
/// another full-screen shader pass.
struct LightModulation {
    enum class Type { NONE, SINE, FLICKER, FAULTY };

    Type type = Type::NONE;
    float amount = 0.0f; // 0..1 variation depth around the authored intensity
    float speed = 1.0f;  // cycles/noise cells per second
    float seed = 0.0f;   // deterministic phase/noise variation
};

/// One world-space dynamic light. Omnilights use radial falloff; spotlights add
/// a directional cone with an angular penumbra. `attach` may be `player`,
/// `avatar:<id>`, or `object:<id>`; attached positions use `offset` in world
/// pixels, while an unattached light uses `at`.
struct RoomLight {
    enum class Type { OMNI, SPOT };

    std::string id;
    Type type = Type::OMNI;
    bool enabled = true;
    geom::Point at{0.0f, 0.0f};
    std::string attach;
    geom::Point offset{0.0f, 0.0f};
    float radius = 1.0f;
    float height = 1.0f; // virtual distance above the image plane (normal maps)
    std::array<float, 3> color{1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
    // Screen-space degrees: 0 points right, 90 down. Spotlights only.
    float direction = 0.0f;
    float angle = 45.0f;   // full outer cone angle in degrees
    float softness = 8.0f; // angular fade width at each cone edge
    bool follow_facing = false;
    LightModulation modulation;
};

/// A polygon whose boundary blocks direct dynamic light. This is a 2D
/// line-of-sight approximation evaluated in the existing lighting pass.
struct LightOccluder {
    std::string id;
    geom::Polygon area;
    bool enabled = true;
};

/// Ambient illumination plus the room's dynamic omni/spot lights. The lighting
/// pass multiplies the fully composed scene by ambient + light contributions,
/// before the authored room post-process stack (including grading).
struct RoomLighting {
    std::array<float, 3> ambient_color{1.0f, 1.0f, 1.0f};
    float ambient_intensity = 0.35f;
    std::string normal_map; // optional room-space tangent normal map
    geom::Point normal_origin{0.0f, 0.0f};
    float normal_scale = 1.0f;
    float normal_strength = 1.0f;
    std::vector<RoomLight> lights;
    std::vector<LightOccluder> occluders;
};

/// A live avatar silhouette projected onto the room plane away from a 2D light
/// point. This is an intentionally painterly 2D approximation rather than a
/// geometry-aware shadow map: `length` maps sprite height onto the floor,
/// `width` controls the silhouette's cross-axis scale, and `softness` feathers
/// the result with a small set of offset draws. The existing appearance ellipse
/// remains as a contact shadow, scaled by `contact_shadow`.
struct ProjectedShadow {
    enum class Casters { PLAYER, ALL };

    bool enabled = true;
    geom::Point light{0.0f, 0.0f};
    // Optional dynamic-light id. When set, the live light position drives the
    // shadow and a disabled/missing attachment suppresses it for that frame.
    std::string source;
    Casters casters = Casters::PLAYER;
    float length = 0.45f;
    float width = 0.75f;
    float opacity = 0.18f;
    float softness = 4.0f;
    float contact_shadow = 0.55f;
    sf::Color color{12, 14, 18};
    // Optional fixed floor depth for both the projected silhouette and contact
    // ellipse. When omitted, shadows keep sorting together with their avatar.
    std::optional<float> z;
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
    // Clockwise degrees about the object's pivot (animated/composite) or its
    // authored top-left (static texture). Script may change it transiently.
    float rotation = 0.0f;
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

/// One declarative room configuration (#185): which managed NPCs / objects /
/// obstacles are present while the room is in this config. Entering or
/// transitioning to a config reconciles the room *exhaustively* — every id in the
/// room's managed set (the union across all configs, see RoomConfigs) is turned ON
/// iff this config names it, OFF otherwise. So an "empty" config is `present: {}`.
/// The optional first-enter / re-enter BEATS live in Lua under
/// `room.configs[<id>]` ({ on_first_enter = fn, on_reenter = fn }), keyed by this
/// same id — static presence in YAML, behavior in Lua.
struct RoomConfig {
    std::string id;
    struct Npc {
        std::string at; // start point name (must be a room point)
        std::string facing = "down";
    };
    std::map<std::string, Npc> npcs;    // NPCs present in this config
    std::vector<std::string> objects;   // object ids shown in this config
    std::vector<std::string> obstacles; // obstacle ids enabled in this config
};

/// A room's `configs:` block (#185). `start` is the config a fresh game begins the
/// room in (the engine then tracks the live value in GameState, replacing the
/// hand-managed `"<room>.cfg"` integer). The `managed_*` unions name every id any
/// config controls, so reconciling one config can turn the rest off.
struct RoomConfigs {
    std::string start;
    std::vector<std::string> order; // config ids in declaration order
    std::map<std::string, RoomConfig> by_id;
    std::vector<std::string> managed_npcs;
    std::vector<std::string> managed_objects;
    std::vector<std::string> managed_obstacles;

    const RoomConfig* find(const std::string& id) const;
};

/// Parsed static room definition (`rooms/<id>.yaml`). Headless and testable; the
/// behavior lives in `rooms/<id>.lua`.
struct RoomData {
    int version = 1;
    std::string id;
    std::optional<RoomPostProcess> post_process;
    std::optional<RoomLighting> dynamic_lighting;
    std::optional<ProjectedShadow> projected_shadow;
    std::optional<core::AmbienceDefinition> ambience;
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
    std::optional<RoomConfigs> configs; // declarative configurations (#185)

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
