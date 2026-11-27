"""Ordered road coordinates from original Virtua Racing Deluxe 32X cartridge.

The original course-specific scene table at 68000 $898A04 has five 20-byte
per-course entries. Its first longword names a source ROM coordinate stream
consisting of signed big-endian 16-bit X/Z pairs. Each course's first route
forms a continuous closed loop, then the stream branches into additional
route data. Both the first loop and the branch separation are validated
against the exact user-owned 68000 source ROM; no route data is embedded.

IMPORTANT: This is original source-ordered *road geometry*. It does not prove
that vertex zero is Sega's official start/finish, nor the original checkpoint,
pit-lane eligibility or AI progress. The native route is used for safer,
more authentic Free Drive staging, NOT competitive laps.
"""
from __future__ import annotations
from dataclasses import dataclass
from math import hypot, atan2, pi

try:
    from tools.inspect_vrd_32x_tables import be32, translate_pointer, ROM_SHA256
    from tools.vrd32x_course_identity import verify_course_identity, COURSE_NAMES
    from tools.vrd32x_route_layers import source_route_layers
except ModuleNotFoundError:
    from inspect_vrd_32x_tables import be32, translate_pointer, ROM_SHA256
    from vrd32x_course_identity import verify_course_identity, COURSE_NAMES
    from vrd32x_route_layers import source_route_layers

COURSE_SCENE_TABLE = 0x18A04  # 68000 $898A04, five 20-byte descriptors
COURSE_SCENE_ENTRY_BYTES = 20
NATIVE_CLOSE_DISTANCE = 512
# All 5 original main loops have fewer than 240 original source coordinates.
MAX_NATIVE_ROUTE_POINTS = 254  # GBA track segment id is u8
MAX_NATIVE_STEP = 1024
SECONDARY_GAP = 1200  # Branch/control data begins beyond the closed loop
GAME_WORLD = 16384
HEIGHT_DIV = 32  # Existing GBA *calibration*, NOT original source scale


def signed16(data: bytes, at: int) -> int:
    if at < 0 or at+2 > len(data):
        raise ValueError('truncated original source route coordinate')
    return int.from_bytes(data[at:at+2], 'big', signed=True)


def native_to_gba(c: int) -> int:
    if not -16384 <= c < 16384:
        raise ValueError('original 32X road point exceeds source world')
    return (c + 16384) // 2


def ordered_source_points(data: bytes, slot: int, *, verified: bool = True):
    """Return original ordered main loop and verified layout evidence.

    The return-to-start boundary must be followed by a distinct gap to
    the source's *additional* road/branch/control point arrays. A repeated
    position early inside a circuit never truncates a loop.
    """
    if slot not in range(5):
        raise ValueError('only five original selectable course indices')
    if verified:
        verify_course_identity(data)
    start = COURSE_SCENE_TABLE + slot * COURSE_SCENE_ENTRY_BYTES
    ptr = be32(data,start)
    base = translate_pointer(ptr,len(data))
    if base + (MAX_NATIVE_ROUTE_POINTS+2)*4 > len(data):
        raise ValueError('truncated original 32X route table')
    points = [(signed16(data,base+4*i),signed16(data,base+4*i+2))
              for i in range(MAX_NATIVE_ROUTE_POINTS+2)]
    first = points[0]
    end = None
    for i in range(40,MAX_NATIVE_ROUTE_POINTS):
        x,z = points[i]
        dx = hypot(x-first[0], z-first[1])
        if dx <= NATIVE_CLOSE_DISTANCE:
            following = hypot(points[i+1][0]-x,points[i+1][1]-z)
            if following > SECONDARY_GAP:
                end = i+1
                break
    if end is None:
        raise ValueError('no verified original 32X closed source road loop')
    main = points[:end]
    for a,b in zip(main,main[1:]):
        if hypot(a[0]-b[0],a[1]-b[1]) > MAX_NATIVE_STEP:
            raise ValueError('discontinuity inside original route')
    for x,z in main:
        native_to_gba(x)
        native_to_gba(z)
    approx_length = sum(hypot(a[0]-b[0],a[1]-b[1])
                        for a,b in zip(main,main[1:]))
    closing = hypot(main[-1][0]-first[0],main[-1][1]-first[1])
    return main, {
        'source_course_slot':slot,
        'original_course_name':COURSE_NAMES[slot],
        'source_scene_descriptor_rom_offset':start,
        'source_main_coordinate_stream_pointer':ptr,
        'ordered_original_68000_source_points':end,
        'native_raw_source_arc_length_estimate':round(approx_length+closing),
        'native_source_closing_gap':round(closing,2),
        'next_source_record_separated_from_main_loop':True,
        'additional_source_route_records_present':True,
        'native_road_order_verified':True,
        'original_start_finish_verified':False,
        'original_race_checkpoint_rules_verified':False,
    }


