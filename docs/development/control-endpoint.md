# Development control endpoint

The engine can expose an opt-in JSON-RPC endpoint for live authoring and
automation. It is currently supported on Linux desktop only. Android does not
compile the socket transport; Windows support may be added later.

Start a game with a fixed port:

```bash
./my-game --control-port 8765
```

Or pass `0` to let Linux select a free port. The engine logs the selected
`127.0.0.1` address. Without this flag no listener exists. The server never
binds a non-loopback interface.

The wire format is JSON-RPC 2.0, one JSON object per newline:

```json
{"jsonrpc":"2.0","id":1,"method":"engine.describe","params":{}}
```

The socket is nonblocking and polled by the application loop. Dispatch happens
on the engine thread, so Lua, scenes, SFML, and render state are never accessed
from a networking thread. Work and message sizes are bounded per frame.

## Services

`engine.describe` reports the services registered at that moment. Core provides:

- `engine.describe` and `engine.state`;
- `lua.call` for synchronous, non-yielding functions;
- `lua.eval` for synchronous development expressions;
- `lua.spawn` for yielding chunks and `lua.task_status` for polling them.

A live `RoomScene` additionally registers:

- `room.render.get`;
- `room.render.set_ambient`;
- `room.render.set_light`;
- `room.render.set_shadow`;
- `room.render.set_grade`;
- `room.render.reset`;
- `room.render.export_yaml`.

Scene registrations are scoped. Leaving the room unregisters `room.*`, so the
router never retains a pointer to a destroyed scene.

Room YAML remains the authored source. Endpoint and Lua changes alter one shared,
session-scoped `RoomRenderState`; reset or room reload restores the authored
snapshot. `export_yaml` returns a snippet but does not write project files.

## Python and Jupyter

[`control_client.py`](../../examples/tools/control_client.py) uses only the Python
standard library. The example
[`room_lighting_control.ipynb`](../../examples/tools/room_lighting_control.ipynb)
adds an `ipywidgets` presentation whose implementation lives in
[`room_lighting_widgets.py`](../../examples/tools/room_lighting_widgets.py).

The widget UI is optional and needs Jupyter plus ipywidgets:

```bash
python -m pip install jupyterlab ipywidgets
cd examples/tools
jupyter lab room_lighting_control.ipynb
```

The panel exposes each light's enabled state, colour, intensity, position,
range, virtual height, and—where applicable—spot direction, cone angle, and edge
softness. Attached lights report their attachment and keep the static-position
controls disabled; placement of their offset remains an editor/YAML operation.

```python
from control_client import ControlClient

client = ControlClient(port=8765).connect()
client.call("room.render.set_light", {"id": "spot4", "intensity": 0.5})
client.lua_eval("return current_room()")
```

The endpoint permits arbitrary calls into the game's Lua API. Enable it only for
trusted local development sessions.

When driving a game from an external editor or notebook, keep the desktop
simulation running while that tool has focus with:

```yaml
development:
  pause_on_focus_loss: false
```

The setting defaults to `true`, does not disable manual pausing, and is ignored
on Android because its rendering surface is unavailable while unfocused.
