#!/usr/bin/env bash
#
# Prove that the engine is actually consumable as a library, both ways a game can
# consume it. This is the guard on `cmake --install` + `find_package(pac_engine)`:
# without it, the export breaks silently and nobody notices until someone tries to
# start a game.
#
#   ./scripts/check-packaging.sh
#
# 1. INSTALLED mode: install the engine to a throwaway prefix, then configure and
#    build packaging/consumer_smoke against it via find_package. Covers both a
#    data-only game (pac::engine) and one with custom C++ scenes (+ pac::sol2,
#    which needs sol2's headers to have shipped).
# 2. SOURCE mode: a game that add_subdirectory()s the engine checkout. Covers the
#    PROJECT_IS_TOP_LEVEL guards — the engine must NOT drag its examples, tests
#    and tests/examples into the game's build.
set -Eeuo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK="${WORK:-$(mktemp -d -t xadv2-packaging-XXXXXX)}"
PREFIX="$WORK/prefix"
BUILD="$WORK/engine-build"

cleanup() { [[ -n "${KEEP:-}" ]] || rm -rf "$WORK"; }
trap cleanup EXIT

echo "==> building + installing the engine to $PREFIX"
cmake -S "$ROOT_DIR" -B "$BUILD" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DPAC_BUILD_TESTS=OFF -DPAC_BUILD_EXAMPLES=OFF > /dev/null
cmake --build "$BUILD" -j"$(nproc)" > /dev/null
cmake --install "$BUILD" > /dev/null

echo "==> installed layout"
find "$PREFIX" -maxdepth 3 -mindepth 1 \
    \( -name 'pac_engine*' -o -name 'engine' -o -name 'sol' -o -name '*.a' -o -name '*.so*' \) \
    -printf '    %P\n' | sort

echo "==> INSTALLED mode: find_package(pac_engine) from an external project"
cmake -S "$ROOT_DIR/packaging/consumer_smoke" -B "$WORK/consumer" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$PREFIX" > /dev/null
cmake --build "$WORK/consumer" -j"$(nproc)" > /dev/null
echo "    built: $(ls "$WORK"/consumer/consumer_smoke "$WORK"/consumer/consumer_smoke_cpp | wc -l)/2 consumers"

echo "==> SOURCE mode: add_subdirectory(<engine>) from an external project"
mkdir -p "$WORK/src-mode"
cp "$ROOT_DIR/packaging/consumer_smoke/main.cpp" "$WORK/src-mode/"
cat > "$WORK/src-mode/CMakeLists.txt" <<EOF
cmake_minimum_required(VERSION 3.16)
project(pac_source_mode_smoke LANGUAGES CXX)
add_subdirectory("$ROOT_DIR" xadv2-engine)
add_executable(source_mode_smoke main.cpp)
target_link_libraries(source_mode_smoke PRIVATE pac::engine)
EOF
cmake -S "$WORK/src-mode" -B "$WORK/src-mode-build" -DCMAKE_BUILD_TYPE=Release > /dev/null
cmake --build "$WORK/src-mode-build" -j"$(nproc)" > /dev/null

# The engine must have contributed ONLY its library to the game's build.
if find "$WORK/src-mode-build" -name 'pac_example_*' -o -name 'pac_core_tests' | grep -q .; then
    echo "    FAIL: the engine leaked its examples/tests into the game's build" >&2
    exit 1
fi
echo "    built: source_mode_smoke (engine built as a subproject, no examples/tests)"

echo
echo "packaging OK — both consumption modes work"
