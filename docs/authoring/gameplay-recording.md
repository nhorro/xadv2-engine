# Gameplay recording

The desktop runner can record a playthrough as semantic events rather than
video or raw mouse input. This is useful for measuring walkthrough timing,
reviewing dialog density, handing scene-entry sequences to an art reviewer, and
building automated drivers later.

Start a game with:

```bash
./build/mygame --record recordings/walkthrough.csv
```

The parent directory is created automatically. The file is truncated at the
start of the run and flushed after every event, so a partial recording remains
readable if the game or development build crashes.

## Schema

The output is UTF-8, semicolon-delimited CSV:

```text
timestamp;event_type;event_id;event_data
0.000;scene_enter;title;{}
3.417;scene_enter;room_view;{}
3.417;room_enter;archive;"{""scene"":""room_view"",""entry_point"":""""}"
6.208;action;look_at;"{""room"":""archive"",""param1_kind"":""room_object"",""param1"":""painting"",""param2_kind"":"""",""param2"":""""}"
6.233;speech;room.archive.painting.look;"{""speaker"":""player"",""room"":""archive"",""text"":""The varnish is cracked.""}"
```

`timestamp` is elapsed session time in seconds to millisecond precision. It uses
a monotonic clock, so changing the machine clock cannot reorder events.
`event_data` is a JSON object inside a normal CSV field. CSV quoting follows the
standard convention: a field containing a semicolon, quote, or newline is
wrapped in quotes and literal quotes are doubled. Use a CSV parser configured
with `;` as the delimiter, then parse `event_data` as JSON.

## Events

| `event_type` | `event_id` | `event_data` |
|---|---|---|
| `scene_enter` | Manifest scene id | `{}` |
| `room_enter` | Room id | Scene id and authored entry point |
| `action` | Verb id, `walk_to`, or `activate` | Room/scene and semantic operands; walks include world coordinates |
| `dialog_option` | Stable text id | Dialog id, node, visible index, room, and displayed option text |
| `speech` | Stable text id | Speaker id, room or scene, and the localized text shown to the player |

Only accepted room commands are recorded. A command that needs the character to
walk closer is recorded once when accepted, not again when it executes. This
makes action counts stable even when pathfinding delays execution. Direct floor
walks and close-up hotspot activations are also actions.

## Streaming and automation extension point

The recorder is also a small in-process event bus. A C++ game integration can
attach an observer during its application configure hook:

```cpp
#include "engine/core/application.hpp"
#include "engine/core/engine_context.hpp"
#include "engine/core/gameplay_recorder.hpp"
#include "engine/core/manifest.hpp"

pac::core::ApplicationHooks hooks;
hooks.configure = [](pac::core::EngineContext& ctx, const pac::core::Manifest&) {
    ctx.recorder.add_observer([](const pac::core::GameplayEvent& event) {
        // Serialize to a socket, test harness, or live analysis process.
    });
};
```

CSV output and observers can be active together. Input automation is
deliberately separate: it can submit language-independent room `Command`
objects through the existing command sink, while this event stream provides the
observable results.
