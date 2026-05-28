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

This builds `pac_engine` + `pac_themummy` (Release) inside an Ubuntu 24.04 image
that mirrors CI, and runs the test suite. The compile also happens at image-build
time, so the first run is the slow one; later runs reuse the cached image.

## Run the sample game (needs X11)

The game opens a window, so the container has to reach the host's X server. On a
Linux host:

```bash
xhost +local:                 # authorize local containers (run once per session)
docker compose run --rm engine
# ... when done, optionally revoke: xhost -local:
```

`docker-compose.yml` shares `/tmp/.X11-unix` and forwards `$DISPLAY`. Override the
command to run a different manifest or a headless smoke:

```bash
docker compose run --rm engine ./build/games/themummy/pac_themummy \
    games/themummy/data/game.yaml --frames 5
```

> Wayland-only hosts: run via XWayland (the default on most desktops) or set
> `DISPLAY` to your XWayland socket. macOS/Windows hosts need an X server
> (XQuartz / VcXsrv) and a TCP `DISPLAY`.
>
> Audio is not wired into the container, so the game runs silently and SFML logs
> harmless `OpenAL` warnings on exit. Map a sound device (e.g. PulseAudio) if you
> need audio.

## Room editor (browser tool)

```bash
docker compose up room-editor
# then open http://localhost:8000
```

It serves the sample game's rooms (`games/themummy/data/rooms`, bind-mounted from
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
