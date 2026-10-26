/* a4_fps.c -- per-frame FPS/frame-time instrumentation for Asphalt 4.
 *
 * a4_fps_tick(tick) is called once per frame by a detour planted just before the
 * present call (+0x1E6), so the on-screen gauge it paints into the back buffer is
 * carried out by the present copy. `tick` is User::TickCount() (euser ord 674),
 * read by the trampoline. The back-buffer base is cached into A4_FPS_BB by a
 * second detour in the textured filler (a4_cachebb_tramp), so drawing is portable
 * (no hardcoded frame address).
 *
 * State lives in a fixed scratch area carved out of the image's .bss, which the
 * injector grows to cover it. The exe's data section loads at its linked base
 * (0x400000) -- unlike code, which relocates -- so a literal absolute address
 * into the grown bss is valid at runtime. The struct is laid out so a bench
 * MEMSNAP reads the live counters and the ring of recent frame deltas directly;
 * that ring is the exact, objective record.
 *
 * On-screen gauge: the game's back buffer has an unusual stride and the display
 * path turns 2D writes into stripes (a bench artifact, and the phone's path may
 * differ again), so a 2D digit readout cannot be drawn reliably. A *contiguous*
 * run of pixels, however, maps to a contiguous screen region on any stride/scale
 * (verified by the solid-marker screenshot). So the gauge is a contiguous bar:
 * a dim full-scale reference with a bright run on top whose length is the
 * per-window frame count. The bright fraction tracks fps, which is what lets a
 * stock-vs-patched comparison be read off by eye; exact fps is in the ring.
 *
 * Build: clang --target=armv5te-none-eabi -marm -mtune=arm1136j-s -O2 \
 *              -ffreestanding -fno-builtin -fropi -c a4_fps.c -o a4_fps.o
 */
#include <stdint.h>

#define A4_FPS_SCRATCH 0x00400400u      /* kept in sync with injector.py */
#define A4_FPS_BB      0x00400700u      /* back-buffer base, stashed by a4_cachebb_tramp */
#define A4_FPS_MAGIC   0xF95C0DE1u
#define RING 128

/* The window is one real second (1,000,000 us from TTime::HomeTime), so the
 * per-window frame count is true fps. Reference ticks give the scale. */
#define WIN_US    1000000u
#define GAUGE_FS  60u                   /* full-scale fps */
#define GAUGE_PER 80u                   /* pixels per fps along the contiguous run */
#define COL_BRIGHT 0x07E0u              /* green = current fps */
#define COL_DIM    0x2965u              /* dim green = full-scale reference */
#define COL_REF30  0xF800u              /* red tick = 30 fps baseline */
#define COL_REF    0xFFFFu              /* white ticks = 15 / 45 fps */

typedef uint32_t (*tick_fn)(void);
typedef int (*home_fn)(void *ttime);    /* TTime::HomeTime(this): fills 8-byte us */

void a4_gl_spike(void);                 /* MBX pbuffer spike (renderer/a4_gl.c); self-gated */

struct a4_fps_state {
    uint32_t magic;
    uint32_t last_tick;
    uint32_t frames;       /* total frames since init */
    uint32_t win_frames;   /* frames in the current one-second window */
    uint32_t win_start_us; /* HomeTime us at window start */
    uint32_t disp;         /* fps: frames in the last closed one-second window */
    uint32_t min_dt, max_dt, sum_dt;
    uint32_t ttime[2];     /* TTime buffer for HomeTime (64-bit us) */
    uint32_t ring_pos;
    uint32_t ring[RING];   /* recent per-frame tick deltas */
};

static void gauge_run(volatile uint16_t *fb, uint32_t from, uint32_t to, uint16_t col)
{
    for (uint32_t i = from; i < to; i++) fb[i] = col;
}

void a4_fps_tick(tick_fn tc, home_fn ht)
{
    volatile struct a4_fps_state *s = (volatile struct a4_fps_state *)A4_FPS_SCRATCH;

    uint32_t tick = tc();
    ht((void *)s->ttime);
    uint32_t us = s->ttime[0];

    if (s->magic != A4_FPS_MAGIC) {
        for (int i = 0; i < RING; i++) s->ring[i] = 0;
        s->last_tick = tick; s->frames = 0; s->win_frames = 0; s->win_start_us = us;
        s->disp = 0; s->min_dt = 0xFFFFFFFFu; s->max_dt = 0; s->sum_dt = 0;
        s->ring_pos = 0; s->magic = A4_FPS_MAGIC;
        return;
    }

    a4_gl_spike();                      /* MBX pbuffer spike; no-op unless GL linked */

    uint32_t dt = tick - s->last_tick;
    s->last_tick = tick;
    s->frames++;
    s->win_frames++;
    if (dt < s->min_dt) s->min_dt = dt;
    if (dt > s->max_dt) s->max_dt = dt;
    s->sum_dt += dt;
    s->ring[s->ring_pos] = dt;
    s->ring_pos = (s->ring_pos + 1) & (RING - 1);

    if ((uint32_t)(us - s->win_start_us) >= WIN_US) {   /* one real second elapsed */
        s->disp = s->win_frames;                        /* == fps */
        s->win_frames = 0;
        s->win_start_us = us;
        s->sum_dt = 0; s->min_dt = 0xFFFFFFFFu; s->max_dt = 0;
    }

    uint32_t bb = *(volatile uint32_t *)A4_FPS_BB;
    if (bb) {
        volatile uint16_t *fb = (volatile uint16_t *)bb;
        uint32_t cur = s->disp; if (cur > GAUGE_FS) cur = GAUGE_FS;
        gauge_run(fb, 0, GAUGE_FS * GAUGE_PER, COL_DIM);      /* 0..60 reference */
        gauge_run(fb, 0, cur * GAUGE_PER, COL_BRIGHT);        /* 0..fps bright */
        /* reference ticks (drawn last, always visible): 30 red, 15/45 white */
        gauge_run(fb, 30 * GAUGE_PER, 30 * GAUGE_PER + GAUGE_PER, COL_REF30);
        gauge_run(fb, 15 * GAUGE_PER, 15 * GAUGE_PER + GAUGE_PER / 2, COL_REF);
        gauge_run(fb, 45 * GAUGE_PER, 45 * GAUGE_PER + GAUGE_PER / 2, COL_REF);
    }
}
