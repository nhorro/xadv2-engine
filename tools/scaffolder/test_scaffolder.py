import tempfile
from contextlib import redirect_stdout
from io import StringIO
from pathlib import Path
from unittest import TestCase

from .scaffold import (
    TEMPLATES_DIR,
    find_project_root,
    main,
    parse_args,
    render_template,
)


class ScaffolderTests(TestCase):
    def test_legacy_new_arguments_remain_supported(self):
        args = parse_args(
            ["--type", "experiment", "--short-name", "shader_lab", "--title", "Shader Lab"]
        )
        self.assertEqual(args.command, "new")
        self.assertEqual(args.type, "experiment")
        self.assertEqual(args.short_name, "shader_lab")

    def test_new_command_defaults_title_to_short_name(self):
        args = parse_args(["new", "prototype", "panel_lab"])
        self.assertEqual(args.command, "new")
        self.assertEqual(args.title, "panel_lab")

    def test_template_placeholders_are_rendered_in_paths_and_contents(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            template = root / "template"
            (template / "rooms").mkdir(parents=True)
            (template / "rooms" / "{{room_id}}.yaml").write_text(
                "id: {{room_id}}\n", encoding="utf-8"
            )

            output = root / "output"
            paths = render_template(template, output, {"room_id": "attic"})

            self.assertEqual(paths, [output / "rooms" / "attic.yaml"])
            self.assertEqual(paths[0].read_text(encoding="utf-8"), "id: attic\n")

    def test_new_prototype_is_a_minimal_standalone_project(self):
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp) / "panel_lab"
            stdout = StringIO()
            with redirect_stdout(stdout):
                result = main(
                    [
                        "new",
                        "prototype",
                        "panel_lab",
                        "--title",
                        "Panel Lab",
                        "--output",
                        str(output),
                    ]
                )

            self.assertEqual(result, 0)
            self.assertTrue((output / "CMakeLists.txt").is_file())
            self.assertTrue((output / "data" / "rooms" / "lab.yaml").is_file())
            manifest = (output / "data" / "game.yaml").read_text(encoding="utf-8")
            self.assertIn("id: panel_lab", manifest)
            self.assertIn("entry: room_view", manifest)
            self.assertNotIn("type: TitleScreen", manifest)
            self.assertNotIn("git init", stdout.getvalue())

    def test_add_room_discovers_project_from_child_and_refuses_collisions(self):
        with tempfile.TemporaryDirectory() as tmp:
            project = Path(tmp) / "game"
            nested = project / "data" / "scripts"
            nested.mkdir(parents=True)
            (project / "data" / "game.yaml").write_text("version: 1\n", encoding="utf-8")

            stdout = StringIO()
            with redirect_stdout(stdout):
                result = main(["add", "room", "attic", "--project", str(nested)])
            self.assertEqual(result, 0)
            room_yaml = project / "data" / "rooms" / "attic.yaml"
            room_lua = project / "data" / "rooms" / "attic.lua"
            self.assertTrue(room_yaml.is_file())
            self.assertTrue(room_lua.is_file())
            self.assertIn("id: attic", room_yaml.read_text(encoding="utf-8"))

            original = room_yaml.read_text(encoding="utf-8")
            with self.assertRaisesRegex(SystemExit, "refusing to overwrite"):
                main(["add", "room", "attic", "--project", str(project)])
            self.assertEqual(room_yaml.read_text(encoding="utf-8"), original)

    def test_add_room_dry_run_does_not_write(self):
        with tempfile.TemporaryDirectory() as tmp:
            project = Path(tmp) / "game"
            (project / "data").mkdir(parents=True)
            (project / "data" / "game.yaml").write_text("version: 1\n", encoding="utf-8")

            stdout = StringIO()
            with redirect_stdout(stdout):
                result = main(
                    ["add", "room", "yard", "--project", str(project), "--dry-run"]
                )

            self.assertEqual(result, 0)
            self.assertIn("would create room 'yard'", stdout.getvalue())
            self.assertFalse((project / "data" / "rooms").exists())

    def test_add_script_scene_creates_files_and_preserves_manifest(self):
        with tempfile.TemporaryDirectory() as tmp:
            project = Path(tmp) / "game"
            (project / "data").mkdir(parents=True)
            manifest = project / "data" / "game.yaml"
            manifest.write_text(
                "version: 1\n# keep this comment\nentry: room\n\nscenes:\n"
                "  - id: room\n    type: RoomScene\n    parameters: {}\n",
                encoding="utf-8",
            )

            result = main(["add", "script-scene", "minigame", "--project", str(project)])

            self.assertEqual(result, 0)
            scene_yaml = project / "data" / "scenes" / "minigame" / "scene.yaml"
            scene_lua = project / "data" / "scenes" / "minigame" / "scene.lua"
            self.assertTrue(scene_yaml.is_file())
            self.assertTrue(scene_lua.is_file())
            self.assertIn("id: minigame", scene_yaml.read_text(encoding="utf-8"))
            updated = manifest.read_text(encoding="utf-8")
            self.assertIn("# keep this comment", updated)
            self.assertIn("  - id: minigame", updated)
            self.assertIn("    type: ScriptScene", updated)
            self.assertIn("      data: scenes/minigame/scene.yaml", updated)

            with self.assertRaisesRegex(SystemExit, "already exists"):
                main(["add", "script-scene", "minigame", "--project", str(project)])

    def test_add_script_scene_dry_run_changes_nothing(self):
        with tempfile.TemporaryDirectory() as tmp:
            project = Path(tmp) / "game"
            (project / "data").mkdir(parents=True)
            manifest = project / "data" / "game.yaml"
            original = "version: 1\nscenes: []\n"
            manifest.write_text(original, encoding="utf-8")

            stdout = StringIO()
            with redirect_stdout(stdout):
                result = main(
                    ["add", "script-scene", "sandbox", "--project", str(project), "--dry-run"]
                )

            self.assertEqual(result, 0)
            self.assertIn("would create script scene 'sandbox'", stdout.getvalue())
            self.assertIn("data/game.yaml (modify)", stdout.getvalue())
            self.assertEqual(manifest.read_text(encoding="utf-8"), original)
            self.assertFalse((project / "data" / "scenes").exists())

    def test_find_project_root_requires_manifest(self):
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaisesRegex(SystemExit, "no project found"):
                find_project_root(Path(tmp))

    def test_builtin_templates_include_prototype(self):
        self.assertTrue((TEMPLATES_DIR / "prototype" / "data" / "game.yaml").is_file())
