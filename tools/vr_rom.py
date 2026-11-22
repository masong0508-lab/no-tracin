"""Read original course geometry and physical road data from a user's VR ROM.

This module contains formats and offsets, never commercial ROM content.  It
accepts only the verified US revision.  All offsets are byte offsets and all
ROM integers are big endian.  The mesh format is independently documented at
https://github.com/PokaMilkTea/VirtuaRacingMDEnhanced/blob/main/docs/svp-commands.md
and https://github.com/Ralakimus/svp-blender .

The physical route and terrain formats below were checked against the supplied
US ROM's 68000 code, including these entry points:

* 0x0220CC selects the mesh pointer from 0x0221A2 by course number.
* 0x028F78 loads the 72-byte course descriptor from 0x035250.
* 0x027AF6 maps a car's position to a road-reference node and lap-progress ID.
* 0x028188 and 0x029A8A read that node's X/Z and heading.
* 0x0281D4 reads the four-neighbour graph (forward alternatives, then reverse).
* 0x027F42 reads the original packed surface map, with flags [2, 4, 1, 8].
* 0x027C16 treats flag bit 0 (raw surface 2) as a solid collision.
* 0x028028 evaluates the original height planes for the car centre.

There are two coordinate systems.  Physics X/Z multiplied by 2 become mesh
X/Z.  Physics Y divided by 8 becomes mesh Y.  Heights are therefore exposed in
both units, without flattening banks or guessing them from model centroids.
The main route follows neighbour column 1 from node 0 back to node 0.  The
other forward column can enter the original pit lane; its nodes and links are
retained in ``road_nodes``.  The progress IDs are NOT render-section numbers.
"""

from __future__ import annotations

import hashlib
import math
import struct
from typing import Any


ROM_SIZE = 2_097_152
ROM_SHA256 = "291e74f9b4d5a2c0f2b7cbf6f2e0fbf72c16cb6d83b1f58dda78209f9a64c486"
# SHA-1 is recorded separately from SHA-256; do not infer it from a filename.
ROM_SHA1 = "ff969ae53120cc4e7cb1a8a7e47458f2eb8a2165"
DESCRIPTOR_TABLE = 0x035250
MESH_SELECTION_TABLE = 0x0221A2
SURFACE_FLAGS = (2, 4, 1, 8)
ASPHALT = 0
SOLID = 2

# These are format checks and metadata, not copied model or terrain payloads.
COURSE_SPECS = (
    dict(name="BIG FOREST", level="BEGINNER", mesh_offset=0x138000,
         section_count=128, polygon_count=3264, mesh_end=0x14BDD0,
         node_count=206, route_count=125, palette_offset=0x1D3020),
    dict(name="BAY BRIDGE", level="MEDIUM", mesh_offset=0x14C000,
         section_count=128, polygon_count=3657, mesh_end=0x16254E,
         node_count=165, route_count=165, palette_offset=0x1D30A0),
    dict(name="ACROPOLIS", level="EXPERT", mesh_offset=0x164000,
         section_count=166, polygon_count=4420, mesh_end=0x17EA5C,
         node_count=206, route_count=166, palette_offset=0x1D3120),
)


class RomError(ValueError):
    """The ROM is unsupported or a verified data invariant did not hold."""


def _require(condition: bool, message: str) -> None:
    if not condition:
        raise RomError(message)


def _u16(data: bytes, offset: int) -> int:
    return struct.unpack_from(">H", data, offset)[0]


def _u32(data: bytes, offset: int) -> int:
    return struct.unpack_from(">I", data, offset)[0]


def _s16(value: int) -> int:
    return ((value + 32768) & 65535) - 32768


def _trunc_div(numerator: int, denominator: int) -> int:
    """68000 DIVS quotient, truncating toward zero rather than floor."""
    result = abs(numerator) // abs(denominator)
    return -result if (numerator < 0) != (denominator < 0) else result


def physics_height_at(course: dict[str, Any], x: int, z: int) -> int:
    """Original 0x028028 car-centre height in physics Y units.

    Inputs are physical X/Z (half the mesh coordinates).  Out-of-map samples
    return 0.  This avoids the original program's unrestricted ROM reads when
    a new host environment permits driving outside its intended map region.
    """
    terrain = course["terrain"]
    gx, gz = (x >> 4) + 0x400, ((-z) >> 4) + 0x400
    col, row = gx >> 4, gz >> 4
    if not 0 <= col < 128 or not 0 <= row < terrain["height_rows"]:
        return 0
    tile = terrain["height_grid"][row * 128 + col]
    sub = ((gx & 15) >> 2) + (gz & 12)
    a, denominator, c, h = terrain["height_planes"][tile * 16 + sub]
    if denominator == 0:
        return 0
    numerator = a * ((x + 0x4000) & 63) + c * ((-z + 0x4000) & 63)
    quotient = _trunc_div(numerator, denominator)
    _require(-32768 <= quotient <= 32767, "Height DIVS overflow")
    return _s16(h - quotient * 16)


