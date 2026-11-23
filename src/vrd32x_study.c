// Driveable 32X road-surface studies derived from verified US cartridge BSP
// samples. The optional private build embeds only sampled data in ROM, not
// an emulated 32X CPU or original game's polygon assets. No EWRAM allocated.
#include "track.h"
#include "world.h"
#include "vrd32x_study.h"
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
static s32 sample(s32 slot,s32 x,s32 z)
{
    if ((u32)slot >= TRACK_VRD_COUNT || (u32)x >= 16384 || (u32)z >= 16384)
        return -1;
    u32 sx=(u32)x>>5, sz=(u32)z>>5;
    u32 tile=(sz>>3)*64+(sx>>3);
    u32 id=vrd_study_lookups[slot][tile];
    if (!id) return -1;
    u16 height=vrd_study_heights[slot][(id-1)*64+(sz&7)*8+(sx&7)];
    return height==65535 ? -1 : height;
}

// The 32X BSP source is sampled every 32 world units; taking the nearest
// sample for each wheel creates 32-unit stair steps on long inclines. Blend
// height *within a fully supported four-sample patch*. If any required
// sample belongs to unsupported/off-road space, use the last known road
// sample rather than inventing a bridge across that gap. Q8 output, no heap
// and no extra tables. Exact 32-unit grid vertices remain byte-identical.
static s32 smooth_height(s32 slot,s32 x,s32 z)
{
    s32 a=sample(slot,x,z);
    if(a<0) return -1;
    s32 fx=x&31,fz=z&31;
    if(!fx && !fz) return a*256;
    s32 b=a,c=a,d=a;
    if(fx) {
        b=sample(slot,x+32,z);
        if(b<0) return a*256;
    }
    if(fz) {
        c=sample(slot,x,z+32);
        if(c<0) return a*256;
    }
    if(fx && fz) {
        d=sample(slot,x+32,z+32);
        if(d<0) return a*256;
    }else if(fx) d=b;
    else d=c;
    // Raw samples are 8..800. All intermediates fit signed 32-bit.
    s32 h0=a*32+(b-a)*fx;
    s32 h1=c*32+(d-c)*fx;
    return (h0*(32-fz)+h1*fz)/4;
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
    // Render ONLY the nearby authenticated surface sample cells. Sample
    // at a coarser 64-unit graphics LOD than the 32-unit collision raster.
    // Max 19x19 candidates, <=180 surface faces, bounded render queue.
    s32 base_x=(focus_x/64)*64-576,base_z=(focus_z/64)*64-576;
    s32 faces=0;
    for(s32 j=0;j<18 && faces<180;j++)for(s32 i=0;i<18 && faces<180;i++){
        s32 x=base_x+i*64,z=base_z+j*64;
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
    // The old preview filled every 64x64 grid tile even when its sampled
    // source BSP mask only covered a sliver. That invents fake wide roads,
    // conceals branches and connects gaps. Each tile is already eight by
    // eight authenticated samples: display only 4x4 quadrants containing
    // at least one on-road sample. No extra data, RAM or staging-line use.
    if((u32)slot>=TRACK_VRD_COUNT || w<=0 || h<=0)return;
    for(s32 row=0;row<64;row++)for(s32 col=0;col<64;col++){
        s32 id=vrd_study_lookups[slot][row*64+col];
        if(!id)continue;
        const u16 *tile=vrd_study_heights[slot]+(id-1)*64;
        for(s32 dy=0;dy<2;dy++)for(s32 dx=0;dx<2;dx++){
            s32 found=0;
            for(s32 sy=dy*4;sy<dy*4+4 && !found;sy++)
                for(s32 sx=dx*4;sx<dx*4+4;sx++)
                    if(tile[sy*8+sx]!=65535){found=1;break;}
            if(!found)continue;
            s32 px=x0+((col*2+dx)*w)/128;
            s32 py=y0+((row*2+dy)*h)/128;
            map_pixel(page,px,py,color);
        }
    }
}
