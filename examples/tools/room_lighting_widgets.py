"""ipywidgets presentation for the room render-control service."""

from __future__ import annotations

from typing import Any

import ipywidgets as widgets
from IPython.display import display

from control_client import ControlClient


def _hex_color(values: list[float]) -> str:
    channels = [max(0, min(255, round(component * 255))) for component in values]
    return "#" + "".join(f"{channel:02x}" for channel in channels)


def _float_color(value: str) -> list[float]:
    value = value.lstrip("#")
    return [int(value[index : index + 2], 16) / 255 for index in (0, 2, 4)]


class RoomLightingPanel:
    """A deliberately thin UI: every mutation is one semantic RPC call."""

    def __init__(self, client: ControlClient):
        self.client = client
        self.state: dict[str, Any] = {}
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
        self.light_color = widgets.ColorPicker(description="Color")
        self.ambient_intensity = widgets.FloatSlider(
            description="Ambient", min=0, max=1, step=0.01, continuous_update=False
        )
        self.ambient_color = widgets.ColorPicker(description="Ambient color")
        self.post_enabled = widgets.Checkbox(description="Post-process")
        self.grade_controls = widgets.VBox()
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
        self.light_color.observe(
            lambda change: self._set_light("color", _float_color(change["new"])), names="value"
        )
        self.ambient_intensity.observe(
            lambda change: self._set_ambient("intensity", change["new"]), names="value"
        )
        self.ambient_color.observe(
            lambda change: self._set_ambient("color", _float_color(change["new"])), names="value"
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
                self.light_angle,
                self.light_softness,
                widgets.HTML("<h4>Final grade</h4>"),
                self.post_enabled,
                self.grade_controls,
                self.export,
            ]
        )
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
        self.light_direction.value = selected.get("direction", 0)
        spot = selected.get("type") == "spot"
        angle = selected.get("angle", 45)
        self.light_angle.value = angle
        self.light_softness.max = max(0.1, angle / 2 - 0.1)
        self.light_softness.value = min(selected.get("softness", 0), self.light_softness.max)
        for control in (self.light_direction, self.light_angle, self.light_softness):
            control.layout.display = "" if spot else "none"
        self.light_color.value = _hex_color(selected["color"])

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

    def _set_ambient(self, field: str, value: Any) -> None:
        if not self._syncing:
            self._call("room.render.set_ambient", {field: value})

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

    def _export(self, _button: widgets.Button) -> None:
        try:
            self.export.value = self.client.call("room.render.export_yaml", {})["yaml"]
        except Exception as error:
            self.status.value = f"<span style='color:#b22'>{type(error).__name__}: {error}</span>"

    def display(self) -> None:
        display(self.widget)
