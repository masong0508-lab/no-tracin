// The map editor: build your own stunts in Freedom City. Ramps, walls and
// boost pads become world features, cones and crates real loose props; the
// map is saved in two slots and comes back whenever you enter Stunt City.
#ifndef EDIT_H
#define EDIT_H

#include "car.h"

extern s32 g_edit_testing;          // test-driving from the editor: the pause menu offers the way back

void edit_enter_city(void);         // loads the saved map into the world
s32  edit_reset_props(void);        // props_reset plus the placed ones; 0 if some had no room
s32  edit_run(Car *car);            // the editor; returns 1 when it dropped the car in for a test drive
void edit_step(Car *c);             // once per physics step in the city: boost pads and bonus stars
s32  edit_boosting(void);           // a boost pad is firing the nitro
void edit_draw_sprites(s32 frame);  // bonus stars

#endif
