# Tools

Offline, web- and CLI-based helpers for preparing game assets. They are
deliberately minimal — enough to get usable assets quickly. The runtime itself is
a native SFML app; these tools only run at authoring time.

| Tool | What it does | Status |
|------|-------------|--------|
| [Chroma key lab](chromakeylab.md) | Turn a chroma-key / solid background into real alpha (notebook + interactive tuner). Auto-detects the key, preserves interior chroma via border flood-fill, reports residual halos, crops to content. | Useful |
| [Spritesheet packer](spritesheet-packer.md) | Detect sprites in a messy generated sheet, pack them into a clean atlas + YAML. | Useful |
| [Room editor](room-editor.md) | Web editor for room geometry, points, zones, and background layers. | Useful |
| [Avatar maker](avatar-maker.md) | Experimental composer for composite (body + head + props) avatars. | Experimental |

!!! info "Why these exist"
    Image generators (ChatGPT and other diffusion models) don't produce real
    transparency and don't respect a fixed sprite grid — output is noisy, has
    artifacts, and is misaligned. The chroma key lab and spritesheet packer turn
    that raw output into engine-ready assets.

The canonical, always-current source for each tool is its `README.md` in the
repository under
[`tools/`](https://github.com/nhorro/xadv2-engine/tree/develop/tools). These pages
summarize each tool and will grow into task-oriented walkthroughs.
