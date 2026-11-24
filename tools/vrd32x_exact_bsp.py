"""Exact integer half-plane reconstruction for the 32X 68000 BSP records.

This is source-road *collision coverage*, not the game's SH-2 graphics mesh,
track-name table, racing line or route graph. It reconstructs each 28-byte
group as an intersection of four linear integer inequalities; clipping
returns rational vertices so no guessed raster trace defines the boundary.

The inequalities reproduce the actual 68000 comparison after its signed
multiply and arithmetic right shift, including floor and strict-boundary
behavior on integer local coordinates. No original ROM bytes are embedded.
"""
from __future__ import annotations

from fractions import Fraction
from typing import NamedTuple

try:
    from tools.decode_vrd_32x_surface import signed16
    from tools.inspect_vrd_32x_tables import be16
except ModuleNotFoundError:
    from decode_vrd_32x_surface import signed16
    from inspect_vrd_32x_tables import be16


class HalfPlane(NamedTuple):
    """Integer lattice inequality: ax*x + az*z <= bound, x,z in 0..511."""
    ax: int
    az: int
    bound: int

    def contains(self, x: int, z: int) -> bool:
        return self.ax * x + self.az * z <= self.bound


class BspRegion(NamedTuple):
    """One independently decoded, convex collision region within a source tile."""
    group: int
    halfplanes: tuple[HalfPlane, ...]
    polygon: tuple[tuple[Fraction, Fraction], ...]


def group_halfplanes(data: bytes, start: int) -> tuple[HalfPlane, ...]:
    """Decode one 28-byte boundary group without sampling pixels.

    The selector stores four triples (D2/D1, coefficient/direct, polarity).
    Half-plane bounds match the 68000 test on the *integer* coordinate
    lattice: x_word=2*x, z_word=2*z. For the coefficient case:
       2*x < (2*z*coef + 512*constant) >> 9
    is equivalent to:
       1024*x - 2*coef*z <= 512*constant - 512
    (the non-less case has bound 511 - 512*constant). Both rules include
    the source's signed ASR floor semantics, even for negative slopes.
    """
    if start < 0 or start + 28 > len(data):
        raise ValueError("truncated 32X BSP region")
    selector = be16(data, start)
    cursor = start + 2
    out: list[HalfPlane] = []

    for _ in range(4):
        b0 = bool(selector & 0x8000)
        selector = (selector << 1) & 0xffff
        direct = bool(selector & 0x8000)
        selector = (selector << 1) & 0xffff
        polarity = bool(selector & 0x8000)
        selector = (selector << 1) & 0xffff
        first = signed16(be16(data, cursor))
        second = signed16(be16(data, cursor + 2))
        cursor += 4
        if direct:
            # 68K CMP.W then BCC/BCS: strict less vs inclusive >=.
            # The second word is reserved in this branch, but still skipped.
            less = polarity if b0 else not polarity
            if b0:
                out.append(HalfPlane(0, 2 if less else -2,
                                     first - 1 if less else -first))
            else:
                out.append(HalfPlane(2 if less else -2, 0,
                                     first - 1 if less else -first))
        else:
            less = (not polarity) if b0 else polarity
            if b0:
                a, b = 1024, -2 * first
            else:
                a, b = -2 * first, 1024
            if less:
                out.append(HalfPlane(a, b, 512 * second - 512))
            else:
                out.append(HalfPlane(-a, -b, 511 - 512 * second))
    if cursor != start + 18:
        raise AssertionError("BSP decoder consumed height-plane coefficients")
    return tuple(out)


def contains(halfplanes: tuple[HalfPlane, ...], x: int, z: int) -> bool:
    """Exact integer membership, identical to the source group branch logic."""
    if not (0 <= x < 512 and 0 <= z < 512):
        return False
    return all(p.contains(x, z) for p in halfplanes)


