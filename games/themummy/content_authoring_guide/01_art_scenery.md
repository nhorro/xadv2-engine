Content authoring guide - Scenery
=================================

## The room skeleton

### Room definition (.yaml)

See [Room section](../../../docs/sources/design/06-data-formats.md)

### Room scripting (.lua)

See [Lua game](../../../docs/coding-guide/lua-game.md)

## Editing rooms

Rooms are edited visually with the **room editor**, a small web tool that loads a
room's `.yaml`, lets you draw and drag the parts of the scene over the actual
artwork, and writes the changes back. You never have to hand-edit coordinates.

### Starting the editor

From the repository root:

```bash
# Open a specific room
PYTHONPATH=tools python3 -m tools.room_editor serve --room games/themummy/data/rooms/study.yaml

# Or start without a room and pick one in the UI (point it at the rooms folder)
PYTHONPATH=tools python3 -m tools.room_editor serve --base-path games/themummy/data/rooms
```

Then open <http://127.0.0.1:8000/> in a browser. Use the **Room file** dropdown
(top-left) to switch between the rooms in that folder without restarting, and
**Save** to write your changes back to the `.yaml`. Full options are in the
[room editor README](../../../tools/room_editor/README.md).

The **Mode** dropdown chooses what you're editing — `walkable`, `obstacles`,
`zones`, `regions`, `hotspots`, `points`, `layers`, or `preview`. This chapter
covers the scenery side; the rest (where the player can walk, what is clickable)
lives in the geometry modes.

## Background layers

A room background is **a stack of images (layers)**, not one flat picture. Each
layer is drawn at a depth (`z`) so the player and other characters can pass
*between* layers — that's how a character walks behind a wall but in front of the
floor. Use layers for the back wall, scenery, parallax backdrops, and **furniture
that the player should be able to walk behind or in front of**.

> The exhaustive list of layer fields is in
> [Data formats § background.layers](../../../docs/sources/design/06-data-formats.md).
> This section is about *doing it* in the editor.

### Placing and moving a layer

1. Switch **Mode** to `layers`.
2. Click a layer in the canvas (or in the **Background** list) to select it. The
   selected layer gets a dashed outline.
3. Drag its body to reposition it, or type exact coordinates in **x / y** and
   click **Apply pos**. The x/y is the layer's top-left corner in room space.

A full-room background normally sits at the origin `(0, 0)`; a piece of furniture
is placed wherever it stands.

### Resizing a layer (a development aid)

Sometimes a furniture image comes in at the wrong size while you're blocking out a
room. With the layer selected in `layers` mode, **drag any corner handle** to
resize it.

- Resizing is **always aspect-locked** — the image is never stretched or
  squashed, only made uniformly bigger or smaller.
- It scales about the **base** (the bottom-centre, marked with an orange
  triangle), so a piece of furniture stays planted on the floor instead of
  sliding around as you resize.
- You can also type an exact **scale** (`1` = native size) or click **Native** to
  snap back.

**In finished art, layers should ship at their correct native size (`scale 1`).**
Resize is for blocking things out, not a substitute for exporting the art at the
right size — so once a piece is final, set it back to Native.

## Getting depth right (z-order)

Everything in the scene — layers, regions, objects, and characters — is drawn back
to front by a single number, **`z`**. Larger `z` is **nearer the camera** and is
drawn on top. Characters sort by **their feet** (the point they stand on), so:

> A character draws **in front of** a layer when its feet are *nearer the camera*
> (lower on screen) than the layer's depth line, and **behind** it otherwise.

The editor shows that threshold while you edit. With a layer selected in `layers`
mode you'll see a **faint teal dotted horizontal line** labelled `z …` across the
room. That line is the layer's depth: a character whose feet are *below* the line
will pass in front of the layer, *above* it will be hidden behind it.

For furniture you almost always want the depth line to sit on the **floor where
the piece stands**. Select the layer and click **z = base** — it sets `z` to the
bottom of the piece (which, because resizing keeps the base fixed, lands right on
the orange base marker). Adjust by hand in the **z** field if you need to nudge it.

## Furniture and perspective

Here is the important catch. The depth line is a **single horizontal line**, but a
piece of furniture drawn in perspective doesn't meet the floor along a flat line —
think of a long table or counter receding into the room. The "in front / behind"
question really depends on *where along the piece* the character is standing, and
one horizontal line can't capture that.

There's a deeper reason not to fight this with a cleverer line: the engine draws a
**whole image** either in front of or behind a character. So even a perfect
slanted line could never make a table hide a character's legs while leaving the
torso visible above the tabletop. That kind of partial occlusion needs the art to
be **split into pieces** — and once you split, each piece's contact with the floor
is short and nearly flat, so the simple depth line works again.

**The pattern for any furniture a character can walk around:**

1. **Back part** → a layer at a *low* `z`, drawn behind characters (the body of
   the table, the wall the shelf hangs on).
2. **Front part** → a separate **occluder** layer: just the parts that should hide
   a character standing behind the piece (the front legs, the front edge / lip,
   the front of a counter). Export it with everything above transparent, so a
   character's upper body still shows through. Set its depth with **z = base** so
   it sits on the floor line where that front edge meets the ground.
3. A character then naturally appears *between* the two: hidden by the front piece
   when standing behind, drawn over it when standing in front.

For a long piece that recedes a lot (a bar, a banister, a row of columns), **split
the front occluder into 2–3 segments** along its length and give each its own
`z = base`. Each short segment has an almost-flat floor line, so the depth stays
correct as the character walks past it. This is the same approach classic
point-and-click engines use, and it keeps every depth decision a simple number the
engine can sort reliably.

> If you ever hit a piece where splitting genuinely can't give a good result,
> raise it — a per-occluder slanted baseline is a possible future engine feature,
> but it's deliberately not built yet because splitting covers the real cases and
> keeps depth predictable.

## Perspective (scaling characters by depth)

In a front-facing room the floor recedes into the distance, so a character should
look **smaller as they walk toward the back** and **larger toward the front**. The
room defines this with a `perspective` block — two floor lines, each a world-space
`y` and the character scale at that line:

```yaml
perspective:
  top:    { y: 380, scale: 0.70 }   # back of the floor → characters at 70%
  bottom: { y: 700, scale: 1.15 }   # front of the floor → characters at 115%
```

The engine reads each character's **feet** (their walking pivot) and interpolates
the scale between the two lines as they move. Above `top` or below `bottom` the
scale just holds at that line's value (it never keeps shrinking/growing off the
ends). Because characters scale about their feet, they stay planted on the floor —
they don't drift or sink as the size changes. This applies to **every** character
in the room, the player and NPCs alike.

**How to choose the two lines:**

1. Pick a `y` near the **back** of the walkable area and a `y` near the **front**.
   The room editor (`points` or `walkable` mode) shows world coordinates as you
   move vertices, so you can read sensible y values straight off the floor.
2. Set `top.scale` smaller and `bottom.scale` larger. Start near the size the
   character looks right at mid-floor and spread out from there — e.g. `0.7` /
   `1.15` — then run the game and adjust by eye.
3. Leave `perspective` out entirely if the room is flat / top-down or the depth is
   shallow enough that constant size looks fine. Without it, characters render at
   their normal size everywhere.

> There's no dedicated perspective tool in the room editor yet — you set the two
> lines by editing the room `.yaml` directly. The exhaustive field reference is in
> [Data formats § room](../../../docs/sources/design/06-data-formats.md).
