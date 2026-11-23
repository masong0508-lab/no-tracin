// Driveable 32X road-surface studies derived from verified US cartridge BSP
// samples. The optional private build embeds only sampled data in ROM, not
// an emulated 32X CPU or original game's polygon assets. No EWRAM allocated.
#include "track.h"
#include "world.h"
#include "vrd32x_study.h"
typedef struct { s16 xyz[9]; } VrdSourceTri;
// Generated ROM-only props are anchored to authenticated source road
// coordinates. They are adapted GBA scenery, not original Sega SH-2 meshes.
typedef struct { s16 x,y,z,ux,uz; u8 kind,size; } VrdScenery;
#include "vrd32x_study_data.h"  // privately generated, ignored by Git

s32 vrd32x_study_slot(void)
{
    if (!g_track || g_track < g_tracks + TRACK_VRD_FIRST ||
        g_track >= g_tracks + TRACK_VRD_FIRST + TRACK_VRD_COUNT) return -1;
    return g_track - g_tracks - TRACK_VRD_FIRST;
}

// The source-generated ROM pack chooses 8 or 16 samples per original
// 512-unit BSP cell. The optional 16x density improves edge fidelity while
// retaining exactly the same bounded runtime lookup and no EWRAM allocation.
// Legacy hand-built synthetic fixtures default to the previous 8x layout.
#ifndef VRD_SAMPLE_SHIFT
#define VRD_SAMPLE_SHIFT 5
#define VRD_TILE_SHIFT 3
#define VRD_TILE_AREA 64
#endif
#define VRD_SAMPLE_PITCH (1 << VRD_SAMPLE_SHIFT)
#define VRD_TILE_SIZE (1 << VRD_TILE_SHIFT)
#define VRD_TILE_MASK (VRD_TILE_SIZE - 1)
static s32 native_reference_height;
void vrd32x_study_set_height_reference(s32 world_height_q8)
{
    native_reference_height=world_height_q8;
}

static s32 normal_sample(s32 slot,s32 x,s32 z)
{
    if ((u32)slot >= TRACK_VRD_COUNT || (u32)x >= 16384 || (u32)z >= 16384)
        return -1;
    u32 sx=(u32)x>>VRD_SAMPLE_SHIFT, sz=(u32)z>>VRD_SAMPLE_SHIFT;
    u32 tile=(sz>>VRD_TILE_SHIFT)*64+(sx>>VRD_TILE_SHIFT);
    u32 id=vrd_study_lookups[slot][tile];
    if (!id) return -1;
    u16 height=vrd_study_heights[slot][(id-1)*VRD_TILE_AREA+
              (sz&VRD_TILE_MASK)*VRD_TILE_SIZE+(sx&VRD_TILE_MASK)];
    return height==65535 ? -1 : height;
}

// The supported US cartridge has a SECOND, native 68000 BSP road mode
// at normal mode 3: the elevated Highland bridge. Its geometry overlaps
// the lower circuit at identical (x,z), but the two source height planes
// differ significantly. Both are preserved in the PRIVATE generated ROM.
#ifdef VRD_HAS_NATIVE_OVERPASS
static s32 overpass_sample(s32 x,s32 z)
{
    if((u32)x>=16384 || (u32)z>=16384) return -1;
    u32 sx=(u32)x>>VRD_SAMPLE_SHIFT,sz=(u32)z>>VRD_SAMPLE_SHIFT;
    u32 tile=(sz>>VRD_TILE_SHIFT)*64+(sx>>VRD_TILE_SHIFT);
    u32 id=vrd_native_overpass_lookup[tile];
    if(!id)return -1;
    u16 h=vrd_native_overpass_heights[(id-1)*VRD_TILE_AREA+
        (sz&VRD_TILE_MASK)*VRD_TILE_SIZE+(sx&VRD_TILE_MASK)];
    return h==65535?-1:h;
}
#endif

