from __future__ import annotations

import tkinter as tk
from tkinter import simpledialog, messagebox, ttk
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

try:
    from PIL import Image, ImageTk
except ImportError:
    Image = None  # type: ignore
    ImageTk = None  # type: ignore

from .room_data import load_room_yaml, save_room_yaml

POLYGON_COLORS = {
    "walkable": "#3b82f6",
    "zones": "#2563eb",
    "regions": "#8b5cf6",
    "hotspots": "#f97316",
}
POINT_COLOR = "#16a34a"
SELECTED_COLOR = "#ef4444"
HANDLE_SIZE = 6


def color_from_room(room: Dict[str, Any]) -> str:
    background = room.get("background", {})
    color = background.get("color") if isinstance(background, dict) else None
    if isinstance(color, dict):
        r = color.get("r", 0)
        g = color.get("g", 0)
        b = color.get("b", 0)
        return f"#{r:02x}{g:02x}{b:02x}"
    return "#111111"


def create_unique_point_id(points: Dict[str, Any], base: str = "point") -> str:
    index = 1
    name = f"{base}_{index}"
    while name in points:
        index += 1
        name = f"{base}_{index}"
    return name


def distance(a: Tuple[float, float], b: Tuple[float, float]) -> float:
    return ((a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2) ** 0.5


def point_near(pos: Tuple[float, float], target: Tuple[float, float], threshold: float = 10.0) -> bool:
    return distance(pos, target) <= threshold


class RoomEditorApp:
    def __init__(self, room_path: Path, base_path: Path):
        self.room_path = room_path
        self.base_path = base_path
        self.room: Dict[str, Any] = {}
        self.root = tk.Tk()
        self.layer_images: List[tk.PhotoImage] = []
        self.selected_mode = tk.StringVar(self.root, value="walkable")
        self.selected_entity: Optional[str] = None
        self.selected_vertex_index: Optional[int] = None
        self.selected_point_id: Optional[str] = None
        self.add_vertex_mode = False
        self.drag_start: Optional[Tuple[float, float]] = None
        self.drag_target: Optional[Tuple[str, int]] = None

        self.root.title(f"Room Editor - {self.room_path.name}")
        self.build_ui()
        self.load_room()

    def build_ui(self) -> None:
        self.root.columnconfigure(0, weight=1)
        self.root.rowconfigure(0, weight=1)

        main_frame = ttk.Frame(self.root, padding=8)
        main_frame.grid(sticky="nsew")
        main_frame.columnconfigure(1, weight=1)
        main_frame.rowconfigure(0, weight=1)

        control_frame = ttk.Frame(main_frame)
        control_frame.grid(row=0, column=0, sticky="ns", padx=(0, 8))
        control_frame.columnconfigure(0, weight=1)

        ttk.Label(control_frame, text="Room file:").grid(row=0, column=0, sticky="w")
        ttk.Label(control_frame, text=str(self.room_path), wraplength=220).grid(row=1, column=0, sticky="w")
        ttk.Label(control_frame, text="Asset base:").grid(row=2, column=0, sticky="w", pady=(8, 0))
        ttk.Label(control_frame, text=str(self.base_path), wraplength=220).grid(row=3, column=0, sticky="w")

        mode_frame = ttk.Labelframe(control_frame, text="Edit mode", padding=8)
        mode_frame.grid(row=4, column=0, sticky="ew", pady=(12, 0))
        for index, mode in enumerate(["walkable", "zones", "regions", "hotspots", "points"]):
            ttk.Radiobutton(
                mode_frame,
                text=mode.capitalize(),
                variable=self.selected_mode,
                value=mode,
                command=self.on_mode_changed,
            ).grid(row=index, column=0, sticky="w")

        entity_frame = ttk.Labelframe(control_frame, text="Entities", padding=8)
        entity_frame.grid(row=5, column=0, sticky="nsew", pady=(12, 0))
        entity_frame.rowconfigure(0, weight=1)
        entity_frame.columnconfigure(0, weight=1)

        self.entity_listbox = tk.Listbox(entity_frame, height=12, exportselection=False)
        self.entity_listbox.grid(row=0, column=0, sticky="nsew")
        self.entity_listbox.bind("<<ListboxSelect>>", lambda event: self.on_entity_selected())

        scrollbar = ttk.Scrollbar(entity_frame, orient="vertical", command=self.entity_listbox.yview)
        scrollbar.grid(row=0, column=1, sticky="ns")
        self.entity_listbox.config(yscrollcommand=scrollbar.set)

        actions_frame = ttk.Frame(control_frame)
        actions_frame.grid(row=6, column=0, sticky="ew", pady=(12, 0))
        actions_frame.columnconfigure((0, 1, 2), weight=1)

        self.add_button = ttk.Button(actions_frame, text="Add", command=self.on_add_entity)
        self.add_button.grid(row=0, column=0, sticky="ew")
        self.delete_button = ttk.Button(actions_frame, text="Delete", command=self.on_delete_entity)
        self.delete_button.grid(row=0, column=1, sticky="ew", padx=4)
        self.vertex_button = ttk.Button(actions_frame, text="Add vertex", command=self.on_toggle_add_vertex)
        self.vertex_button.grid(row=0, column=2, sticky="ew")

        edit_frame = ttk.Frame(control_frame)
        edit_frame.grid(row=7, column=0, sticky="ew", pady=(12, 0))
        edit_frame.columnconfigure((0, 1), weight=1)

        self.save_button = ttk.Button(edit_frame, text="Save", command=self.save_room)
        self.save_button.grid(row=0, column=0, sticky="ew")
        self.reload_button = ttk.Button(edit_frame, text="Reload", command=self.load_room)
        self.reload_button.grid(row=0, column=1, sticky="ew", padx=4)

        self.canvas = tk.Canvas(main_frame, background="#111111", width=900, height=640)
        self.canvas.grid(row=0, column=1, sticky="nsew")
        self.canvas.bind("<ButtonPress-1>", self.on_canvas_click)
        self.canvas.bind("<B1-Motion>", self.on_canvas_drag)
        self.canvas.bind("<ButtonRelease-1>", self.on_canvas_release)

        self.h_scroll = ttk.Scrollbar(main_frame, orient="horizontal", command=self.canvas.xview)
        self.h_scroll.grid(row=1, column=1, sticky="ew")
        self.v_scroll = ttk.Scrollbar(main_frame, orient="vertical", command=self.canvas.yview)
        self.v_scroll.grid(row=0, column=2, sticky="ns")
        self.canvas.configure(xscrollcommand=self.h_scroll.set, yscrollcommand=self.v_scroll.set)

    def load_room(self) -> None:
        try:
            self.room = load_room_yaml(self.room_path)
            if "points" not in self.room:
                self.room["points"] = {}
            self.selected_entity = None
            self.selected_vertex_index = None
            self.selected_point_id = None
            self.add_vertex_mode = False
            self.vertex_button.config(text="Add vertex")
            self.update_entity_list()
            self.redraw_canvas()
        except Exception as exc:
            messagebox.showerror("Error", f"Failed to load room YAML:\n{exc}")

    def save_room(self) -> None:
        try:
            save_room_yaml(self.room_path, self.room)
            messagebox.showinfo("Saved", f"Room saved to {self.room_path}")
        except Exception as exc:
            messagebox.showerror("Error", f"Failed to save room YAML:\n{exc}")

    def on_mode_changed(self) -> None:
        self.selected_entity = None
        self.selected_vertex_index = None
        self.selected_point_id = None
        self.update_entity_list()
        self.redraw_canvas()

    def update_entity_list(self) -> None:
        mode = self.selected_mode.get()
        self.entity_listbox.delete(0, tk.END)

        if mode == "walkable":
            self.entity_listbox.insert(tk.END, "walkable")
            self.selected_entity = "walkable"
        elif mode == "points":
            for key in sorted((self.room.get("points") or {}).keys()):
                self.entity_listbox.insert(tk.END, key)
        else:
            entities = self.get_entities_for_mode(mode)
            for key in sorted(entities.keys()):
                self.entity_listbox.insert(tk.END, key)

        if self.selected_entity is not None:
            items = self.entity_listbox.get(0, tk.END)
            for index, item in enumerate(items):
                if item == self.selected_entity:
                    self.entity_listbox.selection_set(index)
                    break

    def on_entity_selected(self) -> None:
        selection = self.entity_listbox.curselection()
        if not selection:
            return
        index = selection[0]
        self.selected_entity = self.entity_listbox.get(index)
        self.selected_vertex_index = None
        self.selected_point_id = self.selected_entity if self.selected_mode.get() == "points" else None
        self.redraw_canvas()

    def on_add_entity(self) -> None:
        mode = self.selected_mode.get()
        if mode == "points":
            self.add_point()
        elif mode in {"zones", "regions", "hotspots"}:
            self.add_polygon_entity(mode)
        else:
            messagebox.showinfo("Add", "Walkable is a single polygon and cannot be added.")

    def on_delete_entity(self) -> None:
        mode = self.selected_mode.get()
        if mode == "points":
            self.delete_point()
        elif mode in {"zones", "regions", "hotspots"}:
            self.delete_polygon_entity(mode)
        else:
            messagebox.showinfo("Delete", "Walkable cannot be deleted.")

    def on_toggle_add_vertex(self) -> None:
        self.add_vertex_mode = not self.add_vertex_mode
        self.vertex_button.config(text="Cancel" if self.add_vertex_mode else "Add vertex")
        if self.add_vertex_mode:
            messagebox.showinfo("Vertex mode", "Click on the canvas to add a vertex to the selected polygon.")

    def add_point(self) -> None:
        name = simpledialog.askstring("Point ID", "Enter the point id:")
        if not name:
            return
        points = self.room.setdefault("points", {})
        if name in points:
            messagebox.showwarning("Duplicate", "A point with that id already exists.")
            return
        x = self.canvas.canvasx(self.canvas.winfo_width() / 2)
        y = self.canvas.canvasy(self.canvas.winfo_height() / 2)
        points[name] = {"x": int(x), "y": int(y)}
        self.update_entity_list()
        self.selected_entity = name
        self.selected_point_id = name
        self.redraw_canvas()

    def add_polygon_entity(self, mode: str) -> None:
        entity_id = simpledialog.askstring("Entity ID", f"Enter the {mode[:-1]} id:")
        if not entity_id:
            return
        if mode == "zones":
            if any(zone.get("id") == entity_id for zone in self.room.get("zones", [])):
                messagebox.showwarning("Duplicate", "A zone with that id already exists.")
                return
            self.room.setdefault("zones", []).append({"id": entity_id, "polygon": []})
        elif mode == "regions":
            regions = self.room.setdefault("regions", {})
            if entity_id in regions:
                messagebox.showwarning("Duplicate", "A region with that id already exists.")
                return
            regions[entity_id] = {"area": [], "z": 0, "states": {}, "initial": ""}
        elif mode == "hotspots":
            hotspots = self.room.setdefault("hotspots", {})
            if entity_id in hotspots:
                messagebox.showwarning("Duplicate", "A hotspot with that id already exists.")
                return
            hotspots[entity_id] = {"name": entity_id, "area": [], "affordances": ["look_at"]}
        self.update_entity_list()
        self.selected_entity = entity_id
        self.redraw_canvas()

    def delete_point(self) -> None:
        if not self.selected_point_id:
            messagebox.showinfo("Delete", "Select a point first.")
            return
        points = self.room.get("points", {})
        if self.selected_point_id in points:
            del points[self.selected_point_id]
            self.selected_point_id = None
            self.selected_entity = None
            self.update_entity_list()
            self.redraw_canvas()

    def delete_polygon_entity(self, mode: str) -> None:
        if not self.selected_entity:
            messagebox.showinfo("Delete", "Select an entity first.")
            return
        if mode == "zones":
            zones = self.room.get("zones", [])
            self.room["zones"] = [zone for zone in zones if zone.get("id") != self.selected_entity]
        elif mode == "regions":
            regions = self.room.get("regions", {})
            regions.pop(self.selected_entity, None)
        elif mode == "hotspots":
            hotspots = self.room.get("hotspots", {})
            hotspots.pop(self.selected_entity, None)
        self.selected_entity = None
        self.selected_vertex_index = None
        self.update_entity_list()
        self.redraw_canvas()

    def get_entities_for_mode(self, mode: str) -> Dict[str, Any]:
        if mode == "zones":
            return {zone.get("id", ""): zone for zone in self.room.get("zones", []) if isinstance(zone, dict)}
        if mode == "regions":
            return {region_id: region for region_id, region in (self.room.get("regions") or {}).items() if isinstance(region, dict)}
        if mode == "hotspots":
            return {hotspot_id: hotspot for hotspot_id, hotspot in (self.room.get("hotspots") or {}).items() if isinstance(hotspot, dict)}
        return {}

    def get_selected_polygon(self) -> Optional[List[Dict[str, Any]]]:
        mode = self.selected_mode.get()
        if mode == "walkable":
            return self.room.get("walkable")
        if not self.selected_entity:
            return None
        if mode == "zones":
            for zone in self.room.get("zones", []):
                if zone.get("id") == self.selected_entity:
                    return zone.get("polygon")
        if mode == "regions":
            region = (self.room.get("regions") or {}).get(self.selected_entity)
            if region:
                return region.get("area")
        if mode == "hotspots":
            hotspot = (self.room.get("hotspots") or {}).get(self.selected_entity)
            if hotspot:
                return hotspot.get("area")
        return None

    def redraw_canvas(self) -> None:
        self.canvas.delete("all")
        room_width = int(self.room.get("size", {}).get("width", 0) or 0)
        room_height = int(self.room.get("size", {}).get("height", 0) or 0)
        room_width = max(room_width, 800)
        room_height = max(room_height, 600)

        self.canvas.configure(scrollregion=(0, 0, room_width, room_height))
        self.canvas.create_rectangle(0, 0, room_width, room_height, fill=color_from_room(self.room), outline="")

        self.draw_background_layers()
        self.draw_polygons("walkable", [self.room.get("walkable") or []], fill="#22c55e", outline="#15803d")
        self.draw_polygons("zones", [zone.get("polygon") for zone in self.room.get("zones", [])], fill="#60a5fa", outline="#1d4ed8")
        self.draw_polygons("regions", [region.get("area") for region in (self.room.get("regions") or {}).values()], fill="#c084fc", outline="#7c3aed")
        self.draw_polygons("hotspots", [hotspot.get("area") for hotspot in (self.room.get("hotspots") or {}).values()], fill="#fb923c", outline="#ea580c")
        self.draw_points()
        self.draw_selected_handles()

    def resolve_asset_path(self, logical_path: str) -> Optional[Path]:
        candidate = self.base_path / logical_path
        if candidate.exists():
            return candidate
        candidate = self.room_path.parent / logical_path
        if candidate.exists():
            return candidate
        return None

    def draw_background_layers(self) -> None:
        self.layer_images.clear()
        background = self.room.get("background") or {}
        layers = background.get("layers") if isinstance(background, dict) else None
        if not isinstance(layers, list):
            return

        for index, layer in enumerate(layers):
            if not isinstance(layer, dict):
                continue
            image_path = layer.get("image")
            if not isinstance(image_path, str):
                continue
            resolved = self.resolve_asset_path(image_path)
            if resolved is not None:
                try:
                    if Image and ImageTk:
                        pil_image = Image.open(resolved)
                        tk_image = ImageTk.PhotoImage(pil_image)
                    else:
                        tk_image = tk.PhotoImage(file=str(resolved))
                    self.layer_images.append(tk_image)
                    self.canvas.create_image(0, 0, image=tk_image, anchor="nw", tags=("layer", layer.get("id", f"layer_{index}")))
                    continue
                except Exception:
                    pass
            self.canvas.create_text(
                20,
                20 + 20 * index,
                text=f"Missing layer image: {image_path}",
                anchor="nw",
                fill="white",
                font=("Segoe UI", 10, "bold"),
                tags=("layer",),
            )

    def draw_polygons(self, mode: str, polygons: List[Any], fill: str, outline: str) -> None:
        for polygon in polygons:
            if not polygon or not isinstance(polygon, list):
                continue
            coords = []
            for vertex in polygon:
                if isinstance(vertex, dict):
                    coords.extend((vertex.get("x", 0), vertex.get("y", 0)))
                elif isinstance(vertex, (list, tuple)) and len(vertex) == 2:
                    coords.extend(vertex)
            if len(coords) < 6:
                continue
            self.canvas.create_polygon(
                *coords,
                fill=fill,
                outline=outline,
                width=2,
                stipple="gray25",
                tags=(mode,)
            )

    def draw_points(self) -> None:
        points = self.room.get("points", {}) or {}
        for point_id, point in points.items():
            if not isinstance(point, dict):
                continue
            x = point.get("x", 0)
            y = point.get("y", 0)
            self.canvas.create_oval(
                x - 6,
                y - 6,
                x + 6,
                y + 6,
                fill=POINT_COLOR,
                outline="white",
                width=1,
                tags=("point", point_id),
            )
            self.canvas.create_text(x + 12, y - 10, text=point_id, anchor="nw", fill="white", font=("Segoe UI", 9, "bold"))

    def draw_selected_handles(self) -> None:
        polygon = self.get_selected_polygon()
        if polygon and isinstance(polygon, list):
            for index, vertex in enumerate(polygon):
                if not isinstance(vertex, dict):
                    continue
                x = vertex.get("x", 0)
                y = vertex.get("y", 0)
                self.canvas.create_rectangle(
                    x - HANDLE_SIZE,
                    y - HANDLE_SIZE,
                    x + HANDLE_SIZE,
                    y + HANDLE_SIZE,
                    fill=SELECTED_COLOR,
                    outline="white",
                    width=1,
                    tags=("handle", str(index)),
                )
        if self.selected_mode.get() == "points" and self.selected_point_id:
            point = (self.room.get("points") or {}).get(self.selected_point_id)
            if isinstance(point, dict):
                x = point.get("x", 0)
                y = point.get("y", 0)
                self.canvas.create_rectangle(
                    x - HANDLE_SIZE,
                    y - HANDLE_SIZE,
                    x + HANDLE_SIZE,
                    y + HANDLE_SIZE,
                    outline=SELECTED_COLOR,
                    width=2,
                )

    def on_canvas_click(self, event: tk.Event) -> None:
        x = self.canvas.canvasx(event.x)
        y = self.canvas.canvasy(event.y)
        if self.add_vertex_mode and self.selected_mode.get() != "points":
            self.insert_vertex((x, y))
            return

        if self.selected_mode.get() == "points":
            self.select_point_at((x, y))
            return

        if self.select_vertex_handle((x, y)):
            self.drag_start = (x, y)
            return

        self.select_shape_at((x, y))

    def on_canvas_drag(self, event: tk.Event) -> None:
        x = self.canvas.canvasx(event.x)
        y = self.canvas.canvasy(event.y)
        if self.selected_mode.get() == "points" and self.selected_point_id:
            self.move_point(self.selected_point_id, (x, y))
            return
        if self.drag_target is not None:
            self.move_vertex(self.drag_target, (x, y))

    def on_canvas_release(self, event: tk.Event) -> None:
        self.drag_start = None
        self.drag_target = None

    def select_point_at(self, position: Tuple[float, float]) -> None:
        points = self.room.get("points", {}) or {}
        for point_id, point in points.items():
            if not isinstance(point, dict):
                continue
            coord = (point.get("x", 0), point.get("y", 0))
            if point_near(position, coord, threshold=10.0):
                self.selected_point_id = point_id
                self.selected_entity = point_id
                self.update_entity_selection()
                self.redraw_canvas()
                self.drag_start = position
                return
        self.selected_point_id = None
        self.selected_entity = None
        self.update_entity_selection()
        self.redraw_canvas()

    def select_vertex_handle(self, position: Tuple[float, float]) -> bool:
        polygon = self.get_selected_polygon()
        if not polygon:
            return False
        for index, vertex in enumerate(polygon):
            if not isinstance(vertex, dict):
                continue
            coord = (vertex.get("x", 0), vertex.get("y", 0))
            if point_near(position, coord, threshold=HANDLE_SIZE + 2):
                self.selected_vertex_index = index
                self.drag_target = (self.selected_entity or "", index)
                self.redraw_canvas()
                return True
        return False

    def select_shape_at(self, position: Tuple[float, float]) -> None:
        mode = self.selected_mode.get()
        if mode == "walkable":
            self.selected_entity = "walkable"
        else:
            for key, entity in self.get_entities_for_mode(mode).items():
                polygon = entity.get("polygon") if mode == "zones" else entity.get("area")
                if self.point_in_polygon(position, polygon or []):
                    self.selected_entity = key
                    break
        self.update_entity_selection()
        self.redraw_canvas()

    def update_entity_selection(self) -> None:
        self.entity_listbox.selection_clear(0, tk.END)
        if self.selected_entity is None:
            return
        items = self.entity_listbox.get(0, tk.END)
        for index, item in enumerate(items):
            if item == self.selected_entity:
                self.entity_listbox.selection_set(index)
                break

    def insert_vertex(self, position: Tuple[float, float]) -> None:
        polygon = self.get_selected_polygon()
        if polygon is None:
            messagebox.showinfo("Add vertex", "Select a polygon first.")
            return
        polygon.append({"x": int(position[0]), "y": int(position[1])})
        self.selected_vertex_index = len(polygon) - 1
        self.add_vertex_mode = False
        self.vertex_button.config(text="Add vertex")
        self.redraw_canvas()

    def move_point(self, point_id: str, position: Tuple[float, float]) -> None:
        points = self.room.get("points", {}) or {}
        if point_id not in points:
            return
        points[point_id] = {"x": int(position[0]), "y": int(position[1])}
        self.redraw_canvas()

    def move_vertex(self, target: Tuple[str, int], position: Tuple[float, float]) -> None:
        entity_id, index = target
        polygon = self.get_selected_polygon()
        if polygon is None or index < 0 or index >= len(polygon):
            return
        vertex = polygon[index]
        if isinstance(vertex, dict):
            vertex["x"] = int(position[0])
            vertex["y"] = int(position[1])
            self.redraw_canvas()

    def add_missing_geometry_defaults(self) -> None:
        if "points" not in self.room:
            self.room["points"] = {}
        if "zones" not in self.room:
            self.room["zones"] = []
        if "regions" not in self.room:
            self.room["regions"] = {}
        if "hotspots" not in self.room:
            self.room["hotspots"] = {}

    @staticmethod
    def point_in_polygon(point: Tuple[float, float], polygon: List[Dict[str, Any]]) -> bool:
        if not polygon or len(polygon) < 3:
            return False
        x, y = point
        inside = False
        j = len(polygon) - 1
        for i in range(len(polygon)):
            xi = polygon[i].get("x", 0)
            yi = polygon[i].get("y", 0)
            xj = polygon[j].get("x", 0)
            yj = polygon[j].get("y", 0)
            intersect = ((yi > y) != (yj > y)) and (
                x < (xj - xi) * (y - yi) / (yj - yi + 1e-9) + xi
            )
            if intersect:
                inside = not inside
            j = i
        return inside

    def run(self) -> None:
        self.root.mainloop()


def run_gui(room_path: Path, base_path: Path) -> None:
    try:
        app = RoomEditorApp(room_path, base_path)
        app.run()
    except tk.TclError as exc:
        raise RuntimeError("Tkinter is not available or cannot create a GUI in this environment.") from exc
