// Optional original Virtua Racing courses, generated locally from a source ROM.
#ifndef VR_ORIGINAL_H
#define VR_ORIGINAL_H

#include "gba.h"

// All large tables are const ROM data. Only a bounded visible-section list
// lives in RAM; this pack allocates no extra framebuffer or heap memory.
typedef struct {
    s16 xyz[12];
    s16 cx, cz;
    u16 radius, identity;  // equal identities are byte-identical source faces
    u8 count, color, flags, reserved;
} VrOriginalFace;

typedef struct {
    u16 first, count;
    s16 cx, cz;
    u16 radius;
} VrOriginalSection;

typedef struct {
    const VrOriginalFace *faces;
    const VrOriginalSection *sections;
    u16 section_count;
    const u16 *height_grid;
    const s16 *height_planes;
    u16 height_rows;
    const u16 *surface_grid;
    const u8 *surface_tiles;
    u16 surface_rows;
    // Source asphalt-mask bounds, generated privately. Includes pits and
    // side roads omitted by the main-race-centreline preview.
    u16 road_min_x, road_max_x, road_min_z, road_max_z;
    // Generated only from the authenticated source asphalt bitmap.
    // Exactly 102x68/8 packed road pixels per imported course, ROM-only.
    const u8 *preview_bits;
} VrOriginalCourse;

void vr_original_draw(s32 course, s32 focus_x, s32 focus_z, s32 frame);
// Render the *actual source asphalt mask*, not the simplified race spline.
// pts/dot_d are used only for the original start marker and moving dot.
void vr_original_map(volatile u16 *page, s32 course,
                     s32 x0, s32 y0, s32 w, s32 h, u8 color,
                     const void *route_pts, s32 route_count,
                     s32 dot_d, u8 dot_color);
s32 vr_original_height(s32 course, s32 x, s32 z);
s32 vr_original_surface(s32 course, s32 x, s32 z);
void vr_original_gradient(s32 course, s32 x, s32 z, s32 *gx, s32 *gz);
s32 vr_original_ground_plane(s32 course, s32 x, s32 z, s32 radius,
                             s32 *h0, s32 *gx, s32 *gz);
s32 vr_original_collide(s32 course, s32 x, s32 z, s32 radius,
                        s32 *nx, s32 *nz);

#endif