static s32 sample(s32 slot,s32 x,s32 z)
{
    s32 lower=normal_sample(slot,x,z);
#ifdef VRD_HAS_NATIVE_OVERPASS
    if(slot==3){
        s32 upper=overpass_sample(x,z);
        if(upper<0) return lower;
        if(lower<0) return upper;
        // Native-source layer choice is based on true previous car height,
        // not arbitrary 2D route projection. Crossings cannot magically
        // raise an underpass driver to the overpass (or vice versa).
        s32 du=upper*256-native_reference_height;
        s32 dl=lower*256-native_reference_height;
        if(du<0)du=-du;
        if(dl<0)dl=-dl;
        return du<dl?upper:lower;
    }
#endif
    return lower;
}

// Taking the nearest sample for each wheel can create stair steps on
// long inclines (16 units in high-detail builds; 32 in legacy builds). Blend
// height *within a fully supported four-sample patch*. If any required
// sample belongs to unsupported/off-road space, use the last known road
// sample rather than inventing a bridge across that gap. Q8 output, no heap
// and no extra tables. Exact sample vertices retain their raw height.
static s32 smooth_height(s32 slot,s32 x,s32 z)
{
    s32 a=sample(slot,x,z);
    if(a<0) return -1;
    s32 fx=x&(VRD_SAMPLE_PITCH-1),fz=z&(VRD_SAMPLE_PITCH-1);
    if(!fx && !fz) return a*256;
    s32 b=a,c=a,d=a;
    if(fx) {
        b=sample(slot,x+VRD_SAMPLE_PITCH,z);
        if(b<0) return a*256;
    }
    if(fz) {
        c=sample(slot,x,z+VRD_SAMPLE_PITCH);
        if(c<0) return a*256;
    }
    if(fx && fz) {
        d=sample(slot,x+VRD_SAMPLE_PITCH,z+VRD_SAMPLE_PITCH);
        if(d<0) return a*256;
    }else if(fx) d=b;
    else d=c;
    // Raw samples are 8..800. All intermediates fit signed 32-bit.
    s32 h0=a*VRD_SAMPLE_PITCH+(b-a)*fx;
    s32 h1=c*VRD_SAMPLE_PITCH+(d-c)*fx;
    // Q8 exactly. PITCH is 16 or 32, so denominator is 1 or 4.
    return (h0*(VRD_SAMPLE_PITCH-fz)+h1*fz) /
           ((VRD_SAMPLE_PITCH*VRD_SAMPLE_PITCH)/256);
}

s32 vrd32x_study_height(s32 slot,s32 x,s32 z)
{
    s32 h=smooth_height(slot,x,z);
    return h<0 ? 0 : h;
}

s32 vrd32x_study_surface(s32 slot,s32 x,s32 z)
{
    return sample(slot,x,z) >= 0 ? SURF_ROAD : SURF_GRASS;
}

s32 vrd32x_study_ground_plane(s32 slot,s32 x,s32 z,s32 r,s32 *h,s32 *gx,s32 *gz)
{
    // A normal plane is valid only if the car footprint stays on the
    // authenticated road. At cliff edges, fall back to point contacts.
    *h=*gx=*gz=0;
    if(r<0 || sample(slot,x,z)<0 || sample(slot,x-r,z)<0 ||
       sample(slot,x+r,z)<0 || sample(slot,x,z-r)<0 ||
       sample(slot,x,z+r)<0) return 0;
    // Central differences of the *same smoothed surface* used by the car.
    // Using the raw-nearest value for the center but a different plane for
    // its wheels caused pops and unintended velocity impulses on slopes.
    s32 x0=smooth_height(slot,x-32,z),x1=smooth_height(slot,x+32,z);
    s32 z0=smooth_height(slot,x,z-32),z1=smooth_height(slot,x,z+32);
    if(x0<0||x1<0||z0<0||z1<0) return 0;
    *h=smooth_height(slot,x,z);
    *gx=(x1-x0)/64; // Q8 rise per world unit, same convention as track.c
    *gz=(z1-z0)/64;
    return 1;
}

