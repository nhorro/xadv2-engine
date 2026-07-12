#!/usr/bin/env bash
#
# Run {{title}} from the repo root, so that data/ resolves.
#
#   ./run.sh                       # play
#   ./run.sh --frames 5 --shot out.png   # headless smoke + screenshot
set -Eeuo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

BIN=./build/{{short_name}}
if [[ ! -x "$BIN" ]]; then
    echo "not built. Configure and build first:" >&2
    echo "  cmake -S . -B build -DXADV2_ENGINE_DIR=path/to/xadv2-engine" >&2
    echo "  cmake --build build -j\"\$(nproc)\"" >&2
    exit 1
fi

exec "$BIN" "$@"
