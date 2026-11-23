"""Precompute exact original-source GBA road-ribbon vertices and culling groups.

Route coordinates, tangent and widths are supplied by the private 68000
cartridge importer. This tool only performs the same integer arithmetic as
vrd32x_study.c used to do every frame: it adds NO invented geometry, scenery
or authentic race rules. All output arrays live in cartridge ROM; no frame
buffer, heap, EWRAM or IWRAM expansion is required.

Conservative group radius uses ceil(9*distance/7)+child_radius+16 to
dominate the r_visible() horizontal/forward inequalities including fixed-point rounding.
The individual per-segment frustum test is still performed, so the output
triangles are identical (in both order and coordinates) to the old renderer.
"""
from __future__ import annotations

import math
from typing import NamedTuple

GROUP_SIZE = 8


class Ribbon(NamedTuple):
    xyz: tuple[int, ...]       # four vertices, x/y/z per vertex, GBA world
    mx: int
    mz: int
    reach: int
    shade: int
    short_mark: int


class Group(NamedTuple):
    mx: int
    mz: int
    reach: int


def original_ribbon_quads(route: list[dict]) -> list[Ribbon]:
    """Bit-exact copy of the previous source-road ribbon vertex math."""
    if not (3 <= len(route) <= 254):
        raise ValueError('invalid authentic 32X route length')
    rows = []
    for i, a in enumerate(route):
        b = route[(i+1) % len(route)]
        ax, az, ay = a['x'], a['z'], a['y']
        bx, bz, by = b['x'], b['z'], b['y']
        wa = min(110, max(14, (a['half_width']*3) // 4))
        wb = min(110, max(14, (b['half_width']*3) // 4))
        left_ax = (a['uz']*wa) >> 14
        left_az = (a['ux']*wa) >> 14
        left_bx = (b['uz']*wb) >> 14
        left_bz = (b['ux']*wb) >> 14
        xyz = (
            ax-left_ax, ay+3, az+left_az,
            bx-left_bx, by+3, bz+left_bz,
            bx+left_bx, by+3, bz-left_bz,
            ax+left_ax, ay+3, az-left_az,
        )
        if any(not -32768 <= value <= 32767 for value in xyz):
            raise ValueError('native road vertex exceeds signed 16-bit GBA range')
        mx = (ax + bx) // 2
        mz = (az + bz) // 2
        reach = 180 + a['len']//2
        if not all(0 <= value < 65536 for value in (reach,)):
            raise ValueError('out-of-bounds original segment visibility radius')
        rows.append(Ribbon(xyz,mx,mz,reach,int(abs(ay-by)>36),int(a['len']<350)))
    return rows


def conservative_visibility_groups(quads: list[Ribbon], group_size=GROUP_SIZE) -> list[Group]:
    """Conservative grouped source-world visibility; NO missed children."""
    if group_size < 1 or group_size > 32:
        raise ValueError('invalid source ribbon group size')
    if not quads:
        raise ValueError('cannot group empty original course')
    groups=[]
    for start in range(0,len(quads),group_size):
        block=quads[start:start+group_size]
        mx=(min(q.mx for q in block) + max(q.mx for q in block))//2
        mz=(min(q.mz for q in block) + max(q.mz for q in block))//2
        # r_visible side*5 < depth*4+radius*7: camera rotation
        # can shift side by d and depth by -d, so (5+4)*d/7
        # is sufficient. Add 16 for integer-truncation safety.
        radius=max(q.reach + (9*math.ceil(math.hypot(q.mx-mx,q.mz-mz))+6)//7 + 16
                   for q in block)
        if not (-32768 <= mx <= 32767 and -32768 <= mz <= 32767 and 0<=radius<=65535):
            raise ValueError('visibility sphere exceeds original GBA coordinate format')
        groups.append(Group(mx,mz,radius))
    return groups


def visibility(dx: int, dz: int, cos: int, sin: int, reach: int, far: int) -> bool:
    """Reference for production C r_visible(), camera at origin."""
    side = (dx*cos - dz*sin)>>14
    depth = (dx*sin + dz*cos)>>14
    if depth < -reach or depth > far+reach:
        return False
    return abs(side)*5 < depth*4+reach*7


def group_culling_never_drops_visible_child(
    quads: list[Ribbon], groups: list[Group],
    camera_x: int, camera_z: int, cos: int, sin: int,
    far: int = 1800, size: int = GROUP_SIZE
) -> bool:
    for i, quad in enumerate(quads):
        if visibility(quad.mx-camera_x,quad.mz-camera_z,cos,sin,quad.reach,far):
            grp=groups[i//size]
            if not visibility(grp.mx-camera_x,grp.mz-camera_z,cos,sin,grp.reach,far):
                return False
    return True