def surface_at(course: dict[str, Any], x: int, z: int) -> int:
    """Original 0x027F42 raw surface category for physical X/Z.

    Raw 0 is asphalt, raw 2 is solid; the original flags for 0..3 are 2,4,1,8.
    Outside the available donor map returns solid, never arbitrary ROM bytes.
    """
    terrain = course["terrain"]
    gx, gz = (x + 0x4000) >> 4, (0x4000 - z) >> 4
    col, row = gx >> 4, gz >> 4
    if not 0 <= col < 128 or not 0 <= row < terrain["surface_rows"]:
        return SOLID
    tile = terrain["surface_grid"][row * 128 + col]
    packed = terrain["surface_tiles"][tile * 64 + (gz & 15) * 4 + ((gx & 15) >> 2)]
    return (packed >> ((3 - (gx & 3)) * 2)) & 3


def _read_terrain(data: bytes, pointers: tuple[int, ...]) -> dict[str, Any]:
    height_grid_offset, height_tiles_offset = pointers[4:6]
    surface_grid_offset, surface_tiles_offset = pointers[6:8]
    height_size = height_tiles_offset - height_grid_offset
    surface_size = surface_tiles_offset - surface_grid_offset
    _require(height_size > 0 and height_size % 256 == 0, "Invalid height grid")
    _require(surface_size > 0 and surface_size % 256 == 0, "Invalid surface grid")
    height_grid = list(struct.unpack_from(f">{height_size // 2}H", data, height_grid_offset))
    surface_grid = list(struct.unpack_from(f">{surface_size // 2}H", data, surface_grid_offset))
    height_tile_count = max(height_grid) + 1
    surface_tile_count = max(surface_grid) + 1
    height_end = height_tiles_offset + height_tile_count * 128
    surface_end = surface_tiles_offset + surface_tile_count * 64
    _require(height_end == surface_grid_offset, "Height tiles do not end at the surface grid")
    _require(surface_end == pointers[13], "Surface tiles do not end at the next source table")
    planes = [struct.unpack_from(">4h", data, offset)
              for offset in range(height_tiles_offset, height_end, 8)]
    return dict(
        grid_columns=128,
        height_grid_offset=height_grid_offset, height_grid=height_grid,
        height_rows=height_size // 256,
        height_planes_offset=height_tiles_offset, height_planes=planes,
        height_tile_count=height_tile_count, height_end=height_end,
        surface_grid_offset=surface_grid_offset, surface_grid=surface_grid,
        surface_rows=surface_size // 256,
        surface_tiles_offset=surface_tiles_offset,
        surface_tiles=list(data[surface_tiles_offset:surface_end]),
        surface_tile_count=surface_tile_count, surface_end=surface_end,
        surface_flags=list(SURFACE_FLAGS),
    )


def _read_sections(data: bytes, spec: dict[str, Any]) -> list[dict[str, Any]]:
    table = spec["mesh_offset"]
    count = spec["section_count"]
    addresses = list(struct.unpack_from(f">{count}I", data, table))
    _require(addresses[0] == table + count * 4, "Unexpected mesh table length")
    _require(all(a < b for a, b in zip(addresses, addresses[1:])),
             "Mesh section pointers are not ordered")
    sections = []
    total = 0
    for section_id, start in enumerate(addresses):
        end = addresses[section_id + 1] if section_id + 1 < count else spec["mesh_end"]
        polygon_count = _u16(data, start) + 1
        offset = start + 2
        polygons = []
        for polygon_id in range(polygon_count):
            polygon_offset = offset
            header = _u16(data, offset)
            vertex_count = 3 if header & 0x10 else 4
            offset += 2
            _require(offset + vertex_count * 6 <= end, "Polygon exceeds its source section")
            vertices = [struct.unpack_from(">3h", data, offset + vertex * 6)
                        for vertex in range(vertex_count)]
            offset += vertex_count * 6
            polygons.append(dict(
                index=polygon_id, offset=polygon_offset, header=header,
                colors=((header >> 12) & 15, (header >> 8) & 15),
                flags=header & 255, vertices=vertices,
            ))
        _require(offset == end, "Mesh section does not consume exactly its original bytes")
        total += polygon_count
        sections.append(dict(index=section_id, offset=start, end=end, polygons=polygons))
    _require(total == spec["polygon_count"], "Unexpected total polygon count")
    return sections


def _road_width(course: dict[str, Any], x: int, z: int,
                dx: int, dz: int, side: int) -> int:
    """Distance to first non-asphalt donor cell, in mesh X/Z units.

    This is a sampled width of the original *collision surface*, not a claim
    that the graphical road edge has this exact width.  Runtime collision and
    height should use the terrain tables directly when possible.
    """
    length = math.hypot(dx, dz)
    _require(length > 0, "Duplicate consecutive road-reference nodes")
    nx, nz = dz / length * side, -dx / length * side
    for distance in range(1, 1025):
        qx, qz = round(x + nx * distance), round(z + nz * distance)
        if surface_at(course, qx, qz) != ASPHALT:
            return (distance - 1) * 2
    raise RomError("Road-reference width did not find an original asphalt boundary")


