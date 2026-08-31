#!/usr/bin/env python3
"""Scaffolder for new games, prototypes, experiments, and authoring recipes.

Renders one of the templates under `tools/scaffolder/templates/` into a target
directory, substituting placeholders along the way. Templates are plain
directories — drop in a new one and the scaffolder picks it up automatically.

Interactive use:

    python -m tools.scaffolder

Non-interactive use (CI / scripted):

    python -m tools.scaffolder \
        --type experiment \
        --short-name shaders_lab \
        --title "Laboratorio de shaders"

Placeholder set (in any text file under a template):

    {{short_name}}   OS-friendly id (lowercase, no spaces, no accents).
                     Used for directory name, CMake target, manifest id.
    {{title}}        Display title, any UTF-8 (e.g. "El Misterio del Lago").
    {{base}}         Parent directory under the repo root: `experiments` for
                     experiment-typed projects, `games` for game-typed projects,
                     or whatever first component the user picked for "other".

Binary files (PNG, OTF, MP3, ...) are copied verbatim; only the text whitelist
below is substituted. That keeps the template free of accidental corruption
when an artist drops a real font or sample asset into the tree.
"""

from __future__ import annotations

import argparse
import re
import shutil
import sys
import tempfile
from pathlib import Path

# --- Constants -------------------------------------------------------------

# Files we run placeholder substitution on. Binary extensions are copied
# verbatim. Filenames without an extension can be matched explicitly below.
TEXT_SUFFIXES = {
    ".c",
    ".cpp",
    ".h",
    ".hpp",
    ".cmake",
    ".lua",
    ".md",
    ".sh",
    ".txt",
    ".yaml",
    ".yml",
    ".frag",
    ".vert",
}
TEXT_BARE_NAMES = {"CMakeLists.txt", ".gitignore", "README"}

# `[a-z][a-z0-9_-]*` — matches the same constraint the manifest's `id` field
# requires (06 §Game manifest). Lowercase only keeps the binary name and the
# manifest id symmetric across Linux / macOS / Windows.
VALID_SHORT_NAME = re.compile(r"^[a-z][a-z0-9_-]*$")

PLACEHOLDER_RE = re.compile(r"\{\{(\w+)\}\}")

TEMPLATES_DIR = Path(__file__).resolve().parent / "templates"
RECIPES_DIR = Path(__file__).resolve().parent / "recipes"


# --- Rendering -------------------------------------------------------------


def is_text_file(path: Path) -> bool:
    """True when the scaffolder should run placeholder substitution on `path`."""
    if path.name in TEXT_BARE_NAMES:
        return True
    return path.suffix in TEXT_SUFFIXES


def substitute(text: str, context: dict[str, str]) -> str:
    """Replace `{{name}}` placeholders in `text` from `context`. An unknown
    placeholder is a hard error so a typo doesn't quietly produce junk."""

    def repl(match: re.Match[str]) -> str:
        key = match.group(1)
        if key not in context:
            raise SystemExit(
                f"scaffolder: unknown placeholder {{{{{key}}}}}; "
                f"available: {', '.join(sorted(context))}"
            )
        return context[key]

    return PLACEHOLDER_RE.sub(repl, text)


def rendered_relative_path(path: Path, context: dict[str, str]) -> Path:
    """Substitute placeholders in a template path and keep it relative."""
    rendered = Path(substitute(path.as_posix(), context))
    if rendered.is_absolute() or ".." in rendered.parts:
        raise SystemExit(f"scaffolder: template produced unsafe path: {rendered}")
    return rendered


