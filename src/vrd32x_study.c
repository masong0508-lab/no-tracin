// Driveable 32X road-surface studies derived from verified US cartridge BSP
// samples. The optional private build embeds only sampled data in ROM, not
// an emulated 32X CPU or original game's polygon assets. No EWRAM allocated.
#include "track.h"
#include "world.h"
#include "vrd32x_study.h"
typedef struct { s16 xyz[9]; } VrdSourceTri;
#include "vrd32x_study_data.h"  // privately generated, ignored by Git

s32 vrd32x_study_slot(void)
{
    if (!g_track || g_track < g_tracks + TRACK_VRD_FIRST ||
        g_track >= g_tracks + TRACK_VRD_FIRST + TRACK_VRD_COUNT) return -1;
    return g_track - g_tracks - TRACK_VRD_FIRST;
}

// 512x512 raster = 64x64 verified 32X cells, 8 samples/cell. In GBA
// coordinates each sample represents 32x32 world units. 0 means no tile;
// 0xFFFF means an unsupported/offroad sample, never interpreted as height.
// Generated high-detail tiles have 16 samples per source BSP cell; legacy
// donor and synthetic builds use 8. All large samples remain ROM-only.
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

// The 32X BSP source is sampled every 32 world units; taking the nearest
// sample for each wheel creates 32-unit stair steps on long inclines. Blend
// height *within a fully supported four-sample patch*. If any required
// sample belongs to unsupported/off-road space, use the last known road
// sample rather than inventing a bridge across that gap. Q8 output, no heap
// and no extra tables. Exact 32-unit grid vertices remain byte-identical.
// Original 68000 source mode 3 has a physically distinct alternate
// collision stream for its elevated bridge. Preserve both source
// heights at identical X/Z rather than merging into a 2D surface.
#ifdef VRD_HAS_NATIVE_OVERPASS
static s32 overpass_sample(s32 x,s32 z)
{
    if((u32)x>=16384 || (u32)z>=16384)return -1;
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
        if(upper<0)return lower;
        if(lower<0)return upper;
        // Actual previous car altitude prevents crossing between source
        // bridge and underpass collision levels at their intersection.
        s32 du=upper*256-native_reference_height;
        s32 dl=lower*256-native_reference_height;
        if(du<0)du=-du;
        if(dl<0)dl=-dl;
        return du<dl?upper:lower;
    }
#endif
    return lower;
}

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
    return (h0*(VRD_SAMPLE_PITCH-fz)+h1*fz)/
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

void vrd32x_study_draw(s32 slot,s32 focus_x,s32 focus_z,s32 frame)
{
    (void)frame;
    // Low-detail environmental ground, budgeted independently of the road.
    // Use a small number of 256-unit quads, not a framebuffer or heap mesh.
    s32 bx=(focus_x/256)*256-512,bz=(focus_z/256)*256-512;
    for(s32 iz=0;iz<5;iz++) for(s32 ix=0;ix<5;ix++) {
        s32 x=bx+ix*256,z=bz+iz*256;
        if (!r_visible(x+128,z+128,190)) continue;
        ground_quad(x,z,256,0,0,0,0,COLOR(M_GRASS,(ix+iz)&1));
    }
    // Optional near source BSP triangles are partitioned by original
    // first-height-plane priority during private ROM import. They are 68000
    // collision geometry, NOT Sega's original SH-2 scenery meshes.
    s32 exact_faces=0;
#ifdef VRD_EXACT_SOURCE_MESH
    s32 tcx=focus_x>>8,tcz=focus_z>>8;
    for(s32 tz=tcz-1;tz<=tcz+1 && exact_faces<100;tz++)
        for(s32 tx=tcx-1;tx<=tcx+1 && exact_faces<100;tx++){
            if((u32)tx>=64 || (u32)tz>=64)continue;
            s32 tile=tz*64+tx;
            if(!r_visible(tx*256+128,tz*256+128,200))continue;
            const VrdSourceTri *tris=vrd_exact_tris[slot];
            s32 start=vrd_exact_first[slot][tile],count=vrd_exact_count[slot][tile];
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
    // The upper bridge is visible from the true lower underpass.
    // Reserve <=24 existing road faces. Source upper tiles are in ROM;
    // no extra framebuffer, heap, RAM cache or per-frame mesh build.
    if(slot==3 &&
       focus_x+512>=vrd_native_bridge_bounds[0] &&
       focus_x-512<=vrd_native_bridge_bounds[1] &&
       focus_z+512>=vrd_native_bridge_bounds[2] &&
       focus_z-512<=vrd_native_bridge_bounds[3]){
        s32 bridge_faces=0;
        s32 start_x=(focus_x/64)*64-256,start_z=(focus_z/64)*64-256;
        for(s32 iz=0;iz<8 && bridge_faces<24 && exact_faces<180;iz++)
            for(s32 ix=0;ix<8 && bridge_faces<24 && exact_faces<180;ix++){
                s32 x=start_x+ix*64,z=start_z+iz*64;
                s32 top[4]={overpass_sample(x,z),
                            overpass_sample(x,z+64),
                            overpass_sample(x+64,z+64),
                            overpass_sample(x+64,z)};
                if(top[0]<0||top[1]<0||top[2]<0||top[3]<0)continue;
                s32 bottom=normal_sample(slot,x+32,z+32);
                if(bottom<0)continue;
                s32 up=(top[0]+top[1]+top[2]+top[3])/4;
                if(up<=bottom+12 ||
                   native_reference_height>(bottom+up)*128)
                    continue;
                if(!r_visible(x+32,z+32,68))continue;
                ground_quad(x,z,64,top[0],top[1],top[2],top[3],
                            COLOR(M_ASPHALT,(ix+iz)&1));
                bridge_faces++;
                exact_faces++;
            }
    }
#endif
    // Distant sourced road uses the original coarse graphics LOD.
    // Total source road face budget remains <=180. No heap/framebuffer.
    s32 base_x=(focus_x/64)*64-576,base_z=(focus_z/64)*64-576;
    s32 faces=exact_faces;
    for(s32 j=0;j<18 && faces<180;j++)for(s32 i=0;i<18 && faces<180;i++){
        s32 x=base_x+i*64,z=base_z+j*64;
#ifdef VRD_EXACT_SOURCE_MESH
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
