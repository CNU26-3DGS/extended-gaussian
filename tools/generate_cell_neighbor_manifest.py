#!/usr/bin/env python3
"""Generate a camera-bounds manifest for grid-partitioned Gaussian cells."""

from __future__ import annotations

import argparse
import json
import os
import re
from pathlib import Path


def parse_cfg_list(cfg_text: str, key: str) -> list[float]:
    match = re.search(rf"{re.escape(key)}=\[([^\]]+)\]", cfg_text)
    if not match:
        raise ValueError(f"Could not find '{key}' in cfg_args.")
    return [float(token.strip()) for token in match.group(1).split(",")]


def parse_block_dims(cfg_text: str) -> tuple[int, int, int]:
    values = parse_cfg_list(cfg_text, "block_dim")
    if len(values) != 3:
        raise ValueError("block_dim must have exactly 3 values.")
    return int(values[0]), int(values[1]), int(values[2])


def parse_aabb(cfg_text: str) -> tuple[float, float, float, float, float, float]:
    values = parse_cfg_list(cfg_text, "aabb")
    if len(values) != 6:
        raise ValueError("aabb must have exactly 6 values.")
    return tuple(values)  # type: ignore[return-value]


def discover_cells(cells_dir: Path) -> list[tuple[int, Path]]:
    cells: list[tuple[int, Path]] = []
    for child in cells_dir.iterdir():
        if not child.is_dir():
            continue
        match = re.fullmatch(r"cell(\d+)", child.name)
        if not match:
            continue
        cells.append((int(match.group(1)), child))
    cells.sort(key=lambda item: item[0])
    return cells


def compute_bounds(
    block_id: int,
    dims: tuple[int, int, int],
    aabb: tuple[float, float, float, float, float, float],
    origin_y: str,
) -> tuple[list[float], list[float], tuple[int, int, int]]:
    cols, rows, layers = dims
    layer_size = cols * rows
    layer = block_id // layer_size
    layer_index = block_id % layer_size
    row = layer_index // cols
    col = layer_index % cols

    min_x, min_y, min_z, max_x, max_y, max_z = aabb
    cell_size_x = (max_x - min_x) / cols
    cell_size_y = (max_y - min_y) / rows
    cell_size_z = (max_z - min_z) / max(1, layers)

    x0 = min_x + col * cell_size_x
    x1 = x0 + cell_size_x

    if origin_y == "max":
        y1 = max_y - row * cell_size_y
        y0 = y1 - cell_size_y
    else:
        y0 = min_y + row * cell_size_y
        y1 = y0 + cell_size_y

    z0 = min_z + layer * cell_size_z
    z1 = z0 + cell_size_z
    return [x0, y0, z0], [x1, y1, z1], (row, col, layer)


def neighbor_ids(
    block_id: int,
    dims: tuple[int, int, int],
    radius_xy: int,
) -> list[int]:
    cols, rows, layers = dims
    layer_size = cols * rows
    layer = block_id // layer_size
    layer_index = block_id % layer_size
    row = layer_index // cols
    col = layer_index % cols

    result: list[int] = []
    for dy in range(-radius_xy, radius_xy + 1):
        for dx in range(-radius_xy, radius_xy + 1):
            neighbor_row = row + dy
            neighbor_col = col + dx
            if neighbor_row < 0 or neighbor_row >= rows:
                continue
            if neighbor_col < 0 or neighbor_col >= cols:
                continue
            result.append(layer * layer_size + neighbor_row * cols + neighbor_col)
    return result


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("dataset_root", type=Path, help="Root directory containing cfg_args and cells/")
    parser.add_argument("output", type=Path, help="Output manifest JSON path")
    parser.add_argument("--radius-xy", type=int, default=1, help="Neighbor radius in XY grid")
    parser.add_argument("--origin-y", choices=("min", "max"), default="min", help="How block rows map onto Y")
    parser.add_argument(
        "--warm-rule-assets-cpu",
        action="store_true",
        help="Keep every rule-referenced asset CPU-resident while camera rules still decide GPU uploads",
    )
    parser.add_argument(
        "--warm-all-assets",
        action="store_true",
        help="Deprecated alias for --warm-rule-assets-cpu",
    )
    parser.add_argument("--target-vram-mb", type=int, default=0)
    parser.add_argument("--target-ram-mb", type=int, default=0)
    parser.add_argument("--max-upload-mb-per-frame", type=int, default=512)
    parser.add_argument("--max-gpu-evictions-per-frame", type=int, default=4)
    parser.add_argument("--max-cpu-evictions-per-frame", type=int, default=4)
    parser.add_argument("--max-concurrent-disk-loads", type=int, default=2)
    parser.add_argument("--default-unload-hysteresis-sec", type=float, default=1.0)
    args = parser.parse_args()

    dataset_root = args.dataset_root.resolve()
    output_path = args.output.resolve()
    cfg_path = dataset_root / "cfg_args"
    cells_dir = dataset_root / "cells"

    cfg_text = cfg_path.read_text(encoding="utf-8")
    dims = parse_block_dims(cfg_text)
    aabb = parse_aabb(cfg_text)
    cells = discover_cells(cells_dir)
    if not cells:
        raise ValueError(f"No cell directories found in {cells_dir}.")

    expected_count = dims[0] * dims[1] * dims[2]
    if len(cells) != expected_count:
        raise ValueError(f"Expected {expected_count} cells from block_dim, found {len(cells)}.")

    output_path.parent.mkdir(parents=True, exist_ok=True)
    assets: dict[str, dict[str, object]] = {}
    rules: list[dict[str, object]] = []
    warm_rule_assets_cpu = args.warm_rule_assets_cpu or args.warm_all_assets

    for block_id, cell_dir in cells:
        bounds_min, bounds_max, (row, col, layer) = compute_bounds(block_id, dims, aabb, args.origin_y)
        asset_id = f"cell{block_id}"
        relative_model_dir = Path(os.path.relpath(cell_dir, output_path.parent)).as_posix()
        assets[asset_id] = {
            "model_dir": relative_model_dir,
            "tags": [f"row_{row}", f"col_{col}", f"layer_{layer}", "cell"],
            "bounds_min": bounds_min,
            "bounds_max": bounds_max,
            "priority": 0,
            "pin_cpu": False,
            "pin_gpu": False,
            "prefetch_distance": 0.0,
            "unload_hysteresis_sec": args.default_unload_hysteresis_sec,
        }

        required_assets = [f"cell{neighbor_id}" for neighbor_id in neighbor_ids(block_id, dims, args.radius_xy)]
        rules.append(
            {
                "name": f"camera_cell_{block_id}",
                "type": "camera_bounds",
                "region_min": bounds_min,
                "region_max": bounds_max,
                "required": required_assets,
            }
        )

    manifest = {
        "global": {
            "target_vram_mb": args.target_vram_mb,
            "target_ram_mb": args.target_ram_mb,
            "max_upload_mb_per_frame": args.max_upload_mb_per_frame,
            "max_gpu_evictions_per_frame": args.max_gpu_evictions_per_frame,
            "max_cpu_evictions_per_frame": args.max_cpu_evictions_per_frame,
            "max_concurrent_disk_loads": args.max_concurrent_disk_loads,
            "default_unload_hysteresis_sec": args.default_unload_hysteresis_sec,
            "warm_rule_assets_cpu": warm_rule_assets_cpu,
        },
        "assets": assets,
        "rules": rules,
    }

    output_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Wrote manifest to {output_path}")
    print("Sample cell20 neighbors:", ", ".join(next(rule["required"] for rule in rules if rule["name"] == "camera_cell_20")))


if __name__ == "__main__":
    main()
