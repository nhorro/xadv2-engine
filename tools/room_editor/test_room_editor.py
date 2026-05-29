import json
import tempfile
from pathlib import Path
from unittest import TestCase

from room_editor.room_data import (
    apply_room_patch,
    find_missing_assets,
    list_rooms,
    load_room_yaml,
    resolve_within,
    save_room_yaml,
)


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

    def test_apply_room_patch_persists_objects(self):
        # The objects mode (#147) edits room["objects"]; the save patch carries it
        # under geometry, so a round-trip must write it back.
        room = {"id": "test", "objects": {"old": {"sprite": "a.png"}}}
        patch = {
            "geometry": {
                "objects": {
                    "crate": {
                        "sprite": "objects/crate.png",
                        "position": {"x": 10, "y": 20},
                        "scale": 1.5,
                    }
                }
            }
        }
        result = apply_room_patch(room, patch)
        self.assertIn("crate", result["objects"])
        self.assertNotIn("old", result["objects"])  # replaced wholesale, like regions
        self.assertEqual(result["objects"]["crate"]["scale"], 1.5)

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

    def test_list_rooms_finds_room_yamls_and_skips_non_rooms(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            base = Path(tmpdir)
            save_room_yaml(base / "study.yaml", {"id": "study", "background": {"layers": []}})
            save_room_yaml(base / "hall.yml", {"id": "hall", "walkable": []})
            save_room_yaml(base / "game.yaml", {"id": "game", "scenes": []})  # manifest, not a room
            (base / "notes.txt").write_text("ignore me", encoding="utf-8")
            rooms = list_rooms(base)
            self.assertEqual(rooms, ["hall.yml", "study.yaml"])  # sorted, manifest excluded

    def test_resolve_within_blocks_escapes_and_siblings(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            base = Path(tmpdir) / "rooms"
            base.mkdir()
            (Path(tmpdir) / "rooms_secret").mkdir()  # sibling sharing the name prefix
            # Valid: a file (or subpath) inside base resolves fine.
            self.assertEqual(resolve_within(base, "study.yaml"), (base / "study.yaml").resolve())
            # Escapes: parent traversal and the prefix-sibling both rejected.
            with self.assertRaises(ValueError):
                resolve_within(base, "../etc/passwd")
            with self.assertRaises(ValueError):
                resolve_within(base, "../rooms_secret/x.yaml")

    def test_save_room_yaml_writes_polygons_inline(self):
        room = {
            "id": "r",
            "walkable": [{"x": 10, "y": 20}, {"x": 30, "y": 40}],
            "obstacles": [[{"x": 1, "y": 2}, {"x": 3, "y": 4}]],
            "background": {"color": {"r": 1, "g": 2, "b": 3}},
        }
        with tempfile.TemporaryDirectory() as tmpdir:
            room_file = Path(tmpdir) / "r.yaml"
            save_room_yaml(room_file, room)
            text = room_file.read_text(encoding="utf-8")
            self.assertIn("walkable: [{x: 10, y: 20}, {x: 30, y: 40}]", text)  # one-line polygon
            self.assertIn("- [{x: 1, y: 2}, {x: 3, y: 4}]", text)  # inner obstacle polygon flow
            self.assertNotIn("- x: 10", text)  # not the old vertical form
            self.assertIn("    r: 1", text)  # non-point mappings stay block style
            self.assertEqual(load_room_yaml(room_file), room)  # still round-trips

    def test_load_and_save_room_yaml_roundtrip(self):
        room = {"id": "roundtrip", "background": {"layers": []}}
        with tempfile.TemporaryDirectory() as tmpdir:
            room_file = Path(tmpdir) / "room.yaml"
            save_room_yaml(room_file, room)
            loaded = load_room_yaml(room_file)
            self.assertEqual(loaded["id"], "roundtrip")
            self.assertEqual(loaded["background"]["layers"], [])
