// Original meshes, road surfaces and height planes share one uniform transform:
// GBA X/Z = donor mesh X/Z / 4 + 8192; GBA Y = donor mesh Y / 4.
#include "vr_original.h"
#include "track.h"
#include "world.h"
#include "vr_original_mesh.h"

#define VR_ORIGIN 8192
#define VR_MAX_SECTIONS 166
#define VR_MAX_SOURCE_FACES 4420
#define VR_RESERVED_FACES 32

static u8 visible[VR_MAX_SECTIONS] EWRAM_BSS;
static s32 depths[VR_MAX_SECTIONS] EWRAM_BSS;
// Some original sections repeat the same world-space scenery. Draw each
// exact source face once, even when several of those sections are visible.
static u32 drawn[(VR_MAX_SOURCE_FACES + 31) / 32] EWRAM_BSS;

static const VrOriginalCourse *course_at(s32 index)
{
    return (u32)index < 3 ? &vr_original_courses[index] : 0;
}

// Return the original plane for these GBA coordinates. The native index grid
// has 128 columns, with a checked, course-specific number of available rows.
static const s16 *plane_at(const VrOriginalCourse *c, s32 x, s32 z)
{
    if (!c || (u32)x >= 16384 || (u32)z >= 16384) return 0;
    s32 px = (x - VR_ORIGIN) * 2, pz = (z - VR_ORIGIN) * 2;
    s32 gx = (px >> 4) + 1024, gz = ((-pz) >> 4) + 1024;
    s32 col = gx >> 4, row = gz >> 4;
    if ((u32)col >= 128 || (u32)row >= c->height_rows) return 0;
    u32 tile = c->height_grid[row * 128 + col];
    u32 sub = ((gx & 15) >> 2) + (gz & 12);
    return &c->height_planes[(tile * 16 + sub) * 4];
}

s32 vr_original_height(s32 course, s32 x, s32 z)
{
    const s16 *p = plane_at(course_at(course), x, z);
    if (!p || !p[1]) return 0;
    if (!(p[0] | p[2])) return (s32)p[3] * 8;
    s32 px = (x - VR_ORIGIN) * 2, pz = (z - VR_ORIGIN) * 2;
    s32 n = p[0] * ((px + 16384) & 63) + p[2] * ((-pz + 16384) & 63);
    // Match DIVS truncation and the original signed-word result. Native
    // physics Y / 8 = mesh Y; mesh Y / 4 * 256 = our Q8 height.
    return (s32)(s16)(p[3] - (n / p[1]) * 16) * 8;
}

static s32 raw_surface(const VrOriginalCourse *c, s32 x, s32 z)
{
    if (!c || (u32)x >= 16384 || (u32)z >= 16384) return 2;
    s32 px = (x - VR_ORIGIN) * 2, pz = (z - VR_ORIGIN) * 2;
    s32 gx = (px + 16384) >> 4, gz = (16384 - pz) >> 4;
    s32 col = gx >> 4, row = gz >> 4;
    if ((u32)col >= 128 || (u32)row >= c->surface_rows) return 2;
    u32 tile = c->surface_grid[row * 128 + col];
    u32 packed = c->surface_tiles[tile * 64 + (gz & 15) * 4 + ((gx & 15) >> 2)];
    return (packed >> ((3 - (gx & 3)) * 2)) & 3;
}

s32 vr_original_surface(s32 course, s32 x, s32 z)
{
    return raw_surface(course_at(course), x, z) == 0 ? SURF_ROAD : SURF_GRASS;
}

void vr_original_gradient(s32 course, s32 x, s32 z, s32 *gx, s32 *gz)
{
    *gx = (vr_original_height(course, x + 4, z) - vr_original_height(course, x - 4, z)) / 8;
    *gz = (vr_original_height(course, x, z + 4) - vr_original_height(course, x, z - 4)) / 8;
}

