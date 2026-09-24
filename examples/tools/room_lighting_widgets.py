"""ipywidgets presentation for the room render-control service."""

from __future__ import annotations

import copy
import json
import math
import time
from typing import Any

import ipywidgets as widgets
from IPython.display import display

from control_client import ControlClient


SCENE_FORMAT = "xadv2.room-render-scene/v1"
SCENE_FIELDS = ("ambient", "lights", "projected_shadow", "post_process")


def _hex_color(values: list[float]) -> str:
    channels = [max(0, min(255, round(component * 255))) for component in values]
    return "#" + "".join(f"{channel:02x}" for channel in channels)


def _float_color(value: str) -> list[float]:
    value = value.lstrip("#")
    return [int(value[index : index + 2], 16) / 255 for index in (0, 2, 4)]


def _scene_from_state(state: dict[str, Any]) -> dict[str, Any]:
    scene = {"format": SCENE_FORMAT, "room": state.get("room", "")}
    for field in SCENE_FIELDS:
        if field in state:
            scene[field] = copy.deepcopy(state[field])
    return scene


def _scene_payload(scene: dict[str, Any]) -> dict[str, Any]:
    return {field: copy.deepcopy(scene[field]) for field in SCENE_FIELDS if field in scene}


def _lerp(start: Any, target: Any, progress: float, field: str = "") -> Any:
    if progress >= 1:
        return copy.deepcopy(target)
    if field == "aim_at" and isinstance(start, dict) and isinstance(target, dict):
        start_fixed = "x" in start and "y" in start
        target_fixed = "x" in target and "y" in target
        if start_fixed != target_fixed:
            # A fixed coordinate and a symbolic target have different schemas;
            # keep the source reference valid until the final scene frame.
            return copy.deepcopy(start)
    if isinstance(start, bool) or isinstance(target, bool):
        return start
    if isinstance(start, (int, float)) and isinstance(target, (int, float)):
        if field == "direction":
            delta = (target - start + 180) % 360 - 180
            value = start + delta * progress
        else:
            value = start + (target - start) * progress
        return round(value) if isinstance(start, int) and isinstance(target, int) else value
    if isinstance(start, list) and isinstance(target, list) and len(start) == len(target):
        return [_lerp(a, b, progress, field) for a, b in zip(start, target)]
    if isinstance(start, dict) and isinstance(target, dict):
        result = {}
        for key in start.keys() | target.keys():
            if key in start and key in target:
                result[key] = _lerp(start[key], target[key], progress, key)
            elif progress >= 1 and key in target:
                result[key] = copy.deepcopy(target[key])
            elif key in start:
                result[key] = copy.deepcopy(start[key])
        return result
    return copy.deepcopy(target if progress >= 1 else start)


def _blend_scenes(start: dict[str, Any], target: dict[str, Any], progress: float) -> dict[str, Any]:
    """Interpolate compatible snapshots, preserving light identity by id."""
    progress = max(0.0, min(1.0, progress))
    result = _lerp(start, target, progress)

    start_lights = {light["id"]: light for light in start.get("lights", [])}
    target_lights = {light["id"]: light for light in target.get("lights", [])}
    lights = []
    for target_light in target.get("lights", []):
        light_id = target_light["id"]
        start_light = start_lights[light_id]
        light = _lerp(start_light, target_light, progress)
        start_enabled = start_light.get("enabled", True)
        target_enabled = target_light.get("enabled", True)
        light["enabled"] = target_enabled if progress >= 1 else start_enabled or target_enabled
        start_intensity = start_light.get("intensity", 0) if start_enabled else 0
        target_intensity = target_light.get("intensity", 0) if target_enabled else 0
        light["intensity"] = _lerp(start_intensity, target_intensity, progress, "intensity")
        lights.append(light)
    if target_lights:
        result["lights"] = lights

    start_shadow = start.get("projected_shadow")
    target_shadow = target.get("projected_shadow")
    if start_shadow and target_shadow:
        shadow = _lerp(start_shadow, target_shadow, progress)
        start_enabled = start_shadow.get("enabled", True)
        target_enabled = target_shadow.get("enabled", True)
        shadow["enabled"] = target_enabled if progress >= 1 else start_enabled or target_enabled
        start_opacity = start_shadow.get("opacity", 0) if start_enabled else 0
        target_opacity = target_shadow.get("opacity", 0) if target_enabled else 0
        shadow["opacity"] = _lerp(start_opacity, target_opacity, progress, "opacity")
        result["projected_shadow"] = shadow

    return result


