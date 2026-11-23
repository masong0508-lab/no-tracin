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
    from tools.decode_vrd_32x_surface import RoadSurface, signed16
    from tools.export_vrd_32x_surface_mesh import grid_samples
    from tools.inspect_vrd_32x_tables import ROM_SHA256, inspect_image, be16
    from tools.vrd32x_map_geometry import sampled_preview_bits, sampled_road_bounds
    from tools.vrd32x_exact_bsp import mode_polygon_bounds, audit_mode_group_identity, tile_priority_faces
    from tools.vrd32x_layered_course import layered_main_cycle
except ModuleNotFoundError:
    from decode_vrd_32x_surface import RoadSurface, signed16
    from export_vrd_32x_surface_mesh import grid_samples
    from inspect_vrd_32x_tables import ROM_SHA256, inspect_image, be16
    from vrd32x_map_geometry import sampled_preview_bits, sampled_road_bounds
    from vrd32x_exact_bsp import mode_polygon_bounds, audit_mode_group_identity, tile_priority_faces
    from vrd32x_layered_course import layered_main_cycle

GAME_WORLD = 16384
HEIGHT_DIV = 32  # VISUAL/PHYSICS CALIBRATION ONLY; native units unverified
ROAD_SUBDIV = 8
SIDE = 64 * ROAD_SUBDIV
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


