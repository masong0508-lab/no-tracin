#ifndef VRD32X_STUDY_H
#define VRD32X_STUDY_H
#include "gba.h"
// Read-only, locally generated 32X road study. Not original course meshes or
// authentic routes; no donor content exists in the public source tree.
s32 vrd32x_study_slot(void);  // -1 unless an optional 32X study is selected
s32 vrd32x_study_height(s32 slot, s32 x, s32 z);  // Q8
s32 vrd32x_study_surface(s32 slot, s32 x, s32 z);
s32 vrd32x_study_ground_plane(s32 slot,s32 x,s32 z,s32 r,s32 *h,s32 *gx,s32 *gz);
void vrd32x_study_draw(s32 slot,s32 focus_x,s32 focus_z,s32 frame);
void vrd32x_study_map(volatile u16 *page,s32 slot,s32 x0,s32 y0,s32 w,s32 h,u8 color);
#endif
