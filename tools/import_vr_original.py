#!/usr/bin/env python3
"""Build an optional original-course pack from a locally supplied US VR ROM.

No source ROM or generated asset is downloaded. Generated headers and the
manifest go into build/vr-original by default, which is excluded from Git.
The three custom tracks are never regenerated or replaced by this importer.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import tempfile

from vr_rom import RomError, parse_rom

IMPORT_VERSION = 1
ORIGIN = 8192
DIVISOR = 4
WORLD = 16384
GRID = 128
CELL = 128


def require(ok, message):
    if not ok:
        raise RomError(message)


def convert_vertex(vertex):
    x, y, z = vertex
    return round(x / DIVISOR) + ORIGIN, round(y / DIVISOR), round(z / DIVISOR) + ORIGIN


def c_array(kind, name, values, per_line=16):
    # C does not permit portable zero-length arrays.
    values = values or [0]
    result = [f"static const {kind} {name}[{len(values)}] = {{"]
    for start in range(0, len(values), per_line):
        result.append("    " + ", ".join(map(str, values[start:start + per_line])) + ",")
    result.append("};")
    return "\n".join(result)


def route_records(course):
    points = [convert_vertex((p["x"], p["y"], p["z"])) for p in course["route"]]
    require(3 <= len(points) < 256, "Original route exceeds the runtime segment index")
    records = []
    distance = 0
    for i, ((x, y, z), source) in enumerate(zip(points, course["route"])):
        bx, by, bz = points[(i + 1) % len(points)]
        dx, dz = bx - x, bz - z
        exact_length = math.hypot(dx, dz)
        length = round(exact_length)
        require(length > 0, "Quantization collapsed a route segment")
        left = math.floor(source["half_width_left"] / DIVISOR)
        right = math.floor(source["half_width_right"] / DIVISOR)
        width = min(left, right)
        require(width > 24, "Original route is too narrow for the current car collision radius")
        # Keep the independent original asphalt widths for *classification*
        # of source-backed side branches. The narrowest width remains the
        # safe common racing lane for the existing rivals and road collision.
        records.append(dict(x=x, y=y, z=z, ux=round(dx / exact_length * 16384),
                            uz=round(dz / exact_length * 16384), length=length,
                            distance=distance, heading=math.atan2(dx, dz),
                            width=width, left=left, right=right))
        distance += length
    require(distance < 65536, "Original route exceeds the current lap-distance format")
    for i, p in enumerate(records):
        before, after = records[i - 1], records[(i + 1) % len(records)]
        dh = (after["heading"] - before["heading"] + math.pi) % (2 * math.pi) - math.pi
        arc = before["length"] + p["length"]
        p["radius"] = round(min(8000, abs(arc / dh) if abs(dh) > 1e-8 else 8000))
        require(0 <= p["x"] < WORLD and 0 <= p["z"] < WORLD, "Route outside GBA world bounds")
    return records, distance


def lookup_grid(records):
    cells = [[] for _ in range(GRID * GRID)]
    for i, p in enumerate(records):
        q = records[(i + 1) % len(records)]
        reach = max(p["width"], q["width"]) + 240
        x0, x1 = min(p["x"], q["x"]) - reach, max(p["x"], q["x"]) + reach
        z0, z1 = min(p["z"], q["z"]) - reach, max(p["z"], q["z"]) + reach
        for cz in range(max(0, z0 // CELL), min(GRID, z1 // CELL + 1)):
            for cx in range(max(0, x0 // CELL), min(GRID, x1 // CELL + 1)):
                x, z = (cx + 0.5) * CELL, (cz + 0.5) * CELL
                dx, dz = q["x"] - p["x"], q["z"] - p["z"]
                t = max(0, min(1, ((x-p["x"])*dx+(z-p["z"])*dz)/(dx*dx+dz*dz)))
                if math.hypot(x-p["x"]-t*dx, z-p["z"]-t*dz) <= reach + CELL * 0.71:
                    cells[cz * GRID + cx].append(i)
    first, counts, flat = [], [], []
    for cell in cells:
        first.append(len(flat)); counts.append(len(cell)); flat.extend(cell)
    require(len(flat) < 65536 and max(counts) < 256, "Original road lookup exceeds runtime limits")
    return first, counts, flat


def engine_palette(scene):
    # The near palette from render.c's DAY theme. Never map world faces to
    # mutable player-paint or UI materials. The game's selected theme still
    # applies at run time through the normal palette and haze system.
    base = [(14,20,30),(11,11,13),(30,28,12),(23,21,18),(9,21,6),(5,15,5),
            (28,24,18),(25,11,8),(14,18,26),(26,26,25),(28,20,7),(15,22,18),
            (31,5,3),(7,7,9),(9,16,24),(5,5,6),(31,22,4),(31,30,8),(4,4,7),
            (5,13,25),(30,4,4),(30,30,30),(31,18,2),(10,10,10),(5,5,6),(24,28,31)]
    overrides = [
        {4:(8,20,5),5:(4,13,4),7:(15,9,5),1:(12,12,13)},
        {4:(10,20,8),1:(12,12,14),19:(4,12,24)},
        {4:(20,18,9),5:(7,13,5),11:(18,14,10),7:(14,9,5),9:(29,28,25),1:(13,12,12)}]
    for material, rgb in overrides[scene].items(): base[material] = rgb
    return [(material * 4 + shade, tuple(channel * scale // 32 for channel in rgb))
            for material, rgb in enumerate(base) if material not in (12, 17, 18)
            for shade, scale in enumerate((32,26,20,14))]


def face_color(course, polygon, palette):
    a, b = polygon["colors"]
    if not a and not b: return None
    ca, cb = course["palette_rgb3"][a], course["palette_rgb3"][b]
    target = tuple((ca[i]+cb[i]) * 31 / 14 for i in range(3))
    return min(palette, key=lambda entry: sum((entry[1][i]-target[i])**2 for i in range(3)))[0]


def source_road_bounds(terrain):
    """GBA-world bounds of the donor's *packed asphalt mask*, including pits.

    Derive preview extents from source cells, never from a guessed centreline.
    The original map has 128 tile columns, 16x16 two-bit cells per tile,
    and eight GBA world units per collision cell. Only category 0 is asphalt.
    This runs once in the private importer; no scanning or RAM cache at boot.
    """
    rows = terrain["surface_rows"]
    grid = terrain["surface_grid"]
    tiles = terrain["surface_tiles"]
    require(len(grid) == rows * 128, "Invalid surface grid size")
    low_x, high_x = 2048, -1
    low_z, high_z = rows * 16, -1
    for row in range(rows):
        for col in range(128):
            start = grid[row * 128 + col] * 64
            require(start + 64 <= len(tiles), "Source surface tile outside bank")
            for local_z in range(16):
                for byte_x in range(4):
                    packed = tiles[start + local_z * 4 + byte_x]
                    if packed == 0xAA:  # four solid source cells
                        continue
                    for cell in range(4):
                        if (packed >> ((3 - cell) * 2)) & 3:
                            continue
                        x = col * 16 + byte_x * 4 + cell
                        z = row * 16 + local_z
                        low_x = min(low_x, x)
                        high_x = max(high_x, x)
                        low_z = min(low_z, z)
                        high_z = max(high_z, z)
    require(high_x >= low_x and high_z >= low_z, "No original asphalt in surface grid")
    # Keep a small framing margin. Z increases in the opposite direction
    # to the original packed row index (see vr_original.c:raw_surface).
    return (max(0, low_x * 8 - 64),
            min(WORLD - 1, (high_x + 1) * 8 + 64),
            max(0, WORLD - (high_z + 1) * 8 - 64),
            min(WORLD - 1, WORLD - low_z * 8 + 64))


def mesh_header(courses):
    out = ["// Generated from a locally supplied ROM. Do not commit this file.",
           "#ifndef VR_ORIGINAL_MESH_DATA_H", "#define VR_ORIGINAL_MESH_DATA_H"]
    descriptors = []
    stats = []
    for ci, course in enumerate(courses):
        prefix = f"vr{ci}"
        palette = engine_palette(ci)
        faces, sections, identities = [], [], {}
        for section in course["sections"]:
            first = len(faces)
            section_vertices = []
            for polygon in section["polygons"]:
                vertices = [convert_vertex(v) for v in polygon["vertices"]]
                require(all(-32768 <= c < 32768 for v in vertices for c in v), "Mesh coordinate overflow")
                section_vertices.extend(vertices)
                color = face_color(course, polygon, palette)
                if color is None: continue
                # Source meshes and this renderer use opposite front-face
                # winding; reverse only the vertex order, never the geometry.
                vertices = [vertices[2], vertices[1], vertices[0]] + vertices[3:]
                cx = round(sum(v[0] for v in vertices)/len(vertices))
                cz = round(sum(v[2] for v in vertices)/len(vertices))
                radius = math.ceil(max(math.hypot(v[0]-cx,v[2]-cz) for v in vertices)) + 2
                flags = 0 if polygon["flags"] & 0x40 else 1
                # Ground includes banked and climbing road faces. Sorting
                # these by average depth can hide a car standing on them.
                a = [vertices[1][j]-vertices[0][j] for j in range(3)]
                b = [vertices[2][j]-vertices[0][j] for j in range(3)]
                normal = (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])
                if abs(normal[1]) >= max(abs(normal[0]), abs(normal[2])):
                    flags |= 2
                flat = [c for v in vertices for c in v] + [0] * (12-len(vertices)*3)
                # Keep every source face and section. Repeated copies carry
                # one identity so runtime visibility never draws them twice.
                # Compare the complete source data, not rounded coordinates
                # or palette matches, which could merge distinct details.
                key = (polygon['flags'], tuple(polygon['colors']),
                       tuple(tuple(v) for v in polygon['vertices']))
                identity = identities.setdefault(key, len(faces))
                faces.append((flat,cx,cz,radius,identity,len(vertices),color,flags))
            cx = round((min(v[0] for v in section_vertices)+max(v[0] for v in section_vertices))/2)
            cz = round((min(v[2] for v in section_vertices)+max(v[2] for v in section_vertices))/2)
            radius = math.ceil(max(math.hypot(v[0]-cx,v[2]-cz) for v in section_vertices))+2
            sections.append((first,len(faces)-first,cx,cz,radius))
        require(len(faces) <= 4420 and len(sections) <= 166, "Mesh index overflow")
        out.append(f"static const VrOriginalFace {prefix}_faces[{len(faces)}] = {{")
        for xyz,cx,cz,radius,identity,n,color,flags in faces:
            out.append("    {{"+",".join(map(str,xyz))+f"}}, {cx}, {cz}, {radius}, {identity}, {n}, {color}, {flags}, 0"+"},")
        out.append("};")
        out.append(f"static const VrOriginalSection {prefix}_sections[{len(sections)}] = {{")
        out.extend("    {"+", ".join(map(str,s))+"}," for s in sections)
        out.append("};")
        terrain=course["terrain"]
        road_bounds=source_road_bounds(terrain)
        out.extend((c_array("u16",prefix+"_height_grid",terrain["height_grid"]),
                    c_array("s16",prefix+"_planes",[v for p in terrain["height_planes"] for v in p]),
                    c_array("u16",prefix+"_surface_grid",terrain["surface_grid"]),
                    c_array("u8",prefix+"_surface_tiles",terrain["surface_tiles"],32)))
        descriptors.append(f"    {{ {prefix}_faces, {prefix}_sections, {len(sections)}, {prefix}_height_grid, {prefix}_planes, {terrain['height_rows']}, {prefix}_surface_grid, {prefix}_surface_tiles, {terrain['surface_rows']}, {', '.join(map(str, road_bounds))} }},")
        stats.append(dict(name=course["name"],source_sections=len(sections),
                          source_polygons=course["polygon_count"],rendered_polygons=len(faces),
                          unique_polygons=len(identities),duplicate_polygons=len(faces)-len(identities),
                          source_mesh_offset=course["mesh_offset"],source_descriptor=course["descriptor_offset"],
                          source_asphalt_bounds_gba=list(road_bounds)))
    out.append("static const VrOriginalCourse vr_original_courses[3] = {")
    out.extend(descriptors);out.extend(("};","#endif",""))
    return "\n".join(out), stats


def track_header(result):
    identity=hashlib.sha256((result["source"]["sha256"]+f":{IMPORT_VERSION}:1/4:8192").encode()).hexdigest()
    version=int(identity[:8],16) or 1
    out=["// Generated from a locally supplied ROM. Do not commit this file.",
         "#ifndef VR_ORIGINAL_TRACK_DATA_H","#define VR_ORIGINAL_TRACK_DATA_H",
         f"#define VR_ORIGINAL_PACK_VERSION 0x{version:08X}u"]
    definitions=[];stats=[]
    for ci,course in enumerate(result["courses"]):
        pre=f"vr{ci}"
        records,lap=route_records(course)
        first,count,flat=lookup_grid(records)
        out.append(f"static const TrackPt {pre}_points[{len(records)}] = {{")
        for i,p in enumerate(records):
            values=(p['x'],p['z'],p['y'],p['ux'],p['uz'],p['length'],p['distance'],
                    round(p['heading']/math.tau*65536)&65535,p['radius'],8 if not i else 0,0)
            out.append("    {"+", ".join(map(str,values))+"},")
        out.append("};")
        out.extend((c_array("u16",pre+"_widths",[p['width'] for p in records]),
                    c_array("u16",pre+"_native_lefts",[p['left'] for p in records]),
                    c_array("u16",pre+"_native_rights",[p['right'] for p in records]),
                    c_array("u16",pre+"_cell_first",first),c_array("u8",pre+"_cell_count",count,32),
                    c_array("u8",pre+"_cell_list",flat,32)))
        start_time=min(240,max(64,math.ceil(lap/500)))
        cp_time=min(120,max(32,math.ceil(lap/1000)))
        scene=("SCENE_FOREST","SCENE_BAY","SCENE_MOUNTAINS")[ci]
        definitions.append(f'    {{ "{course["name"]}", "{course["level"]}", {pre}_points, {len(records)}, {lap}, {pre}_cell_first, {pre}_cell_count, {pre}_cell_list, 0, 0, 0, 0, 0, 0, 0, 0, 3, {start_time}, {cp_time}, {scene}, {pre}_widths }},')
        stats.append(dict(name=course['name'],route_points=len(records),lap_world_units=lap,
                          minimum_half_width=min(p['width'] for p in records),
                          asymmetric_native_widths_preserved=True,
                          minimum_native_left_width=min(p['left'] for p in records),
                          minimum_native_right_width=min(p['right'] for p in records)))
    # Only the private, optional original pack uses these per-side widths.
    # Preserve the existing TrackDef and GBA SRAM layout for every build.
    out.append("static const u16 *const vr_native_left_widths[3] = { vr0_native_lefts, vr1_native_lefts, vr2_native_lefts };")
    out.append("static const u16 *const vr_native_right_widths[3] = { vr0_native_rights, vr1_native_rights, vr2_native_rights };")
    out.append("static const u8 vr_native_route_counts[3] = { "+", ".join(str(len(c["route"])) for c in result["courses"])+" };")
    out.append("#define VR_ORIGINAL_TRACK_DEFS \\")
    for i,definition in enumerate(definitions):
        out.append(definition+(" \\" if i+1<len(definitions) else ""))
    out.extend(("#endif",""))
    return "\n".join(out), version, stats


def atomic_write(path, content):
    path.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.NamedTemporaryFile(mode="w",encoding="utf-8",dir=path.parent,delete=False) as f:
        temp=Path(f.name);f.write(content)
    os.replace(temp,path)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rom",type=Path,required=True)
    parser.add_argument("--output-dir",type=Path,default=Path("build/vr-original"))
    args=parser.parse_args()
    try:
        result=parse_rom(args.rom.read_bytes())
        tracks,version,route_stats=track_header(result)
        mesh,mesh_stats=mesh_header(result["courses"])
        manifest=dict(source=result["source"],import_version=IMPORT_VERSION,pack_version=version,
                      transform=dict(mesh_divisor=DIVISOR,xz_origin=ORIGIN),
                      courses=[dict(a,**{k:v for k,v in b.items() if k!='name'}) for a,b in zip(route_stats,mesh_stats)],
                      scope="Original course meshes, native main routes, surfaces and height planes; No Tracin driving/race rules; source colors mapped to its GBA palette.")
        for name,text in (("vr_original_tracks.h",tracks),("vr_original_mesh.h",mesh),
                          ("manifest.json",json.dumps(manifest,indent=2)+"\n")):
            atomic_write(args.output_dir/name,text)
        print(f"Imported three original courses from verified {result['source']['product']} into {args.output_dir}")
        for course in manifest['courses']:
            print(f"  {course['name']}: {course['route_points']} road nodes, {course['source_sections']} mesh sections, {course['source_polygons']} source polygons")
    except (OSError,RomError) as error:
        parser.exit(2,f"Original-course import failed: {error}\n")


if __name__=="__main__":
    main()
