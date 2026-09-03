# Architecture tour

Do **not** start with `RoomScene`. Do **not** start with the historical design
pages (M4, issue numbers). Start with the few abstractions every later flow
reuses, then learn one vertical slice at a time.

The pages under [`../design/`](../design/00-index.md) are [archived](../history/README.md).
This tour is the as-built map. The destination model — P&C as a kit on core 2D —
is in [target architecture and debt](target-and-debt.md).

---

## The rule

One idea per document:

- A **concept chapter** names types, ownership, and invariants. No feature history.
- A **flow chapter** is one sequence: diagram, collaborating types, pitfalls for
  that sequence only.

If a chapter needs more than about 15 types, split it.

---

## Reading order

```
0. This map
1. Core abstractions          ← start here
2. Lua: state, bindings, coroutines        (to write)
3. Scenes and the stack                    (to write)
4. Display, coordinates, input             (to write)
5. Resources and packaging                 (to write)
6. Generic 2D: sprites and ScriptScene     (to write)
7. Point & click model
8–9. Room session + SCUMM composer         (folded into 7 for now)
10. Rendering, z-sort, shaders             (to write)
11. Lighting                               (to write)
12. Persistence and chapters               (to write)
13. Platform (SFML / Android)              (to write)
14. How a game plugs in (Fuera de Cuadro)  (see target-and-debt)
```

Chapters 1–5 are required before editing engine code.
Chapter 7 is required before editing verbs, rooms, or the panel.
Chapters 10–11 before editing a `.frag` or a light in YAML.

Written today:

- [1 — Core abstractions](01-core-abstractions.md)
- [Point & click kit](07-point-and-click.md)
- [Target architecture and debt](target-and-debt.md)

---

## Why this order

A developer who opens `room_scene.cpp` first sees lighting RTs, Lua usertypes,
pause menus, and footsteps in one file and concludes the architecture is “the
scene class.” The engine is:

```
Game  →  Scene stack  →  EngineContext services
                ↑
         one Lua state + scopes
                ↑
         logical resources
                ↑
         virtual coordinates / Display
                ↑
         SFML window
```

Point-and-click is a kit on top of that. Lighting is a pass on the room
renderer. Neither is a third engine.

---

## Three-day onboarding

**Day 1 — core.** Read chapter 1. Build the engine. Run `01_hello_room` and
`07_script_scene`. Trace `run()` → `enter()` once in a debugger.

**Day 2 — genre kit.** Read the [P&C guide](07-point-and-click.md). Run
`02_scumm_inventory` with `edit_mode` and F4. Write a headless test that
`submit`s `LOOK_AT` without a mouse event.

**Day 3 — product.** Skim Fuera de Cuadro `src/game.cpp` and one chapter
`chapter.yaml`. Follow `talk` from Lua to `SpeechManager`. Do not start a
feature on day 3.

---

## PR rule

Before a change lands, the author should answer:

1. Is this a core/gfx capability, a P&C policy, or a game feature?
2. If it is a capability, does it live below `pnc`?
3. Does it add methods to `RoomScene`? If yes, the default is reject.

Details: [target architecture and debt](target-and-debt.md).
