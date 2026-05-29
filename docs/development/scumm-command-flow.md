# SCUMM Command Flow

The command system follows the design rule that the SCUMM panel is a view/input
component, not the command system itself.

```text
RoomScene input hit testing -> CommandController -> CommandState -> ScummPanel
ScummPanel command UI intents ------^
ScummPanel navigation intents -> SceneManager overlays
CommandController completed command -> RoomScene movement + Lua dispatch
```

## Responsibilities

`ScummPanel` renders `CommandState` and translates panel clicks into UI intents:
verb selection, inventory item selection, inventory page changes, and independent
navigation intents such as opening Settings. It does not inspect room hotspots,
own `CommandBuilder`, execute Lua handlers, or decide whether a command is valid.

`CommandController` owns `CommandBuilder` and the authoritative `CommandState`.
It receives UI intents and room-object events, resolves operand metadata through
its host, computes the command preview text, and returns a completed
language-independent `Command` when the builder reaches `COMMAND_READY`.

`RoomScene` remains the orchestrator. It maps physical input to either panel
space or room/world space, emits the corresponding controller input, handles
walk-to-approach behavior, and dispatches completed commands to the current room,
inventory behavior, or global `game.lua` fallback.

Navigation controls in the panel are not command controls. For example, the
Settings button emits `OPEN_SETTINGS`; `RoomScene` consumes it and asks
`SceneManager` to push the configured Settings overlay without touching
`CommandController`.

## Current Boundary

This first refactoring keeps movement and Lua dispatch in `RoomScene` so behavior
stays equivalent. The next natural extraction is a `CommandDispatcher` that owns
the existing handler precedence:

1. First operand inventory handler.
2. Second operand hotspot handler.
3. Global `game.lua` fallback.

That dispatcher should still consume completed `Command` values, not SCUMM panel
state or UI strings.