def _read_road(data: bytes, course: dict[str, Any], pointers: tuple[int, ...]) -> None:
    count = (pointers[10] - pointers[9]) // 2
    _require(count == course["node_count"], "Unexpected physical road-node count")
    nodes = []
    for index in range(count):
        x, z = struct.unpack_from(">2h", data, pointers[8] + index * 4)
        heading = _u16(data, pointers[9] + index * 2)
        neighbors = struct.unpack_from(">4H", data, pointers[10] + index * 8)
        _require(all(node < count for node in neighbors), "Road graph contains an invalid node")
        y = physics_height_at(course, x, z)
        nodes.append(dict(index=index, physics_x=x, physics_z=z, physics_y=y,
                          x=x * 2, z=z * 2, y=y / 8,
                          heading=heading, neighbors=neighbors,
                          progress=data[pointers[12] + index]))
    route_ids = []
    seen = set()
    index = 0
    while index not in seen:
        seen.add(index)
        route_ids.append(index)
        index = nodes[index]["neighbors"][1]
    _require(index == 0, "Main road route does not close at its original first node")
    _require(len(route_ids) == course["route_count"], "Unexpected main-road route length")
    route = []
    for position, node_id in enumerate(route_ids):
        node = dict(nodes[node_id])
        nxt = nodes[route_ids[(position + 1) % len(route_ids)]]
        x, z = node["physics_x"], node["physics_z"]
        _require(surface_at(course, x, z) == ASPHALT,
                 "A main-road reference point is not on the original asphalt")
        dx, dz = nxt["physics_x"] - x, nxt["physics_z"] - z
        node["half_width_left"] = _road_width(course, x, z, dx, dz, -1)
        node["half_width_right"] = _road_width(course, x, z, dx, dz, 1)
        route.append(node)
    course["road_nodes"] = nodes
    course["route_node_ids"] = route_ids
    course["route"] = route
    course["road_source"] = dict(
        position_offset=pointers[8], heading_offset=pointers[9],
        neighbor_offset=pointers[10], progress_offset=pointers[12],
        forward_column=1,
        coordinates="x,z in mesh units (physics times 2); y in mesh units (physics divided by 8)",
    )


def parse_rom(data: bytes) -> dict[str, Any]:
    """Validate and extract all three exact original US courses.

    Returns ``{source: {...}, courses: [...]}``.  Each course has the unmodified
    original section polygons and palette, original road graph and main route,
    and the original packed terrain tables.  Generated results contain game
    data and belong in ignored local build output, not in source control.
    """
    _require(len(data) == ROM_SIZE, f"Expected {ROM_SIZE} bytes for the verified US ROM")
    digest = hashlib.sha256(data).hexdigest()
    _require(digest == ROM_SHA256,
             "Unsupported ROM revision: expected Virtua Racing (USA), SHA-256 " + ROM_SHA256)
    _require(hashlib.sha1(data).hexdigest() == ROM_SHA1, "ROM SHA-1 did not match")
    checksum = sum(struct.unpack_from(f">{(len(data) - 0x200) // 2}H", data, 0x200)) & 65535
    _require(checksum == _u16(data, 0x18E) == 0x95AB, "ROM header checksum did not match")
    courses = []
    for course_id, spec in enumerate(COURSE_SPECS):
        course = dict(spec, index=course_id)
        descriptor_offset = DESCRIPTOR_TABLE + course_id * 72
        pointers = struct.unpack_from(">18I", data, descriptor_offset)
        _require(_u32(data, MESH_SELECTION_TABLE + course_id * 4) * 2 == spec["mesh_offset"],
                 "Course selection points to an unexpected mesh table")
        course["descriptor_offset"] = descriptor_offset
        course["descriptor_pointers"] = pointers
        course["sections"] = _read_sections(data, spec)
        vertices = [vertex for section in course["sections"]
                    for polygon in section["polygons"] for vertex in polygon["vertices"]]
        course["bounds"] = dict(min=[min(v[axis] for v in vertices) for axis in range(3)],
                                max=[max(v[axis] for v in vertices) for axis in range(3)])
        palette = list(struct.unpack_from(">16H", data, spec["palette_offset"]))
        course["palette_words"] = palette
        course["palette_rgb3"] = [((v >> 1) & 7, (v >> 5) & 7, (v >> 9) & 7) for v in palette]
        course["terrain"] = _read_terrain(data, pointers)
        _read_road(data, course, pointers)
        courses.append(course)
    return dict(source=dict(sha256=digest, sha1=ROM_SHA1, size=len(data),
                            checksum=checksum, region="U", product="MK-1229 -00",
                            format_version=1), courses=courses)
