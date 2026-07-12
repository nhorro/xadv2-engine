#!/usr/bin/env python3
"""Copy the shared asset kit into every example's data/ directory.

A manifest has exactly one resource root (`resources.src`), so an example cannot
reach outside its own data/ for the font or the character rig. Rather than make
the examples depend on a build step, each one carries its own copy of the kit and
is therefore self-contained: copy any examples/<name>/data/ anywhere and it runs.

The kit is small (~340 KB: one quantized atlas, one font, six flat backgrounds),
so the duplication is cheap and the "copy this folder" property is worth it.
`examples/_assets/` stays the single source of truth — edit there, then run:

    python3 examples/tools/sync_assets.py            # copy kit -> every example
    python3 examples/tools/sync_assets.py --check    # fail if any copy is stale

Files that are *game logic* (game.yaml, rooms/, scripts/, dialogs/, inventory/,
closeups/, cutscenes/) are the example's own and are never touched here.
"""

import argparse
import filecmp
import pathlib
import shutil
import sys

EXAMPLES = pathlib.Path(__file__).resolve().parent.parent
ASSETS = EXAMPLES / "_assets"

# Everything under _assets that gets mirrored into each example's data/.
KIT = ["cast.yaml", "fonts", "characters", "backgrounds", "strings", "shaders", "ui"]


def example_dirs():
    return sorted(p for p in EXAMPLES.iterdir()
                  if p.is_dir() and p.name[0].isdigit() and (p / "data").is_dir())


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--check", action="store_true",
                    help="report stale/missing copies instead of writing them")
    args = ap.parse_args()

    stale = []
    for example in example_dirs():
        data = example / "data"
        for entry in KIT:
            src, dst = ASSETS / entry, data / entry
            if args.check:
                same = dst.exists() and (
                    filecmp.cmp(src, dst, shallow=False) if src.is_file()
                    else not filecmp.dircmp(src, dst).diff_files
                    and not filecmp.dircmp(src, dst).left_only)
                if not same:
                    stale.append(f"{dst.relative_to(EXAMPLES.parent)}")
                continue
            if src.is_dir():
                shutil.copytree(src, dst, dirs_exist_ok=True)
            else:
                dst.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(src, dst)
        if not args.check:
            print(f"synced {example.name}/data")

    if stale:
        print("stale or missing copies of the shared kit:", file=sys.stderr)
        for s in stale:
            print(f"  {s}", file=sys.stderr)
        print("run: python3 examples/tools/sync_assets.py", file=sys.stderr)
        return 1
    if args.check:
        print(f"{len(example_dirs())} examples in sync with _assets/")
    return 0


if __name__ == "__main__":
    sys.exit(main())
