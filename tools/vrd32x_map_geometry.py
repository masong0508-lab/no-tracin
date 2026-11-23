"""Source-grounded 32X road-map framing; no external dependencies or donor data.

The original 68000 BSP sampler produces a 512x512 boolean road mask.
This module computes *sampled* source coverage bounds for private GBA
preview generation, rather than treating an entire active 64x64 tile as
road or fitting the map around an invented staging cycle.

No named course identity, original SH-2 mesh or racing route is inferred.
"""


def sampled_road_bounds(valid_mask, *, side=512, units_per_sample=32, margin=2):
    """Return (min_x,max_x,min_z,max_z) inclusive in GBA world units.

    valid_mask is a flat side*side iterable of source BSP validity booleans,
    in row-major +Z order. Margins are in *sample* units. This is a display
    bound, not a collision boundary or original source asset bounding box.
    """
    if side < 2 or units_per_sample < 1 or margin < 0:
        raise ValueError("invalid source road-grid geometry")
    lo_x = lo_z = side
    hi_x = hi_z = -1
    count = 0
    for i, valid in enumerate(valid_mask):
        if i >= side * side:
            raise ValueError("road mask exceeds its declared dimensions")
        count = i + 1
        if not valid:
            continue
        z, x = divmod(i, side)
        lo_x = min(lo_x, x)
        hi_x = max(hi_x, x)
        lo_z = min(lo_z, z)
        hi_z = max(hi_z, z)
    if count != side * side:
        raise ValueError("road mask is truncated")
    if hi_x < 0:
        raise ValueError("no authenticated road samples in mask")
    limit = side * units_per_sample - 1
    return (
        max(0, (lo_x - margin) * units_per_sample),
        min(limit, (hi_x + margin + 1) * units_per_sample - 1),
        max(0, (lo_z - margin) * units_per_sample),
        min(limit, (hi_z + margin + 1) * units_per_sample - 1),
    )


def sampled_preview_bits(valid_mask, bounds, *, side=512,
                         units_per_sample=32, width=102, height=68):
    """Generate a ROM-only road silhouette for the animated GBA selector.

    This is the *same* source-backed, aspect-preserving, centre-and-corners
    projection as vrd32x_study_map at its normal 102x68 menu size.
    Output is packed little-bit-first, row-major, no proprietary bytes.
    All work happens at private import time, never in the render loop.
    """
    if side < 2 or units_per_sample < 1 or width <= 4 or height <= 4:
        raise ValueError("invalid 32X preview format")
    valid = list(valid_mask)
    if len(valid) != side * side:
        raise ValueError("source road mask has incorrect dimensions")
    minx, maxx, minz, maxz = bounds
    world = side * units_per_sample
    if not (0 <= minx < maxx < world and 0 <= minz < maxz < world):
        raise ValueError("invalid sampled source-road bounds")
    dx, dz = maxx - minx, maxz - minz
    sx, sz = dx * 256 // (width-4), dz * 256 // (height-4)
    scale = max(1, sx, sz)
    ox = (width - dx * 256 // scale) // 2
    oy = (height + dz * 256 // scale) // 2
    probe = max(1, scale // 1024)
    bits = [0] * ((width * height + 7) // 8)

    def trunc_div(a, b):
        return a // b if a >= 0 else -((-a) // b)

    def road(x, z):
        if not (0 <= x < world and 0 <= z < world):
            return False
        return bool(valid[(z // units_per_sample) * side
                          + (x // units_per_sample)])

    for py in range(height):
        z = minz + trunc_div((oy-py) * scale, 256)
        for px in range(width):
            x = minx + trunc_div((px-ox) * scale, 256)
            if (road(x,z) or road(x-probe,z-probe)
                    or road(x+probe,z-probe) or road(x-probe,z+probe)
                    or road(x+probe,z+probe)):
                i = py*width+px
                bits[i>>3] |= 1 << (i&7)
    return bits
