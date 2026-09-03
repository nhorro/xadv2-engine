# xadv2-engine contributor guide

This repository owns the C++20/SFML runtime. Keep its boundary narrow:

- `lib/` is the reusable engine library.
- `examples/` contains small executable contracts for supported features.
- `tests/` contains automated runtime tests.
- `android/` and `packaging/` prove supported delivery paths.
- `docs/authoring/` documents the current Lua and YAML contract.
- `docs/development/tour/` explains the current implementation.

Games belong in their own repositories. Editors, generators, the resource
packer, and scaffolding belong in
[`xadv2-tools`](https://github.com/nhorro/xadv2-tools). One-off experiments do
not belong in the engine; promote useful cases to an example or an automated
test.

## Documentation status

Treat the code, tests, current architecture tour, and authoring reference as
the contract. Keep plans under `docs/development/plans/` and label them as
proposals. Files under `docs/development/history/` describe earlier designs and
may disagree with the implementation.

When behavior changes, update its current documentation in the same change.
Do not make historical pages authoritative again.

## Build and test

```bash
cmake -S . -B build -DPAC_BUILD_TESTS=ON -DPAC_BUILD_EXAMPLES=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
./scripts/check-packaging.sh
```

Build the documentation with:

```bash
python3 -m venv .venv
.venv/bin/pip install -r docs/requirements.txt
.venv/bin/mkdocs build --strict
```

Useful focused checks:

```bash
python3 examples/tools/sync_assets.py --check
clang-format --dry-run --Werror path/to/changed.cpp
```

## Dependency direction

The public layers are `core`, `geom`, `gfx`, and `pnc`. Generic facilities
must not depend on the point-and-click kit. Prefer a small interface in a lower
layer over adding another responsibility to `RoomScene`.

Public C++ headers live under `lib/include/engine/`; implementations mirror
them under `lib/src/`. Runtime resources are addressed by logical paths through
the resource-source abstraction, never by opening game files directly.

## Lua and data

Lua bindings are a public API. Verify their exact names and semantics in the
binding code and document them in `docs/authoring/lua-api.md`. YAML defines
static data; Lua defines behavior. Persistent state supports scalar boolean,
number, and string values.

Lua callbacks that may yield must run through the engine's task/coroutine
machinery. Direct lifecycle callbacks must not yield. Preserve this distinction
in code, tests, and documentation.

## Change discipline

- Follow `.clang-format` and `docs/development/coding-conventions.md`.
- Add or update a focused test for behavior changes.
- Add an example only when it teaches a supported capability not already shown.
- Keep generated build output and game-specific assets out of the repository.
- Keep compatibility at the engine/tool file-format boundary explicit and
  tested in both repositories.
