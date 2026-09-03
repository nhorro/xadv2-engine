# 1. Core abstractions

Audience: a developer who has compiled the engine and has not yet opened `RoomScene`.
Companion: [tour index](index.md).

XADV2 is a C++20 library. A game is a separate executable that links `pac::engine`, ships YAML + Lua + assets, and optionally registers extra `Scene` types. The engine process is always the same: build services, bind core Lua, construct the manifest’s entry scene, run a fixed-timestep loop.

---

## 1.1 Layers

```
Game          manifest, assets, optional C++ scenes
Point & Click pac::pnc     rooms, verbs, dialog, SCUMM widgets
Generic 2D    pac::gfx     sprites, ScriptScene, shader chain
Core          pac::core    loop, Scene, resources, Lua, audio
              pac::geom    polygons, pathfinding
```

A layer may depend only downward. `engine/core` must never include `engine/pnc`. Folders enforce the rule; there are not separate shipped libraries.

If you need “just a 2D scene with Lua,” you want `gfx::ScriptScene`, not a room. If you need verbs and walking, you want `pnc`. Custom minigames in a game repo subclass `core::Scene`.

---

## 1.2 Process ownership

```
main
  parse_run_options           --frames, --shot, --pak
  pac::game::create()         game composition (scenes + hooks)
  run_game / run_from_resources
      load manifest
      construct services
      bind_core_api           Lua globals that are not genre-specific
      hooks.configure         game binds Notebook, Map, …
      factory.make(entry)
      window + letterbox
      loop until quit
```

`run` reads a host `game.yaml`. `run_from_resources` takes a `ResourceSource` (pak, Android assets). Gameplay code never cares which backend supplied the bytes.

`ApplicationHooks`:

- `configure(EngineContext&, Manifest&)` — after core Lua, before the first scene.
- `update(dt)` / `draw(target)` — after the scene stack, every frame (FdC uses this for toasts and opening titles).

The derived `Game` object lives for the whole run. Anything a hook captures must be owned by that `Game`, not by a scene.

---

## 1.3 EngineContext

Services are constructed once and borrowed:

```
Display, ResourceCache, Audio, Scripting, StateStore,
Settings + SettingsStore, SceneManager, Strings, Localization,
Diagnostics, DevFlags, SaveService, Cursor, Thumbnail, SpeechConfig
```

Scenes receive `EngineContext&` at construction. They must not extend service lifetime or hand service pointers to background threads.

Treat the context as an explicit bag, not a place to hide new singletons. Genre objects (`DialogRuntime`, `CommandBuilder`) should take the two services they need, not the whole bag.

---

## 1.4 Scene

A **scene** is a top-level application state listed in the manifest (`type: TitleScreen`, `RoomScene`, `CloseUp`, `Notebook`, …).

```
enter()           become active / pushed
leave()           replaced or popped
handle_event()    focused scene only
update(dt)        focused scene only, fixed 60 Hz
draw(target)      every scene on the stack, bottom to top
opaque()          if true, scenes below are not drawn
enter_pause_menu / leave_pause_menu / pause_menu_active
wants_thumbnail() RoomScene COMMAND frames only, for save slots
prepare_for_application_exit()
```

`SceneFactory` maps the YAML `type` string to a constructor. Built-ins register through `pac::pnc::register_builtin_scenes`. A game adds more in `pac::game::create()`.

`SceneManager` operations:

| Call | Effect |
|---|---|
| `goto_scene(id)` | Replace the stack (can fade to black, swap at black, fade in). |
| `push_scene(id)` | Overlay. No fade. Room beneath stops updating. |
| `pop_scene()` | Remove the overlay. |

Only the top scene gets input and `update`. Lua’s scheduler still runs globally, which is why a close-up script may call `set_state` on the frozen room but must not `avatar:move_to` (the mover is not ticking).

A **room** is not a scene. One `RoomScene` loads many rooms. That distinction is load-bearing for saves and for script scopes.

---

## 1.5 Time and the frame

Fixed timestep `dt = 1/60`. Wall clock accumulates; a slow frame runs several updates (capped). Draw may happen more often than update on a 144 Hz display.

```
poll events
  window → virtual (Display)
  top scene.handle_event
while timestep remaining:
  top scene.update(dt)
  hooks.update(dt)
  scripting.update(dt)          resume ready coroutines
draw stack bottom → top
hooks.draw
fade quad if any
apply cursor
maybe capture thumbnail
swap
```

Gameplay that must be deterministic belongs in `update`, not in `draw`. `draw` is `const` on scenes for that reason (renderer caches are `mutable`).

---

## 1.6 Coordinates

Three spaces:

```
world --camera--> virtual --letterbox--> window
```

- **Virtual** is the authored resolution (`resolution:` in the manifest). All UI and pointer events above `Display` use it.
- **World** exists only when a scene has a camera (rooms). Hotspots and walkboxes are world.
- **Window** is OS pixels. Game code does not construct window-space points.

Letterbox bars are not part of virtual space. A pointer on a pillarbox is not a gameplay click.

---

## 1.7 Resources

Every asset is a **logical path** (`rooms/plaza.yaml`, `shared/fonts/….ttf`). No `std::filesystem` paths in scenes or scripts.

```
ResourceSource          exists / read_text / read_bytes / list
  FilesystemResourceSource     authoring
  PackResourceSource           resources.pak (XOR-obfuscated TOC)
ResourceCache           logical path → Texture, Font, SoundBuffer, Shader
```

Fonts and music must outlive the `sf::` objects that reference their bytes; the cache is the owner. Missing assets fail loudly in development.

---

## 1.8 Lua (preview; full story is chapter 2)

One `lua_State`, owned by `Scripting`, wrapped by sol2. Nothing else creates a state.

- Engine → script: `sol::protected_function`. Errors log; they do not abort the frame.
- Script → engine: snake_case globals (`play_music`, `set_state`, …). Genre APIs (`talk`, `change_room`) are bound by the live room, not by `bind_core_api`.
- Handles (`avatar(id)`) store an **id**, never a C++ pointer.
- `spawn` / `wait` / `wait_event` / `emit` are the coroutine API. Every task belongs to a **scope**. Cancelling a scope drops tasks without running Lua cleanup. Cleanup is `on_unload` / dialog `on_exit`.

Saved games never resume coroutines. They restore `GameState` and run `on_load` again.

---

## 1.9 Persistence (preview; full story is chapter 12)

If it must survive a process death, it lives in an engine store:

- `StateStore` — `get_state` / `set_state` (and room-scoped variants)
- inventory, region states, dialog `once` flags
- current scene + current room + player pose

`GameState` is that snapshot. `SaveService` writes YAML slots. Lua locals are transient on purpose.

---

## 1.10 Invariants to memorize

1. Services outlive every scene; scenes borrow.
2. Only the top scene updates; the Lua scheduler still runs.
3. Logical paths only, above `ResourceSource`.
4. Virtual coordinates only, above `Display`.
5. One Lua state; handles are ids; scopes own tasks.
6. Layers never include upward.
7. Standard games need no game C++. Custom scenes register in `create()`, they do not fork the engine.

When a later chapter (lighting, SCUMM, FdC notebook) feels like it is inventing a second engine, come back here. It should be a client of these types.
