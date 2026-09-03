# SCUMM Command Flow

The SCUMM panel is a view/input component, not the command system.

As-built tour: [Point & click kit](tour/02-point-and-click.md).
Do not extract a third `CommandDispatcher` — that object is `RoomCommandProcessor`.

```text
RoomScene input router
  DialogWidget / ScummWidget  -> RoomUiIntent -> RoomScene::handle_ui_intent
  RoomScene scenery adapter   -> hotspot / walk
CommandController             -> Command (when builder reaches COMMAND_READY)
RoomCommandSink::submit       -> RoomCommandProcessor
  validate, walk to approach or chase moving bind
  dispatch Lua:
    1. inventory handler
    2. hotspot handler
    3. game.lua fallback
```

`ScummPanel` renders `CommandState` and emits panel intents. `ScummWidget`
adapts those to `RoomUiIntent`. Neither owns `CommandBuilder` or calls Lua.

`CommandController` owns the builder and preview text.
`RoomCommandProcessor` owns validation, approach/chase, and handler order.
`RoomScene` orchestrates and currently implements the host interfaces.
New command policy does not belong in the scene.

Navigation buttons (`OPEN_SETTINGS`, `PUSH_SCENE`) are not commands.
