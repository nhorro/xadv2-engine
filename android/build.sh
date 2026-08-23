#!/usr/bin/env bash
# Build an engine example or any game implementing the standard pac::game target.
set -euo pipefail

android_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)

if (( $# > 1 )); then
    echo "Usage: ./android/build.sh [path/to/game]" >&2
    exit 2
fi
if (( $# == 1 )); then
    game_dir=$(cd -- "$1" && pwd)
    if [[ ! -f "$game_dir/CMakeLists.txt" || ! -f "$game_dir/data/game.yaml" ]]; then
        echo "Game must contain CMakeLists.txt and data/game.yaml: $game_dir" >&2
        exit 1
    fi
    export PAC_ANDROID_DATA_DIR="$game_dir/data"
fi

exec "$android_dir/build-android.sh"
