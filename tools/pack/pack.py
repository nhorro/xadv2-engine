#!/usr/bin/env python3
"""
Pack a resource directory into a `.pak` archive (issue #109).

The pak format is defined in `lib/include/engine/core/pack_format.hpp` — this
script and the C++ runtime MUST stay in sync. Logical paths are stored with
forward slashes regardless of host platform; non-file entries (directories,
symlinks, hidden files) are skipped.

Usage:
    python -m tools.pack <resources_root> <output_pak>
    python tools/pack/pack.py <resources_root> <output_pak>

Examples:
    python tools/pack/pack.py examples/01_hello_room/data build/resources.pak
"""

from __future__ import annotations

import argparse
import os
import secrets
import struct
import sys
from pathlib import Path
from typing import Iterable

# --- Constants mirroring pack_format.hpp -----------------------------------

MAGIC = b"PAC1"
VERSION = 1
SEED_BYTES = 16
HEADER_BYTES = 36  # magic(4) + ver(4) + count(4) + toc_off(8) + seed(16)
MAX_PATH_LEN = 1024

# --- Keystream (must match pack_format.cpp) --------------------------------


def fnv1a_32(s: bytes) -> int:
    h = 0x811C9DC5
    for b in s:
        h ^= b
        h = (h * 0x01000193) & 0xFFFFFFFF
    return h


def keystream_byte(seed: bytes, path_hash: int, i: int) -> int:
    s = seed[(i + (path_hash & 0xF)) % SEED_BYTES]
    hbyte = (path_hash >> ((i & 3) * 8)) & 0xFF
    cnt = (i * 0x5B) & 0xFF
    return s ^ hbyte ^ cnt


def xor_payload(seed: bytes, logical: str, data: bytes) -> bytes:
    h = fnv1a_32(logical.encode("utf-8"))
    out = bytearray(len(data))
    for i, b in enumerate(data):
        out[i] = b ^ keystream_byte(seed, h, i)
    return bytes(out)


# --- Walk + pack -----------------------------------------------------------


def is_skipped(rel: Path) -> bool:
    # Skip hidden/temp files at any depth — they're never authoring inputs.
    return any(part.startswith(".") or part.startswith("__") for part in rel.parts)


def iter_resources(root: Path) -> Iterable[tuple[str, Path]]:
    """Yield (logical_path, host_path) pairs for files under `root`, sorted."""
    items: list[tuple[str, Path]] = []
    for host in root.rglob("*"):
        if not host.is_file():
            continue
        rel = host.relative_to(root)
        if is_skipped(rel):
            continue
        logical = rel.as_posix()  # forward slashes regardless of OS
        if len(logical.encode("utf-8")) > MAX_PATH_LEN:
            raise SystemExit(
                f"pack: '{logical}' exceeds {MAX_PATH_LEN}-byte path limit"
            )
        items.append((logical, host))
    items.sort(key=lambda x: x[0])
    return items


def pack(root: Path, output: Path) -> tuple[int, int]:
    seed = secrets.token_bytes(SEED_BYTES)

    # Two-pass write: first reserve the header, then stream payloads and remember
    # each file's offset + length, then write the TOC, then patch the header.
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("wb") as fp:
        # Reserve header (we patch it at the end).
        fp.write(b"\x00" * HEADER_BYTES)

        toc_entries: list[tuple[str, int, int]] = []  # (logical, offset, size)
        for logical, host in iter_resources(root):
            data = host.read_bytes()
            obfuscated = xor_payload(seed, logical, data)
            offset = fp.tell()
            fp.write(obfuscated)
            toc_entries.append((logical, offset, len(data)))

        toc_offset = fp.tell()
        for logical, offset, size in toc_entries:
            path_bytes = logical.encode("utf-8")
            fp.write(struct.pack("<H", len(path_bytes)))
            fp.write(path_bytes)
            fp.write(struct.pack("<QQ", offset, size))

        # Patch the header now that toc_offset and count are known.
        fp.seek(0)
        fp.write(MAGIC)
        fp.write(struct.pack("<I", VERSION))
        fp.write(struct.pack("<I", len(toc_entries)))
        fp.write(struct.pack("<Q", toc_offset))
        fp.write(seed)

    return len(toc_entries), output.stat().st_size


# --- CLI -------------------------------------------------------------------


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(
        description="Pack a resource directory into a .pak archive."
    )
    parser.add_argument("root", type=Path, help="Resource root directory.")
    parser.add_argument("output", type=Path, help="Path of the .pak file to write.")
    parser.add_argument(
        "--verbose",
        action="store_true",
        help="Print each packed logical path.",
    )
    args = parser.parse_args(argv)

    if not args.root.is_dir():
        parser.error(f"resource root '{args.root}' is not a directory")

    if args.verbose:
        for logical, _ in iter_resources(args.root):
            print(logical)

    count, size = pack(args.root, args.output)
    print(f"packed {count} files into {args.output} ({size} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