def render_template(
    template_dir: Path,
    output_dir: Path,
    context: dict[str, str],
    *,
    refuse_existing: bool = False,
    dry_run: bool = False,
) -> list[Path]:
    """Walk `template_dir` and materialize it under `output_dir`. Returns the
    list of paths created (or planned for a dry run).

    Every file is rendered in memory before anything is written. Additive
    recipes use `refuse_existing`, which checks all collisions up front and
    rolls back newly-created files if an unexpected write fails.
    """
    rendered_files: list[tuple[Path, Path, bytes, int]] = []
    destinations: set[Path] = set()
    for src in sorted(path for path in template_dir.rglob("*") if path.is_file()):
        rel = src.relative_to(template_dir)
        dst = output_dir / rendered_relative_path(rel, context)
        if dst in destinations:
            raise SystemExit(f"scaffolder: template produces duplicate path: {dst}")
        destinations.add(dst)
        if is_text_file(src):
            text = src.read_text(encoding="utf-8")
            data = substitute(text, context).encode("utf-8")
        else:
            data = src.read_bytes()
        rendered_files.append((src, dst, data, src.stat().st_mode))

    if refuse_existing:
        conflicts = [dst for _, dst, _, _ in rendered_files if dst.exists()]
        if conflicts:
            formatted = "\n".join(f"  {path}" for path in conflicts)
            raise SystemExit(f"scaffolder: refusing to overwrite existing files:\n{formatted}")

    planned = [dst for _, dst, _, _ in rendered_files]
    if dry_run:
        return planned

    created: list[Path] = []
    try:
        output_dir.mkdir(parents=True, exist_ok=True)
        for _, dst, data, mode in rendered_files:
            dst.parent.mkdir(parents=True, exist_ok=True)
            dst.write_bytes(data)
            if mode & 0o111:
                dst.chmod(dst.stat().st_mode | 0o755)
            created.append(dst)
    except Exception:
        if refuse_existing:
            for path in reversed(created):
                path.unlink(missing_ok=True)
        raise
    return created


# --- Discovery -------------------------------------------------------------


def list_templates(templates_dir: Path) -> list[str]:
    return sorted(p.name for p in templates_dir.iterdir() if p.is_dir())


def default_base_for(template_name: str) -> str | None:
    """Default parent directory for a given template type. None means we'll
    prompt the user (the `other` case).

    An `experiment` is part of the engine repo, so it lands under experiments/.
    A `game` is NOT: it is a standalone project with its own repository, its own
    CMakeLists, and the engine as a library. Its default home is therefore a
    SIBLING of the engine checkout, not a directory inside it — see
    ../<short-name> in resolve_inputs()."""
    return {"experiment": "experiments"}.get(template_name)


# --- CLI -------------------------------------------------------------------


def parse_legacy_args(argv: list[str]) -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="Scaffold a new game or experiment from a template.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=(
            "Examples:\n"
            "  python -m tools.scaffolder                     # interactive\n"
            "  python -m tools.scaffolder --list              # list templates\n"
            "  python -m tools.scaffolder --type experiment --short-name lab --title 'Lab'\n"
        ),
    )
    p.add_argument("--type", help="Template to render (e.g. experiment, game).")
    p.add_argument("--short-name", help="OS-friendly id, [a-z][a-z0-9_-]*.")
    p.add_argument("--title", help='Display title (any UTF-8, e.g. "El Misterio del Lago").')
    p.add_argument(
        "--output",
        type=Path,
        help=(
            "Target directory. Built-in templates default to "
            "experiments/<short-name> or games/<short-name>; for any other "
            "template you must supply this."
        ),
    )
    p.add_argument(
        "--templates-dir",
        type=Path,
        default=TEMPLATES_DIR,
        help="Templates root (default: tools/scaffolder/templates/).",
    )
    p.add_argument("--force", action="store_true", help="Overwrite the target directory if it exists.")
    p.add_argument("--list", action="store_true", help="List available templates and exit.")
    args = p.parse_args(argv)
    args.command = "new"
    return args


