#!/usr/bin/env bash
#
# Run one of the examples. Default: 01_hello_room.
#
#   ./run-game.sh                     # the hello-room example
#   ./run-game.sh 03_dialog_npc       # any other example
#   ./run-game.sh 05_closeup --frames 60 --shot out.png
#
# The examples take their data from examples/<name>/data, so run this from the
# repo root. Your own game lives in its own repository and links the engine as a
# library — see docs/authoring/building-a-game.md.
set -Eeuo pipefail

EXAMPLE="${1:-01_hello_room}"
shift || true

BIN="./build/examples/${EXAMPLE}/pac_example_${EXAMPLE}"
if [[ ! -x "$BIN" ]]; then
    echo "not built: $BIN" >&2
    echo "available:" >&2
    ls -1 examples | grep -E '^[0-9]' | sed 's/^/  /' >&2
    exit 1
fi

exec "$BIN" "examples/${EXAMPLE}/data/game.yaml" "$@"
