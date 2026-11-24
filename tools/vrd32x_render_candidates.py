"""Source-exact render eligibility masks for owner-private 32X road data.

Stdlib-only by design: tests and standard GBA source builds do not depend on
NumPy/SciPy, original Sega ROMs, or proprietary generated course data.
This decision gate never alters original road collision or game physics.
"""

def raster_road_candidates(normal_samples, side: int, alternate_samples=None):
    """Exact, conservative per-64-unit raster draw eligibility in 8 KiB.

    The optional GBA renderer only draws a 64-unit source patch if at
    least 3 of its 4 sampled corners are native road. Compute that gate
    once against the same 16/32-unit source samples during private import.
    For Highland, upper/lower road coverage is UNIONED only for this
    boolean draw gate; distinct native heights are NOT combined. Never
    use this mask to alter physics or ground-plane classification.

    Bit i corresponds to source patch (x=i%256, z=i//256), little-bit-first.
    Every possible production quad is included, with no false negatives.
    This is a render-only ROM optimization, not original 32X mesh data.
    """
    if side not in (512, 1024) or len(normal_samples) != side * side:
        raise ValueError('invalid original 68000 road sampling lattice')
    if alternate_samples is not None and len(alternate_samples) != side * side:
        raise ValueError('invalid alternate native-source lattice')
    stride = side // 256
    out = bytearray(8192)

    def road(sx, sz):
        if sx >= side or sz >= side:
            return False
        i = sz * side + sx
        return (normal_samples[i] is not None or
                (alternate_samples is not None and alternate_samples[i] is not None))

    for z in range(256):
        sz = z * stride
        for x in range(256):
            sx = x * stride
            supported = (road(sx, sz) + road(sx+stride, sz) +
                         road(sx+stride, sz+stride) + road(sx, sz+stride))
            if supported >= 3:
                index = z * 256 + x
                out[index >> 3] |= 1 << (index & 7)
    return list(out)
