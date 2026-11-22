#!/usr/bin/env python3
"""Private, hash-checked 32X road study importer for the GBA.

Produces five *unnamed* free-drive road-study environments. Source road
surfaces are sampled from the verified 68000 BSP+height equations, but the
skeleton-derived staging routes, vertical/world conversion and graphics are
study approximations, NOT original Sega races, polygon assets or AI paths.

Never alters the source ROM or commits generated user-owned game data.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path

import networkx as nx
import numpy as np
from scipy.ndimage import distance_transform_edt
from skimage.morphology import skeletonize

try:
    from tools.decode_vrd_32x_surface import RoadSurface
    from tools.export_vrd_32x_surface_mesh import grid_samples
    from tools.inspect_vrd_32x_tables import ROM_SHA256
except ModuleNotFoundError:
    from decode_vrd_32x_surface import RoadSurface
    from export_vrd_32x_surface_mesh import grid_samples
    from inspect_vrd_32x_tables import ROM_SHA256

SIDE = 512
GAME_WORLD = 16384
HEIGHT_DIV = 32  # VISUAL/PHYSICS CALIBRATION ONLY; native units unverified
ROAD_SUBDIV = 8
PIXEL_WORLD = GAME_WORLD // SIDE
MAX_POINTS = 230


def skeleton_route(mask):
    """Stage a path *inside* verified road mask, not an original racing line."""
    nodes = set(map(tuple, np.argwhere(skeletonize(mask))))
    graph = nx.Graph()
    graph.add_nodes_from(nodes)
    for y,x in nodes:
        for dy,dx in ((1,0),(0,1),(1,1),(1,-1)):
            other = (y+dy,x+dx)
            if other in nodes:
                graph.add_edge((y,x),other,weight=1.41421356237 if dy and dx else 1.)
    if not nodes: raise ValueError('empty road skeleton')
    comp=max(nx.connected_components(graph),key=len)
    graph=graph.subgraph(comp).copy()
    cycles=nx.cycle_basis(graph)
    if not cycles: raise ValueError('could not derive a safe circular staging route')
    circle=max(cycles,key=lambda path:sum(math.hypot(path[i][0]-path[i-1][0],path[i][1]-path[i-1][1]) for i in range(len(path))))
    if len(circle)<35: raise ValueError('largest staging route too small')
    # The cycle list follows adjacent skeleton vertices, but has no authentic
    # start/finish association. Sample evenly without exceeding u8 seg ids.
    stride=max(1,math.ceil(len(circle)/MAX_POINTS))
    pts=circle[::stride]
    if len(pts)>MAX_POINTS:pts=pts[:MAX_POINTS]
    if len(pts)<20: raise ValueError('not enough staging points')
    for i,(y,x) in enumerate(pts):
        if not mask[y,x]:raise ValueError('staging path left source road')
    return pts


def make_route(heights, mask, slot, dist_to_edge):
    pts=skeleton_route(mask)
    raw_vals=[h for h in heights if h is not None]
    low=min(raw_vals)
    # Entire 2D road area, including divergent branches, remains drivable.
    # The representative loop is for camera/spawn/course preview ONLY.
    out=[];total=0
    for i,(y,x) in enumerate(pts):
        yn,xn=pts[(i+1)%len(pts)]
        ax,az=int(x*PIXEL_WORLD+PIXEL_WORLD//2),int(y*PIXEL_WORLD+PIXEL_WORLD//2)
        bx,bz=int(xn*PIXEL_WORLD+PIXEL_WORLD//2),int(yn*PIXEL_WORLD+PIXEL_WORLD//2)
        dx,dz=bx-ax,bz-az
        length=max(1,int(math.sqrt(dx*dx+dz*dz)+.5))
        nx=int(round(dx/length*16384)); nz=int(round(dz/length*16384))
        heading=round(math.atan2(dx,dz)/math.tau*65536)&65535
        raw=heights[y*SIDE+x]
        height=max(8,min(800,int((raw-low)/HEIGHT_DIV)+12))
        radius=8000
        nxtnext=pts[(i+2)%len(pts)];ex=(nxtnext[1]-xn)*32;ez=(nxtnext[0]-yn)*32
        turn=abs(math.atan2(ex,ez)-math.atan2(dx,dz));turn=min(turn,math.tau-turn)
        if turn>.01:radius=max(80,min(8000,int(length/turn)))
        width=max(24,min(170, int(dist_to_edge[y,x]*PIXEL_WORLD*.8)))
        out.append(dict(x=ax,z=az,y=height,ux=nx,uz=nz,len=length,dist=total,
                        heading=heading,radius=radius,half_width=width,flags=0,verge=12))
        total+=length
    if total>65535:
        raise ValueError(f'route {slot} length {total} exceeds u16; reduce sampling scale')
    return out,total,low


def grid_lists(route):
    reach=230
    out=[set() for _ in range(128*128)]
    for i,p in enumerate(route):
        ax,az=p['x'],p['z'];bx=ax+p['ux']*p['len']/16384; bz=az+p['uz']*p['len']/16384
        cx0=max(0,int((min(ax,bx)-reach)//128));cx1=min(127,int((max(ax,bx)+reach)//128))
        cz0=max(0,int((min(az,bz)-reach)//128));cz1=min(127,int((max(az,bz)+reach)//128))
        for cz in range(cz0,cz1+1):
            for cx in range(cx0,cx1+1):
                mx=(cx+.5)*128;mz=(cz+.5)*128
                along=max(0,min(p['len'],((mx-ax)*p['ux']+(mz-az)*p['uz'])/16384))
                rx=mx-ax-p['ux']*along/16384;rz=mz-az-p['uz']*along/16384
                if rx*rx+rz*rz < (reach+100)**2:out[cz*128+cx].add(i)
    first=[];count=[];flat=[]
    for cell in out:
        v=sorted(cell)
        if len(v)>255:raise ValueError('nearby route exceeds u8 list count')
        first.append(len(flat)); count.append(len(v));flat+=v
    if len(flat)>65535:raise ValueError('route grid larger than u16 offsets')
    return first,count,flat


def c_array(ctype,name,items,n=16):
    parts=[f'static const {ctype} {name}[{len(items)}] = {{']
    for i in range(0,len(items),n):parts.append('    '+', '.join(map(str,items[i:i+n]))+',')
    parts.append('};');return '\n'.join(parts)


def write_pack(source:bytes,output:Path):
    if hashlib.sha256(source).hexdigest()!=ROM_SHA256:
        raise ValueError('32X ROM hash does not match verified US source revision')
    output.mkdir(parents=True,exist_ok=True)
    data=['// PRIVATE GENERATED source-ROM-derived road height tiles. Do NOT commit.','// Source SHA256 '+ROM_SHA256,'// Heights are calibrated for study play, not original 32X units.']
    routes=['// PRIVATE GENERATED source-ROM-derived approximate staging loops. Do NOT commit.']
    defs=[];lookups=[];heights_all=[];statistics=[]
    for slot in range(5):
        surface=RoadSurface(source,slot)
        side,samples=grid_samples(surface,ROAD_SUBDIV)
        if side!=SIDE:raise ValueError('wrong map resolution')
        mask=np.array([h is not None for h in samples],dtype=bool).reshape((SIDE,SIDE))
        distmap=distance_transform_edt(mask)
        route,lap,minimum=make_route(samples,mask,slot,distmap)
        first,count,flat=grid_lists(route)
        active=[];chunkmap=[];tiles=[]
        for cell in range(4096):
            cy,cx=divmod(cell,64);sub=[]
            for row in range(8):
                for col in range(8):
                    h=samples[(cy*8+row)*SIDE+(cx*8+col)]
                    sub.append(65535 if h is None else max(8,min(800,int((h-minimum)/HEIGHT_DIV)+12)))
            if all(h==65535 for h in sub):chunkmap.append(0)
            else:
                active.append(cell);chunkmap.append(len(active));tiles+=sub
        lookups.append(f'vrd_study_tile_{slot}')
        heights_all.append(f'vrd_study_height_{slot}')
        data.append(c_array('u16',lookups[-1],chunkmap,16))
        data.append(c_array('u16',heights_all[-1],tiles,16))
        stem=f'vrd_study_{slot}'
        rows=[]
        for p in route:rows.append('{ '+', '.join(map(str,(p['x'],p['z'],p['y'],p['ux'],p['uz'],p['len'],p['dist'],p['heading'],p['radius'],p['flags'],p['verge'])))+' }')
        routes.append(f'static const TrackPt {stem}_pts[{len(route)}] = {{\n    '+',\n    '.join(rows)+'\n};')
        routes.append(c_array('u16',stem+'_cell_first',first,16))
        routes.append(c_array('u8',stem+'_cell_count',count,32))
        routes.append(c_array('u8',stem+'_cell_list',flat,32))
        routes.append(c_array('u16',stem+'_half_widths',[p['half_width'] for p in route],20))
        defs.append(f'{{ "32X SLOT {slot}", "ROAD STUDY", {stem}_pts, {len(route)}, {lap}, '+
                    f'{stem}_cell_first, {stem}_cell_count, {stem}_cell_list, '+
                    f'0, 0, 0, 0, 0, 0, 0, 0, 3, 180, 90, SCENE_MOUNTAINS, {stem}_half_widths }}')
        statistics.append(dict(slot=slot,route_points=len(route),staging_route_length=lap,
                               road_tile_count=len(active),valid_sample_pixels=int(mask.sum()),
                               min_raw_plane_height=minimum,calibrated_height_divisor=HEIGHT_DIV,
                               route_type='skeleton_cycle_NOT_authentic_start_finish_or_lap',
                               geometry='private_68000_BSP_sampled_ROAD_STUDY'))
    routes.append('#define VRD32X_STUDY_TRACK_DEFS \\\n    '+', \\\n    '.join(defs))
    data.append('static const u16 *const vrd_study_lookups[5] = { '+', '.join(lookups)+' };')
    data.append('static const u16 *const vrd_study_heights[5] = { '+', '.join(heights_all)+' };')
    for filename,content in [('vrd32x_study_tracks.h','\n\n'.join(routes)+'\n'),('vrd32x_study_data.h','\n\n'.join(data)+'\n')]:
        (output/filename).write_text(content)
    report=dict(source_sha256=ROM_SHA256,import_type='PRIVATE_32X_BSP_ROAD_STUDY_NOT_AUTHENTIC_RACES',
                named_course_ids_verified=False,source_poly_meshes_recovered=False,
                physics_height_scale_verified=False,lap_start_finish_verified=False,
                adjustable_handling_and_motion_options_preserved=True,slots=statistics)
    (output/'study-manifest.json').write_text(json.dumps(report,indent=2)+'\n')
    return report


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--rom',type=Path,required=True)
    p.add_argument('--out-dir',type=Path,required=True)
    a=p.parse_args()
    report=write_pack(a.rom.read_bytes(),a.out_dir)
    print('Private 32X road-study pack:',[(s['slot'],s['route_points'],s['road_tile_count']) for s in report['slots']])

if __name__=='__main__':main()
