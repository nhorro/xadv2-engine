# 07 — A scriptable scene

**Shows:** a generic 2D scene with no point-and-click assumptions. YAML instances
a static sprite and an animated sprite; Lua polls movement keys, receives
normalized pointer events, and changes entity components through stable handles.

```bash
./build/examples/07_script_scene/pac_example_07_script_scene
```

Move with **WASD** or the arrow keys. Click to place the marker. Press **Esc** to
quit.

Read `data/scenes/play/scene.yaml` for the declarative entity registry, then
`scene.lua` for the lifecycle, input, and fixed-step update callbacks. The scene
uses engine animation, resource caching, virtual coordinates, scene transitions,
state, and Lua tasks without depending on room, avatar, verb, or hotspot code.

This first version deliberately keeps the registry small. Its YAML sections are
component-shaped and Lua accesses entities by id, leaving room for runtime
creation and an ECS-style implementation later without rewriting scene scripts.
