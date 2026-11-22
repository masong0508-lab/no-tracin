// Validate production camera clipping and scanline fill under signed-shift UBSan.
#include <assert.h>
#include <stdio.h>
#include "render.c"

void hspan(volatile u16 *p, s32 count, u32 value)
{
    for (s32 i = 0; i < count * 2; ++i) p[i] = (u16)value;
}

int main(void)
{
    static u16 frame[SCREEN_W * SCREEN_H / 2];
    target = frame;
    r_init();

    // A screen edge can travel toward negative X while scanning downward.
    assert(edge_slope(-105, 7) < 0);
    assert(edge_slope(105, 7) > 0);

    // Scissor the visible area of a quad whose left corners are offscreen.
    const s16 sx[] = {-20, 90, 90, -20};
    const s16 sy[] = {12, 12, 85, 85};
    fill_poly(sx, sy, 4, 7);
    s32 painted = 0;
    for (s32 i = 0; i < SCREEN_W * SCREEN_H / 2; i++)
        if (frame[i]) painted++;
    assert(painted > 150);

    // The inside-to-outside edge has a *negative* near-plane numerator.
    const Vec3 q[] = {
        {-80, -30, 92}, {80, -30, 92}, {80, 30, 8}, {-80, 30, 8}
    };
    s16 px[MAXV], py[MAXV];
    s32 n = project_poly(q, 4, px, py, 0);
    assert(n >= 3 && n <= MAXV);
    for (s32 i = 0; i < n; i++)
        assert(px[i] >= -LIMIT && px[i] <= LIMIT &&
               py[i] >= -LIMIT && py[i] <= LIMIT);

    printf("PASS: near-plane clipping and left-of-screen polygon edges (%d filled spans)\n", painted);
    return 0;
}