s32 vr_original_ground_plane(s32 course, s32 x, s32 z, s32 radius,
                             s32 *h0, s32 *gx, s32 *gz)
{
    const VrOriginalCourse *c = course_at(course);
    const s16 *p = plane_at(c, x, z);
    *h0 = vr_original_height(course, x, z);
    *gx = *gz = 0;
    if (!p || !p[1]) return 0;
    // The soft-body solver may cache a plane only when all contact corners
    // really use it. Otherwise its existing per-node queries stay accurate.
    if (plane_at(c, x-radius, z-radius) != p || plane_at(c, x+radius, z-radius) != p ||
        plane_at(c, x-radius, z+radius) != p || plane_at(c, x+radius, z+radius) != p) return 0;
    *gx = -(256 * (s32)p[0]) / p[1];
    *gz = (256 * (s32)p[2]) / p[1];
    return 1;
}

static void deeper(s32 penetration, s32 dx, s32 dz,
                   s32 *best, s32 *nx, s32 *nz)
{
    if (penetration > *best) {
        *best = penetration; *nx = dx; *nz = dz;
    }
}

s32 vr_original_collide(s32 course, s32 x, s32 z, s32 radius, s32 *nx, s32 *nz)
{
    const VrOriginalCourse *c = course_at(course);
    static const s16 dx[8] = {16384,11585,0,-11585,-16384,-11585,0,11585};
    static const s16 dz[8] = {0,11585,16384,11585,0,-11585,-16384,-11585};
    *nx = *nz = 0;
    if (!c || radius < 1) return 0;
    // Map limits are solid too, including the absent rows of each course.
    s32 min_z = 16384 - c->surface_rows * 128 + 1, best = 0;
    deeper(radius-x, 16384, 0, &best, nx, nz);
    deeper(x+radius-16384, -16384, 0, &best, nx, nz);
    deeper(min_z+radius-z, 0, 16384, &best, nx, nz);
    deeper(z+radius-16384, 0, -16384, &best, nx, nz);
    if (x < 0 || x >= 16384 || z < min_z || z >= 16384) return best;
    if (raw_surface(c, x, z) == 2) {
        // Recovery from a spawn/save exactly inside a donor barrier cell.
        for (s32 distance = 1; distance <= radius + 64; distance++)
            for (s32 k = 0; k < 8; k++)
                if (raw_surface(c, x + ((dx[k]*distance)>>14), z + ((dz[k]*distance)>>14)) != 2) {
                    *nx = dx[k]; *nz = dz[k];
                    return distance + radius;
                }
        // A broad-phase query must still report a barrier if a saved position
        // is buried too deeply for the local recovery search. With no clear
        // direction, leave the normal zero rather than inventing an escape.
        *nx = *nz = 0;
        return radius;
    }
    // The mask uses 8-unit cells. Test every solid cell intersecting the
    // circle, including thin walls entirely inside a broad-phase footprint.
    // Decode four cells per byte and skip empty groups before rectangle work.
    s32 x0 = (x-radius) >> 3, x1 = (x+radius) >> 3;
    s32 z0 = (16384-z-radius) >> 3, z1 = (16384-z+radius) >> 3;
    if (x0 < 0) x0 = 0;
    if (x1 > 2047) x1 = 2047;
    if (z0 < 0) z0 = 0;
    if (z1 >= c->surface_rows * 16) z1 = c->surface_rows * 16 - 1;
    s32 nearest = radius * radius, near_x = 0, near_z = 0;
    for (s32 gz = z0; gz <= z1; gz++) {
        const u16 *row = &c->surface_grid[(gz >> 4) * 128];
        s32 lo_z = 16384 - (gz+1)*8, hi_z = lo_z + 8;
        s32 dz = z < lo_z ? z-lo_z : z > hi_z ? z-hi_z : 0;
        for (s32 group = x0 & ~3; group <= x1; group += 4) {
            u32 tile = row[group >> 4];
            u32 packed = c->surface_tiles[tile*64 + (gz & 15)*4 + ((group & 15) >> 2)];
            u32 solid = (packed >> 1) & ~packed & 0x55;
            if (!solid) continue;
            for (s32 k = 0; k < 4; k++) {
                s32 cell = group+k;
                if (cell < x0 || cell > x1 || !(solid & (1u << ((3-k)*2)))) continue;
                s32 lo_x = cell*8, hi_x = lo_x+8;
                s32 dx = x < lo_x ? x-lo_x : x > hi_x ? x-hi_x : 0;
                s32 d2 = dx*dx + dz*dz;
                if (d2 >= nearest) continue;
                if (!d2) {
                    // A non-solid centre can sit exactly on a cell edge.
                    s32 l=x-lo_x, r=hi_x-x, b=z-lo_z, t=hi_z-z;
                    s32 m=l<r?l:r, n=b<t?b:t;
                    if (m < n) deeper(radius+m, l<r?-16384:16384, 0, &best, nx, nz);
                    else deeper(radius+n, 0, b<t?-16384:16384, &best, nx, nz);
                } else {
                    nearest=d2; near_x=dx; near_z=dz;
                }
            }
        }
    }
    if (nearest < radius * radius) {
        s32 distance = isqrt(nearest);
        deeper(radius-distance, (near_x*16384)/distance, (near_z*16384)/distance, &best, nx, nz);
    }
    return best;
}

