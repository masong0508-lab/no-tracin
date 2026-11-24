"""Native Sega 32X alternate overpass route, preserving both road heights.

Original 68000 mode 3 has an alternate source BSP surface spanning the
missing Highland bridge. The lower road and upper bridge overlap in X/Z but
not height. Do NOT collapse this to a planar junction. Routes here are
native road-derived staging loops, not verified Sega race start/finish data.
"""
import math
import networkx as nx
import numpy as np
from skimage.morphology import skeletonize


def layered_main_cycle(base_mask,bridge_mask,base_heights,bridge_heights,
                       *,world_per_pixel,max_points=230):
    """Return a closed [(source_layer,row,col),...] road loop and audit."""
    if base_mask.shape!=bridge_mask.shape or base_mask.ndim!=2:
        raise ValueError('source normal/alternate masks must match')
    side=len(base_mask)
    if side not in (512,1024) or world_per_pixel!=16384//side:
        raise ValueError('unsupported source world mask density')
    if len(base_heights)!=side*side or len(bridge_heights)!=side*side:
        raise ValueError('source 68000 height data does not match road mask')
    if not 30<=max_points<256:
        raise ValueError('invalid GBA route node budget')
    g=nx.Graph()
    for layer,mask in enumerate((base_mask,bridge_mask)):
        coords=set(map(tuple,np.argwhere(skeletonize(mask))))
        g.add_nodes_from((layer,y,x) for y,x in coords)
        for y,x in coords:
            for dy,dx in ((1,0),(0,1),(1,1),(1,-1)):
                if (y+dy,x+dx) in coords:
                    g.add_edge((layer,y,x),(layer,y+dy,x+dx),
                               weight=math.hypot(dy,dx))
    main_ends=[n for n,d in g.degree() if n[0]==0 and d==1]
    bridge_ends=[n for n,d in g.degree() if n[0]==1 and d==1]
    if len(main_ends)<2 or len(bridge_ends)<2:
        raise ValueError('source bridge does not have authentic ramp tips')
    bridge_start=min(bridge_ends,key=lambda n:(n[2],n[1]))
    bridge_end=max(bridge_ends,key=lambda n:(n[2],n[1]))
    max_join=24 if side==1024 else 12
    transitions=[]
    for end in (bridge_start,bridge_end):
        _,y,x=end
        i=y*side+x
        lo,hi=base_heights[i],bridge_heights[i]
        if lo is None or hi is None or abs(lo-hi)>300:
            raise ValueError('native overpass entry cannot match normal source height')
        choices=sorted((math.hypot(n[1]-y,n[2]-x),n) for n in main_ends)
        if not choices or choices[0][0]>max_join:
            raise ValueError('no source-supported normal-road bridge ramp')
        dist,n=choices[0]
        # EVERY intermediate ramp cell must have valid original 68000
        # collision and must not cross a different-altitude underpass.
        steps=max(abs(n[1]-y),abs(n[2]-x))
        for step in range(steps+1):
            qy=round(y+(n[1]-y)*step/max(1,steps))
            qx=round(x+(n[2]-x)*step/max(1,steps))
            k=qy*side+qx
            a,b=base_heights[k],bridge_heights[k]
            if a is None and b is None:
                raise ValueError('bridge ramp crosses unsupported road space')
            if a is not None and b is not None and abs(a-b)>300:
                raise ValueError('bridge ramp crosses two-level underpass')
        g.add_edge(end,n,weight=dist)
        main_ends.remove(n)
        transitions.append(dict(bridge=(int(y),int(x)),
                                main=(int(n[1]),int(n[2])),
                                separation_pixels=round(dist,2),
                                native_height_difference=lo-hi))
    viable=[cycle for cycle in nx.cycle_basis(g)
            if sum(n[0]==1 for n in cycle)>=10
            and sum(n[0]==0 for n in cycle)>=30]
    if not viable:raise ValueError('no original normal+alternate source course loop')
    def length(cycle):
        return sum(float(g[cycle[i]][cycle[(i+1)%len(cycle)]]['weight'])
                   for i in range(len(cycle)))
    best=max(viable,key=length)
    native_length=length(best)*world_per_pixel
    if not 8000<native_length<65535:
        raise ValueError('native 68000 layered route outside GBA format')
    y0=(bridge_start[1]+bridge_end[1])//2
    x0=(bridge_start[2]+bridge_end[2])//2
    # Provisional free-drive start, explicitly NOT Sega source start grid.
    start=max((i for i,n in enumerate(best) if n[0]==0),
              key=lambda i:(best[i][1]-y0)**2+(best[i][2]-x0)**2)
    ordered=best[start:]+best[:start]
    sampled=ordered[::max(1,math.ceil(len(ordered)/max_points))]
    if len(sampled)<30 or not any(n[0]==1 for n in sampled):
        raise ValueError('GBA route simplification erased native bridge')
    for layer,y,x in sampled:
        if not (base_mask if layer==0 else bridge_mask)[y,x]:
            raise ValueError('native source route left original surface')
    return sampled,dict(
        route_kind='NATIVE_68000_NORMAL_AND_ALTERNATE_3D_SOURCE_LOOP',
        source_game_mode=3,source_alternate_mode=3,
        original_pixel_path_length=round(length(best),2),
        gba_world_route_length_estimate=round(native_length),
        sampled_route_points=len(sampled),
        source_bridge_points=sum(n[0]==1 for n in sampled),
        bridge_transitions=transitions,
        original_start_finish_verified=False,
        distinct_source_overpass_and_underpass=True)
