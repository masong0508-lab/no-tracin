"""Private 32X-derived landscape under authentic original road surfaces.

Only road heights, masks and centreline positions are native Sega 68000 data.
Offroad terrain is a *derived* bounded interpolation, and roadside props are
clearly labeled authored GBA approximations, never a claimed source SH-2 mesh.
ROM-only data; no asset extraction / proprietary original ROM included.
"""
from __future__ import annotations
import math
import numpy as np
from scipy.ndimage import distance_transform_edt, gaussian_filter

WORLD = 16384
TERRAIN_STEP = 256
TERRAIN_SIDE = 65


def terrain_from_source(road_samples, side: int, native_low: int,
                        slot: int, native_route: list[dict]):
    """Build smoothly joined 64x64 terrain from verified road height samples.

    Original road elevations dominate the local ground baseline; farther from
    the source track a continuous, deterministic low-frequency relief blends
    in. Do not mistake extrapolated terrain for an original SH-2 scene mesh.
    Returns 65x65 u16 vertex heights, 64x64 natural land cover, props.
    """
    if side not in (512, 1024) or len(road_samples) != side*side:
        raise ValueError('native 68000 raster dimensions mismatch')
    if slot not in range(5):
        raise ValueError('only five authenticated original course modes')
    if not native_route:
        raise ValueError('source-ordered road points required')
    pitch = WORLD // side
    valid = np.fromiter((p is not None for p in road_samples),
                        dtype=np.bool_, count=side*side).reshape(side,side)
    if valid.sum() < 20:
        raise ValueError('source cartridge road mask cannot form terrain')
    raw = np.fromiter((int(p) if p is not None else int(native_low)
                      for p in road_samples),dtype=np.int32,
                      count=side*side).reshape(side,side)
    # Exact nearest *original road* source raster, not the invented route.
    dist,nearest=distance_transform_edt(~valid,return_indices=True)
    cs=np.minimum(np.arange(TERRAIN_SIDE)*256//pitch,side-1)
    gz,gx=np.meshgrid(cs,cs,indexing='ij')
    rh=raw[nearest[0,gz,gx],nearest[1,gz,gx]]
    base=np.clip((rh-native_low)//32+12,8,800).astype(np.float64)
    dd=dist[gz,gx]*pitch
    X,Z=np.meshgrid(np.arange(TERRAIN_SIDE)*256,
                    np.arange(TERRAIN_SIDE)*256,indexing='xy')
    # Wide ridges, not per-cell random checkerboard. Vary independent of the
    # view/camera, to remain stable through driving / turns and across frames.
    ridges=(np.sin((X+slot*431)/620.0)*np.cos((Z-slot*709)/730.0)
            +0.36*np.sin((X+Z+slot*101)/390.0))
    strength=(21,24,100,62,145)[slot]
    far=np.minimum(1.,dd/650.0)
    terrain=np.maximum(0,np.minimum(780,base-14 +
                  (0.62+ridges)*strength*far -np.minimum(dd/55.,24.)))
    # Blend coarse offroad elevations, NEVER alter the original 68000 road
    # samples. Slight smoothing avoids artificial 256-unit ground cliffs.
    terrain=gaussian_filter(terrain,sigma=0.7,mode='nearest')
    terrain=np.rint(terrain).astype(np.uint16)
    cover=[]
    for row in range(64):
        for col in range(64):
            distance=dd[row,col]
            h=float(terrain[row,col])
            x=(col+0.5)*256; z=(row+0.5)*256
            patch=(math.sin((x+slot*389)/1040.)+
                   0.55*math.cos((z-slot*151)/870.))
            if slot==0:      # Big Forest: grass and natural earth shoulders
                style=0 if patch>-.35 else 4
            elif slot==1:    # Bay Bridge: open water and pale docklands
                style=3 if distance>480 and h<max(30,base[row,col]-13) else (
                      1 if patch<-.45 else 0)
            elif slot==2:    # Acropolis: light rocky valleys and slopes
                style=5 if distance>230 and patch>-.2 else (1 if patch>-.45 else 4)
            elif slot==3:    # Highland: cool uplands / green inclines
                style=5 if distance>360 and patch>0.3 else (4 if patch<-.35 else 0)
            else:           # Sand Park: ochre sand and rock escarpments
                style=2 if patch>-.4 else (5 if distance>450 else 1)
            cover.append(style)
    out_props=[]
    # Distinct skyline landmarks use original course-ordered positions and
    # verified nearby road extents. The MODELS AND THEIR PLACEMENT are
    # GBA-authored approximations until Sega's SH-2 model data is decoded.
    # Entry: source node index, side (-1/+1, or 0 for an over-road portal),
    # rendering kind, model size; no original graphics bytes are included.
    anchored={
        0:((2,-1,0,100),(4,1,1,110),(8,1,6,122)),
        1:((2,0,9,136),(4,-1,2,130),(9,1,3,125)),
        2:((2,-1,4,155),(4,1,8,135),(9,1,7,128)),
        3:((2,-1,0,120),(5,1,4,132),(9,1,0,104)),
        4:((2,-1,8,148),(3,1,8,178),(8,-1,4,145)),
    }
    for idx,side,kind,size in anchored[slot]:
        p=native_route[min(idx,len(native_route)-1)]
        tangent_x=p['ux'];tangent_z=p['uz']
        if side:
            dist=max(175,p['half_width']+110)
            x=int(p['x']+side*(-tangent_z)*dist/16384)
            z=int(p['z']+side*tangent_x*dist/16384)
            for _ in range(4):
                if not 200<=x<WORLD-200 or not 200<=z<WORLD-200:
                    break
                if not valid[min(len(valid)-1,z//pitch),
                             min(len(valid)-1,x//pitch)]:
                    break
                dist+=65
                x=int(p['x']+side*(-tangent_z)*dist/16384)
                z=int(p['z']+side*tangent_x*dist/16384)
        else:
            x,z=p['x'],p['z']
        if not 192<=x<WORLD-192 or not 192<=z<WORLD-192:
            continue
        h=p['y'] if not side else int(terrain[min(64,z//256),min(64,x//256)])
        out_props.append((x,h,z,int(tangent_x),int(tangent_z),kind,size))
    # Every point is anchored to a verified source-ordered route segment,
    # then offset OUT of original native asphalt. Not claimed original
    # scenery placement; avoid trees/objects in the active road collision.
    phase=slot*7
    for index in range(0,len(native_route),5):
        p=native_route[index]
        tangent_x=p['ux'];tangent_z=p['uz']
        turn=-1 if ((index//5+phase)&1) else 1
        size=48+((index*19+phase*11)%48)
        offset=150+((index*37+phase*47)%220)
        x=int(p['x']+turn*(-tangent_z)*offset/16384)
        z=int(p['z']+turn*tangent_x*offset/16384)
        if not 192<=x<WORLD-192 or not 192<=z<WORLD-192:
            continue
        sample_x=min(64,max(0,x//256));sample_z=min(64,max(0,z//256))
        if valid[min(side-1,z//pitch),min(side-1,x//pitch)]:
            continue
        height=int(terrain[sample_z,sample_x])
        if slot==0: kind=(0 if index%4 else 1) # tall pines and deciduous trees
        elif slot==1:kind=(2 if index%3 else 3) # port structures, sparse trees
        elif slot==2:kind=(4 if index%3 else 5) # cut rock, low stone ruins
        elif slot==3:kind=(0 if index%3 else 4) # pines, exposed rock
        else:kind=(4 if index%3 else 5)        # desert cliffs, sandstone
        out_props.append((x,height,z,int(tangent_x),int(tangent_z),kind,size))
    return terrain.flatten().tolist(),cover,out_props


def ribbon_overdraw_skip(route: list[dict]) -> list[int]:
    """64-unit source-road raster cells completely covered by main-road strip.

    A draw pass with original 68000 route strips replaces raster quads only
    when an ENTIRE 64x64 patch is conservatively within the smaller strip.
    Preserve native branch roads and all unsupported-mask gaps. The skip
    mask is 8,192 bytes per course stored in ROM, never allocated per frame.
    """
    tile_side=256
    mask=bytearray((tile_side*tile_side)//8)
    for i,a in enumerate(route):
        b=route[(i+1)%len(route)]
        dx=b['x']-a['x'];dz=b['z']-a['z']
        dist2=dx*dx+dz*dz
        if dist2<36:continue
        wa=min(110,max(14,(a['half_width']*3)//4))
        wb=min(110,max(14,(b['half_width']*3)//4))
        # 45 ~= half-diagonal of a 64x64 raster cell. Never hide road
        # near uncertain boundaries or at tight bends.
        inner=min(wa,wb)-48
        if inner<18:continue
        r=inner+48
        x0=max(0,(min(a['x'],b['x'])-r)//64)
        x1=min(255,(max(a['x'],b['x'])+r)//64)
        z0=max(0,(min(a['z'],b['z'])-r)//64)
        z1=min(255,(max(a['z'],b['z'])+r)//64)
        for row in range(z0,z1+1):
            z=row*64+32
            for col in range(x0,x1+1):
                x=col*64+32
                vx=x-a['x'];vz=z-a['z']
                along=(vx*dx+vz*dz)/dist2
                if not 0.05<=along<=0.95:continue
                edge_x=vx-along*dx;edge_z=vz-along*dz
                if edge_x*edge_x+edge_z*edge_z>inner*inner:continue
                index=row*tile_side+col
                mask[index>>3] |= 1<<(index&7)
    return list(mask)
