import tempfile
from pathlib import Path
from unittest import TestCase

from closeup_editor.__main__ import _default_base_path
from closeup_editor.closeup_data import (
    apply_editable_areas,
    apply_hotspots,
    background_asset_name,
    document_kind,
    editable_areas,
    list_closeups,
    load_closeup_yaml,
    normalize_hotspots,
    resolve_within,
    save_closeup_yaml,
)


class CloseUpEditorTests(TestCase):
    def test_normalize_rounds_and_validates(self):
        hs = {
            "skull": {
                "name": "el cráneo",
                "area": [{"x": 1.4, "y": 2.6}, {"x": 10, "y": 2}, {"x": 5, "y": 9}],
            }
        }
        out = normalize_hotspots(hs)
        self.assertEqual(out["skull"]["area"][0], {"x": 1, "y": 3})  # rounded ints
        self.assertEqual(out["skull"]["name"], "el cráneo")

    def test_normalize_rejects_bad_shapes(self):
        with self.assertRaises(ValueError):
            normalize_hotspots({"x": {"area": [{"x": 0, "y": 0}, {"x": 1, "y": 1}]}})  # < 3 pts
        with self.assertRaises(ValueError):
            normalize_hotspots({"x": {"area": "nope"}})
        with self.assertRaises(ValueError):
            normalize_hotspots([])  # not a mapping

    def test_apply_hotspots_preserves_other_fields(self):
        data = {
            "version": 1,
            "id": "lab_skull_closeup",
            "background": "closeups/skull.png",
            "background_color": {"r": 20, "g": 16, "b": 12},
            "hotspots": {"old": {"area": [{"x": 0, "y": 0}, {"x": 1, "y": 0}, {"x": 1, "y": 1}]}},
        }
        apply_hotspots(data, {"craneo": {"name": "cráneo", "area": [
            {"x": 10, "y": 10}, {"x": 100, "y": 10}, {"x": 100, "y": 100}]}})
        self.assertEqual(data["background"], "closeups/skull.png")
        self.assertEqual(data["background_color"], {"r": 20, "g": 16, "b": 12})
        self.assertIn("craneo", data["hotspots"])
        self.assertNotIn("old", data["hotspots"])  # replaced, not merged

    def test_template_slots_are_exposed_as_geometry_only(self):
        data = {
            "id": "template_a",
            "background": "a.png",
            "canvas_height": 592,
            "slots": {
                "place": {
                    "accepts": ["place"],
                    "solution": "hostel",
                    "area": [{"x": 0, "y": 0}, {"x": 10, "y": 0}, {"x": 10, "y": 10}],
                }
            },
        }
        self.assertEqual(document_kind(data), "template")
        self.assertEqual(
            editable_areas(data),
            {"place": {"area": [{"x": 0, "y": 0}, {"x": 10, "y": 0}, {"x": 10, "y": 10}]}},
        )

    def test_apply_template_areas_preserves_slot_semantics(self):
        data = {
            "id": "template_a",
            "background": "a.png",
            "slots": {
                "place": {
                    "accepts": ["place"],
                    "solution": "hostel",
                    "area": [{"x": 0, "y": 0}, {"x": 10, "y": 0}, {"x": 10, "y": 10}],
                }
            },
        }
        apply_editable_areas(
            data,
            {
                "place": {
                    "area": [
                        {"x": 1.2, "y": 2.7},
                        {"x": 20, "y": 3},
                        {"x": 20, "y": 30},
                    ]
                }
            },
        )
        self.assertEqual(data["slots"]["place"]["accepts"], ["place"])
        self.assertEqual(data["slots"]["place"]["solution"], "hostel")
        self.assertEqual(data["slots"]["place"]["area"][0], {"x": 1, "y": 3})

    def test_apply_template_areas_rejects_slot_membership_changes(self):
        data = {
            "slots": {
                "place": {
                    "accepts": ["place"],
                    "solution": "hostel",
                    "area": [{"x": 0, "y": 0}, {"x": 10, "y": 0}, {"x": 10, "y": 10}],
                }
            }
        }
        with self.assertRaisesRegex(ValueError, "cannot be added or removed"):
            apply_editable_areas(data, {})

    def test_template_background_is_relative_to_template(self):
        base = Path("/game/data")
        path = base / "cases" / "last_afternoon" / "template_a.yaml"
        data = {"background": "a.png", "slots": {}}
        self.assertEqual(
            background_asset_name(data, path, base),
            "cases/last_afternoon/a.png",
        )

    def test_nested_closeup_background_is_relative_to_yaml(self):
        base = Path("/game/data")
        path = base / "closeups" / "documents" / "letter" / "closeup.yml"
        data = {"background": "background.png", "hotspots": {}}
        self.assertEqual(
            background_asset_name(data, path, base),
            "closeups/documents/letter/background.png",
        )

    def test_leading_slash_background_is_relative_to_resource_root(self):
        base = Path("/game/data")
        path = base / "closeups" / "painting" / "closeup.yml"
        data = {"background": "/backgrounds/painting.png", "hotspots": {}}
        self.assertEqual(
            background_asset_name(data, path, base),
            "backgrounds/painting.png",
        )

    def test_nested_closeup_infers_data_directory_as_asset_base(self):
        path = Path("/game/data/closeups/documents/letter/closeup.yml")
        self.assertEqual(_default_base_path(path), Path("/game/data"))

    def test_save_load_round_trip(self):
        data = {
            "version": 1,
            "id": "c",
            "background": "closeups/skull.png",
            "hotspots": {
                "a": {"name": "uno", "area": [{"x": 0, "y": 0}, {"x": 5, "y": 0}, {"x": 5, "y": 5}]}
            },
        }
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "c.yml"
            save_closeup_yaml(path, data)
            text = path.read_text(encoding="utf-8")
            # Polygons render inline (one line), like the room editor's output.
            self.assertIn("area: [{x: 0, y: 0}, {x: 5, y: 0}, {x: 5, y: 5}]", text)
            reloaded = load_closeup_yaml(path)
            self.assertEqual(reloaded["hotspots"]["a"]["name"], "uno")
            self.assertEqual(reloaded["id"], "c")

    def test_template_save_keeps_accepts_compact(self):
        data = {
            "id": "template_a",
            "background": "a.png",
            "slots": {
                "place": {
                    "accepts": ["place"],
                    "solution": "hostel",
                    "area": [{"x": 0, "y": 0}, {"x": 5, "y": 0}, {"x": 5, "y": 5}],
                }
            },
        }
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "template_a.yaml"
            save_closeup_yaml(path, data)
            self.assertIn("accepts: [place]", path.read_text(encoding="utf-8"))

    def test_resolve_within_blocks_escape(self):
        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp)
            with self.assertRaises(ValueError):
                resolve_within(base, "../escape.png")

    def test_list_closeups_finds_closeup_files(self):
        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp)
            (base / "closeups").mkdir()
            save_closeup_yaml(
                base / "closeups" / "skull.yml",
                {"background": "closeups/skull.png", "hotspots": {}},
            )
            # A non-close-up YAML (no background) must be ignored.
            save_closeup_yaml(base / "cast.yaml", {"characters": {"x": {}}})
            found = list_closeups(base)
            self.assertEqual(found, ["closeups/skull.yml"])
