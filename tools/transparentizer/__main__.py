#!/usr/bin/env python3

from pathlib import Path
from PIL import Image
import numpy as np
import argparse
import sys


def smoothstep(x):
    x = np.clip(x, 0.0, 1.0)
    return x * x * (3.0 - 2.0 * x)


def estimate_key_color(rgb, patch=20):
    h, w, _ = rgb.shape

    patch = min(patch, h, w)
    if patch <= 0:
        raise ValueError("Imagen demasiado pequeña para estimar el color de fondo")

    patches = [
        rgb[:patch, :patch],
        rgb[:patch, w - patch:],
        rgb[h - patch:, :patch],
        rgb[h - patch:, w - patch:],
    ]

    samples = np.concatenate([p.reshape(-1, 3) for p in patches], axis=0)
    return np.median(samples, axis=0)


def crop_to_content(rgba, alpha_threshold=5, padding=0):
    alpha = rgba[:, :, 3]
    mask = alpha > alpha_threshold

    if not np.any(mask):
        return rgba

    ys, xs = np.where(mask)

    y0 = max(ys.min() - padding, 0)
    y1 = min(ys.max() + padding + 1, rgba.shape[0])

    x0 = max(xs.min() - padding, 0)
    x1 = min(xs.max() + padding + 1, rgba.shape[1])

    return rgba[y0:y1, x0:x1]


def remove_key_background(
    input_path,
    output_path,
    key_color=None,
    hard_threshold=25,
    soft_threshold=95,
    despill=True,
    crop=True,
    crop_alpha_threshold=5,
    crop_padding=0,
):
    img = Image.open(input_path).convert("RGBA")
    arr = np.asarray(img).astype(np.float32)

    rgb = arr[:, :, :3]
    original_alpha = arr[:, :, 3] / 255.0

    if key_color is None:
        key = estimate_key_color(rgb)
    else:
        key = np.array(key_color, dtype=np.float32)

    dist = np.linalg.norm(rgb - key, axis=2)

    if soft_threshold <= hard_threshold:
        raise ValueError("--soft debe ser mayor que --hard")

    t = (dist - hard_threshold) / (soft_threshold - hard_threshold)
    alpha = smoothstep(t)
    alpha = alpha * original_alpha

    corrected_rgb = rgb.copy()

    if despill:
        a = np.clip(alpha[:, :, None], 1e-4, 1.0)
        corrected_rgb = (rgb - key[None, None, :] * (1.0 - a)) / a
        corrected_rgb = np.clip(corrected_rgb, 0, 255)
        corrected_rgb = np.where(alpha[:, :, None] < 0.02, rgb, corrected_rgb)

    out = np.dstack([corrected_rgb, alpha * 255.0])
    out = np.clip(out, 0, 255).astype(np.uint8)

    if crop:
        out = crop_to_content(
            out,
            alpha_threshold=crop_alpha_threshold,
            padding=crop_padding,
        )

    Image.fromarray(out, mode="RGBA").save(output_path)

    print(
        f"[OK] {input_path} -> {output_path} "
        f"(key={tuple(int(x) for x in key)})"
    )


def parse_key_color(value):
    if value.lower() == "auto":
        return None

    parts = value.split(",")
    if len(parts) != 3:
        raise argparse.ArgumentTypeError("Usar formato R,G,B o 'auto'")

    try:
        rgb = tuple(int(p) for p in parts)
    except ValueError:
        raise argparse.ArgumentTypeError("R,G,B deben ser enteros")

    if any(x < 0 or x > 255 for x in rgb):
        raise argparse.ArgumentTypeError("Cada componente RGB debe estar entre 0 y 255")

    return rgb


def iter_png_files(directory):
    # No recursivo: solo el directorio indicado.
    # Si luego quieres recursivo, cambia por directory.rglob("*")
    for p in sorted(directory.iterdir()):
        if p.is_file() and p.suffix.lower() == ".png":
            yield p


def process_single_file(input_file, output_target, args):
    remove_key_background(
        input_path=input_file,
        output_path=output_target,
        key_color=args.key,
        hard_threshold=args.hard,
        soft_threshold=args.soft,
        despill=not args.no_despill,
        crop=not args.no_crop,
        crop_alpha_threshold=args.crop_alpha,
        crop_padding=args.padding,
    )


def process_input(input_path, output_path, args):
    if input_path.is_file():
        # Si output es directorio existente, guardar con el mismo nombre.
        if output_path.exists() and output_path.is_dir():
            output_file = output_path / input_path.name
        else:
            output_file = output_path

        output_file.parent.mkdir(parents=True, exist_ok=True)
        process_single_file(input_path, output_file, args)
        return

    if input_path.is_dir():
        output_path.mkdir(parents=True, exist_ok=True)

        png_files = list(iter_png_files(input_path))
        if not png_files:
            print(f"[WARN] No se encontraron PNG en {input_path}")
            return

        for input_file in png_files:
            output_file = output_path / input_file.name
            process_single_file(input_file, output_file, args)
        return

    raise FileNotFoundError(f"No existe la ruta de entrada: {input_path}")


def main():
    parser = argparse.ArgumentParser(
        description="Reemplaza fondo tipo chroma-key por canal alfa y recorta al contenido."
    )

    parser.add_argument("input", help="Archivo PNG de entrada o directorio")
    parser.add_argument("output", help="Archivo PNG de salida o directorio")

    parser.add_argument(
        "--key",
        type=parse_key_color,
        default=None,
        help="Color de fondo R,G,B o 'auto'. Ej: --key 255,0,255",
    )

    parser.add_argument(
        "--hard",
        type=float,
        default=25,
        help="Distancia RGB bajo la cual el píxel es totalmente transparente",
    )

    parser.add_argument(
        "--soft",
        type=float,
        default=95,
        help="Distancia RGB sobre la cual el píxel es totalmente opaco",
    )

    parser.add_argument(
        "--no-despill",
        action="store_true",
        help="Desactiva corrección de halo/color spill",
    )

    parser.add_argument(
        "--no-crop",
        action="store_true",
        help="No recorta la imagen al contenido visible",
    )

    parser.add_argument(
        "--crop-alpha",
        type=int,
        default=5,
        help="Alfa mínimo para considerar un píxel como parte del objeto",
    )

    parser.add_argument(
        "--padding",
        type=int,
        default=0,
        help="Margen opcional alrededor del objeto recortado",
    )

    args = parser.parse_args()

    input_path = Path(args.input)
    output_path = Path(args.output)

    try:
        process_input(input_path, output_path, args)
    except Exception as e:
        print(f"[ERROR] {e}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()