def clipped_polygon(halfplanes: tuple[HalfPlane, ...],
                    side: int = 512) -> tuple[tuple[Fraction, Fraction], ...]:
    """Clip an original tile by its exact four source BSP inequalities.

    Use rational intersections rather than floating guesses to retain real
    source boundary coordinates; no rasterization/texture/SH-2 mesh is
    inferred. A degenerate zero-area region may still cover lattice points.
    """
    if not (2 <= side <= 512):
        raise ValueError("invalid original tile size")
    m = side - 1
    poly = [(Fraction(0), Fraction(0)),
            (Fraction(m), Fraction(0)),
            (Fraction(m), Fraction(m)),
            (Fraction(0), Fraction(m))]
    for p in halfplanes:
        if not poly:
            break
        result = []
        x0, z0 = poly[-1]
        d0 = p.ax * x0 + p.az * z0 - p.bound
        for x1, z1 in poly:
            d1 = p.ax * x1 + p.az * z1 - p.bound
            in0, in1 = d0 <= 0, d1 <= 0
            if in0 != in1:
                frac = d0 / (d0 - d1)
                result.append((x0 + frac * (x1 - x0),
                               z0 + frac * (z1 - z0)))
            if in1:
                result.append((x1, z1))
            x0, z0, d0 = x1, z1, d1
        # Eliminate duplicate edge vertices without changing the polygon.
        poly = []
        for point in result:
            if not poly or point != poly[-1]:
                poly.append(point)
        if len(poly) > 1 and poly[0] == poly[-1]:
            poly.pop()
    return tuple(poly)


def tile_regions(road, row: int, col: int) -> tuple[BspRegion, ...]:
    """Read actual counted-record polygons for one verified ROM-backed tile.

    These source BSP groups may overlap: the original CPU selects the FIRST
    matching region and its height-plane. The returned convex footprints
    cannot be treated as an ordered/occlusion-correct SH-2 scenery mesh.
    """
    if not (0 <= row < 64 and 0 <= col < 64):
        raise ValueError("source road tile out of range")
    offset = road.grid[row * 64 + col]
    if offset == road.background:
        return ()
    if not (0 <= offset < road.background):
        raise ValueError("unsupported source road record offset")
    start = road.bank_start + 2 * offset
    count = be16(road.rom, start)
    if count > 64 or offset + 15 + 14 * count > road.limit_word:
        raise ValueError("corrupt counted road group")
    out = []
    for group in range(count + 1):
        at = start + 2 + 28 * group
        constraints = group_halfplanes(road.rom, at)
        poly = clipped_polygon(constraints)
        if poly:
            out.append(BspRegion(group, constraints, poly))
    return tuple(out)


def first_group(regions: tuple[BspRegion, ...], x: int, z: int) -> int | None:
    """Original source priority: first accepting BSP group wins."""
    for region in regions:
        if contains(region.halfplanes, x, z):
            return region.group
    return None


def signed_area(poly: tuple[tuple[Fraction, Fraction], ...]) -> Fraction:
    """Exact polygon area for private audit reports, not a road-width proxy."""
    if len(poly) < 3:
        return Fraction(0)
    twice = sum(x0*z1 - x1*z0 for (x0,z0),(x1,z1)
                in zip(poly, poly[1:] + poly[:1]))
    return abs(twice) / 2


def mode_polygon_bounds(road, *, margin: int = 64) -> tuple[int, int, int, int]:
    """Original 68000 BSP coverage bounds, mapped to the 16,384 GBA world.

    This traces the exact *source collision boundary inequalities* instead
    of deriving bounds from the centres of coarse 8x8 raster samples.
    It includes narrow branches which a 64-unit source sampling grid can
    miss entirely. It does not infer a racing route, pit entry or SH-2 mesh.

    A 32X 512-unit tile occupies 256 GBA coordinates. Fractional source
    polygon vertices are rounded OUTWARDS with a small bounded margin.
    """
    import math

    if margin < 0 or margin > 512:
        raise ValueError("invalid source-BSP framing margin")
    minx = minz = None
    maxx = maxz = None
    for index, marker in enumerate(road.grid):
        if marker == road.background:
            continue
        row, col = divmod(index, 64)
        for region in tile_regions(road, row, col):
            for x, z in region.polygon:
                xx, zz = (col * 512 + x) / 2, (row * 512 + z) / 2
                if minx is None:
                    minx = maxx = xx
                    minz = maxz = zz
                else:
                    minx, maxx = min(minx, xx), max(maxx, xx)
                    minz, maxz = min(minz, zz), max(maxz, zz)
    if minx is None:
        raise ValueError("no source BSP road polygons")
    return (max(0, math.floor(minx) - margin),
            min(16383, math.ceil(maxx) + margin),
            max(0, math.floor(minz) - margin),
            min(16383, math.ceil(maxz) + margin))


