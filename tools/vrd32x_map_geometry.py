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