static void ground_quad(s32 x,s32 z,s32 step,s32 y0,s32 y1,s32 y2,s32 y3,u8 color)
{
    Vec3 v[4]={{x,y0,z},{x,y1,z+step},{x+step,y2,z+step},{x+step,y3,z}};
    r_ground(v,4,color);
}

#ifdef VRD_NATIVE_SCENE
// Flat checkerboard planes had hidden the real source road. Ground vertices
// now come from source-collision-height-anchored landscape data, not 0.
// Only the off-road extrapolation and vegetation are artistic approximations.
static s32 terrain_vertex(s32 slot,s32 col,s32 row)
{
    if((u32)col>=65 || (u32)row>=65)return 0;
    return vrd_native_terrain_height[slot][row*65+col];
}

static u8 terrain_color(s32 slot,s32 col,s32 row)
{
    if((u32)col>=64 || (u32)row>=64)return COLOR(M_GRASS,1);
    u8 kind=vrd_native_terrain_cover[slot][row*64+col];
    if(kind==3)return COLOR(M_WATER,0);
    if(kind==2)return COLOR(M_BLD4,1);
    if(kind==5)return COLOR(M_BLD5,1);
    if(kind==1)return COLOR(M_SIDEWALK,2);
    return kind==4 ? COLOR(M_GRASS,2) : COLOR(M_GRASS,0);
}