def audit_mode_group_identity(road, *, sample_points=(0, 1, 63, 127, 255,
                                                       383, 447, 510, 511)) -> dict:
    """Private donor cross-check: original CPU group ID vs convex regions.

    Checks both road coverage and the first matched height-plane selector,
    rather than merely checking a 2D silhouette. Any mismatch is an error;
    otherwise the importer can rely on the exact integer inequalities.
    """
    comparisons = 0
    occupied_tiles = 0
    polygons = 0
    for index, marker in enumerate(road.grid):
        if marker == road.background:
            continue
        row, col = divmod(index, 64)
        regions = tile_regions(road, row, col)
        occupied_tiles += 1
        polygons += len(regions)
        for z in sample_points:
            for x in sample_points:
                hit = road.sample_local(row, col, x, z)
                predicted = first_group(regions, x, z)
                actual = hit.group if hit is not None else None
                if predicted != actual:
                    raise ValueError(
                        f"Source BSP polygon mismatch slot={road.slot} tile="
                        f"{row},{col} at {x},{z}: {predicted} != {actual}"
                    )
                comparisons += 1
    if occupied_tiles <= 0:
        raise ValueError("empty source road mode")
    return {"mode_slot": road.slot,
            "active_source_tiles": occupied_tiles,
            "clipped_source_regions": polygons,
            "sample_points_verified": comparisons,
            "first_match_source_parity": True}


class PriorityFace(NamedTuple):
    """A disjoint collision-plane footprint with its native source group."""
    group: int
    polygon: tuple[tuple[Fraction, Fraction], ...]


def clip_existing_polygon(poly: tuple[tuple[Fraction, Fraction], ...],
                          halfplane: HalfPlane, *,
                          inside: bool = True
                          ) -> tuple[tuple[Fraction, Fraction], ...]:
    """Clip a source polygon to one halfplane or its outside complement.

    The continuous boundaries share an edge; the 68000 exact integer
    strict/inclusive boundary is already encoded in the inequality limit.
    Fraction arithmetic makes this partition deterministic with no
    floating-point slivers.
    """
    if not poly:
        return ()
    out = []
    x0, z0 = poly[-1]
    d0 = halfplane.ax*x0 + halfplane.az*z0 - halfplane.bound
    if not inside:
        d0 = -d0
    for x1, z1 in poly:
        d1 = halfplane.ax*x1 + halfplane.az*z1 - halfplane.bound
        if not inside:
            d1 = -d1
        keep0, keep1 = d0 <= 0, d1 <= 0
        if keep0 != keep1:
            frac = d0 / (d0-d1)
            out.append((x0+frac*(x1-x0),z0+frac*(z1-z0)))
        if keep1:
            out.append((x1,z1))
        x0,z0,d0=x1,z1,d1
    clean=[]
    for p in out:
        if not clean or p!=clean[-1]:
            clean.append(p)
    if len(clean)>1 and clean[-1]==clean[0]:
        clean.pop()
    return tuple(clean)


def tile_priority_faces(road, row: int, col: int,
                        *, max_faces: int = 2048) -> tuple[PriorityFace, ...]:
    """Partition native collision footprints by original BSP group priority.

    Each later region loses area already covered by any *earlier* group.
    This preserves original plane precedence when generating a private
    3D collision-surface inspection mesh. No SH-2 scene vertices are
    claimed or exported. The safety limit rejects pathological tiles.
    """
    if not 1 <= max_faces <= 65535:
        raise ValueError("invalid source polygon budget")
    regions = tile_regions(road,row,col)
    result = []
    earlier = []
    for source in regions:
        if signed_area(source.polygon) <= 0:
            earlier.append(source)
            continue
        fragments=[source.polygon]
        for old in earlier:
            new=[]
            for poly in fragments:
                still_inside=poly
                # Outside each constraint in turn, keeping all earlier
                # constraints inside on the remainder. These disjoint
                # outside fragments exactly partition poly \ old.
                for plane in old.halfplanes:
                    outside = clip_existing_polygon(still_inside,plane,
                                                    inside=False)
                    if signed_area(outside)>0:
                        new.append(outside)
                        if len(result)+len(new)>max_faces:
                            raise ValueError("source BSP face budget exceeded")
                    still_inside = clip_existing_polygon(still_inside,plane,
                                                         inside=True)
                    if not still_inside:
                        break
            fragments=new
            if not fragments:
                break
        for fragment in fragments:
            result.append(PriorityFace(source.group,fragment))
            if len(result)>max_faces:
                raise ValueError("source BSP face budget exceeded")
        earlier.append(source)
    return tuple(result)