IWRAM_CODE void vr_original_draw(s32 course, s32 focus_x, s32 focus_z, s32 frame)
{
    (void)focus_x; (void)focus_z; (void)frame;
    const VrOriginalCourse *c = course_at(course);
    if (!c) return;
    for (u32 i = 0; i < sizeof(drawn) / sizeof(drawn[0]); i++) drawn[i] = 0;
    s32 count = 0;
    for (s32 i = 0; i < c->section_count; i++) {
        const VrOriginalSection *s = &c->sections[i];
        if (!r_visible(s->cx, s->cz, s->radius)) continue;
        s32 side, depth = r_depth(s->cx, s->cz, &side);
        s32 j = count++;
        while (j && depths[j-1] > depth) {
            depths[j] = depths[j-1]; visible[j] = visible[j-1]; j--;
        }
        depths[j] = depth; visible[j] = i;
    }
    // Submit near sections first. Existing depth sorting supplies occlusion;
    // preserve room for the player and effects in the fixed face queue.
    for (s32 k = 0; k < count; k++) {
        const VrOriginalSection *s = &c->sections[visible[k]];
        for (s32 i = 0; i < s->count; i++) {
            const VrOriginalFace *p = &c->faces[s->first + i];
            u32 bit = 1u << (p->identity & 31);
            u32 *word = &drawn[p->identity >> 5];
            if (*word & bit) continue;
            *word |= bit;
            if (!r_visible(p->cx, p->cz, p->radius)) continue;
            if (r_faces_free() <= VR_RESERVED_FACES) return;
            Vec3 v[4];
            for (s32 j = 0; j < p->count; j++) {
                v[j].x=p->xyz[j*3]; v[j].y=p->xyz[j*3+1]; v[j].z=p->xyz[j*3+2];
            }
            r_face(v, p->count, p->color, p->flags);
        }
    }
}


// The three imported Genesis courses already carry the original two-bit
// collision-surface grids. Present that road *area* on the map rather than
// drawing a fabricated constant-width line around the main route. Because
// the grid includes asphalt beyond the main racing line, pit branches and
// junctions appear wherever the actual source marks them as driveable.
// Bounding coordinates were generated by scanning that exact packed mask.
static void original_map_pixel(volatile u16 *page,s32 x,s32 y,u8 color)
{
    if ((u32)x >= SCREEN_W || (u32)y >= SCREEN_H) return;
    volatile u16 *word=page+(y*SCREEN_W+x)/2;
    *word=(x&1)?((*word&255)|(color<<8)):((*word&0xFF00)|color);
}

