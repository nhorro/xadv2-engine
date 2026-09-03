# From a playable room to a lit room

This tutorial starts with an ordinary point-and-click room—background art, a
walkable area, hotspots, the player, and perhaps some NPCs—and develops it into a
finished scene with **dynamic lighting, contact and projected shadows, and a
final colour grade**.

You do not need to write a shader or understand 3D rendering. The useful mental
model is:

1. compose the room and its characters;
2. decide how much light reaches each part of that composition;
3. give the completed image its final palette.

The examples use a 1280×720 game with a 1280×592 scenery viewport. Coordinates
and light ranges are room pixels, so adjust the numbers for your own resolution.
For the exhaustive schema, keep the
[scenery authoring reference](scenery.md)
open beside this guide.

## The rendering pipeline

The order matters because each stage receives the result of the previous stage:

```text
source textures and animation frames
        │
        ├─ per-drawable shaders (optional)
        │  layers, regions, objects, avatar appearances
        ▼
z-sorted room composition
        │  backdrop, layers, regions, objects, walk-behinds,
        │  contact/projected shadows, player and NPC sprites
        ▼
dynamic lighting (one full-scene pass, optional)
        │  ambient darkness + omni/spot lights
        │  + optional light occluders and normal map
        ▼
room post-processing (optional ordered shader stack)
        │  colour grading normally belongs here
        ▼
speech, floating text, debug guides, and room UI
```

The scenery is first drawn into one viewport-sized texture whenever room lighting
or post-processing is active. Dynamic lighting processes that complete texture,
then every enabled `post_process` shader processes the lit result. Speech and the
SCUMM panel are drawn afterward, so a dark room does not make its interface hard
to read.

This has several practical consequences:

- **Characters receive room lighting automatically.** You do not need to repeat
  the room grade or light shader on every avatar.
- **Contact and projected shadows are lit and graded with the room.** Choose
  their colour while looking at the final image, not the raw shadow alone.
- **A per-layer shader runs earlier.** Use it for a local material effect such as
  water, heat distortion, or a glowing screen—not for the room's final grade.
- **The final grade is global.** It gives backgrounds, foreground props,
  characters, and shadows one palette.
- **Debug guides are trustworthy.** Walkboxes, hotspot outlines, anchors, and
  speech retain their UI colours because they are outside the effects stack.

### What “light” means in this engine

The lighting pass is deliberately painterly and LDR (low dynamic range). In
simplified form, each output pixel is:

```text
lit pixel = source pixel × clamp(ambient + direct lights, 0, 1)
```

An omni or spotlight therefore **restores and tints detail that ambient lighting
has darkened**. It does not paint an additive white blob over the background, and
it cannot make a source pixel brighter than it was in the original artwork.

!!! tip "Keep useful colour information in the source art"
    Start with a reasonably bright or neutral background. Let ambient lighting
    create the runtime darkness. If the source PNG already contains crushed black
    areas, no light can recover the missing colour and detail.

This multiplication also makes the system friendly to modest GPUs: all visible
lights, their modulation, normal response, and simple occlusion are evaluated in
one full-scene lighting pass.

## Start from a working room

Do presentation work only after walking, depth, and interaction are basically
correct. A minimal room might look like this:

```yaml
version: 1
id: study

background:
  color: {r: 12, g: 12, b: 14}
  layers:
    - {id: room, image: study.png, z: 0}

walkable:
  - {x: 80, y: 440}
  - {x: 1200, y: 440}
  - {x: 1260, y: 590}
  - {x: 20, y: 590}

points:
  player_start: {x: 820, y: 540}
  archivist_start: {x: 470, y: 500}
  at_door: {x: 1080, y: 520}

avatars:
  - {id: player, start: player_start, player: true, orientation: left}
  - {id: archivist, start: archivist_start, orientation: right}

hotspots:
  door:
    name: la puerta
    area: [{x: 1040, y: 170}, {x: 1150, y: 170}, {x: 1150, y: 480}, {x: 1040, y: 480}]
    approach: at_door
    affordances: [look_at, open]
```

Before adding effects, verify:

- avatar feet align with the painted floor;
- foreground layers and walk-behinds sort correctly;
- the perspective scale feels plausible;
- important hotspots and NPC silhouettes remain easy to identify.