// Two crossed conifer silhouettes use a handful of faces and no asset RAM.
// The shapes are original compact GBA representations, not original meshes.
static void draw_native_scenery(const VrdScenery *p,s32 depth)
{
    s32 x=p->x,y=p->y,z=p->z;
    s32 size=p->size;
    if(p->kind==0 || p->kind==1){
        s32 h=size*2,wide=size/2;
        Vec3 a[3]={{x,y+h,z},{x-wide,y+15,z},{x+wide,y+15,z}};
        Vec3 b[3]={{x,y+h,z},{x,y+15,z-wide},{x,y+15,z+wide}};
        r_face(a,3,COLOR(M_TREE,p->kind?2:0),RF_TWO_SIDED);
        r_face(b,3,COLOR(M_TREE,p->kind?1:2),RF_TWO_SIDED);
        if(depth<330)r_box(x-4,y,z-4,x+4,y+32,z+4,M_BLD1);
    }else if(p->kind==2){  // compact source-course-inspired dock structure
        r_box(x-size,y,z-size/2,x+size,y+35+size/2,z+size/2,M_BLD2);
        if(depth<550)r_box(x-size/2,y+35+size/2,z-size/2,
                           x+size/2,y+40+size/2,z+size/2,M_SIDEWALK);
    }else if(p->kind==3){  // harbour crane, single silhouetted gantry
        r_box(x-6,y,z-6,x+6,y+size*3,z+6,M_BLD4);
        r_box(x-8,y+size*3-10,z-size,x+8,y+size*3+4,z+size/2,M_BLD4);
    }else if(p->kind==4){  // granite/ochre rock outcrop
        s32 w=size/2;
        Vec3 a[3]={{x,y+size*2,z},{x-w,y,z-w},{x+w,y,z-w}};
        Vec3 b[3]={{x,y+size*2,z},{x+w,y,z-w},{x+w,y,z+w}};
        Vec3 c[3]={{x,y+size*2,z},{x+w,y,z+w},{x-w,y,z+w}};
        r_face(a,3,COLOR(M_BLD5,0),RF_TWO_SIDED);
        r_face(b,3,COLOR(M_BLD5,1),RF_TWO_SIDED);
        r_face(c,3,COLOR(M_BLD4,2),RF_TWO_SIDED);
    }else if(p->kind==5){ // weathered stone remnants
        s32 w=size/2;
        r_box(x-w,y,z-8,x-w+12,y+size,z+8,M_BLD3);
        r_box(x+w-12,y,z-8,x+w,y+size,z+8,M_BLD3);
        r_box(x-w-9,y+size-3,z-11,x+w+9,y+size+12,z+11,M_BLD4);
    }else if(p->kind==6){ // small fairground observation wheel
        s32 h=size*2,rad=size*3/4;
        r_box(x-6,y,z-6,x+6,y+h,z+6,M_BLD4);
        // Eight simple ring spokes in the X/Y plane, no framebuffer sprites.
        for(s32 i=0;i<8;i++){
            s32 a=i*128,b=(i+1)*128;
            s32 ax=(icos(a)*rad)>>14,ay=(isin(a)*rad)>>14;
            s32 bx=(icos(b)*rad)>>14,by=(isin(b)*rad)>>14;
            Vec3 tri[3]={{x,y+h,z},{x+ax,y+h+ay,z},
                         {x+bx,y+h+by,z}};
            r_face(tri,3,COLOR(M_STUNT_RED,i&1),RF_TWO_SIDED);
        }
    }else if(p->kind==7){ // monumental cliff-side temple outline
        s32 w=size,h=size*3/2;
        r_box(x-w,y,z-32,x-w+18,y+h,z-12,M_BLD3);
        r_box(x+w-18,y,z-32,x+w,y+h,z-12,M_BLD3);
        r_box(x-w-16,y+h-8,z-45,x+w+16,y+h+20,z+8,M_BLD4);
    }else if(p->kind==8){ // sandstone escarpment from large triangular faces
        s32 w=size/2;
        Vec3 a[3]={{x,y+size*3,z},{x-w,y,z-w},{x+w,y,z-w}};
        Vec3 b[3]={{x,y+size*3,z},{x+w,y,z-w},{x+w,y,z+w}};
        Vec3 c[3]={{x,y+size*3,z},{x+w,y,z+w},{x-w,y,z+w}};
        r_face(a,3,COLOR(M_BLD4,0),RF_TWO_SIDED);
        r_face(b,3,COLOR(M_BLD5,1),RF_TWO_SIDED);
        r_face(c,3,COLOR(M_BLD4,2),RF_TWO_SIDED);
    }else if(p->kind==9){ // source-tangent-aligned crossing/bridge silhouette
        // Decorations are non-collidable until exact SH-2 bridge assets can
        // replace these GBA stand-ins. The route position is authentic.
        s32 w=size;
        s32 offx=-(p->uz*w)>>14,offz=(p->ux*w)>>14;
        s32 lx=x+offx,lz=z+offz,rx=x-offx,rz=z-offz;
        s32 h=y+size*2;
        r_box(lx-10,y,lz-10,lx+10,h,lz+10,M_BLD4);
        r_box(rx-10,y,rz-10,rx+10,h,rz+10,M_BLD4);
        Vec3 beam[4]={{lx,h-18,lz},{rx,h-18,rz},
                      {rx,h+8,rz},{lx,h+8,lz}};
        r_face(beam,4,COLOR(M_BLD4,0),RF_TWO_SIDED);
    }
}

