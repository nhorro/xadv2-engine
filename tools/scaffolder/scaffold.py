#!/usr/bin/env python3
"""Scaffolder for new games / experiments (issue #134).

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


def render_template(template_dir: Path, output_dir: Path, context: dict[str, str]) -> list[Path]:
    """Walk `template_dir` and materialize it under `output_dir`. Returns the
    list of paths created (for the post-run summary)."""
    output_dir.mkdir(parents=True, exist_ok=True)
    created: list[Path] = []
    for src in sorted(template_dir.rglob("*")):
        rel = src.relative_to(template_dir)
        dst = output_dir / rel
        if src.is_dir():
            dst.mkdir(parents=True, exist_ok=True)
            continue
        dst.parent.mkdir(parents=True, exist_ok=True)
        if is_text_file(src):
            text = src.read_text(encoding="utf-8")
            dst.write_text(substitute(text, context), encoding="utf-8")
        else:
            shutil.copyfile(src, dst)
        # Preserve the executable bit (useful for shell scripts under the
        # template).
        mode = src.stat().st_mode
        if mode & 0o111:
            dst.chmod(dst.stat().st_mode | 0o755)
        created.append(dst)
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


def parse_args(argv: list[str]) -> argparse.Namespace:
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
            "Target directory. For type=experiment|game the default is "
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
    return p.parse_args(argv)


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
        if template == "game":
            # A game is a standalone repository, not a subdirectory of the engine.
            # Default it next to the engine checkout; the user can always --output
            # somewhere else.
            output = Path("..") / short_name
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

    if inputs["template"] == "game":
        # A game is a standalone project: it does not join the engine's build, it
        # links the engine as a library. Its next steps are its own repository and
        # its own configure.
        engine_dir = Path.cwd()
        short = inputs["short_name"]
        print()
        print("next steps — this is a standalone repository, not part of the engine:")
        print(f"    cd {output_dir}")
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


if __name__ == "__main__":
    raise SystemExit(main())