def parse_command_args(argv: list[str]) -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Create projects and add common authoring recipes.")
    commands = p.add_subparsers(dest="command", required=True)

    new = commands.add_parser("new", help="Create a project from a template.")
    new.add_argument("type", help="Template to render (game, prototype, or experiment).")
    new.add_argument("short_name", help="OS-friendly id, [a-z][a-z0-9_-]*.")
    new.add_argument("--title", help="Display title; defaults to the short name.")
    new.add_argument("--output", type=Path, help="Target directory override.")
    new.add_argument("--templates-dir", type=Path, default=TEMPLATES_DIR)
    new.add_argument("--force", action="store_true", help="Replace an existing target directory.")
    new.set_defaults(list=False)

    add = commands.add_parser("add", help="Add a recipe to an existing project.")
    recipes = add.add_subparsers(dest="recipe", required=True)
    room = recipes.add_parser("room", help="Add a room YAML and Lua pair.")
    room.add_argument("room_id", help="Room id, [a-z][a-z0-9_-]*.")
    room.add_argument(
        "--project",
        type=Path,
        default=Path.cwd(),
        help="Project directory or any path inside it (default: current directory).",
    )
    room.add_argument("--dry-run", action="store_true", help="Print files without writing them.")
    room.add_argument("--recipes-dir", type=Path, default=RECIPES_DIR)

    script_scene = recipes.add_parser(
        "script-scene", help="Add a generic YAML + Lua scene and register it in game.yaml."
    )
    script_scene.add_argument("scene_id", help="Scene id, [a-z][a-z0-9_-]*.")
    script_scene.add_argument(
        "--project",
        type=Path,
        default=Path.cwd(),
        help="Project directory or any path inside it (default: current directory).",
    )
    script_scene.add_argument(
        "--dry-run", action="store_true", help="Print files and manifest change without writing them."
    )
    script_scene.add_argument("--recipes-dir", type=Path, default=RECIPES_DIR)
    args = p.parse_args(argv)
    if args.command == "new" and args.title is None:
        args.title = args.short_name
    return args


def parse_args(argv: list[str]) -> argparse.Namespace:
    if argv and argv[0] in {"new", "add"}:
        return parse_command_args(argv)
    return parse_legacy_args(argv)


def prompt_value(label: str, *, validator=None, allow_empty: bool = False) -> str:
    """Read a non-empty value from the TTY, optionally validating."""
    while True:
        raw = input(f"{label}: ").strip()
        if not raw and not allow_empty:
            print("required.")
            continue
        if validator is not None:
            error = validator(raw)
            if error:
                print(error)
                continue
        return raw


def prompt_choice(label: str, choices: list[str]) -> str:
    options = "/".join(choices)
    while True:
        raw = input(f"{label} [{options}]: ").strip()
        if raw in choices:
            return raw
        print(f"choose one of: {', '.join(choices)}")


def validate_short_name(value: str) -> str | None:
    if not VALID_SHORT_NAME.fullmatch(value):
        return (
            "short name must match [a-z][a-z0-9_-]* — lowercase ASCII, "
            "digits, underscore, dash; no spaces or accented characters."
        )
    return None


def resolve_inputs(args: argparse.Namespace, available_templates: list[str]) -> dict:
    """Combine CLI args with interactive prompts to fill in every required
    piece of context. Returns a dict with `template`, `short_name`, `title`,
    and `output` (Path)."""

    choices = available_templates + (["other"] if "other" not in available_templates else [])

    template = args.type
    if template is None:
        template = prompt_choice("Type", choices)

    output_override = args.output
    if template == "other":
        # The user picks the actual template *and* the target directory.
        if not available_templates:
            raise SystemExit("scaffolder: no templates installed under templates/.")
        actual = prompt_choice("Template to use", available_templates)
        template = actual
        if output_override is None:
            output_override = Path(
                prompt_value(
                    "Output directory (relative to repo root, e.g. mygames/foo)",
                )
            )

    if template not in available_templates:
        raise SystemExit(
            f"scaffolder: unknown template {template!r}. "
            f"Available: {', '.join(available_templates)}"
        )

    short_name = args.short_name or prompt_value(
        "Short name (lowercase ASCII, no spaces)", validator=validate_short_name
    )
    error = validate_short_name(short_name)
    if error:
        raise SystemExit("scaffolder: " + error)

    title = args.title or prompt_value("Title (display name, UTF-8 OK)")

    if output_override is None:
        if template in {"game", "prototype"}:
            # A game or disposable prototype is a standalone project, not a
            # subdirectory of the engine.
            # The workspace layout is:
            #
            #     point-and-click-game/
            #     ├── xadv2-engine/     <- we are here
            #     └── games/<short_name>/
            #
            # so a new game lands in the sibling games/ directory, one working copy
            # per game repository. --output overrides this.
            output = Path("..") / "games" / short_name
        else:
            base = default_base_for(template)
            if base is None:
                raise SystemExit(
                    f"scaffolder: template {template!r} has no default base; pass --output."
                )
            output = Path(base) / short_name
    else:
        # If the user passed only the parent (e.g. mygames/), append the short
        # name so the project ends up in its own directory.
        if output_override.exists() and output_override.is_dir():
            output = output_override / short_name
        else:
            output = output_override

    return {
        "template": template,
        "short_name": short_name,
        "title": title,
        "output": output,
        "base": output.parent.name,
    }


