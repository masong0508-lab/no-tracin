// Real soft-body arithmetic at the allowed lab extremes. Production impact
// response and car integration run against a flat-road/barrier fixture.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "softbody.c"

const TrackDef *g_track;
const TrackDef g_tracks[TRACK_COUNT] = {0};
void race_on_respawn(Car *player) { (void)player; assert(!g_track); }
const Loop the_loop = {1000,-10000,200,100,100};
static s32 wall_z, slope_gz;
s32 world_height(s32 x,s32 z) { (void)x;return (z-1000)*slope_gz; }
s32 world_surface(s32 x,s32 z) { (void)x;(void)z;return SURF_ROAD; }
s32 world_in_water(s32 x,s32 z) { (void)x;(void)z;return 0; }
s32 world_collide(s32 x,s32 z,s32 radius,s32 *nx,s32 *nz)
{
    (void)x;*nx=*nz=0;
    if(wall_z && z+radius>wall_z) { *nz=-16384;return z+radius-wall_z; }
    return 0;
}
s32 world_ground_plane(s32 x,s32 z,s32 radius,s32 *h,s32 *gx,s32 *gz)
{ (void)x;(void)z;(void)radius;*h=*gx=*gz=0;return 1; }
s32 track_find(s32 x,s32 z,TrackHit *hit)
{ (void)x;(void)z;*hit=(TrackHit){0};return 1; }
// Artificial track fixture: route-only lookup shares its known geometry.
s32 track_find_road(s32 x, s32 z, TrackHit *hit) { return track_find(x, z, hit); }
s32 track_height_near(s32 segment,s32 x,s32 z)
{ (void)segment;(void)x;(void)z;return 0; }
void track_gradient(s32 x,s32 z,s32 *gx,s32 *gz)
{ (void)x;(void)z;*gx=*gz=0; }
void loop_point(s32 theta,s32 *x,s32 *y,s32 *z)
{
    const Loop *l = &the_loop;
    *x = l->x*256 + l->shift*theta/4;
    *z = l->z*256 + ((l->radius*isin(theta)) >> 6);
    *y = l->radius*256 - ((l->radius*icos(theta)) >> 6);
}
void sound_play(s32 sound) { (void)sound; }
void game_message(const char *a,const char *b,s32 palette,s32 steps)
{ (void)a;(void)b;(void)palette;(void)steps; }


static Car prepare(s32 handling,s32 motion)
{
    memset(g_tune,TUNE_STOCK,sizeof(g_tune));
    car_g=35; car_grip=38; car_power=28000; car_crashes=1;
    car_softbody = handling==CAR_PHYS_SOFT_BODY;
    car_physics_profile=handling; car_motion_profile=motion;
    cheat_fly=cheat_hop=cheat_glitch=0; wall_z=slope_gz=0;
    tune_apply();
    Car c; car_reset(&c,1000,1000,0); return c;
}

static Car loop_roof(s32 handling,s32 motion,s32 velocity)
{
    Car c=prepare(handling,motion);
    s32 len=(the_loop.radius*411775)>>8;
    c.loop_s=len/2;
    c.loop_v=velocity;
    c.mode=CAR_LOOP;
    loop_point(512,&c.x,&c.y,&c.z);
    return c;
}

static void loop_contact(void)
{
    // A slow car cannot be held upside down by a road that only pushes.
    for(s32 handling=CAR_PHYS_SOFT_BODY;handling<CAR_PHYS_COUNT;handling++) {
        Car old=loop_roof(handling,CAR_MOTION_ORIGINAL,0);
        car_step(&old,0);
        assert(old.mode==CAR_CRASH && old.event==EV_FELL);

        Car real=loop_roof(handling,CAR_MOTION_NEWTON,0);
        car_step(&real,0);
        assert(real.mode==CAR_AIR && real.crash_kind==0);
        assert(real.event==EV_NONE && real.vy==0 && real.y>100*256);
        s32 y=real.y, vy=real.vy;
        car_step(&real,0);
        assert(real.mode==CAR_AIR && real.y<y && real.vy<vy);
        if(handling!=CAR_PHYS_SOFT_BODY) {
            assert(real.vy==vy-car_g);
            assert(real.y==y+real.vy);
        }
    }
    // At high speed centripetal acceleration is enough to stay on track.
    Car old=loop_roof(CAR_PHYS_ARCADE,CAR_MOTION_ORIGINAL,4000);
    Car now=loop_roof(CAR_PHYS_ARCADE,CAR_MOTION_NEWTON,4000);
    car_step(&old,0); car_step(&now,0);
    assert(now.mode==CAR_LOOP && old.mode==CAR_LOOP);
    assert(now.loop_s==old.loop_s && now.loop_v==old.loop_v);
    assert(now.x==old.x && now.y==old.y && now.z==old.z);
}

static void sloped_touchdown(void)
{
    Car c=prepare(CAR_PHYS_ARCADE,CAR_MOTION_NEWTON);
    slope_gz=64; // plane: 1 unit higher for 4 units along the road
    c.mode=CAR_AIR;
    c.x=c.z=1000*256;
    c.y=600;
    c.vx=0; c.vz=300; c.vy=-1000;
    s32 vz=c.vz-(c.vz*tn.drag_lin >>17);
    s32 vy=c.vy-car_g;
    s32 tangent= vz+vy/4;
    car_step(&c,0);
    assert(c.mode==CAR_AIR && c.landed>700 && c.vy>0 && c.vz<vz);
    s32 delta=tangent - (c.vz+c.vy/4);
    if(delta<0)delta=-delta;
    assert(delta<=5); // only a normal impulse; lateral impulse is absent
    assert(c.y>world_height(c.x>>8,c.z>>8));
}

static void handling_is_independent(void)
{
    // Motion has no influence on ordinary steering before a stunt occurs.
    for(s32 handling=CAR_PHYS_ARCADE;handling<CAR_PHYS_COUNT;handling++) {
        Car a=prepare(handling,CAR_MOTION_ORIGINAL);
        for(int i=0;i<80;i++)car_step(&a,KEY_A|(i>30?KEY_RIGHT:0));
        Car b=prepare(handling,CAR_MOTION_NEWTON);
        for(int i=0;i<80;i++)car_step(&b,KEY_A|(i>30?KEY_RIGHT:0));
        assert(memcmp(&a,&b,sizeof(Car))==0);
    }
}

int main(void)
{
    r_init();
    loop_contact();
    sloped_touchdown();
    handling_is_independent();
    puts("PASS: independent motion, upside-down loop contact, ballistic drop, slope-normal bounce");
    return 0;
}