// Original ordered signed-X/Z road points (not a procedural skeleton). The
// ribbons are conservative interiors of the source road and use original
// source heights; outlying asphalt still uses the 68000 collision raster.
static void draw_native_source_road(s32 slot)
{
    const TrackDef *t=g_track;
    if(!t || !t->half_widths)return;
    s32 drawn=0,edges=0;
    for(s32 i=0;i<t->count && drawn<24;i++){
        const TrackPt *a=&t->pts[i],*b=&t->pts[i+1==t->count?0:i+1];
        s32 mx=(a->x+b->x)/2,mz=(a->z+b->z)/2;
        if(!r_visible(mx,mz,180+a->len/2))continue;
        s32 wa=(t->half_widths[i]*3)/4;
        s32 wb=(t->half_widths[i+1==t->count?0:i+1]*3)/4;
        if(wa>110)wa=110;
        if(wb>110)wb=110;
        if(wa<14)wa=14;
        if(wb<14)wb=14;
        Vec3 v[4]={
            {a->x-((a->uz*wa)>>14),a->y+3,a->z+((a->ux*wa)>>14)},
            {b->x-((b->uz*wb)>>14),b->y+3,b->z+((b->ux*wb)>>14)},
            {b->x+((b->uz*wb)>>14),b->y+3,b->z-((b->ux*wb)>>14)},
            {a->x+((a->uz*wa)>>14),a->y+3,a->z-((a->ux*wa)>>14)}
        };
        s32 delta=a->y-b->y;
        if(delta<0)delta=-delta;
        r_ground(v,4,COLOR(M_ASPHALT,delta>36?1:0));
        drawn++;
        // Quiet lane-edge marks instead of noisy checkerboard road paint.
        // Existing map has no verified original finish line or lane count.
        if(edges<5 && r_visible(mx,mz,110) && a->len<350){
            Vec3 line[4]={{v[0].x,v[0].y+2,v[0].z},
                          {v[1].x,v[1].y+2,v[1].z},
                          {v[1].x+((b->uz*4)>>14),v[1].y+2,v[1].z-((b->ux*4)>>14)},
                          {v[0].x+((a->uz*4)>>14),v[0].y+2,v[0].z-((a->ux*4)>>14)}};
            r_ground(line,4,COLOR(M_STUNT_WHITE,1));
            edges++;
        }
    }
    (void)slot;
}
#endif