Lighting enhances this foundation. It is a poor tool for hiding incorrect depth
or unreadable composition.

## Plan the light before editing values

Look at the background and answer four questions:

1. **What is the key light?** The sun, a window, a ceiling lamp, a torch?
2. **Where is it?** Its position should agree with highlights and painted shadows
   already present in the art.
3. **What should remain readable?** Usually the walkable floor, exits, NPC faces,
   and puzzle objects.
4. **What is the mood?** Warm afternoon, cool moonlight, green fluorescent light,
   candlelit darkness?

A restrained adventure-game room normally needs:

- one ambient term;
- one dominant light;
- optionally one weaker practical or fill light;
- contact shadows on characters;
- optionally one projected-shadow treatment;
- one final grade.

More lights are not automatically more cinematic. A clear hierarchy usually
looks better and guides the player more effectively.

## Step 1: give avatars contact shadows

The inexpensive contact shadow is defined on an avatar **appearance** in
`cast.yaml`, not in every room. Every character using that appearance inherits
it:

```yaml
appearances:
  julia:
    type: animated_sprite
    sprite: characters/julia/julia.anim.yml
    shadow:
      size: {x: 44, y: 13}
      color: {r: 12, g: 14, b: 18, a: 72}
```

`size.x` and `size.y` are the full ellipse dimensions in room pixels at avatar
scale 1. The ellipse follows perspective scaling. `color.a` is opacity from 0 to
255.

