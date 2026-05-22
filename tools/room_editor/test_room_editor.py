import json
import tempfile
from pathlib import Path
from unittest import TestCase

from room_editor.room_data import apply_room_patch, find_missing_assets, load_room_yaml, save_room_yaml


class RoomEditorTests(TestCase):
    def test_apply_room_patch_preserves_unknown_sections(self):
        room = {
            "id": "test",
            "size": {"width": 1600, "height": 720},
            "background": {"layers": [{"id": "bg", "image": "back.png", "z": 0}]},
            "objects": {"lamp": {"sprite": "anims/lamp.anim.yaml"}},
        }
        patch = {
            "background": {"layers": [{"id": "bg", "image": "new_back.png", "z": 0}]},
            "geometry": {"walkable": [{"x": 0, "y": 0}, {"x": 100, "y": 0}, {"x": 100, "y": 100}]},
        }

        result = apply_room_patch(room, patch)
        self.assertEqual(result["background"]["layers"][0]["image"], "new_back.png")
        self.assertIn("objects", result)
        self.assertEqual(result["walkable"][2]["y"], 100)

    def test_find_missing_assets_reports_missing_files(self):
        room = {
            "background": {"layers": [{"id": "bg", "image": "missing.png", "z": 0}]},
            "objects": {"lamp": {"sprite": "present.anim.yaml"}},
            "regions": {
                "drawer": {
                    "states": {
                        "shut": "regions/drawer_shut.png",
                        "open": "regions/drawer_open.png",
                    },
                    "initial": "shut",
                }
            },
        }
        with tempfile.TemporaryDirectory() as tmpdir:
            base_path = Path(tmpdir)
            (base_path / "present.anim.yaml").write_text("{}", encoding="utf-8")
            missing = find_missing_assets(room, base_path)
            self.assertIn("missing.png", missing)
            self.assertIn("regions/drawer_shut.png", missing)
            self.assertIn("regions/drawer_open.png", missing)

    def test_load_and_save_room_yaml_roundtrip(self):
        room = {"id": "roundtrip", "background": {"layers": []}}
        with tempfile.TemporaryDirectory() as tmpdir:
            room_file = Path(tmpdir) / "room.yaml"
            save_room_yaml(room_file, room)
            loaded = load_room_yaml(room_file)
            self.assertEqual(loaded["id"], "roundtrip")
            self.assertEqual(loaded["background"]["layers"], [])
