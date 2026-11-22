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
} VrOriginalCourse;

void vr_original_draw(s32 course, s32 focus_x, s32 focus_z, s32 frame);
s32 vr_original_height(s32 course, s32 x, s32 z);
s32 vr_original_surface(s32 course, s32 x, s32 z);
void vr_original_gradient(s32 course, s32 x, s32 z, s32 *gx, s32 *gz);
s32 vr_original_ground_plane(s32 course, s32 x, s32 z, s32 radius,
                             s32 *h0, s32 *gx, s32 *gz);
s32 vr_original_collide(s32 course, s32 x, s32 z, s32 radius,
                        s32 *nx, s32 *nz);

#endif