class RoomLightingPanel:
    """A deliberately thin UI: every mutation is one semantic RPC call."""

    def __init__(
        self,
        client: ControlClient,
        scene_collection: dict[str, dict[str, Any]] | None = None,
    ):
        self.client = client
        self.state: dict[str, Any] = {}
        self.scenes = scene_collection if scene_collection is not None else {}
        self.copied_scene: dict[str, Any] | None = None
        self._syncing = False
        self.status = widgets.HTML()
        self.light = widgets.Dropdown(description="Light")
        self.light_enabled = widgets.Checkbox(description="Enabled")
        self.light_intensity = widgets.FloatSlider(
            description="Intensity", min=0, max=4, step=0.01, continuous_update=False
        )
        self.light_direction = widgets.FloatSlider(
            description="Direction", min=-180, max=360, step=0.5, continuous_update=False
        )
        self.light_aim_mode = widgets.Dropdown(
            description="Aim mode",
            options=[("Direction", "direction"), ("Fixed point", "point"), ("Track target", "target")],
        )
        self.light_aim_x = widgets.FloatSlider(
            description="Aim X", min=-2000, max=4000, step=1, continuous_update=False
        )
        self.light_aim_y = widgets.FloatSlider(
            description="Aim Y", min=-2000, max=4000, step=1, continuous_update=False
        )
        self.light_aim_target = widgets.Text(
            description="Target", placeholder="player / avatar:id / object:id", continuous_update=False
        )
        self.light_aim_anchor = widgets.Text(
            description="Anchor", placeholder="pivot / center / named anchor", continuous_update=False
        )
        self.light_aim_offset_x = widgets.FloatSlider(
            description="Aim off X", min=-1000, max=1000, step=1, continuous_update=False
        )
        self.light_aim_offset_y = widgets.FloatSlider(
            description="Aim off Y", min=-1000, max=1000, step=1, continuous_update=False
        )
        self.light_range = widgets.FloatSlider(
            description="Range", min=1, max=3000, step=1, continuous_update=False
        )
        self.light_height = widgets.FloatSlider(
            description="Height", min=0, max=2000, step=1, continuous_update=False
        )
        self.light_x = widgets.FloatSlider(
            description="X", min=-2000, max=4000, step=1, continuous_update=False
        )
        self.light_y = widgets.FloatSlider(
            description="Y", min=-2000, max=4000, step=1, continuous_update=False
        )
        self.light_angle = widgets.FloatSlider(
            description="Cone angle", min=1, max=179, step=0.5, continuous_update=False
        )
        self.light_softness = widgets.FloatSlider(
            description="Edge soft", min=0, max=89, step=0.5, continuous_update=False
        )
        self.light_beam_width = widgets.FloatSlider(
            description="Beam width", min=0, max=500, step=1, continuous_update=False
        )
        self.light_color = widgets.ColorPicker(description="Color")
        self.ambient_intensity = widgets.FloatSlider(
            description="Ambient", min=0, max=1, step=0.01, continuous_update=False
        )
        self.ambient_color = widgets.ColorPicker(description="Ambient color")
        self.shadow_enabled = widgets.Checkbox(description="Enabled")
        self.shadow_casters = widgets.Dropdown(
            description="Casters", options=[("Player", "player"), ("All avatars", "all")]
        )
        self.shadow_length = widgets.FloatSlider(
            description="Length", min=0.01, max=2, step=0.01, continuous_update=False
        )
        self.shadow_width = widgets.FloatSlider(
            description="Width", min=0.01, max=2, step=0.01, continuous_update=False
        )
        self.shadow_opacity = widgets.FloatSlider(
            description="Opacity", min=0, max=1, step=0.01, continuous_update=False
        )
        self.shadow_softness = widgets.FloatSlider(
            description="Softness", min=0, max=32, step=0.25, continuous_update=False
        )
        self.shadow_contact = widgets.FloatSlider(
            description="Contact", min=0, max=1, step=0.01, continuous_update=False
        )
        self.shadow_color = widgets.ColorPicker(description="Color")
        self.shadow_fixed_depth = widgets.Checkbox(description="Fixed depth")
        self.shadow_depth = widgets.FloatSlider(
            description="Depth", min=-2000, max=4000, step=1, continuous_update=False
        )
        self.shadow_source = widgets.HTML()
        self.shadow_controls = widgets.VBox(
            [
                self.shadow_enabled,
                self.shadow_casters,
                self.shadow_source,
                self.shadow_length,
                self.shadow_width,
                self.shadow_opacity,
                self.shadow_softness,
                self.shadow_contact,
                self.shadow_color,
                self.shadow_fixed_depth,
                self.shadow_depth,
            ]
        )
        self.post_enabled = widgets.Checkbox(description="Post-process")
        self.grade_controls = widgets.VBox()
        self.scene_name = widgets.Text(description="Name", placeholder="e.g. Cold moonlight")
        self.scene_select = widgets.Dropdown(description="Scene")
        self.scene_duration = widgets.FloatSlider(
            description="Blend (s)", min=0.1, max=10, value=2, step=0.1, continuous_update=False
        )
        self.scene_status = widgets.HTML()
        self.scene_dictionary = widgets.Textarea(
            description="Dictionary", layout=widgets.Layout(width="100%", height="180px")
        )
        copy_scene = widgets.Button(description="Capture dict", icon="copy")
        save_scene = widgets.Button(description="Add scene", icon="plus")
        remove_scene = widgets.Button(description="Remove", icon="trash")
        apply_scene = widgets.Button(description="Set scene", icon="check")
        blend_scene = widgets.Button(description="Blend to scene", icon="adjust")
        copy_scene.on_click(self._copy_scene)
        save_scene.on_click(self._save_scene)
        remove_scene.on_click(self._remove_scene)
        apply_scene.on_click(self._apply_selected_scene)
        blend_scene.on_click(self._blend_selected_scene)
        self.scene_select.observe(self._select_scene, names="value")
        self.scene_controls = widgets.VBox(
            [
                widgets.HBox([self.scene_name, copy_scene, save_scene]),
                widgets.HBox([self.scene_select, apply_scene, remove_scene]),
                widgets.HBox([self.scene_duration, blend_scene]),
                self.scene_status,
                self.scene_dictionary,
            ]
        )
        self.export = widgets.Textarea(description="YAML", layout=widgets.Layout(width="100%", height="220px"))
        refresh = widgets.Button(description="Refresh", icon="refresh")
        reset = widgets.Button(description="Reset authored", icon="undo")
        export = widgets.Button(description="Export YAML", icon="code")
        refresh.on_click(lambda _button: self.refresh())
        reset.on_click(lambda _button: self._call("room.render.reset", {}))
        export.on_click(self._export)
        self.light.observe(self._select_light, names="value")
        self.light_enabled.observe(lambda change: self._set_light("enabled", change["new"]), names="value")
        self.light_intensity.observe(
            lambda change: self._set_light("intensity", change["new"]), names="value"
        )
        self.light_direction.observe(
            lambda change: self._set_light("direction", change["new"]), names="value"
        )
        self.light_aim_mode.observe(self._set_light_aim_mode, names="value")
        self.light_aim_x.observe(
            lambda change: self._set_light_aim_position("x", change["new"]), names="value"
        )
        self.light_aim_y.observe(
            lambda change: self._set_light_aim_position("y", change["new"]), names="value"
        )
        self.light_aim_target.observe(lambda _change: self._set_light_aim_target(), names="value")
        self.light_aim_anchor.observe(lambda _change: self._set_light_aim_target(), names="value")
        self.light_aim_offset_x.observe(
            lambda _change: self._set_light_aim_target(), names="value"
        )
        self.light_aim_offset_y.observe(
            lambda _change: self._set_light_aim_target(), names="value"
        )
        self.light_range.observe(
            lambda change: self._set_light("radius", change["new"]), names="value"
        )
        self.light_height.observe(
            lambda change: self._set_light("height", change["new"]), names="value"
        )
        self.light_x.observe(
            lambda change: self._set_light_position("x", change["new"]), names="value"
        )
        self.light_y.observe(
            lambda change: self._set_light_position("y", change["new"]), names="value"
        )
        self.light_angle.observe(
            lambda change: self._set_light("angle", change["new"]), names="value"
        )
        self.light_softness.observe(
            lambda change: self._set_light("softness", change["new"]), names="value"
        )
        self.light_beam_width.observe(
            lambda change: self._set_light("beam_width", change["new"]), names="value"
        )
        self.light_color.observe(
            lambda change: self._set_light("color", _float_color(change["new"])), names="value"
        )
        self.ambient_intensity.observe(
            lambda change: self._set_ambient("intensity", change["new"]), names="value"
        )
        self.ambient_color.observe(
            lambda change: self._set_ambient("color", _float_color(change["new"])), names="value"
        )
        self.shadow_enabled.observe(
            lambda change: self._set_shadow("enabled", change["new"]), names="value"
        )
        self.shadow_casters.observe(
            lambda change: self._set_shadow("casters", change["new"]), names="value"
        )
        self.shadow_length.observe(
            lambda change: self._set_shadow("length", change["new"]), names="value"
        )
        self.shadow_width.observe(
            lambda change: self._set_shadow("width", change["new"]), names="value"
        )
        self.shadow_opacity.observe(
            lambda change: self._set_shadow("opacity", change["new"]), names="value"
        )
        self.shadow_softness.observe(
            lambda change: self._set_shadow("softness", change["new"]), names="value"
        )
        self.shadow_contact.observe(
            lambda change: self._set_shadow("contact_shadow", change["new"]), names="value"
        )
        self.shadow_color.observe(
            lambda change: self._set_shadow("color", _float_color(change["new"])), names="value"
        )
        self.shadow_fixed_depth.observe(self._set_shadow_fixed_depth, names="value")
        self.shadow_depth.observe(
            lambda change: self._set_shadow("z", change["new"]), names="value"
        )
        self.post_enabled.observe(self._set_post_enabled, names="value")
        self.widget = widgets.VBox(
            [
                widgets.HBox([refresh, reset, export]),
                self.status,
                widgets.HTML("<h4>Ambient</h4>"),
                self.ambient_intensity,
                self.ambient_color,
                widgets.HTML("<h4>Spot/omni lights</h4>"),
                self.light,
                self.light_enabled,
                self.light_intensity,
                self.light_color,
                widgets.HTML("<b>Placement and area</b>"),
                self.light_x,
                self.light_y,
                self.light_range,
                self.light_height,
                self.light_direction,
                self.light_aim_mode,
                self.light_aim_x,
                self.light_aim_y,
                self.light_aim_target,
                self.light_aim_anchor,
                self.light_aim_offset_x,
                self.light_aim_offset_y,
                self.light_angle,
                self.light_softness,
                self.light_beam_width,
                widgets.HTML("<h4>Projected shadow</h4>"),
                self.shadow_controls,
                widgets.HTML("<h4>Final grade</h4>"),
                self.post_enabled,
                self.grade_controls,
                widgets.HTML("<h4>Lighting scenes</h4>"),
                self.scene_controls,
                self.export,
            ]
        )
        self._refresh_scene_options()
        self.refresh()

    def _call(self, method: str, params: dict[str, Any]) -> None:
        try:
            self.state = self.client.call(method, params)
            self.status.value = f"<span style='color:#2a7'>Connected · {self.state.get('room', '')}</span>"
            self._sync()
        except Exception as error:  # notebook surface: show errors instead of losing callback tracebacks
            self.status.value = f"<span style='color:#b22'>{type(error).__name__}: {error}</span>"

    def refresh(self) -> None:
        self._call("room.render.get", {})

    def _sync(self) -> None:
        self._syncing = True
        try:
            ambient = self.state.get("ambient", {})
            self.ambient_intensity.value = ambient.get("intensity", 0)
            self.ambient_color.value = _hex_color(ambient.get("color", [1, 1, 1]))
            lights = self.state.get("lights", [])
            previous = self.light.value
            self.light.options = [(item["id"], item["id"]) for item in lights]
            if previous in dict(self.light.options).values():
                self.light.value = previous
            elif lights:
                self.light.value = lights[0]["id"]
            self._sync_selected_light()
            self._sync_shadow()
            post = self.state.get("post_process", {})
            self.post_enabled.value = post.get("enabled", False)
            self._build_grade_controls(post)
        finally:
            self._syncing = False

    def _selected(self) -> dict[str, Any] | None:
        return next((item for item in self.state.get("lights", []) if item["id"] == self.light.value), None)

    def _select_light(self, _change: dict[str, Any]) -> None:
        if not self._syncing:
            self._syncing = True
            try:
                self._sync_selected_light()
            finally:
                self._syncing = False

    def _sync_selected_light(self) -> None:
        selected = self._selected()
        if not selected:
            return
        self.light_enabled.value = selected["enabled"]
        self.light_intensity.value = selected["intensity"]
        radius = selected.get("radius", 1)
        self.light_range.max = max(3000, radius * 1.25)
        self.light_range.value = radius
        height = selected.get("height", 0)
        self.light_height.max = max(2000, height * 1.25)
        self.light_height.value = height
        position = selected.get("at", {"x": 0, "y": 0})
        x = position.get("x", 0)
        y = position.get("y", 0)
        self.light_x.min = min(-2000, x - 1000)
        self.light_x.max = max(4000, x + 1000)
        self.light_y.min = min(-2000, y - 1000)
        self.light_y.max = max(4000, y + 1000)
        self.light_x.value = x
        self.light_y.value = y
        attached = bool(selected.get("attach"))
        self.light_x.disabled = attached
        self.light_y.disabled = attached
        spot = selected.get("type") == "spot"
        aim = selected.get("aim_at")
        fixed_aim = aim if isinstance(aim, dict) and "x" in aim and "y" in aim else None
        tracked_aim = aim if isinstance(aim, dict) and "target" in aim else None
        if isinstance(aim, str):
            tracked_aim = {"target": aim}
        direction = selected.get("direction", 0)
        if fixed_aim and not selected.get("attach"):
            direction = math.degrees(
                math.atan2(fixed_aim["y"] - y, fixed_aim["x"] - x)
            )
        self.light_direction.value = direction
        self.light_aim_mode.value = (
            "target" if tracked_aim else "point" if fixed_aim else "direction"
        )
        if fixed_aim:
            aim_x = fixed_aim.get("x", 0)
            aim_y = fixed_aim.get("y", 0)
            self.light_aim_x.min = min(-2000, aim_x - 1000)
            self.light_aim_x.max = max(4000, aim_x + 1000)
            self.light_aim_y.min = min(-2000, aim_y - 1000)
            self.light_aim_y.max = max(4000, aim_y + 1000)
            self.light_aim_x.value = aim_x
            self.light_aim_y.value = aim_y
        self.light_aim_target.value = tracked_aim.get("target", "player") if tracked_aim else "player"
        self.light_aim_anchor.value = tracked_aim.get("anchor", "") if tracked_aim else ""
        offset = tracked_aim.get("offset", {}) if tracked_aim else {}
        self.light_aim_offset_x.value = offset.get("x", 0)
        self.light_aim_offset_y.value = offset.get("y", 0)
        self.light_direction.disabled = aim is not None
        self.light_aim_x.disabled = fixed_aim is None
        self.light_aim_y.disabled = fixed_aim is None
        angle = selected.get("angle", 45)
        self.light_angle.value = angle
        self.light_softness.max = max(0.1, angle / 2 - 0.1)
        self.light_softness.value = min(selected.get("softness", 0), self.light_softness.max)
        beam_width = selected.get("beam_width", 0)
        self.light_beam_width.max = max(500, beam_width * 1.25)
        self.light_beam_width.value = beam_width
        for control in (
            self.light_direction,
            self.light_aim_mode,
            self.light_aim_x,
            self.light_aim_y,
            self.light_aim_target,
            self.light_aim_anchor,
            self.light_aim_offset_x,
            self.light_aim_offset_y,
            self.light_angle,
            self.light_softness,
            self.light_beam_width,
        ):
            control.layout.display = "" if spot else "none"
        self.light_aim_x.layout.display = "" if spot and fixed_aim else "none"
        self.light_aim_y.layout.display = "" if spot and fixed_aim else "none"
        for control in (
            self.light_aim_target,
            self.light_aim_anchor,
            self.light_aim_offset_x,
            self.light_aim_offset_y,
        ):
            control.layout.display = "" if spot and tracked_aim else "none"
        self.light_color.value = _hex_color(selected["color"])

    def _sync_shadow(self) -> None:
        shadow = self.state.get("projected_shadow")
        self.shadow_controls.layout.display = "" if shadow else "none"
        if not shadow:
            return
        self.shadow_enabled.value = shadow.get("enabled", True)
        self.shadow_casters.value = shadow.get("casters", "player")
        sources = shadow.get("sources")
        if sources:
            source_text = ", ".join(sources)
        elif "source" in shadow:
            source_text = shadow["source"]
        else:
            point = shadow.get("light", {})
            source_text = f"fixed ({point.get('x', 0):g}, {point.get('y', 0):g})"
        self.shadow_source.value = f"<b>Source:</b> {source_text}"
        for control, key in (
            (self.shadow_length, "length"),
            (self.shadow_width, "width"),
            (self.shadow_opacity, "opacity"),
            (self.shadow_softness, "softness"),
            (self.shadow_contact, "contact_shadow"),
        ):
            value = shadow.get(key, 0)
            control.max = max(control.max, value * 1.25)
            control.value = value
        self.shadow_color.value = _hex_color(shadow.get("color", [12 / 255, 14 / 255, 18 / 255]))
        depth = shadow.get("z")
        self.shadow_fixed_depth.value = depth is not None
        self.shadow_depth.disabled = depth is None
        if depth is not None:
            self.shadow_depth.min = min(-2000, depth - 1000)
            self.shadow_depth.max = max(4000, depth + 1000)
            self.shadow_depth.value = depth

    def _set_light(self, field: str, value: Any) -> None:
        if not self._syncing and self.light.value:
            self._call("room.render.set_light", {"id": self.light.value, field: value})

    def _set_light_position(self, axis: str, value: float) -> None:
        if self._syncing or not self.light.value:
            return
        selected = self._selected()
        if not selected or selected.get("attach"):
            return
        position = dict(selected.get("at", {"x": 0, "y": 0}))
        position[axis] = value
        self._call("room.render.set_light", {"id": self.light.value, "at": position})

    def _set_light_aim_mode(self, change: dict[str, Any]) -> None:
        if self._syncing or not self.light.value:
            return
        selected = self._selected()
        if not selected or selected.get("type") != "spot":
            return
        position = selected.get("at", {"x": 0, "y": 0})
        if change["new"] == "point":
            radians = math.radians(selected.get("direction", 0))
            distance = selected.get("radius", 100)
            target = {
                "x": position.get("x", 0) + math.cos(radians) * distance,
                "y": position.get("y", 0) + math.sin(radians) * distance,
            }
            self._call("room.render.set_light", {"id": self.light.value, "aim_at": target})
        elif change["new"] == "target":
            self._set_light_aim_target(force=True)
        else:
            target = selected.get("aim_at")
            if isinstance(target, dict) and "x" in target and "y" in target:
                direction = math.degrees(
                    math.atan2(
                        target.get("y", 0) - position.get("y", 0),
                        target.get("x", 0) - position.get("x", 0),
                    )
                )
                self._call(
                    "room.render.set_light", {"id": self.light.value, "direction": direction}
                )
            else:
                self._call(
                    "room.render.set_light",
                    {"id": self.light.value, "direction": self.light_direction.value},
                )

    def _set_light_aim_position(self, axis: str, value: float) -> None:
        if self._syncing or not self.light.value:
            return
        selected = self._selected()
        target = selected.get("aim_at") if selected else None
        if not isinstance(target, dict) or "x" not in target or "y" not in target:
            return
        target = dict(target)
        target[axis] = value
        self._call("room.render.set_light", {"id": self.light.value, "aim_at": target})

    def _set_light_aim_target(self, force: bool = False) -> None:
        if self._syncing or not self.light.value:
            return
        if not force and self.light_aim_mode.value != "target":
            return
        target = self.light_aim_target.value.strip()
        if not target:
            return
        aim: dict[str, Any] = {"target": target}
        anchor = self.light_aim_anchor.value.strip()
        if anchor:
            aim["anchor"] = anchor
        offset_x = self.light_aim_offset_x.value
        offset_y = self.light_aim_offset_y.value
        if offset_x != 0 or offset_y != 0:
            aim["offset"] = {"x": offset_x, "y": offset_y}
        self._call("room.render.set_light", {"id": self.light.value, "aim_at": aim})

    def _set_ambient(self, field: str, value: Any) -> None:
        if not self._syncing:
            self._call("room.render.set_ambient", {field: value})

    def _set_shadow(self, field: str, value: Any) -> None:
        if not self._syncing and self.state.get("projected_shadow"):
            self._call("room.render.set_shadow", {field: value})

    def _set_shadow_fixed_depth(self, change: dict[str, Any]) -> None:
        if self._syncing or not self.state.get("projected_shadow"):
            return
        self._set_shadow("z", self.shadow_depth.value if change["new"] else None)

    def _set_post_enabled(self, change: dict[str, Any]) -> None:
        if not self._syncing:
            self._call("room.render.set_grade", {"enabled": change["new"]})

    def _build_grade_controls(self, post: dict[str, Any]) -> None:
        effects = post.get("effects", [])
        params = effects[0].get("params", {}) if effects else {}
        controls = []
        for name, value in params.items():
            if isinstance(value, (int, float)) and not isinstance(value, bool):
                slider = widgets.FloatSlider(
                    description=name,
                    value=value,
                    min=-1 if name == "brightness" else 0,
                    max=4 if name in {"contrast", "saturation"} else 2,
                    step=0.01,
                    continuous_update=False,
                )
                slider.observe(
                    lambda change, param=name: self._set_grade_param(param, change["new"]),
                    names="value",
                )
                controls.append(slider)
        self.grade_controls.children = tuple(controls)

    def _set_grade_param(self, name: str, value: float) -> None:
        if not self._syncing:
            self._call("room.render.set_grade", {"effect": 0, "params": {name: value}})

    def capture_scene(self) -> dict[str, Any]:
        """Return an independent dictionary snapshot of the live render state."""
        state = self.client.call("room.render.get", {})
        self.state = state
        self._sync()
        return _scene_from_state(state)

    def save_scene(self, name: str, scene: dict[str, Any] | None = None) -> dict[str, Any]:
        """Capture or copy a snapshot into the panel's named scene collection."""
        name = name.strip()
        if not name:
            raise ValueError("scene name cannot be empty")
        saved = copy.deepcopy(scene) if scene is not None else self.capture_scene()
        self._validate_scene(saved)
        self.scenes[name] = saved
        self._refresh_scene_options(name)
        return copy.deepcopy(saved)

    def apply_scene(self, scene: str | dict[str, Any]) -> dict[str, Any]:
        """Apply a named scene or a scene dictionary immediately."""
        target = self._resolve_scene(scene)
        self._validate_scene(target)
        self.state = self.client.call("room.render.set_scene", _scene_payload(target))
        self._sync()
        return self.state

    def blend_to_scene(
        self,
        scene: str | dict[str, Any],
        seconds: float = 2.0,
        frames_per_second: float = 20.0,
    ) -> dict[str, Any]:
        """Synchronously crossfade the live room to a scene using smoothstep easing."""
        target = self._resolve_scene(scene)
        self._validate_scene(target)
        if seconds <= 0:
            return self.apply_scene(target)
        if frames_per_second <= 0:
            raise ValueError("frames_per_second must be positive")
        start = _scene_from_state(self.client.call("room.render.get", {}))
        self._validate_compatible_scenes(start, target)
        steps = max(1, math.ceil(seconds * frames_per_second))
        started = time.monotonic()
        for step in range(1, steps + 1):
            linear = step / steps
            eased = linear * linear * (3 - 2 * linear)
            blended = _blend_scenes(start, target, eased)
            self.state = self.client.call("room.render.set_scene", _scene_payload(blended))
            remaining = started + seconds * linear - time.monotonic()
            if remaining > 0 and step < steps:
                time.sleep(remaining)
        self._sync()
        return self.state

    def _resolve_scene(self, scene: str | dict[str, Any]) -> dict[str, Any]:
        if isinstance(scene, str):
            if scene not in self.scenes:
                raise KeyError(f"unknown scene: {scene}")
            return copy.deepcopy(self.scenes[scene])
        return copy.deepcopy(scene)

    def _validate_scene(self, scene: dict[str, Any]) -> None:
        if scene.get("format") != SCENE_FORMAT:
            raise ValueError(f"scene must use format {SCENE_FORMAT!r}")
        live_room = self.state.get("room")
        if live_room and scene.get("room") != live_room:
            raise ValueError(f"scene belongs to room {scene.get('room')!r}, not {live_room!r}")
        if not isinstance(scene.get("lights"), list):
            raise ValueError("scene must contain a lights list")

    @staticmethod
    def _validate_compatible_scenes(start: dict[str, Any], target: dict[str, Any]) -> None:
        start_ids = {light.get("id") for light in start.get("lights", [])}
        target_ids = {light.get("id") for light in target.get("lights", [])}
        if start_ids != target_ids:
            raise ValueError("blended scenes must contain the same light ids")
        start_effects = start.get("post_process", {}).get("effects", [])
        target_effects = target.get("post_process", {}).get("effects", [])
        if [effect.get("source") for effect in start_effects] != [
            effect.get("source") for effect in target_effects
        ]:
            raise ValueError("blended scenes must contain the same post-process effects")

    def _refresh_scene_options(self, selected: str | None = None) -> None:
        previous = selected or self.scene_select.value
        names = list(self.scenes)
        self.scene_select.options = names
        if previous in names:
            self.scene_select.value = previous
        elif names:
            self.scene_select.value = names[0]

    def _show_scene(self, scene: dict[str, Any]) -> None:
        self.scene_dictionary.value = json.dumps(scene, indent=2, sort_keys=True)

    def _copy_scene(self, _button: widgets.Button) -> None:
        try:
            self.copied_scene = self.capture_scene()
            self._show_scene(self.copied_scene)
            self.scene_status.value = "<span style='color:#2a7'>Current settings copied to panel.copied_scene</span>"
        except Exception as error:
            self._scene_error(error)

    def _save_scene(self, _button: widgets.Button) -> None:
        try:
            name = self.scene_name.value.strip() or f"Scene {len(self.scenes) + 1}"
            scene = self.save_scene(name)
            self.scene_name.value = name
            self._show_scene(scene)
            self.scene_status.value = f"<span style='color:#2a7'>Saved {name}</span>"
        except Exception as error:
            self._scene_error(error)

    def _remove_scene(self, _button: widgets.Button) -> None:
        name = self.scene_select.value
        if name in self.scenes:
            del self.scenes[name]
            self._refresh_scene_options()
            self.scene_dictionary.value = ""
            self.scene_status.value = f"Removed {name}"

    def _select_scene(self, change: dict[str, Any]) -> None:
        name = change["new"]
        if name in self.scenes:
            self._show_scene(self.scenes[name])

    def _apply_selected_scene(self, _button: widgets.Button) -> None:
        try:
            name = self.scene_select.value
            if not name:
                raise ValueError("save or select a scene first")
            self.apply_scene(name)
            self.scene_status.value = f"<span style='color:#2a7'>Applied {name}</span>"
        except Exception as error:
            self._scene_error(error)

    def _blend_selected_scene(self, _button: widgets.Button) -> None:
        try:
            name = self.scene_select.value
            if not name:
                raise ValueError("save or select a scene first")
            self.scene_status.value = f"Blending to {name}…"
            self.blend_to_scene(name, self.scene_duration.value)
            self.scene_status.value = f"<span style='color:#2a7'>Blended to {name}</span>"
        except Exception as error:
            self._scene_error(error)

    def _scene_error(self, error: Exception) -> None:
        self.scene_status.value = f"<span style='color:#b22'>{type(error).__name__}: {error}</span>"

    def _export(self, _button: widgets.Button) -> None:
        try:
            self.export.value = self.client.call("room.render.export_yaml", {})["yaml"]
        except Exception as error:
            self.status.value = f"<span style='color:#b22'>{type(error).__name__}: {error}</span>"

    def display(self) -> None:
        display(self.widget)
