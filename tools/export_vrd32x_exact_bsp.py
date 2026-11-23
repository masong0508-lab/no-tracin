#!/usr/bin/env python3
"""Privately export precise Virtua Racing Deluxe 32X collision polygons.

The source is the owner's verified cartridge; there are no donor bytes in
this script. Polygons reconstruct the original 68000 BSP *collision* planes,
not the original SH-2 display meshes, pit AI, textures or named race paths.

Outputs live exclusively under this repository's ignored build/ directory.
They must not be copied to public source control or called complete tracks.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

try:
    from tools.decode_vrd_32x_surface import RoadSurface
    from tools.inspect_vrd_32x_tables import ROM_SHA256, inspect_image
    from tools.vrd32x_exact_bsp import (
        audit_mode_group_identity, mode_polygon_bounds, tile_regions
    )
except ModuleNotFoundError:
    from decode_vrd_32x_surface import RoadSurface
    from inspect_vrd_32x_tables import ROM_SHA256, inspect_image
    from vrd32x_exact_bsp import (
        audit_mode_group_identity, mode_polygon_bounds, tile_regions
    )


ROOT = Path(__file__).resolve().parents[1]
BUILD = (ROOT / "build").resolve()


def vector_svg(road) -> tuple[str, dict]:
    """Pure source-derived collision-area silhouette for one verified slot.

    Each clipped group produces its exact road footprint. Some BSP group
    polygons overlap: their union is correct for classification, but the
    ORIGINAL CPU's first-match group decides height at overlaps. This is
    not a render-ready 3D mesh and may not be assigned a named course.
    """
    paths = []
    active = source_regions = plotted = 0
    for tile, marker in enumerate(road.grid):
        if marker == road.background:
            continue
        active += 1
        row, col = divmod(tile, 64)
        for region in tile_regions(road, row, col):
            source_regions += 1
            if len(region.polygon) < 3:
                continue
            points = []
            for x, z in region.polygon:
                # 32X local 512-unit tile -> GBA 256-world-unit tile.
                # Flip screen Y so positive original Z faces upward.
                xx = (col * 512 + float(x)) / 2
                yy = 16384 - (row * 512 + float(z)) / 2
                points.append(f"{xx:.4f},{yy:.4f}")
            paths.append('<polygon points="' + ' '.join(points) + '"/>')
            plotted += 1
    if not plotted:
        raise ValueError("no source BSP collision polygons found")
    svg = '\n'.join([
        '<?xml version="1.0" encoding="UTF-8"?>',
        '<svg xmlns="http://www.w3.org/2000/svg" '
        'width="1024" height="1024" viewBox="0 0 16384 16384">',
        '<title>Private 32X 68000 source BSP road footprints — unnamed mode '
        + str(road.slot) + '</title>',
        '<desc>Collision-area boundaries, NOT original SH-2 polygon scenery '
        'or an authenticated named racing circuit.</desc>',
        '<rect width="16384" height="16384" fill="#171b20"/>',
        '<g fill="#e5c8a0" stroke="none">',
        *paths,
        '</g>',
        '</svg>',
        ''
    ])
    return svg, {"mode_slot": road.slot,
                 "active_source_tiles": active,
                 "reconstructed_bsp_groups": source_regions,
                 "nondegenerate_vector_footprints": plotted,
                 "svg_coordinates": "GBA world, source X right / source +Z up",
                 "original_sh2_render_mesh_recovered": False,
                 "named_deluxe_course_verified": False}


def run(rom: Path, out_dir: Path) -> dict:
    target = out_dir.resolve()
    if not target.is_relative_to(BUILD):
        raise ValueError("private vector output must remain under ignored build/")
    source = rom.read_bytes()
    # Full-file hash+header validation BEFORE any generated file is written.
    if hashlib.sha256(source).hexdigest() != ROM_SHA256:
        raise ValueError("not the verified owner-supplied US 32X cartridge")
    inspect_image(source, require_identity=True)
    target.mkdir(parents=True,exist_ok=True)
    rows = []
    for slot in range(5):
        surface = RoadSurface(source,slot,verify_identity=False)
        svg, stats = vector_svg(surface)
        # Compare the original source 68000 selected group with our exact
        # polygon constraint model at a repeatable interior/boundary grid.
        audit = audit_mode_group_identity(
            surface, sample_points=(0,128,256,384,511))
        assert audit["first_match_source_parity"]
        stats["source_cpu_polygon_audit"] = audit
        stats["exact_collision_bounds_gba"] = mode_polygon_bounds(surface)
        name = f"32x-source-bsp-mode-{slot}.svg"
        (target / name).write_text(svg,encoding="utf-8")
        stats["file"] = name
        rows.append(stats)
    manifest = {
        "source_sha256": ROM_SHA256,
        "purpose": "PRIVATE_NATIVE_68000_COLLISION_VECTOR_EVIDENCE",
        "authentic_32x_racing_courses_completed": False,
        "verified_deluxe_names": False,
        "source_road_boundaries_exact_on_integer_sample_grid": True,
        "32x_sh2_render_mesh_recovered": False,
        "native_routing_and_checkpoint_graph_recovered": False,
        "modes": rows,
    }
    (target/"vector-manifest.json").write_text(
        json.dumps(manifest,indent=2,sort_keys=True)+"\n",encoding="utf-8")
    return manifest


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rom",type=Path,required=True,
                        help="owner-supplied, unmodified, supported US 32X ROM")
    parser.add_argument("--out-dir",type=Path,default=BUILD/"vrd32x-bsp")
    args=parser.parse_args()
    result=run(args.rom,args.out_dir)
    for row in result["modes"]:
        print("MODE",row["mode_slot"],":",
              row["reconstructed_bsp_groups"],"source BSP groups,",
              row["nondegenerate_vector_footprints"],"collision footprints;",
              "unnamed, not original SH-2 meshes")


if __name__ == "__main__":
    main()
