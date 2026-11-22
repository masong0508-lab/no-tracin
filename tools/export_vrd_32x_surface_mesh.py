#!/usr/bin/env python3
"""Export private *sampled* Virtua Racing Deluxe 32X road surfaces as OBJ.

Each vertex comes from the original 68000 BSP boundary classifier and
plane_eval height equation via decode_vrd_32x_surface.py. Quads join four
neighboring on-road samples; they are a reconstruction at fixed resolution,
NOT Sega's original polygon/mesh topology, materials, scenery or racing route.

The default vertical exaggeration is for viewing only. Native calibration,
tunnel/overpass layers, collision thickness and named course IDs remain
unverified. Never commit the derived .obj back to the source repository.
"""
import argparse
from pathlib import Path
try:
    from tools.decode_vrd_32x_surface import RoadSurface
except ModuleNotFoundError:
    from decode_vrd_32x_surface import RoadSurface


def grid_samples(surface: RoadSurface, samples_per_cell: int = 8):
    """Yield 2D road height samples at cell-local pixel centers."""
    if not 1 <= samples_per_cell <= 16 or 512 % samples_per_cell:
        raise ValueError('samples per 32X tile must divide 512 (1,2,4,8,16)')
    pitch = 512 // samples_per_cell
    side = 64 * samples_per_cell
    heights = [None] * (side * side)
    for tile, marker in enumerate(surface.grid):
        if marker == surface.background:
            continue
        ty, tx = divmod(tile, 64)
        for sy in range(samples_per_cell):
            z = sy * pitch + pitch // 2
            row = (ty * samples_per_cell + sy) * side + tx * samples_per_cell
            for sx in range(samples_per_cell):
                x = sx * pitch + pitch // 2
                hit = surface.sample_local(ty, tx, x, z)
                if hit is not None:
                    heights[row+sx] = hit.height
    return side, heights


def surface_faces(side: int, heights: list):
    """Generate triangles only on surfaces where all 4 samples are on-road.

    Reverse a diagonal when it creates less vertical error on steep hills.
    Uses deterministic point indexing, never connects across background.
    """
    if side < 2 or len(heights) != side * side:
        raise ValueError("invalid sample raster dimensions")
    for row in range(side-1):
        for col in range(side-1):
            a=row*side+col;b=a+1;c=a+side;d=c+1
            if any(heights[i] is None for i in (a,b,c,d)):
                continue
            if abs(heights[a]-heights[d]) <= abs(heights[b]-heights[c]):
                yield (a,c,d),(a,d,b)
            else:
                yield (a,c,b),(c,d,b)


def write_obj(path: Path, side: int, heights: list, *, vertical_divisor=32):
    """3D world: X,Z = source 32X coordinates/8; Y = raw plane output/divisor."""
    if side<2 or len(heights)!=side*side or vertical_divisor<=0:
        raise ValueError('invalid source sample raster/vertical scaling')
    vertex_ids=[0] * len(heights)
    nodes=0; faces=0
    with open(path,'w',encoding='utf-8') as out:
        out.write('# User-owned source ROM: sampled BSP/height surface, not native 32X meshes\n')
        out.write('# Course names, branch ordering and height units are not established\n')
        out.write('o reconstructed_vrd_32x_road_surface\n')
        for y in range(side):
            for x in range(side):
                idx=y*side+x;h=heights[idx]
                if h is None:continue
                nodes+=1;vertex_ids[idx]=nodes
                world_x=((x+.5)/side*32768 - 16384)/8
                world_z=((y+.5)/side*32768 - 16384)/8
                out.write(f'v {world_x:.4f} {h/vertical_divisor:.4f} {world_z:.4f}\n')
        for triangle_pair in surface_faces(side,heights):
            for a,b,c in triangle_pair:
                out.write(f'f {vertex_ids[a]} {vertex_ids[b]} {vertex_ids[c]}\n')
                faces+=1
    return {'vertices':nodes,'triangles':faces,'grid_side':side,
            'vertical_divisor':vertical_divisor,'source_height_raw':'signed 68000 plane_eval'}


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('rom',type=Path,help='Original US 32X cartridge, never modified')
    p.add_argument('--out-dir',type=Path,required=True,help='Private derived-asset directory')
    p.add_argument('--samples-per-cell',type=int,default=8,choices=[1,2,4,8,16])
    p.add_argument('--vertical-divisor',type=int,default=32)
    a=p.parse_args()
    raw=a.rom.read_bytes()
    a.out_dir.mkdir(parents=True,exist_ok=True)
    import json
    records=[]
    for slot in range(5):
        road=RoadSurface(raw,slot)
        side,heights=grid_samples(road,a.samples_per_cell)
        output=a.out_dir/f'vrd_32x_road_slot_{slot}.obj'
        statistics=write_obj(output,side,heights,vertical_divisor=a.vertical_divisor)
        records.append({'slot':slot,'file':output.name,**statistics})
        print(f'Slot {slot}: {statistics["vertices"]} vertices, {statistics["triangles"]} triangles')
    (a.out_dir/'study_manifest.json').write_text(json.dumps({
      'status':'source-derived-sampled-study-mesh_NOT_playable_course',
      'source_sha256':__import__('hashlib').sha256(raw).hexdigest(),
      'height_unit_calibration_verified':False,
      'course_names_mapped':False,
      'original_32x_meshes_recovered':False,
      'meshes':records},indent=2)+'\n')


if __name__=='__main__':main()