void vr_original_map(volatile u16 *page,s32 course,
                     s32 x0,s32 y0,s32 w,s32 h,u8 color,
                     const void *route_pts,s32 route_count,
                     s32 dot_d,u8 dot_color)
{
    const VrOriginalCourse *c=course_at(course);
    if(!c || w<=4 || h<=4) return;
    s32 minx=c->road_min_x,maxx=c->road_max_x;
    s32 minz=c->road_min_z,maxz=c->road_max_z;
    s32 dx=maxx-minx,dz=maxz-minz;
    if(dx<=0 || dz<=0) return;
    // Same aspect-preserving, centred orientation as track_map's old
    // centreline. Never stretch hills/bridges or discard pit-lane edges.
    s32 sx=(dx*256)/(w-4),sz=(dz*256)/(h-4);
    s32 scale=sx>sz?sx:sz;
    if(scale<=0)scale=1;
    s32 ox=x0+(w-(dx*256)/scale)/2;
    s32 oy=y0+(h+(dz*256)/scale)/2;
    // At small preview resolutions, a single centre probe can entirely
    // miss a narrow authentic road. Four footprint probes preserve these
    // source-covered lanes without painting a non-source skeleton.
    s32 half=scale/512;
    if(half<1)half=1;
    if (c->preview_bits && w==102 && h==68) {
        // The animated menu redraws every frame. Its exact source asphalt
        // silhouette is generated at PRIVATE import time and packed as
        // 867 bytes of immutable ROM bits, rather than doing ~35,000
        // original tile decodes each frame. Only the dot/starting line
        // below are dynamic; no framebuffer cache or extra EWRAM.
        for(s32 py=0;py<68;py++)for(s32 px=0;px<102;px++){
            s32 i=py*102+px;
            if(c->preview_bits[i>>3] & (1u<<(i&7)))
                original_map_pixel(page,x0+px,y0+py,color);
        }
    }else{
        // Arbitrary viewport sizes in diagnostics still project directly
        // from the exact original packed asphalt grid, never fake roads.
        for(s32 py=y0;py<y0+h;py++){
            if((u32)py>=SCREEN_H)continue;
            s32 z=minz+((oy-py)*scale)/256;
            for(s32 px=x0;px<x0+w;px++){
                if((u32)px>=SCREEN_W)continue;
                s32 x=minx+((px-ox)*scale)/256;
                if(raw_surface(c,x,z)==0 ||
                   raw_surface(c,x-half,z-half)==0 ||
                   raw_surface(c,x+half,z-half)==0 ||
                   raw_surface(c,x-half,z+half)==0 ||
                   raw_surface(c,x+half,z+half)==0)
                    original_map_pixel(page,px,py,color);
            }
        }
    }
    // The verified native route supplies the start/vehicle positions. It
    // does not determine the road preview shape or suppress its branches.
    const TrackPt *pts=(const TrackPt *)route_pts;
    if(!pts || route_count<=0)return;
    s32 start_x=ox+((pts[0].x-minx)*256)/scale;
    s32 start_y=oy-((pts[0].z-minz)*256)/scale;
    for(s32 j=-2;j<=2;j++){
        original_map_pixel(page,start_x+j,start_y-1,COLOR(M_STUNT_WHITE,0));
        original_map_pixel(page,start_x+j,start_y+1,COLOR(M_STUNT_WHITE,0));
    }
    if(dot_d<0)return;
    s32 lap=pts[route_count-1].dist+pts[route_count-1].len;
    if(lap<=0)return;
    s32 d=dot_d%lap;
    for(s32 i=0;i<route_count;i++){
        const TrackPt *p=&pts[i];
        if(d<p->dist || d>=p->dist+p->len)continue;
        s32 along=d-p->dist;
        s32 wx=p->x+((p->ux*along)>>14);
        s32 wz=p->z+((p->uz*along)>>14);
        s32 px=ox+((wx-minx)*256)/scale;
        s32 py=oy-((wz-minz)*256)/scale;
        for(s32 j=-2;j<=2;j++)for(s32 k=-2;k<=2;k++)
            original_map_pixel(page,px+j,py+k,dot_color);
        break;
    }
}
