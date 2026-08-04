import json
import tempfile
from pathlib import Path
from unittest import TestCase

from room_editor.room_data import (
    apply_room_patch,
    find_cast_file,
    find_missing_assets,
    list_assets,
    list_rooms,
    load_avatar_catalog,
    load_room_yaml,
    resolve_asset_within,
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

    def test_apply_room_patch_persists_lights_and_preserves_other_lighting(self):
        room = {
            "id": "test",
            "lighting": {
                "ambient": {"intensity": 0.4},
                "normal_map": {"image": "room_normals.png"},
                "projected_shadows": {"source": "old_lamp"},
                "lights": [
                    {
                        "id": "old_lamp",
                        "type": "omni",
                        "at": {"x": 10, "y": 20},
                        "radius": 100,
                        "modulation": {"type": "flicker", "amount": 0.1},
                    }
                ],
            },
        }
        patch = {
            "lighting": {
                "lights": [
                    {
                        "id": "new_spot",
                        "type": "spot",
                        "at": {"x": 30, "y": 40},
                        "range": 240,
                        "direction": 20,
                        "angle": 50,
                    }
                ]
            }
        }

        result = apply_room_patch(room, patch)
        self.assertEqual(result["lighting"]["lights"][0]["id"], "new_spot")
        self.assertEqual(result["lighting"]["ambient"]["intensity"], 0.4)
        self.assertEqual(result["lighting"]["normal_map"]["image"], "room_normals.png")
        self.assertEqual(result["lighting"]["projected_shadows"]["source"], "old_lamp")

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

    def test_data_assets_are_room_relative_and_cannot_escape_data(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            data = Path(tmpdir) / "data"
            rooms = data / "rooms"
            objects = data / "props"
            rooms.mkdir(parents=True)
            objects.mkdir()
            (rooms / "background.png").write_bytes(b"room")
            (objects / "desk.png").write_bytes(b"object")
            (data / "root.png").write_bytes(b"root")

            assets = list_assets(data, relative_to=rooms)
            self.assertIn("background.png", assets)
            self.assertIn("../props/desk.png", assets)
            self.assertEqual(
                resolve_asset_within(data, rooms, "../props/desk.png"),
                (objects / "desk.png").resolve(),
            )
            self.assertEqual(
                resolve_asset_within(data, rooms, "/root.png"),
                (data / "root.png").resolve(),
            )
            with self.assertRaises(ValueError):
                resolve_asset_within(data, rooms, "../../outside.png")
            with self.assertRaises(ValueError):
                resolve_asset_within(data, rooms, "/../../outside.png")

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

    def test_avatar_catalog_resolves_first_sequence_frames_and_mirroring(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            data = Path(tmpdir) / "data"
            rooms = data / "rooms"
            character_dir = data / "characters" / "hero"
            rooms.mkdir(parents=True)
            character_dir.mkdir(parents=True)
            room_file = rooms / "study.yaml"
            save_room_yaml(room_file, {"id": "study", "background": {"layers": []}})
            save_room_yaml(
                data / "cast.yaml",
                {
                    "appearances": {
                        "hero": {
                            "type": "animated_sprite",
                            "sprite": "characters/hero/hero.anim.yml",
                        }
                    },
                    "characters": {
                        "player": {
                            "appearance": "hero",
                            "name": "Hero",
                        }
                    },
                },
            )
            save_room_yaml(
                character_dir / "hero.anim.yml",
                {
                    "spritesheet": "hero.yml",
                    "pivot": "feet",
                    "sequences": {
                        "stand_down": {
                            "loop": True,
                            "frames": [{"sprite": "idle", "duration": 0.2}],
                        },
                        "stand_left": {
                            "loop": True,
                            "h_mirror": True,
                            "frames": [{"sprite": "idle", "duration": 0.2}],
                        },
                    },
                },
            )
            save_room_yaml(
                character_dir / "hero.yml",
                {
                    "image": "hero.png",
                    "size": {"width": 64, "height": 96},
                    "sprites": [
                        {
                            "id": "idle",
                            "rect": {"x": 4, "y": 8, "width": 32, "height": 48},
                            "anchors": {"feet": {"x": 12, "y": 44}},
                        }
                    ],
                },
            )
            (character_dir / "hero.png").write_bytes(b"preview")

            cast_path = find_cast_file(room_file, rooms)
            self.assertEqual(cast_path, data / "cast.yaml")
            catalog = load_avatar_catalog(cast_path)
            self.assertEqual(catalog["errors"], [])
            self.assertEqual(len(catalog["characters"]), 1)
            hero = catalog["characters"][0]
            self.assertEqual(hero["id"], "player")
            self.assertEqual(hero["default_sequence"], "stand_down")
            self.assertEqual(hero["image"], "characters/hero/hero.png")
            self.assertEqual(
                hero["sequences"]["stand_down"]["rect"],
                {"x": 4, "y": 8, "width": 32, "height": 48},
            )
            self.assertEqual(
                hero["sequences"]["stand_down"]["pivot"],
                {"x": 12.0, "y": 44.0},
            )
            self.assertFalse(hero["sequences"]["stand_down"]["h_mirror"])
            self.assertTrue(hero["sequences"]["stand_left"]["h_mirror"])
