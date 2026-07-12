# Docker environments

Containerized environments to build/run the engine and the authoring tools on
Linux without installing the toolchain on the host (issue #125). Everything is
driven from `docker-compose.yml` at the repo root.

| Service | Purpose | Needs |
|---------|---------|-------|
| `engine` | Build + run the sample game | X11 display |
| `engine-test` | Build + run the headless doctest/CTest suite | — |
| `room-editor` | Web-based room YAML editor | a browser |
| `tools` | Interactive shell with the Python tools + repo mounted | — |

Prerequisites: Docker Engine and the Compose plugin (`docker compose version`).

## Build and test the engine (headless)

```bash
docker compose run --rm engine-test
```

This builds `pac_engine` + the examples (Release) inside an Ubuntu 24.04 image
that mirrors CI, and runs the test suite. The compile also happens at image-build
time, so the first run is the slow one; later runs reuse the cached image.

## Run the sample game (needs X11)

The game opens a window, so the container has to reach the host's X server. On a
Linux host this works with no prep:

```bash
docker compose run --rm engine
```

`docker-compose.yml` shares `/tmp/.X11-unix`, forwards `$DISPLAY`, and mounts the
host's X auth cookie (`$XAUTHORITY`, falling back to `~/.Xauthority`) at a fixed
path with `XAUTHORITY` pointing at it — so the container authenticates without
`xhost`. If your cookie can't be found or doesn't match (some remote/SSH or
unusual setups), authorize local connections instead:

```bash
xhost +local: && docker compose run --rm engine   # revoke later: xhost -local:
```

Override the command to run a different manifest or a headless smoke:

```bash
docker compose run --rm engine ./build/examples/01_hello_room/pac_example_01_hello_room \
    examples/01_hello_room/data/game.yaml --frames 5
```

> Wayland-only hosts: run via XWayland (the default on most desktops) or set
> `DISPLAY` to your XWayland socket. macOS/Windows hosts need an X server
> (XQuartz / VcXsrv) and a TCP `DISPLAY`.

### Audio

The `engine` service **targets a desktop host**. It shares the host's
**PulseAudio / PipeWire** socket (`$XDG_RUNTIME_DIR/pulse/native`, via
`PULSE_SERVER`) and the pulse cookie (`~/.config/pulse/cookie`, via
`PULSE_COOKIE`) so the game has sound out of the box — no extra flags. The cookie
is what classic PulseAudio (e.g. Ubuntu 20.04 / 22.04) needs to authenticate;
PipeWire's pulse-compat server ignores it, so the same config works on both. The
image ships `libpulse0` so OpenAL's PulseAudio backend loads (it dlopens
libpulse).

> **No pulse on the host?** Compose has no "mount only if it exists", so the bind
> mounts are unconditional: on a host *without* the pulse socket / cookie Docker
> would create **root-owned empty directories** at those source paths (and audio
> would still end up silent). So this is not a graceful fallback. On a bare /
> headless host, either use the `engine-test` service (no host mounts), or remove
> the pulse `environment` + `volumes` lines from the `engine` service to run the
> game silently. For ALSA instead of pulse, drop those lines and expose the sound
> devices: `docker compose run --rm --device /dev/snd engine`.

## Room editor (browser tool)

```bash
docker compose up room-editor
# then open http://localhost:8000
```

It serves an example's rooms (`examples/01_hello_room/data/rooms`, bind-mounted from
the host so edits persist). Pick a room from the dropdown and edit. Point it at a
different folder by overriding the command:

```bash
docker compose run --rm --service-ports room-editor \
    python3 -m tools.room_editor serve --base-path games/othergame/data/rooms \
    --host 0.0.0.0 --port 8000
```

## Other tools (interactive shell)

```bash
docker compose run --rm tools
```

Drops you into a shell at `/work` (the repo, mounted live) with Python, PyYAML,
numpy, and OpenCV available. Examples:

```bash
# Pack a spritesheet
python3 tools/spritesheet_packer/pack_spritesheet.py --help

# Chroma-key a frame sequence
python3 tools/chromakeylab/chroma_key_tuner.py --help

# Avatarmaker (browser UI) — publish its port when launching the shell:
#   docker compose run --rm --service-ports tools
python3 tools/avatarmaker/avatar_composer.py \
    --atlas <atlas.yml> --animation <anim.yml> --host 0.0.0.0 --port 8766
```

The containerized OpenCV is the **headless** wheel, so the interactive `cv2`
preview windows (the chroma tuner GUI) are unavailable in the container — run
that one on the host, or build a GUI-enabled tools image with X11 like `engine`.

## Notes

- The engine image compiles into `/work/build`; the host's own `build*` dirs are
  excluded from the build context by `.dockerignore`, so a host (e.g. Fedora)
  build never leaks into the Linux image.
- Rebuild after changing dependencies or the Dockerfiles:
  `docker compose build engine room-editor`.