void vrd32x_study_draw(s32 slot,s32 focus_x,s32 focus_z,s32 frame)
{
    (void)frame;
#ifdef VRD_NATIVE_SCENE
    // Terrain is a continuous height mesh keyed to the original 68000
    // road height data; no chessboard tiles or flat zero-height fields.
    // 512-unit world-space LOD reaches over a kilometer to either side
    // without extra terrain faces; 256-unit checker tiles ended halfway to
    // the horizon and were plainly visible as flat rectangles.
    s32 col0=focus_x/512-2,row0=focus_z/512-2;
    for(s32 rz=0;rz<5;rz++)for(s32 rx=0;rx<5;rx++){
        s32 col=col0+rx,row=row0+rz;
        if((u32)col>=32 || (u32)row>=32)continue;
        s32 x=col*512,z=row*512;
        if(!r_visible(x+256,z+256,350))continue;
        s32 gx=col*2,gz=row*2;
        ground_quad(x,z,512,
          terrain_vertex(slot,gx,gz),
          terrain_vertex(slot,gx,gz+2),
          terrain_vertex(slot,gx+2,gz+2),
          terrain_vertex(slot,gx+2,gz),terrain_color(slot,gx,gz));
    }
#else
    // Compatibility for synthetic fixtures and older, donor-less maps.
    s32 bx=(focus_x/256)*256-512,bz=(focus_z/256)*256-512;
    for(s32 iz=0;iz<5;iz++)for(s32 ix=0;ix<5;ix++){
        s32 x=bx+ix*256,z=bz+iz*256;
        if(!r_visible(x+128,z+128,190))continue;
        ground_quad(x,z,256,0,0,0,0,COLOR(M_GRASS,1));
    }
#endif
    s32 exact_faces=0;
#ifdef VRD_EXACT_SOURCE_MESH
    // Native 68000 BSP surface triangles, clipped to source boundaries and
    // partitioned by first-height-plane priority during private import.
    // Detail is confined to the near 2x2 source-tile neighborhood. We
    // reserve the rest of the 180-face queue for distant coarse road LOD.
    s32 tcx=focus_x>>8,tcz=focus_z>>8;
    for(s32 tz=tcz-1;tz<=tcz+1 && exact_faces<100;tz++)
        for(s32 tx=tcx-1;tx<=tcx+1 && exact_faces<100;tx++){
            if((u32)tx>=64 || (u32)tz>=64)continue;
            s32 tile=tz*64+tx;
            if(!r_visible(tx*256+128,tz*256+128,200))continue;
            const VrdSourceTri *tris=vrd_exact_tris[slot];
            s32 start=vrd_exact_first[slot][tile], count=vrd_exact_count[slot][tile];
            for(s32 k=0;k<count && exact_faces<100;k++){
                const VrdSourceTri *p=&tris[start+k];
                s32 mx=(p->xyz[0]+p->xyz[3]+p->xyz[6])/3;
                s32 mz=(p->xyz[2]+p->xyz[5]+p->xyz[8])/3;
                if(!r_visible(mx,mz,180))continue;
                Vec3 v[3]={{p->xyz[0],p->xyz[1],p->xyz[2]},
                           {p->xyz[3],p->xyz[4],p->xyz[5]},
                           {p->xyz[6],p->xyz[7],p->xyz[8]}};
                r_ground(v,3,COLOR(M_ASPHALT,((tx+tz)&1)));
                exact_faces++;
            }
        }
#endif
#ifdef VRD_HAS_NATIVE_OVERPASS
    // In the lower Highland tunnel, keep the actual bridge visually above
    // the road. Even at a crossing the high source plane is never replaced
    // by the false 2D merge. Reserve only 24 of the same 180 road faces.
    if(slot==3 &&
       focus_x+512>=vrd_native_bridge_bounds[0] &&
       focus_x-512<=vrd_native_bridge_bounds[1] &&
       focus_z+512>=vrd_native_bridge_bounds[2] &&
       focus_z-512<=vrd_native_bridge_bounds[3]){
        // Keep the bridge budget independent of optional near-field BSP
        // triangles: the previous shared counter accidentally suppressed
        // every overpass face once that detail renderer used >24 faces.
        s32 bridge_faces=0;
        s32 start_x=(focus_x/64)*64-256,start_z=(focus_z/64)*64-256;
        for(s32 iz=0;iz<8 && bridge_faces<24 && exact_faces<180;iz++)
            for(s32 ix=0;ix<8 && bridge_faces<24 && exact_faces<180;ix++){
                s32 x=start_x+ix*64,z=start_z+iz*64;
                s32 top[4]={overpass_sample(x,z),
                            overpass_sample(x,z+64),
                            overpass_sample(x+64,z+64),
                            overpass_sample(x+64,z)};
                if(top[0]<0 || top[1]<0 || top[2]<0 || top[3]<0)
                    continue;
                s32 bottom=normal_sample(slot,x+32,z+32);
                if(bottom<0)continue; // bridge-only road rendered normally
                s32 up=(top[0]+top[1]+top[2]+top[3])/4;
                if(up<=bottom+12 ||
                   native_reference_height>(bottom+up)*128)
                    continue; // no separate upper pass needed from bridge
                if(!r_visible(x+32,z+32,68))continue;
                ground_quad(x,z,64,top[0],top[1],top[2],top[3],
                            COLOR(M_ASPHALT,(ix+iz)&1));
                bridge_faces++;
                exact_faces++;
            }
    }
#endif
    // Render farther source sample cells at a coarse 64-unit graphics LOD.

    // Max 19x19 candidates, <=180 surface faces, bounded render queue.
    s32 base_x=(focus_x/64)*64-576,base_z=(focus_z/64)*64-576;
    s32 faces=exact_faces;
    for(s32 j=0;j<18 && faces<(slot==2||slot==3?110:140);j++)
        for(s32 i=0;i<18 && faces<(slot==2||slot==3?110:140);i++){
        s32 x=base_x+i*64,z=base_z+j*64;
#ifdef VRD_NATIVE_SCENE
        // Original ordered native-road ribbons already cover these cells.
        // Skip duplicate fill entirely, but do NOT erase pit/branch asphalt.
        if((u32)x<16384 && (u32)z<16384){
            u32 rc=(u32)(z>>6)*256+(u32)(x>>6);
            // Offline ROM-only gate mirrors the exact 3-of-4 native
            // sample rule below. Empty scenery-space candidates never
            // enter the per-frame 68000 road height decoder.
            u8 bit=(u8)(1u<<(rc&7));
            if(!(vrd_native_raster_candidates[slot][rc>>3] & bit))continue;
            if(vrd_native_ribbon_skip[slot][rc>>3] & bit)continue;
        }
#endif
#ifdef VRD_EXACT_SOURCE_MESH
        // Close-up original BSP triangles replace, not overlay, coarse
        // road patches. Preserve the 180-face source-road display cap.
        if(x>=focus_x-192 && x<focus_x+192 &&
           z>=focus_z-192 && z<focus_z+192)continue;
#endif
        if(!r_visible(x+32,z+32,66))continue;
        s32 a=sample(slot,x,z),b=sample(slot,x,z+64);
        s32 c=sample(slot,x+64,z+64),d=sample(slot,x+64,z);
        s32 valid=(a>=0)+(b>=0)+(c>=0)+(d>=0);
        if(valid<3)continue;
        // Shade steep grades using the four *already fetched* authentic
        // height samples. Flat roads keep a quiet two-tone texture, while
        // uphill/downhill surfaces gain directional light without extra
        // polygons, collision queries or framebuffer allocations.
        s32 shade=((x>>7)+(z>>7))&1;
        if(valid==4) {
            s32 dx=(c+d)-(a+b),dz=(b+c)-(a+d);
            s32 grade=(dx<0?-dx:dx)+(dz<0?-dz:dz);
            if(grade>24) shade=(dx-dz)>=0 ? 0 : 2;
        }
        u8 mat=COLOR(M_ASPHALT,shade);
        if(valid==4){ground_quad(x,z,64,a,b,c,d,mat);faces++;}
        else {
            // Draw triangles along actual mask boundaries, never a quad
            // connecting unsupported offroad samples.
            Vec3 p[4]={{x,a,z},{x,b,z+64},{x+64,c,z+64},{x+64,d,z}};
            Vec3 tri[3];s32 n=0;
            for(s32 k=0;k<4;k++) if(((s32[]){a,b,c,d})[k]>=0)tri[n++]=p[k];
            if(n==3){r_ground(tri,3,mat);faces++;}
        }
    }
#ifdef VRD_NATIVE_SCENE
    draw_native_source_road(slot);
    // Props are submitted after immediate ground/road, but the renderer's
    // existing solid-face depth sort handles their true height and occlusion.
    s32 drawn_props=0;
    const VrdScenery *props=vrd_native_scenery[slot];
    for(s32 i=0;i<vrd_native_scenery_count[slot] && drawn_props<5;i++){
        const VrdScenery *p=&props[i];
        if(!r_visible(p->x,p->z,p->size*2+40))continue;
        s32 side,depth=r_depth(p->x,p->z,&side);
        if(depth>900 || depth<0 || r_faces_free()<48)continue;
        draw_native_scenery(p,depth);
        drawn_props++;
    }
#endif
}

