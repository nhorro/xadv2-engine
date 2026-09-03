# Architecture tour

This tour has five jobs:

1. **Drivers from now on** — platform-agnostic games; a general 2D engine plus
   optional kits (point-and-click); Lua strong enough that a simple game needs
   no kit; kits make complex games efficient and still YAML/Lua.
2. **Onboard a new developer** — concepts and pitfalls, not `room_scene.cpp` first.
3. **Support refactors** — interfaces, dependencies, sequences, diagrams.
4. **Navigate the current tree** — name the code that implements each concept.

Historical pages under [`../history/design/`](../history/design/00-index.md) are
[archived](../history/README.md).

---

## Read in this order

| Doc | Job |
|-----|-----|
| [Design drivers](00-drivers.md) | Rules for new work. Start here. |
| [1 — Core abstractions](01-core-abstractions.md) | `Game`, `Scene`, loop, resources, Lua preview. |
| [Point & click kit](02-point-and-click.md) | Commands, session vs widgets, sequences, pitfalls. |

Proposed changes are kept separately in [plans and technical debt](../plans/target-and-debt.md).

```mermaid
flowchart TB
  D[Drivers] --> C[Core abstractions]
  C --> Kit[P&C kit]
  C --> Lua[Current Lua API]
```

---

## Three-day onboarding

**Day 1.** Drivers + core. Build the engine. Run `01_hello_room` and
`07_script_scene` (the no-kit path).

**Day 2.** P&C kit. Run `02_scumm_inventory` with `edit_mode` and F4. Submit a
`LOOK_AT` in a headless test.

**Day 3.** One FdC room YAML+Lua pair. Follow `talk`. No feature work.

---

## PR rule

1. Capability, kit policy, or game feature?
2. If a capability, does it live below `pnc` and have a Lua surface (or a ticket)?
3. Does it add methods to `RoomScene`? Default is reject.