def make_route(heights, mask, slot, dist_to_edge, *, layered=None,
               bridge_heights=None, bridge_dist=None):
    # Source layer 1 is only the real 68000 alternate mode-3 bridge.
    pts=layered if layered is not None else [(0,y,x) for y,x in skeleton_route(mask)]
    raw_vals=[h for h in heights if h is not None]
    if bridge_heights is not None:
        raw_vals.extend(h for h in bridge_heights if h is not None)
    low=min(raw_vals)
    # Entire 2D road area, including divergent branches, remains drivable.
    # The representative loop is for camera/spawn/course preview ONLY.
    out=[];total=0
    for i,(layer,y,x) in enumerate(pts):
        _,yn,xn=pts[(i+1)%len(pts)]
        ax,az=int(x*PIXEL_WORLD+PIXEL_WORLD//2),int(y*PIXEL_WORLD+PIXEL_WORLD//2)
        bx,bz=int(xn*PIXEL_WORLD+PIXEL_WORLD//2),int(yn*PIXEL_WORLD+PIXEL_WORLD//2)
        dx,dz=bx-ax,bz-az
        length=max(1,int(math.sqrt(dx*dx+dz*dz)+.5))
        nx=int(round(dx/length*16384)); nz=int(round(dz/length*16384))
        heading=round(math.atan2(dx,dz)/math.tau*65536)&65535
        raw=(bridge_heights if layer else heights)[y*SIDE+x]
        if raw is None:raise ValueError('source route left its elevation layer')
        height=max(8,min(800,int((raw-low)/HEIGHT_DIV)+12))
        radius=8000
        nxtnext=pts[(i+2)%len(pts)];ex=(nxtnext[2]-xn)*PIXEL_WORLD;ez=(nxtnext[1]-yn)*PIXEL_WORLD
        turn=abs(math.atan2(ex,ez)-math.atan2(dx,dz));turn=min(turn,math.tau-turn)
        if turn>.01:radius=max(80,min(8000,int(length/turn)))
        active_dist=bridge_dist if layer else dist_to_edge
        width=max(24,min(170, int(active_dist[y,x]*PIXEL_WORLD*.8)))
        out.append(dict(x=ax,z=az,y=height,ux=nx,uz=nz,len=length,dist=total,
                        heading=heading,radius=radius,half_width=width,
                        native_source_layer=layer,flags=0,verge=12))
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


def exact_collision_tris(road, raw_minimum):
    """Priority-resolved source 68000 collision triangles for private GBA ROM.

    These are NOT the original 32X SH-2 display/scenery meshes.
    """
    rows=[];starts=[];counts=[]
    for tile in range(4096):
        starts.append(len(rows))
        before=len(rows)
        if road.grid[tile] == road.background:
            counts.append(0);continue
        cy,cx=divmod(tile,64)
        start=road.bank_start+2*road.grid[tile]
        for face in tile_priority_faces(road,cy,cx):
            poly=face.polygon
            if len(poly)<3:continue
            pos=start+2+28*face.group
            ax=signed16(be16(road.rom,pos+18))
            az=signed16(be16(road.rom,pos+20))
            constant=signed16(be16(road.rom,pos+22))
            shift=5 if (be16(road.rom,pos+24) & 0x80) else 6
            corners=[]
            for x,z in poly:
                gx=round((cx*512+float(x))/2)
                gz=round((cy*512+float(z))/2)
                raw=(ax*2*float(x)+az*2*float(z)+32*constant)/(1<<shift)
                gy=max(8,min(800,int((raw-raw_minimum)/HEIGHT_DIV)+12))
                corners.append((gx,gy,gz))
            for k in range(1,len(corners)-1):
                a,b,c=corners[0],corners[k],corners[k+1]
                if (b[0]-a[0])*(c[2]-a[2]) == (c[0]-a[0])*(b[2]-a[2]):continue
                if not all(0<=p[0]<GAME_WORLD and 0<=p[2]<GAME_WORLD for p in (a,b,c)):
                    raise ValueError('native BSP triangle outside GBA world')
                rows.append(a+b+c)
        count=len(rows)-before
        if count>65535 or len(rows)>65535:
            raise ValueError('native BSP triangle index exceeds u16 budget')
        counts.append(count)
    return starts,counts,rows


def write_pack(source:bytes,output:Path, *, exact_mesh=False):
    if hashlib.sha256(source).hexdigest()!=ROM_SHA256:
        raise ValueError('32X ROM hash does not match verified US source revision')
    # Validate every original 68000 source pointer and the full cartridge
    # identity exactly ONCE, before any private output is generated. Each
    # RoadSurface then reuses that immutable verified image (not five deep
    # reparses of every 4096-cell mode).
    inspect_image(source,require_identity=True)
    output.mkdir(parents=True,exist_ok=True)
    data=['// PRIVATE GENERATED source-ROM-derived road height tiles. Do NOT commit.',
          '// Source SHA256 '+ROM_SHA256,
          '// Heights are calibrated for study play, not original 32X units.',
          f'#define VRD_SAMPLE_SHIFT {int(math.log2(PIXEL_WORLD))}',
          f'#define VRD_TILE_SHIFT {int(math.log2(ROAD_SUBDIV))}',
          f'#define VRD_TILE_AREA {ROAD_SUBDIV*ROAD_SUBDIV}']
    routes=['// PRIVATE GENERATED source-ROM-derived approximate staging loops. Do NOT commit.']
    defs=[];lookups=[];heights_all=[];statistics=[];road_bounds=[];previews=[]
    mesh_firsts=[];mesh_counts=[];meshes=[]
    for slot in range(5):
        surface=RoadSurface(source,slot,verify_identity=False)
        side,samples=grid_samples(surface,ROAD_SUBDIV)
        if side!=SIDE:raise ValueError('wrong map resolution')
        mask=np.array([h is not None for h in samples],dtype=bool).reshape((SIDE,SIDE))
        bridge_surface=None
        bridge_samples=None
        bridge_mask=None
        bridge_report=None
        if slot==3:
            # Original 68000 mode 3 has a DIFFERENT alternate lookup for
            # its actual elevated road. It overlaps a lower native road.
            bridge_surface=RoadSurface(source,3,True,verify_identity=False)
            bridge_side,bridge_samples=grid_samples(bridge_surface,ROAD_SUBDIV)
            if bridge_side!=SIDE:raise ValueError('native 32X overpass grid mismatch')
            bridge_mask=np.array([h is not None for h in bridge_samples],dtype=bool).reshape((SIDE,SIDE))
            audit=audit_mode_group_identity(
                bridge_surface,sample_points=(0,128,256,384,511))
            if not audit['first_match_source_parity']:
                raise ValueError('authentic alternate source BSP mismatch')
        # Road coverage is from authenticated source BSP hits, NOT the
        # skeleton loop. Retain the +Z image coordinate convention.
        # Exact *source 68000 BSP* regions may include thin roads missed
        # by the coarse 8x8 collision/height raster. Reconstruct original
        # integer constraints to bound the complete collision geometry;
        # fail closed if the analytical first-match regions disagree with
        # the original ROM interpreter at representative source coordinates.
        region_audit=audit_mode_group_identity(
            surface, sample_points=(0,128,256,384,511))
        bounds=mode_polygon_bounds(surface)
        if bridge_surface is not None:
            bb=mode_polygon_bounds(bridge_surface)
            bounds=(min(bounds[0],bb[0]),max(bounds[1],bb[1]),
                    min(bounds[2],bb[2]),max(bounds[3],bb[3]))
        road_bounds.append(bounds)
        # This fixed GBA selector bitmap queries the ORIGINAL source BSP
        # at each projected pixel, not the approximated staging sample grid.
        # The actual physics collision raster remains an approximation.
        def exact_gba_road(x,z):
            wx,wz=x*2-GAME_WORLD,z*2-GAME_WORLD
            return (surface.sample_world(wx,wz) is not None or
                    (bridge_surface is not None and
                     bridge_surface.sample_world(wx,wz) is not None))
        preview=sampled_preview_bits(mask.flat,bounds,side=SIDE,
                                     units_per_sample=PIXEL_WORLD,
                                     source_sampler=exact_gba_road)
        preview_name=f'vrd_study_preview_{slot}'
        previews.append(preview_name)
        data.append(c_array('u8',preview_name,preview,32))
        distmap=distance_transform_edt(mask)
        if bridge_surface is not None:
            layered,bridge_report=layered_main_cycle(
                mask,bridge_mask,samples,bridge_samples,
                world_per_pixel=PIXEL_WORLD,max_points=MAX_POINTS)
            route,lap,minimum=make_route(
                samples,mask,slot,distmap,layered=layered,
                bridge_heights=bridge_samples,
                bridge_dist=distance_transform_edt(bridge_mask))
        else:
            route,lap,minimum=make_route(samples,mask,slot,distmap)
        first,count,flat=grid_lists(route)
        if exact_mesh:
            exact_first,exact_counts,exact_tris=exact_collision_tris(surface,minimum)
            stem=f'vrd_exact_{slot}'
            mesh_firsts.append(stem+'_first')
            mesh_counts.append(stem+'_count')
            meshes.append(stem+'_tris')
            data.append(c_array('u16',mesh_firsts[-1],exact_first,16))
            data.append(c_array('u16',mesh_counts[-1],exact_counts,16))
            data.append('static const VrdSourceTri '+meshes[-1]+'['+str(len(exact_tris)) + '] = {' + chr(10) +
                        (','+chr(10)).join('    {{'+', '.join(map(str,tri))+'}}' for tri in exact_tris) + chr(10) + '};')
        active=[];chunkmap=[];tiles=[]
        for cell in range(4096):
            cy,cx=divmod(cell,64);sub=[]
            for row in range(ROAD_SUBDIV):
                for col in range(ROAD_SUBDIV):
                    h=samples[(cy*ROAD_SUBDIV+row)*SIDE+(cx*ROAD_SUBDIV+col)]
                    sub.append(65535 if h is None else max(8,min(800,int((h-minimum)/HEIGHT_DIV)+12)))
            if all(h==65535 for h in sub):chunkmap.append(0)
            else:
                active.append(cell);chunkmap.append(len(active));tiles+=sub
        if bridge_surface is not None:
            # ROM-only independent source height grid for native upper road.
            alt_active=[];alt_lookup=[];alt_tiling=[]
            for cell in range(4096):
                cy,cx=divmod(cell,64);sub=[]
                for row in range(ROAD_SUBDIV):
                    for col in range(ROAD_SUBDIV):
                        h=bridge_samples[(cy*ROAD_SUBDIV+row)*SIDE+(cx*ROAD_SUBDIV+col)]
                        sub.append(65535 if h is None else
                                   max(8,min(800,int((h-minimum)/HEIGHT_DIV)+12)))
                if all(h==65535 for h in sub):alt_lookup.append(0)
                else:
                    alt_active.append(cell);alt_lookup.append(len(alt_active));alt_tiling+=sub
            data.append('#define VRD_HAS_NATIVE_OVERPASS 1')
            data.append(c_array('u16','vrd_native_overpass_lookup',alt_lookup,16))
            data.append(c_array('u16','vrd_native_overpass_heights',alt_tiling,16))
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
                               native_collision_triangle_count=len(exact_tris) if exact_mesh else None,
                               sample_subdivision=ROAD_SUBDIV,
                               collision_sample_pitch_gba=PIXEL_WORLD,
                               preview_sampled_road_bounds_gba=list(bounds),
                               preview_is_source_mask_not_skeleton=True,
                               preview_is_exact_rom_bsp=True,
                               native_integer_bsp_region_audit=region_audit,
                               source_preview_rom_bytes=len(preview),
                               min_raw_plane_height=minimum,calibrated_height_divisor=HEIGHT_DIV,
                               layered_alternate_68000_source_route=bridge_report,
                               native_alternate_road_tiles=len(alt_active) if bridge_surface is not None else 0,
                               native_alternate_road_samples=int(bridge_mask.sum()) if bridge_surface is not None else 0,
                               route_type=('source_normal_and_alternate_3d_layered_loop'
                                           if bridge_report is not None else
                                           'skeleton_cycle_NOT_authentic_start_finish_or_lap'),
                               geometry='private_68000_BSP_sampled_ROAD_STUDY'))
    routes.append('#define VRD32X_STUDY_TRACK_DEFS \\\n    '+', \\\n    '.join(defs))
    # Four immutable u16s per mode in ROM, no extra runtime EWRAM or heap.
    data.append('static const u16 vrd_study_preview_bounds[5][4] = {' + chr(10) +
                '    ' + (',' + chr(10) + '    ').join(
                    '{ '+', '.join(map(str, b))+' }' for b in road_bounds) +
                chr(10) + '};')
    if exact_mesh:
        data.append('#define VRD_EXACT_SOURCE_MESH 1')
        data.append('static const u16 *const vrd_exact_first[5] = { '+', '.join(mesh_firsts)+' };')
        data.append('static const u16 *const vrd_exact_count[5] = { '+', '.join(mesh_counts)+' };')
        data.append('static const VrdSourceTri *const vrd_exact_tris[5] = { '+', '.join(meshes)+' };')
    data.append('static const u8 *const vrd_study_preview_bits[5] = { '+', '.join(previews)+' };')
    data.append('static const u16 *const vrd_study_lookups[5] = { '+', '.join(lookups)+' };')
    data.append('static const u16 *const vrd_study_heights[5] = { '+', '.join(heights_all)+' };')
    for filename,content in [('vrd32x_study_tracks.h','\n\n'.join(routes)+'\n'),('vrd32x_study_data.h','\n\n'.join(data)+'\n')]:
        (output/filename).write_text(content)
    report=dict(source_sha256=ROM_SHA256,import_type='PRIVATE_32X_BSP_ROAD_STUDY_NOT_AUTHENTIC_RACES',
                named_course_ids_verified=False,source_poly_meshes_recovered=False,
                physics_height_scale_verified=False,lap_start_finish_verified=False,
                native_collision_mesh_in_gba=exact_mesh,
                adjustable_handling_and_motion_options_preserved=True,slots=statistics)
    (output/'study-manifest.json').write_text(json.dumps(report,indent=2)+'\n')
    return report


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--rom',type=Path,required=True)
    p.add_argument('--out-dir',type=Path,required=True)
    p.add_argument('--exact-mesh',action='store_true',help='private source BSP road polygons for close-up render')
    p.add_argument('--samples-per-cell',type=int,choices=(8,16),default=8,
                   help='8=legacy, 16=denser native-source collision sampling')
    a=p.parse_args()
    global SIDE,ROAD_SUBDIV,PIXEL_WORLD
    ROAD_SUBDIV=a.samples_per_cell
    SIDE=64*ROAD_SUBDIV
    PIXEL_WORLD=GAME_WORLD//SIDE
    report=write_pack(a.rom.read_bytes(),a.out_dir,exact_mesh=a.exact_mesh)
    print('Private 32X road-study pack:',[(s['slot'],s['route_points'],s['road_tile_count']) for s in report['slots']])

if __name__=='__main__':main()
