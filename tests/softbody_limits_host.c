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
static s32 wall_z;
s32 world_height(s32 x,s32 z) { (void)x;(void)z;return 0; }
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
{ (void)theta;*x=*y=*z=0; }
void sound_play(s32 sound) { (void)sound; }
void game_message(const char *a,const char *b,s32 palette,s32 steps)
{ (void)a;(void)b;(void)palette;(void)steps; }

static Car setup(s32 config)
{
    memset(g_tune,10,sizeof(g_tune));
    car_g=35;car_grip=38;car_power=28000;car_crashes=car_softbody=1;
    cheat_fly=cheat_hop=cheat_glitch=0;
    if(config==1)g_tune[TN_SPRINGS]=30;
    if(config==2)g_tune[TN_CRUMPLE]=0;
    if(config==3 || config==4) {
        car_g=13;car_grip=56;
        g_tune[TN_GRAVITY]=1;g_tune[TN_GRIP]=30;
        if(config==4) {
            g_tune[TN_NITRO]=30;g_tune[TN_TOPSPEED]=30;g_tune[TN_DOWNFORCE]=30;
        }
    }
    tune_apply();
    Car car;car_reset(&car,1000,6000,0);
    return car;
}

static void angular_direction(void)
{
    Car car=setup(0);sb_reset(&car);
    for(s32 k=0;k<3;k++) {
        cen[k]=0;accv[k]=accw[k]=0;
        ax_r[k]=k==0?16384:0;ax_u[k]=k==1?16384:0;ax_f[k]=k==2?16384:0;
        inv_i[k]=512;
    }
    // 16-unit downward lever arm, backward impulse: positive pitch torque.
    // Exact powers-of-two scaling makes the expected 160,000 independent
    // of the implementation's projection helpers.
    const s32 at[3]={0,-4096,0},force[3]={0,0,-80000};
    impulse(force,at);
    assert(accw[0]==160000 && accw[1]==0 && accw[2]==0);
    assert(accv[0]==0 && accv[1]==0 && accv[2]<0);
}

static void stationary_load(void)
{
    Car car=setup(3);sb_reset(&car);
    // A load impulse can temporarily raise the grip limit above 46,340.
    // Stationary contacts still produce exactly zero tyre force.
    for(s32 w=0;w<4;w++)hub_load[w]=298;
    tyres(&car,0,0);
    for(s32 k=0;k<3;k++)assert(accv[k]==0 && accw[k]==0);
    assert(car.spin==0 && car.skid==0);
}


#ifdef CHECK_FRICTION
static void friction_boundaries(void)
{
    static const struct {s32 t,l,lim,want_t,want_l,slides;} cases[]={
        {12,16,25,12,16,0},
        {300,400,100,52,69,1},
        {-300,400,100,-52,69,1},
        {30000,40000,10000,5250,7000,1},
        {-30000,-40000,10000,-5250,-7000,1},
        {300000,400000,100000,52500,70000,1},
        {300000,-400000,600000,300000,-400000,0},
        {0,0,50064,0,0,0},
    };
    for(u32 n=0;n<sizeof(cases)/sizeof(cases[0]);n++) {
        s32 t=cases[n].t,l=cases[n].l;
        s32 slides=limit_tyre_force(&t,&l,cases[n].lim);
        assert(t==cases[n].want_t && l==cases[n].want_l && slides==cases[n].slides);
    }
}

#endif

static void drive(s32 config,s32 scenario)
{
    Car car=setup(config);
    if(scenario==0) { car.y=100*256;car.mode=CAR_AIR; }
    if(scenario==1) { car.y=1000*256;car.mode=CAR_AIR; }
    if(scenario==2) { car.vz=3500;wall_z=7000; }
    if(scenario==3) { car_reset(&car,1000,3000,0);wall_z=7000; }
    s32 maxhit=0,maxland=0;
    for(s32 step=0;step<1500;step++) {
        car_step(&car,KEY_A);
        if(car.hit>maxhit)maxhit=car.hit;
        if(car.landed>maxland)maxland=car.landed;
    }
    if(wall_z) {
        assert(maxhit>0);
        assert((car.z>>8)<wall_z+80);
    } else assert(maxland>0);
}

int main(int argc,char **argv)
{
    assert(argc>=2);r_init();
    if(!strcmp(argv[1],"angular"))angular_direction();
    else if(!strcmp(argv[1],"stationary"))stationary_load();
    #ifdef CHECK_FRICTION
    else if(!strcmp(argv[1],"friction"))friction_boundaries();
    #endif
    else { assert(argc==3);drive(atoi(argv[1]),atoi(argv[2])); }
    printf("PASS: soft-body arithmetic %s\n",argv[1]);
    return 0;
}