def original_source_route(rom: bytes, slot: int, normal, alternate, min_raw: int,
                          dist_normal, dist_alt, *, pitch: int, max_points=254):
    """Map an authentic ordered source route into existing GBA TrackPt data.

    Returns vertices, GBA loop length and an evidence report. Unlike the
    old skeleton route, EACH position is the signed coordinate in the
    Sega cartridge. The previous main geometry is not guessed or filled.
    """
    raw_points, audit = ordered_source_points(rom,slot,verified=False)
    if len(raw_points)>max_points:
        raise ValueError('original road exceeds GBA u8 route node budget')
    if pitch not in (16,32):
        raise ValueError('unverified GBA native-source coordinate scale')
    layers=source_route_layers(raw_points,normal,alternate)
    src=[]
    alt_count=0
    for i,(nx,nz) in enumerate(raw_points):
        base=normal.sample_world(nx,nz)
        alt=alternate.sample_world(nx,nz) if alternate is not None else None
        if base is None and alt is None:
            raise ValueError(f'original source road vertex {i} is not on source BSP')
        # Resolve overlaps by continuity across the closed source route.
        # An upper surface existing above a lower road does not establish
        # that the racing line actually uses the upper surface there.
        layer=layers[i]
        hit=alt if layer else base
        alt_count += layer
        gx,gz=native_to_gba(nx),native_to_gba(nz)
        src.append((gx,gz,hit.height,layer,nx,nz))
    # Prove that every *interpolated quarter-section* also falls within
    # original 68000 drivable BSP coverage. That catches a source vertex
    # set whose chord shortcuts across grass, voids or lower road gaps.
    # Highland may transition between planes at a native source ramp, so
    # either layer can support a midpoint when their heights agree there.
    checked=0
    for i,(x,z,raw,layer,nx,nz) in enumerate(src):
        next_native=src[(i+1)%len(src)]
        tx,tz=next_native[4],next_native[5]
        for step in (1,2,3):
            qx=round(nx+(tx-nx)*step/4)
            qz=round(nz+(tz-nz)*step/4)
            if normal.sample_world(qx,qz) is None and (
                alternate is None or alternate.sample_world(qx,qz) is None
            ):
                raise ValueError(f'original course {slot} segment {i} crosses unsupported source road')
            checked+=1
    out=[];total=0
    for i,(x,z,raw,layer,_,_) in enumerate(src):
        bx,bz,*_=src[(i+1)%len(src)]
        dx,dz=bx-x,bz-z
        length=max(1,round(hypot(dx,dz)))
        if length>MAX_NATIVE_STEP//2:
            raise ValueError('source road track segment crosses unsupported area')
        heading=round(atan2(dx,dz)/(2*pi)*65536)&65535
        ux=round(dx/length*16384)
        uz=round(dz/length*16384)
        cx,cz=src[(i+1)%len(src)][:2]
        nx2,nz2=src[(i+2)%len(src)][:2]
        a=atan2(dx,dz); b=atan2(nx2-cx,nz2-cz)
        turn=abs(a-b);turn=min(turn,2*pi-turn)
        radius=max(80,min(8000,int(length/turn))) if turn>.01 else 8000
        distance_map=dist_alt if layer else dist_normal
        cell_z=min(distance_map.shape[0]-1,z//pitch)
        cell_x=min(distance_map.shape[1]-1,x//pitch)
        width=max(24,min(170,int(distance_map[cell_z,cell_x]*pitch*.8)))
        height=max(8,min(800,int((raw-min_raw)/HEIGHT_DIV)+12))
        out.append(dict(x=x,z=z,y=height,ux=ux,uz=uz,len=length,
                        dist=total,heading=heading,radius=radius,
                        half_width=width,native_source_layer=layer,
                        flags=0,verge=12))
        total+=length
    if not 8000 < total <=65535:
        raise ValueError('invalid main source road length in GBA world')
    audit.update(
        source_racing_path_kind='ORIGINAL_68000_ORDERED_MAIN_ROAD_GEOMETRY',
        original_loop_length_gba=total,
        original_alternate_bridge_centerline_points=alt_count,
        GBA_u8_route_point_budget_satisfied=True,
        original_source_chord_quarter_samples_verified=checked,
        original_native_sh2_scenery_recovered=False,
        race_start_finish_verified=False)
    return out,total,audit