Use the smallest ellipse that convincingly plants the feet. A large dark oval
looks like a stain and competes with the more expressive projected shadow added
later. See [Scenery authoring](scenery.md#npcs-characters-in-the-room).

## Step 2: establish ambient illumination

Add `lighting.ambient` to the room YAML:

```yaml
lighting:
  ambient:
    color: [0.72, 0.78, 0.90]
    intensity: 0.46
```

Ambient affects the whole composed scene equally. It is the room's baseline—the
light that still reaches areas outside every direct light.

| Parameter | Range | What it changes |
|-----------|-------|-----------------|
| `color` | three values from `0` to `1` | Colour of the baseline illumination. Values below 1 remove that channel. |
| `intensity` | `0` to `1` | Overall baseline brightness. `1` with white leaves the source art unchanged; lower values darken it. |

Useful starting points, not rules:

| Situation | Ambient colour | Intensity |
|-----------|----------------|-----------|
| Soft exterior daylight | `[0.80, 0.86, 0.96]` | `0.50–0.65` |
| Window-lit interior | `[0.65, 0.72, 0.85]` | `0.35–0.50` |
| Warm lamp-lit room | `[0.70, 0.60, 0.48]` | `0.25–0.42` |
| Night exterior | `[0.35, 0.45, 0.70]` | `0.20–0.38` |

Start slightly brighter than your intended result. Direct lights and grading are
easier to judge when the floor and characters have not already disappeared.

## Step 3: place direct lights in the room editor

Start the [xadv2 room editor](https://github.com/nhorro/xadv2-tools/tree/main/room_editor), select **lights** from the mode
list, and choose **Add omni** or **Add spotlight**.

The editor represents each light with simple room-space primitives:

| Primitive | Meaning | How to edit it |
|-----------|---------|----------------|
| Centre marker | Static `at` position, or the previewed attachment plus `offset`. | Drag the centre; attached lights update their offset. |
| Diamond range handle | `radius` for an omni or `range` for a spotlight. | Drag away from/toward the centre. |
| Omni circle | Maximum radial reach. It fades smoothly before the boundary. | Change the range handle or inspector value. |
| Spotlight wedge | Direction and full outer cone. | Drag the direction handle and either cone-edge handle. |
| Cone-edge handles | `angle`, with the two arms kept symmetric. | Widen for broad window/sunlight; narrow for a torch. |

The inspector edits id, type, static/attached placement, range, virtual height,
direction, angle, softness, colour, intensity, and initial enabled state. For an
attachment, use `player`, `avatar:<id>`, or `object:<id>`.

!!! note "Editor primitive versus final light"
    The web editor shows placement and geometry, not the exact GPU-composited
    result. It previews attached lights at their initial room placement. Save the
    room, then use the in-game F9 tuner to judge brightness, colour, modulation,
    shadows, and grading together.

The room editor deliberately preserves advanced lighting fields it does not
visualize: ambient, modulation, normal maps, light occluders, and projected
shadows. Edit those in YAML.

### Choose the right primitive

| Use | Best primitive | Why |
|-----|----------------|-----|
| Table lamp, bulb, candle, fire | `omni` | Light spreads in every direction from a nearby point. |
| Window beam, flashlight, stage light | `spot` | The cone gives a visible direction and controllable edge. |
| Sun | Distant, broad `spot` | An offscreen origin and large range approximate near-parallel daylight. |
| General room brightness | `ambient`, not a huge omni | Ambient has no circular hotspot and costs no extra light slot. |

### Light YAML and parameters

An omni is the simplest direct light:

```yaml
lighting:
  ambient: {color: [0.70, 0.76, 0.86], intensity: 0.42}
  lights:
    - id: desk_lamp
      type: omni
      at: {x: 520, y: 220}
      radius: 300
      height: 180
      color: [1.0, 0.74, 0.40]
      intensity: 0.82
```

A spotlight adds cone controls:

```yaml
    - id: window_daylight
      type: spot
      at: {x: 90, y: 210}
      range: 1050
      height: 600
      direction: 24
      angle: 72
      softness: 18
      color: [0.84, 0.92, 1.0]
      intensity: 0.68
```

| Parameter | Applies to | Meaning |
|-----------|------------|---------|
| `id` | both | Unique stable id used by shadows and Lua. |
| `type` | both | `omni` or `spot`. |
| `at` | both | Static origin in room coordinates. Use exactly one of `at` and `attach`. |
| `attach` | both | Follow `player`, `avatar:<id>`, or `object:<id>`. |
| `offset` | attached | Pixel offset from the attachment's pivot. |
| `radius` / `range` | both | Positive maximum reach. `range` is the spotlight-friendly spelling of the same value. |
| `height` | both | Virtual distance above the image plane. It matters only when a room normal map is present; otherwise leave it at the default. |
| `color` | both | RGB contribution, each channel `0..1`. Use restrained tints; saturated lights destroy information quickly. |
| `intensity` | both | Peak contribution `0..4` before modulation. Values around `0.3–1.0` are usually sufficient in an LDR scene. |
| `enabled` | both | Initial state, default `true`. Lua may change it while the room is loaded. |
| `direction` | spot | Screen-space degrees: `0` right, `90` down, `180` left, `270` up. |
| `angle` | spot | Full outer cone angle, greater than 0 and less than 180. |
| `softness` | spot | Penumbra width at each cone edge. It must be less than half the angle. |
| `follow_facing` | attached spot | Rotates the cone with a player/avatar. The authored direction becomes an offset from the cardinal facing. |

The falloff is smooth from the origin to the range boundary. Avoid placing a
boundary across a frequently visited face or hotspot; extend the range and lower
the intensity instead.

## Step 4: add motion only when the source calls for it

Modulation changes a light's intensity inside the same lighting pass:

```yaml
      modulation:
        type: flicker
        amount: 0.05
        speed: 5.0
        seed: 12
```

| Parameter | Meaning |
|-----------|---------|
| `type` | `sine` for regular breathing/pulsing, `flicker` for smooth fire-like variation, `faulty` for irregular variation with abrupt dropouts, or `none`. |
| `amount` | Variation depth from `0` to `1`. Start around `0.02–0.08`; subtle movement feels more like light. |
| `speed` | Positive cycles/noise cells per second. |
| `seed` | Any number; changes deterministic phase/pattern so nearby lights do not move identically. |

Do not modulate daylight or every lamp in a room. Constant movement attracts the
eye and can make interaction tiring. Reserve it for a source whose physical
story explains the change: fire, failing fluorescent tube, television, rotating
beacon.

For a carried torch or flashlight:

```yaml
    - id: flashlight
      type: spot
      attach: player
      offset: {x: 22, y: -58}
      range: 430
      follow_facing: true
      direction: 0
      angle: 46
      softness: 12
      color: [0.90, 0.95, 1.0]
      intensity: 0.90
```

## Step 5: project character shadows

Projected shadows reuse each caster's current animation frame as a softened
silhouette laid across the floor away from a light. Point them at a declared
dynamic light with `source`:

```yaml
lighting:
  # ambient and lights are above
  projected_shadows:
    enabled: true
    source: window_daylight
    casters: all
    length: 0.58
    width: 0.76
    opacity: 0.24
    softness: 3.0
    contact_shadow: 0.60
    color: {r: 12, g: 15, b: 22}
```

Use `source` whenever the shadow belongs to a dynamic light. Its live position,
enabled state, runtime intensity, and modulation are respected. Use
`light: {x, y}` instead only for an independent fixed shadow direction. Exactly
one of `source` or `light` is required.

| Parameter | Meaning |
|-----------|---------|
| `enabled` | Bypasses projected shadows when false. |
| `source` | Id of a light in `lighting.lights`. |
| `light` | Alternative fixed room-space origin. |
| `casters` | `player` or `all` present avatars. |
| `length` | Projected length as a fraction of the live sprite height. |
| `width` | Cross-axis silhouette scale. |
| `opacity` | Maximum projected-shadow opacity from `0` to `1`. |
| `softness` | Penumbra radius in room pixels. `0` is crisp; larger values use sampled silhouettes. |
| `contact_shadow` | Multiplier applied to the appearance's ordinary ellipse while projection is active. |
| `z` | Optional fixed floor depth. Omit to sort with each avatar. |
| `color` | RGB shadow tint before lighting and grading. |

Projected shadows are a 2D approximation. They do not bend onto walls, discover
painted furniture, or clip themselves to the walkable polygon. Keep them short,
soft, and translucent. If a foreground table should cover a shadow independently
of the avatar, set `z` just above the base floor and below that furniture's
depth. Otherwise, omitting `z` is safer.

### Light occluders are different from projected shadows

An occluder blocks **direct illumination** across authored line segments; it does
not cast an avatar silhouette:

```yaml
  occluders:
    - id: closed_door
      area: [{x: 720, y: 180}, {x: 720, y: 520}]
```

Two points create one blocking wall. Three or more create a closed polygon
boundary. Use these sparingly for major barriers, not every edge painted into a
background. Up to 32 enabled edges are evaluated, in authoring order.

Lua can synchronize a changing barrier:

```lua
light_occluder("closed_door"):disable()
```

## Step 6: grade the completed image

Lighting answers “where does light reach?” Grading answers “what palette and
contrast should the finished room have?” Put the grade in `post_process`, which
runs after lighting:

```yaml
post_process:
  enabled: true
  shader:
    source: shaders/color_grade.frag
    params:
      tint: [0.94, 0.98, 1.04]
      brightness: -0.03
      contrast: 1.08
      saturation: 0.86
      strength: 0.72
```

Shader parameter names are defined by the shader, not globally by YAML. The
engine examples include a reusable
[colour-grade shader](https://github.com/nhorro/xadv2-engine/blob/develop/examples/_assets/shaders/color_grade.frag);
copy it into your game's `data/shaders/` directory before referencing it.

For that shader:

| Parameter | Neutral | Meaning |
|-----------|---------|---------|
| `tint` | `[1, 1, 1]` | Per-channel multiplier. Raise red for warmth or blue for coolness; make small changes. |
| `brightness` | `0` | Additive offset, conventionally `-1..1`. Adjust last and gently. |
| `contrast` | `1` | Contrast around mid-grey. Values above 1 separate tones; below 1 flatten them. |
| `saturation` | `1` | `0` is greyscale, `1` preserves the input, values above 1 exaggerate colour. |
| `strength` | `1` | Mix between original lit image (`0`) and the full grade (`1`). This is the safest master control. |

Do not use grading to repair a misplaced light. First tune ambient and direct
lights until the room reads correctly, then use the grade to unify it. Comparing
with `enabled: false` is useful, but the in-game tuner makes this faster.

## Step 7: tune in the running game

Enable authoring controls in `game.yaml`:

```yaml
development:
  edit_mode: true
```

Press **F9** in the normal room command view. The overlay replaces the SCUMM
panel and captures gameplay input while animation and rendering continue.

- **Ambient** adjusts colour and baseline intensity.
- **Lights** selects authored lights and edits enabled state, colour, intensity,
  range/cone geometry, height, and modulation.
- **Grading** edits post-process passes and their numeric/vector parameters.
- **View: Live/YAML** switches between the working values and the loaded file.
- **Reset** discards the working adjustments.
- **Copy YAML** or **Ctrl+C** copies complete `lighting:` and `post_process:`
  sections for pasting into the room file.

F9 changes are intentionally temporary and do not alter scripts, saves, or YAML.
Use the room editor for spatial placement, use F9 for final visual tuning, then
paste the exported YAML and reload the room with **F5**.

## Complete interior example

This stack creates cool window light, a warm practical lamp, coherent avatar
shadows, and a restrained final grade:

```yaml
post_process:
  enabled: true
  shader:
    source: shaders/color_grade.frag
    params:
      tint: [0.94, 0.98, 1.04]
      brightness: -0.03
      contrast: 1.08
      saturation: 0.86
      strength: 0.72

lighting:
  ambient:
    color: [0.70, 0.76, 0.88]
    intensity: 0.43
  lights:
    - id: window_daylight
      type: spot
      at: {x: 90, y: 210}
      range: 1050
      direction: 24
      angle: 72
      softness: 18
      color: [0.84, 0.92, 1.0]
      intensity: 0.68
    - id: desk_lamp
      type: omni
      at: {x: 760, y: 230}
      radius: 300
      color: [1.0, 0.74, 0.40]
      intensity: 0.82
  projected_shadows:
    source: window_daylight
    casters: all
    length: 0.58
    width: 0.76
    opacity: 0.24
    softness: 3.0
    contact_shadow: 0.60
    color: {r: 12, g: 15, b: 22}
```

## Recipe: exterior sunlight

An omni reads as a nearby point source, so use a distant broad spotlight for the
sun. Put it far outside the room; the room editor can scroll beyond the artwork:

```yaml
lighting:
  ambient: {color: [0.80, 0.86, 0.96], intensity: 0.56}
  lights:
    - id: afternoon_sun
      type: spot
      at: {x: -1800, y: -1700}
      range: 5200
      height: 2400
      direction: 40
      angle: 40
      softness: 7
      color: [1.0, 0.90, 0.72]
      intensity: 0.48
  projected_shadows:
    source: afternoon_sun
    casters: all
    length: 0.58
    width: 0.76
    opacity: 0.24
    softness: 3
    contact_shadow: 0.60
```

The distant origin makes rays and projected shadows appear approximately
parallel. Choose the side that agrees with the background's painted highlights.
For overcast art, raise ambient slightly and lower the sun contribution; do not
remove direction completely if character shadows still need a source.

## Optional advanced features

### Normal maps

A room-space tangent normal map lets surfaces respond differently according to
their apparent direction:

```yaml
lighting:
  normal_map:
    image: study_normals.png
    origin: {x: 0, y: 0}
    scale: 1
    strength: 0.7
```

- Flat/neutral is RGB `(128, 128, 255)`.
- Red points right, green points down, blue points out of the image.
- `origin` places the texture in room space.
- `scale` is its uniform world scale and must be positive.
- `strength` is `0..2`; start below 1.
- Light `height` affects the computed direction only when a normal map exists.

The map is sampled over the complete composition, including moving characters.
Keep the walkable character corridor close to flat unless you intentionally want
avatars to inherit the receiver surface. Normal maps are optional polish, not a
prerequisite for convincing 2D light.

### Scripted light changes

Lua can change an authored light without rebuilding the rendering stack:

```lua
light("desk_lamp"):disable()
light("window_daylight"):set_intensity(0.35, 0.8)
```

Overrides are transient and reset on room reload. Reapply persistent story
states from `on_load`. Modulation multiplies the current runtime intensity.
See the [light handle API](lua-api.md#scenery-light-and-ui).

## Tips for point-and-click rooms

- **Guide attention, do not hide required information.** A puzzle object may be
  in shadow, but its silhouette or local contrast should remain discoverable.
- **Keep exits readable.** Players build a mental map from doors and paths. Avoid
  putting a cone boundary or crushed black level across an important exit.
- **Light faces more than walls.** Check every NPC at its authored standing point
  and the player across the entire walkable polygon.
- **Match the painted art.** Runtime light should reinforce existing highlights
  and shadows. Contradicting them makes the scene feel cut out even when the math
  is correct.
- **Use warm/cool contrast gently.** A cool ambient plus warm practical is a
  reliable interior recipe, but pure blue/orange quickly becomes artificial.
- **Preserve contact.** Keep part of the small ellipse when projected shadows are
  enabled; it prevents feet from floating when the silhouette is long or soft.
- **Prefer one motivated key light.** Add fill only to restore playability. Several
  equally strong lights erase direction and flatten the scene.
- **Judge during movement.** A still screenshot cannot reveal cone crossings,
  camera scrolling, attachment offsets, modulation speed, or shadow rotation.
- **Compare at the final target resolution.** Thin penumbras and small colour
  differences can change after scaling.

## Performance guidance

- The lighting system accepts up to **eight visible lights** overlapping the
  camera. They share one full-scene shader pass; modulation adds no pass.
- Light occlusion accepts up to **32 enabled polygon edges**.
- Each enabled room post-process shader is another full-scene pass. One colour
  grade is inexpensive; stack effects only when they materially improve the room.
- Soft projected shadows use several silhouette draws. Keep softness and caster
  count modest, especially with `casters: all` in a crowded room.
- Per-drawable multi-pass shaders can be more expensive than the room-level
  grade because they run separately for each affected drawable.
- A normal map is evaluated in the existing lighting pass, but it adds a texture
  sample and more arithmetic per pixel. Use it where the effect is visible.

The defaults are intended for ordinary laptops. Optimize after measuring a real
room; do not remove a clear artistic benefit based only on the number of YAML
fields.

## Troubleshooting

| Symptom | Likely cause | Try |
|---------|--------------|-----|
| Everything became uniformly dark | Ambient is too low, or no direct light reaches the camera. | Raise ambient first; then inspect light range and enabled state. |
| A light seems to do nothing | Source art is already black, the light is off-camera/out of range, its attachment is missing, or illumination already clamps at 1. | Compare with F9, verify `at`/`attach`, lower ambient to expose the contribution, and inspect logs. |
| A spotlight has a visible hard triangle | Cone too narrow or `softness` too small. | Widen `angle`, increase softness, extend range, and lower intensity. |
| A circular bright patch is obvious | Omni radius is too small/intense for the intended fill. | Increase radius and reduce intensity, or move that contribution into ambient. |
| Character enters a dark band | Light boundary crosses the walkable area. | Test the full path; increase range or add restrained fill. |
| Projected shadow points the wrong way | Its source disagrees with the painted key light. | Move the source or reference the correct dynamic-light id. |
| Shadow draws over furniture | Avatar-relative depth cannot represent the floor receiver. | Give the shadow a fixed floor `z` below foreground props. |
| Room looks good ungraded but muddy after grading | Contrast, brightness, tint, or strength is too aggressive. | Return the grade to neutral values and add one adjustment at a time. |
| NPC and background do not feel integrated | Light direction/colour conflicts with the art, or the contact shadow is missing. | Fix contact first, then check the NPC at its real standing point under the final stack. |
| F9 changes disappear | The tuner uses a temporary working copy. | Copy YAML before closing, paste it into the room, and reload with F5. |

## Final checklist

- [ ] Background art retains colour/detail before runtime darkness.
- [ ] Ambient alone leaves navigation and silhouettes readable.
- [ ] Every direct light has a motivated visible or offscreen source.
- [ ] Spotlight direction follows screen coordinates (`0` right, `90` down).
- [ ] Player and every NPC were checked throughout their movement area.
- [ ] Contact shadows ground the feet.
- [ ] Projected shadows agree with the key light and stay subtle.
- [ ] Foreground props occlude avatars and shadows correctly.
- [ ] The grade unifies the scene without repairing lighting mistakes.
- [ ] F9 values were copied back into YAML and the room was reloaded.
- [ ] The scene was tested at target resolution and on representative hardware.

## References

- [Scenery authoring](scenery.md) — layers, depth, objects, NPCs, hotspots,
  walk-behinds, and the shorter lighting recipe.
- [xadv2 room editor](https://github.com/nhorro/xadv2-tools/tree/main/room_editor)
  — installation, startup, and editor modes.
- [Scenery authoring](scenery.md) — current room YAML examples and the rendering
  model behind layers, lighting, grading, and shadows.
- [Dynamic-light Lua handles](lua-api.md#scenery-light-and-ui) — runtime
  enable/intensity and occluder controls.