static void map_pixel(volatile u16 *page,s32 x,s32 y,u8 color)
{
    if((u32)x>=240 || (u32)y>=160)return;
    volatile u16 *p=page+(y*240+x)/2;
    *p=(x&1)?((*p&255)|(color<<8)):((*p&0xff00)|color);
}

void vrd32x_study_map(volatile u16 *page,s32 slot,s32 x0,s32 y0,s32 w,s32 h,u8 color)
{
    // The current road import is still a *sampled* 68000 collision surface,
    // not Sega's original SH-2 polygon mesh or race routing. The preview
    // must nevertheless use its actual sample mask instead of filling
    // 64x64 ROM lookup tiles or using a fabricated staging centreline.
    //
    // Source-mask bounds are calculated once during PRIVATE ROM import:
    // five x four u16 constants in the generated ROM header. No runtime
    // framebuffer/heap/new tables, and no dynamic route skeleton graphics.
    if ((u32)slot >= TRACK_VRD_COUNT || w <= 4 || h <= 4) return;
    const u16 *bounds=vrd_study_preview_bounds[slot];
    s32 minx=bounds[0],maxx=bounds[1],minz=bounds[2],maxz=bounds[3];
    s32 dx=maxx-minx,dz=maxz-minz;
    if (dx<=0 || dz<=0) return;
    // Project sampled +Z upward, consistent with source world-space track
    // previews. Keep the true X/Z proportions: no stretched roads/corners.
    s32 sx=dx*256/(w-4),sz=dz*256/(h-4);
    s32 scale=sx>sz?sx:sz;
    if (scale<=0) scale=1;
    s32 ox=x0+(w-dx*256/scale)/2;
    s32 oy=y0+(h+dz*256/scale)/2;

    if (w==102 && h==68) {
        // This screen is animated, but the original sampled road silhouette
        // is static. Use a private-source-derived 867-byte ROM bitmap
        // instead of tens of thousands of 32X tile lookups each video frame.
        // The cached bits are the same source-mask projection as below.
        const u8 *bits=vrd_study_preview_bits[slot];
        for(s32 py=0;py<68;py++)for(s32 px=0;px<102;px++){
            s32 i=py*102+px;
            if(bits[i>>3] & (1u<<(i&7)))
                map_pixel(page,x0+px,y0+py,color);
        }
        return;
    }
    // A pixel represents several ROM sample cells on a small screen.
    // Source hit probes at its centre and four subpixel offsets retain
    // thin verified side roads without inventing a tile-wide road fill.
    // The maximum false-positive extension is one thumbnail pixel.
    s32 probe=scale/1024;
    if (probe<1) probe=1;
    for(s32 py=y0;py<y0+h;py++){
        if((u32)py>=SCREEN_H)continue;
        s32 z=minz+(oy-py)*scale/256;
        for(s32 px=x0;px<x0+w;px++){
            if((u32)px>=SCREEN_W)continue;
            s32 x=minx+(px-ox)*scale/256;
            if(sample(slot,x,z)>=0 ||
               sample(slot,x-probe,z-probe)>=0 ||
               sample(slot,x+probe,z-probe)>=0 ||
               sample(slot,x-probe,z+probe)>=0 ||
               sample(slot,x+probe,z+probe)>=0)
                map_pixel(page,px,py,color);
        }
    }
}