def main(argv: list[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)

    if args.command == "add":
        if args.recipe == "room":
            return add_room(args)
        if args.recipe == "script-scene":
            return add_script_scene(args)
        raise SystemExit(f"scaffolder: unsupported recipe {args.recipe!r}")

    templates_dir: Path = args.templates_dir.resolve()
    if not templates_dir.is_dir():
        print(f"scaffolder: templates dir not found: {templates_dir}", file=sys.stderr)
        return 2

    available = list_templates(templates_dir)
    if args.list:
        for name in available:
            print(name)
        return 0
    if not available:
        print(f"scaffolder: no templates found in {templates_dir}", file=sys.stderr)
        return 2

    inputs = resolve_inputs(args, available)
    template_dir = templates_dir / inputs["template"]
    output_dir: Path = inputs["output"]

    if output_dir.exists():
        if not args.force:
            print(
                f"scaffolder: output directory already exists: {output_dir}\n"
                f"           pass --force to overwrite",
                file=sys.stderr,
            )
            return 1
        shutil.rmtree(output_dir)

    context = {
        "short_name": inputs["short_name"],
        "title": inputs["title"],
        "base": inputs["base"],
    }

    created = render_template(template_dir, output_dir, context)

    print(f"scaffolded {inputs['template']!r} at {output_dir} ({len(created)} files)")

    if inputs["template"] in {"game", "prototype"}:
        # A game or prototype is standalone: it does not join the engine's build
        # and instead links the engine as a library.
        engine_dir = Path.cwd()
        short = inputs["short_name"]
        print()
        print("next steps — this is a standalone project, not part of the engine build:")
        print(f"    cd {output_dir}")
        if inputs["template"] == "game":
            print("    git init && git add -A && git commit -m 'initial scaffold'")
        print(f"    cmake -S . -B build -DXADV2_ENGINE_DIR={engine_dir}")
        print('    cmake --build build -j"$(nproc)"')
        print("    ./run.sh")
        print()
        print(f"    (the authoring tools stay here: export XADV2_ENGINE={engine_dir})")
        return 0

    # An experiment DOES join the engine's build, as a sibling under its parent's
    # CMakeLists.txt, so it needs an add_subdirectory(<short_name>) entry.
    parent_cmake = output_dir.parent / "CMakeLists.txt"
    if parent_cmake.exists():
        line = f"add_subdirectory({inputs['short_name']})"
        if line not in parent_cmake.read_text(encoding="utf-8"):
            print()
            print(f"next step: add this line to {parent_cmake}:")
            print(f"    {line}")

    return 0


def find_project_root(start: Path) -> Path:
    """Find the nearest ancestor containing the canonical game manifest."""
    current = start.expanduser().resolve()
    if current.is_file():
        current = current.parent
    for candidate in (current, *current.parents):
        if (candidate / "data" / "game.yaml").is_file():
            return candidate
    raise SystemExit(
        f"scaffolder: no project found from {start}; expected an ancestor containing data/game.yaml"
    )


def add_room(args: argparse.Namespace) -> int:
    error = validate_short_name(args.room_id)
    if error:
        raise SystemExit("scaffolder: room " + error)

    project_root = find_project_root(args.project)
    recipe_dir = args.recipes_dir.expanduser().resolve() / "room"
    if not recipe_dir.is_dir():
        raise SystemExit(f"scaffolder: room recipe not found: {recipe_dir}")

    files = render_template(
        recipe_dir,
        project_root,
        {"room_id": args.room_id},
        refuse_existing=True,
        dry_run=args.dry_run,
    )
    action = "would create" if args.dry_run else "created"
    print(f"{action} room {args.room_id!r} in {project_root}:")
    for path in files:
        print(f"  {path.relative_to(project_root)}")
    if not args.dry_run:
        print()
        print(f'enter it from Lua with: change_room("{args.room_id}", "player_start")')
    return 0


def manifest_with_script_scene(text: str, scene_id: str) -> str:
    """Return `text` with one ScriptScene list entry appended.

    This intentionally edits the small manifest surface as text instead of
    parsing and re-emitting YAML, so author comments and formatting survive.
    """
    lines = text.splitlines(keepends=True)
    scenes_index = next(
        (i for i, line in enumerate(lines) if re.match(r"^scenes:\s*(?:#.*)?(?:\r?\n)?$", line)),
        None,
    )
    empty_index = next(
        (i for i, line in enumerate(lines) if re.match(r"^scenes:\s*\[\s*\]\s*(?:#.*)?(?:\r?\n)?$", line)),
        None,
    )
    if scenes_index is None and empty_index is None:
        raise SystemExit("scaffolder: data/game.yaml has no root 'scenes:' list")

    scenes_index = empty_index if scenes_index is None else scenes_index
    block_end = len(lines)
    for i in range(scenes_index + 1, len(lines)):
        line = lines[i]
        if line.strip() and not line.startswith((" ", "\t", "#", "\r", "\n")):
            block_end = i
            break

    scene_block = "".join(lines[scenes_index:block_end])
    ids = re.findall(r"^\s+-\s+id:\s*['\"]?([^\s#,'\"}\]]+)", scene_block, re.MULTILINE)
    if scene_id in ids:
        raise SystemExit(f"scaffolder: scene {scene_id!r} already exists in data/game.yaml")

    entry = (
        f"  - id: {scene_id}\n"
        "    type: ScriptScene\n"
        "    parameters:\n"
        f"      data: scenes/{scene_id}/scene.yaml\n"
        f"      logic: scenes/{scene_id}/scene.lua\n"
    )
    if empty_index is not None:
        suffix = "\n" if lines[empty_index].endswith(("\n", "\r")) else ""
        lines[empty_index] = "scenes:\n" + entry + suffix
    else:
        if block_end > scenes_index + 1 and lines[block_end - 1].strip():
            entry += "\n"
        lines.insert(block_end, entry)
    return "".join(lines)


def write_text_atomic(path: Path, text: str) -> None:
    """Replace one UTF-8 text file without exposing a partially written file."""
    temporary: Path | None = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w", encoding="utf-8", dir=path.parent, prefix=f".{path.name}.", delete=False
        ) as handle:
            handle.write(text)
            temporary = Path(handle.name)
        temporary.replace(path)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def add_script_scene(args: argparse.Namespace) -> int:
    error = validate_short_name(args.scene_id)
    if error:
        raise SystemExit("scaffolder: scene " + error)

    project_root = find_project_root(args.project)
    recipe_dir = args.recipes_dir.expanduser().resolve() / "script-scene"
    if not recipe_dir.is_dir():
        raise SystemExit(f"scaffolder: script-scene recipe not found: {recipe_dir}")

    manifest = project_root / "data" / "game.yaml"
    updated_manifest = manifest_with_script_scene(
        manifest.read_text(encoding="utf-8"), args.scene_id
    )
    context = {"scene_id": args.scene_id}
    files = render_template(
        recipe_dir,
        project_root,
        context,
        refuse_existing=True,
        dry_run=True,
    )

    action = "would create" if args.dry_run else "created"
    if not args.dry_run:
        created: list[Path] = []
        try:
            created = render_template(
                recipe_dir, project_root, context, refuse_existing=True
            )
            write_text_atomic(manifest, updated_manifest)
        except Exception:
            for path in reversed(created):
                path.unlink(missing_ok=True)
            raise

    print(f"{action} script scene {args.scene_id!r} in {project_root}:")
    for path in files:
        print(f"  {path.relative_to(project_root)}")
    print("  data/game.yaml (modify)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
