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

/* TICK_RATE: User::TickCount units per window. The per-window frame count is the
 * gauge value and, when the window is one second, is fps directly. The raw
 * frames/span in the ring let the reader recover exact fps for any tick rate, so
 * the gauge needs only to be proportional (good enough to compare two builds). */
#define TICK_RATE 1000u

/* Gauge: full scale and pixels-per-unit for the contiguous bar. */
#define GAUGE_FS  60u
#define GAUGE_PER 80u
#define COL_BRIGHT 0x07E0u              /* green */
#define COL_DIM    0x2965u              /* dim green reference */

struct a4_fps_state {
    uint32_t magic;
    uint32_t last_tick;
    uint32_t frames;       /* total frames since init */
    uint32_t win_frames;   /* frames in the current window */
    uint32_t win_start;    /* tick at window start */
    uint32_t disp;         /* frames counted in the last closed window */
    uint32_t min_dt, max_dt, sum_dt;
    uint32_t last_span;    /* ticks the last window actually spanned (for calibration) */
    uint32_t ring_pos;
    uint32_t ring[RING];   /* recent per-frame tick deltas */
};

void a4_fps_tick(uint32_t tick)
{
    volatile struct a4_fps_state *s = (volatile struct a4_fps_state *)A4_FPS_SCRATCH;

    if (s->magic != A4_FPS_MAGIC) {
        for (int i = 0; i < RING; i++) s->ring[i] = 0;
        s->last_tick = tick; s->frames = 0; s->win_frames = 0; s->win_start = tick;
        s->disp = 0; s->min_dt = 0xFFFFFFFFu; s->max_dt = 0; s->sum_dt = 0;
        s->last_span = 0; s->ring_pos = 0; s->magic = A4_FPS_MAGIC;
        return;
    }

    uint32_t dt = tick - s->last_tick;
    s->last_tick = tick;
    s->frames++;
    s->win_frames++;
    if (dt < s->min_dt) s->min_dt = dt;
    if (dt > s->max_dt) s->max_dt = dt;
    s->sum_dt += dt;
    s->ring[s->ring_pos] = dt;
    s->ring_pos = (s->ring_pos + 1) & (RING - 1);

    uint32_t span = tick - s->win_start;
    if (span >= TICK_RATE) {
        s->disp = s->win_frames;
        s->last_span = span;
        s->win_frames = 0;
        s->win_start = tick;
        s->sum_dt = 0; s->min_dt = 0xFFFFFFFFu; s->max_dt = 0;
    }

    uint32_t bb = *(volatile uint32_t *)A4_FPS_BB;
    if (bb) {
        volatile uint16_t *fb = (volatile uint16_t *)bb;
        uint32_t cur = s->disp; if (cur > GAUGE_FS) cur = GAUGE_FS;
        for (uint32_t i = 0; i < GAUGE_FS * GAUGE_PER; i++) fb[i] = COL_DIM;
        for (uint32_t i = 0; i < cur * GAUGE_PER; i++) fb[i] = COL_BRIGHT;
    }
}