// Render a moving marker on the ORIGINAL source-ordered road only.
// The course thumbnail still displays full original BSP road coverage:
// the marker is not an invented uniform-width racing line and its starting
// location is not claimed as Sega's official race-start/checkpoint.
void vrd32x_study_map_marker(volatile u16 *page,s32 slot,
                             s32 x0,s32 y0,s32 w,s32 h,
                             s32 road_x,s32 road_z,u8 color)
{
    if((u32)slot>=TRACK_VRD_COUNT || w<=4 || h<=4)return;
    const u16 *bounds=vrd_study_preview_bounds[slot];
    s32 minx=bounds[0],maxx=bounds[1],minz=bounds[2],maxz=bounds[3];
    s32 dx=maxx-minx,dz=maxz-minz;
    if(dx<=0||dz<=0)return;
    s32 sx=dx*256/(w-4),sz=dz*256/(h-4);
    s32 scale=sx>sz?sx:sz;
    if(scale<=0)scale=1;
    s32 ox=x0+(w-dx*256/scale)/2;
    s32 oy=y0+(h+dz*256/scale)/2;
    s32 px=ox+(road_x-minx)*256/scale;
    s32 py=oy-(road_z-minz)*256/scale;
    // Keep the marker inside the source-map panel; no border overwrite.
    if(px<x0+2 || px>=x0+w-2 || py<y0+2 || py>=y0+h-2)return;
    for(s32 y=-1;y<=1;y++)for(s32 x=-1;x<=1;x++)
        map_pixel(page,px+x,py+y,color);
}
