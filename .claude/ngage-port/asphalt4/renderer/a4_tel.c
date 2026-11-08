/* a4_tel.c -- on-device telemetry/capture for the Asphalt 4 MBX build.
 *
 * The phone has no bench MEMSNAP, so to read what the build is doing on real
 * hardware we write files the owner retrieves (same principle as the classic
 * gate6 logs). The game imports no efsrv, so the injector adds one: the efsrv
 * IAT (a4_efs_iat in a4_gl.s) holds the ordinals Connect(68), Replace(108),
 * Write-at-pos(101), Flush(96) -- all <= the N95 efsrv.dll's 351 exports, so
 * they are the same functions there (ordinals are append-only). RFs::Close(390)
 * is NOT on the N95, so we never close: connect once, keep open, Flush each
 * write (the process exit closes them; Flush commits to flash).
 *
 * Output: a ring of 4 files C:\a4telN.dat (N=0..3), each rewritten from pos 0:
 *   [header: magic, version, and the glr counters]  then  [the game framebuffer].
 * The ring catches a few moments (e.g. the ~1.2s normal/glitch alternation) in
 * one run. I parse the header and decode the framebuffer here. Writing is bursty
 * (one ~150 KB write every TEL_EVERY frames) -- a capture build, not a timing one.
 */
#include <stdint.h>

#ifdef A4_TEL

extern volatile uint32_t *a4_efs_iat_ptr(void);
extern uint32_t a4_ntick(void);              /* a4_gl.s: User::NTickCount */
extern uint32_t a4_gl_rdinfo(int k);         /* a4_gl.c: the read-format probe */
#ifdef A4_DUALCAP
extern void a4_gl_dumpgpu(void (*wr)(int, const void *, int), int base);
#endif
#define EF(i) (a4_efs_iat_ptr()[i])

#ifndef TEL_EVERY
#define TEL_EVERY 24              /* frames between captures (~1-2 s at race fps) */
#endif
#ifndef TEL_RING
#define TEL_RING  4               /* number of ring files C:\a4telN.dat */
#endif
#define A4_GL_SCRATCH 0x00401000u
#define A4_FPS_SCRATCH 0x00400400u /* a4_fps_state: disp(fps), min/max_dt, 128-delta ring */
#define FPS_BYTES 560u            /* sizeof(struct a4_fps_state): 140 words */
#define A4_GL_BB      0x00400700u /* holds the game framebuffer pointer (cache hook) */
#define A4_TEL_SCRATCH 0x00400800u
#define VP_W 320
#define VP_H 240
#define FB_STRIDE 320
#define FB_BYTES  ((uint32_t)VP_W * VP_H * 2u)   /* 150 KB: blit-proven mapped */
#define TEL_MAGIC 0x4C546134u     /* "a4TL" */
#define TEL_HDR_WORDS 64          /* version 4: 256-byte header (3 had 32 words) */
#define A4_PERF ((volatile uint32_t *)0x00412C00u)   /* a4_gl.c PF_*: same order */
enum { PF_MODE, PF_RENDERS, PF_T0, PF_COMP, PF_READ, PF_DRAW, PF_GLFLUSH,
       PF_CALLS, PF_TRIS, PF_FLAT, PF_ADD, PF_DUAL, PF_SHAD, PF_M1BAKE, PF_M1TEX, PF_M1T, PF_AFFINE, PF_LATE,
       PF_BB, PF_BBSW, PF_BB2, PF_FILL, PF_TEXRE, PF_EVICT, PF_SURF,
       PF_T3D, PF_TFLUSH, PF_T2D, PF_TPRES, PF_HOOK, PF_MFIRST, PF_MFLEND, PF_MPRES, PF_FLAGS, PF_HOOKN,
       PF_SKIPPED, PF_REO, PF_RUNS0, PF_NPRES, PF_LASTFIRST,
       PF_MESH, PF_MKEYS, PF_MDRAW, PF_MLOST,   /* A4_MESH: this window's list triangles drawn, its draw calls, its ticks, triangles lost */
       /* A4_STEPTIME (round 30): this window's logic ticks, steps, timed spans, +12D5A ticks;
        * then the open span's mark, its counts (n | m << 8), its +12D5A and +15A52 ticks and calls */
       PF_TLOGIC, PF_NSTEP, PF_NSPAN, PF_TCAM, PF_MLOGIC, PF_LSTEP, PF_SCAM, PF_SMAIN, PF_NMAIN, PF_TSPEC, PF_MSPEC,
       PF_PREVHAD, PF_HELD,   /* A4_DROPHOLD (round 37): the last 3D frame's triangles; frames held back | no-render presents held << 16 (totals) */
       PF_HOLDCALL, PF_HOLDT, /* A4_DROPHOLD: the held present's call site; when it was held (ntick) */
       PF_FWHELD,             /* A4_DROPHOLD: framework posts (+6B4) held (total) */
       PF_N };
#define EFILE_WRITE 0x200u
#define DES(len) ((1u<<28) | ((uint32_t)(len)))  /* TPtrC iLength: EPtrC(1)<<28 | len */

/* client-side session/subsession objects live in scratch; over-sized so the
 * real RFs/RFile layouts fit whatever the ROM uses. */
struct tel {
    uint32_t inited;              /* 0 not tried, 1 ok, 2 failed */
    uint32_t fcount;             /* frames seen */
    uint32_t caps;               /* captures written (ring index source) */
    uint32_t dbg_iat0;           /* diag: resolved Connect address (0 => IAT unresolved) */
    int32_t  dbg_crc;            /* diag: RFs::Connect result */
    int32_t  dbg_rrc;            /* diag: RFile::Replace result (last) */
    int32_t  dbg_wrc;            /* diag: RFile::Write result (last) */
    uint32_t actframes;          /* active-3D frames seen (TEL_SKIP gating) */
    uint32_t fillcap;            /* A4_FILLCAP: GPU fills counted at the last capture */
    uint32_t dropcap;            /* A4_DROPCAP: drop frames counted at the last capture */
    uint8_t  rfs[32];
    uint8_t  rfile[TEL_RING][32];
};

/* a two-word descriptor header (TPtrC8/TPtrC16: {EPtrC<<28|len, ptr}) */
struct des { uint32_t tl; const void *ptr; };

static int efs_connect(void *rfs, int slots) {
    return ((int (*)(void *, int))EF(0))(rfs, slots);
}
static int efs_replace(void *rfile, void *rfs, const void *name16, uint32_t mode) {
    return ((int (*)(void *, void *, const void *, uint32_t))EF(1))(rfile, rfs, name16, mode);
}
static int efs_writep(void *rfile, int pos, const void *des8) {
    return ((int (*)(void *, int, const void *))EF(2))(rfile, pos, des8);
}
static int efs_flush(void *rfile) {
    return ((int (*)(void *))EF(3))(rfile);
}

#ifdef A4_DUALCAP
/* a4_gl_dumpgpu's writer: into the capture being written (caps not yet advanced) */
static void dual_wr(int pos, const void *p, int len)
{
    struct tel *t = (struct tel *)A4_TEL_SCRATCH;
    struct des d = { DES((uint32_t)len), p };
    efs_writep(&t->rfile[t->caps % TEL_RING], pos, &d);
}
#endif

/* Called on every present from a4_gl_frame; had = 3D primitives drawn since the
 * previous present (0 on the two re-presents of each game iteration and in menus). */
void a4_tel_tick(uint32_t had)
{
    struct tel *t = (struct tel *)A4_TEL_SCRATCH;    /* .bss-zeroed at load */
    t->dbg_iat0 = a4_efs_iat_ptr()[0];
    if (a4_efs_iat_ptr()[0] < 0x10000u) return;      /* efsrv not linked */
    if (t->inited == 2) return;

    if (t->inited == 0) {
        /* filename "C:\\a4telN.dat" as UTF-16, built on the stack (no relocated
         * data). One file per ring slot. */
        for (int k = 0; k < TEL_RING; k++) {
            uint16_t fn[16];
            const char *c = "C:\\a4tel0.dat";
            int n = 0; while (c[n]) { fn[n] = (uint16_t)(uint8_t)c[n]; n++; }
            fn[8] = (uint16_t)('0' + k);              /* the ring digit (the '0' in a4tel0) */
            struct des nd = { DES(n), fn };
            if (k == 0) { t->dbg_crc = efs_connect(t->rfs, -1); if (t->dbg_crc != 0) { t->inited = 2; return; } }
            t->dbg_rrc = efs_replace(&t->rfile[k], t->rfs, &nd, EFILE_WRITE);
            if (t->dbg_rrc != 0) { t->inited = 2; return; }
        }
        t->inited = 1;
    }

    t->fcount++;
    /* Grab the first TEL_RING ACTIVE-3D captures and then STOP (do not cycle).
     * Exiting a race goes through the pause menu, whose frames otherwise fill the
     * ring; capturing only early driving and then freezing keeps live race frames.
     * "Active 3D" = the last frame drew real geometry (last_prims big); menus draw
     * little, so this skips the menu/boot frames without a fixed frame count. */
    volatile uint32_t *g = (volatile uint32_t *)A4_GL_SCRATCH;
#ifndef TEL_CYCLE
    if (t->caps >= TEL_RING) return;             /* ring filled once: freeze on early race */
#endif
    /* Only presents that carried a real 3D render: the two re-presents of each game
     * iteration (had == 0) would be byte-identical duplicates, and the pause menu
     * draws no 3D, so a cycling ring (TEL_CYCLE) keeps the last race captures before
     * the player pauses to quit instead of filling with menu frames. (The old gate,
     * g[8] = last_prims, stays high through a pause.) TEL_EVERY counts these. */
#ifdef A4_DROPCAP
#ifndef A4_SLOWREC
#error "A4_DROPCAP reads the A4_SLOWREC drop count"
#endif
    /* bench/phone instrument: capture only the frames A4_SLOWREC counted as drop
     * frames (triangles halved or worse against the render before), which sit under
     * the 100-triangle gate below. slow_rec runs before this tick in a4_gl_frame. */
    {   uint32_t dc = ((volatile uint32_t *)0x00455800u)[18];
        if (!had || dc == t->dropcap) return;
        t->dropcap = dc;
        t->actframes++;
    }
#else
    if (had < 100u) return;
    t->actframes++;
#endif
#ifdef TEL_SKIP
    if (t->actframes < TEL_SKIP) return;         /* skip early race: capture deeper, at speed */
#endif
#if defined(A4_DROPCAP)
#elif defined(A4_FILLCAP)
    /* bench instrument: capture the renders that drew a solid fill on the GPU (the
     * letterbox bars), at most one in 8 renders, instead of every TEL_EVERY */
    if (A4_PERF[PF_FILL] == t->fillcap || (t->actframes & 7u) != 0) return;
    t->fillcap = A4_PERF[PF_FILL];
#else
    if ((t->actframes % TEL_EVERY) != 0) return;
#endif

    uint32_t bb = *(volatile uint32_t *)A4_GL_BB;

    /* header: magic, version, geometry, then the glr counters we read on the
     * bench via MEMSNAP (same field order). */
    volatile uint32_t *fp = (volatile uint32_t *)A4_FPS_SCRATCH;  /* a4_fps_state */

    uint32_t h[TEL_HDR_WORDS];
    int i = 0;
    h[i++] = TEL_MAGIC;
    h[i++] = 4;                       /* version 4: 64-word header (the GL phase timers) */
    h[i++] = t->caps;                 /* capture index */
    h[i++] = t->fcount;
    h[i++] = VP_W; h[i++] = VP_H; h[i++] = FB_STRIDE; h[i++] = FB_BYTES;
    h[i++] = bb;
    h[i++] = g[0];  /* up */        h[i++] = g[1];  /* frames */
    h[i++] = g[2];  /* prims */     h[i++] = g[3];  /* errc */
    /* [13] [14] [16] [17] [18] held ntex (as [54]), bstep (6 once up) and the A4_WINDOW
     * fields, none of them live in the blit build; from round 16 the window's phase timers:
     * 3D pass, our hook (sampled), 2D after the composite, the game's present, composite */
    h[i++] = A4_PERF[PF_T3D];       h[i++] = A4_PERF[PF_HOOK];
    h[i++] = g[8];  /* last_prims */h[i++] = A4_PERF[PF_T2D];
    h[i++] = A4_PERF[PF_TPRES];     h[i++] = A4_PERF[PF_TFLUSH];
    h[i++] = g[11]; /* miny */      h[i++] = g[12]; /* maxy */
    h[i++] = g[13]; /* pminy */     h[i++] = g[14]; /* pmaxy */
    h[i++] = fp[5];  /* fps (a4_fps disp: frames in the last 1s window) */
    h[i++] = fp[6];  /* min_dt */   h[i++] = fp[7];  /* max_dt */
    h[i++] = FPS_BYTES;              /* [26] fps block size; block follows the 128B header */
    {   /* [27..31] GL draw-call census (a4_gl.c, scratch 0x412000): renders, total draw
         * calls, total triangles, max calls in one render, calls in the last render */
        volatile uint32_t *rd = (volatile uint32_t *)0x00412000u;
        h[i++] = rd[0]; h[i++] = rd[1]; h[i++] = rd[2]; h[i++] = rd[3]; h[i++] = rd[5];
    }
    {   /* [32..] the window since the previous capture (round 10): its A/B mode, renders,
         * length and the GL phases inside it, all in User::NTickCount ticks; then the
         * clocks now (NTickCount, TickCount, HomeTime us) so captures calibrate the
         * NTickCount period; the read-format probe; flat/additive/dual counts (totals). */
        volatile uint32_t *pf = A4_PERF;
        uint32_t now = a4_ntick();
        h[i++] = pf[PF_MODE];   h[i++] = pf[PF_RENDERS]; h[i++] = now - pf[PF_T0];
        h[i++] = pf[PF_COMP];   h[i++] = pf[PF_READ];    h[i++] = pf[PF_DRAW];
        /* [38] was this window's glFlush ticks (AB_FLUSH, unused since round 12); from
         * round 14 the textures remade in place (total: the descriptor held another) */
#ifdef A4_MESH
        h[i++] = pf[PF_MESH];   /* [38] in a mesh build: this window's list triangles drawn */
#else
        h[i++] = pf[PF_TEXRE];
#endif
        h[i++] = pf[PF_CALLS];  h[i++] = pf[PF_TRIS];
        h[i++] = now;           h[i++] = fp[1];  /* a4_fps last_tick (User::TickCount) */
        /* [43] was HomeTime's low word (calibrated NTickCount = 1 ms, round 10); from round
         * 15 the triangles to a second 3D target in a frame (total) */
#ifdef A4_MESH
        h[i++] = pf[PF_MDRAW];  /* [43] in a mesh build: this window's ticks drawing the list */
#else
        h[i++] = pf[PF_SURF];
#endif
        /* [46] was the read-format probe's GL error (0 on the N95, round 10); from round
         * 14 the cache entries evicted (total) */
        /* [44] [45] were the read format and type (BGRA, UNSIGNED_BYTE on the N95, rounds
         * 10-17); from round 18 this window's re-presents skipped and presents made */
#ifdef A4_MESH
        h[i++] = pf[PF_SKIPPED]; h[i++] = pf[PF_NPRES]; h[i++] = pf[PF_MKEYS];   /* [46]: the list's draw calls */
#else
        h[i++] = pf[PF_SKIPPED]; h[i++] = pf[PF_NPRES]; h[i++] = pf[PF_EVICT];
#endif
        /* [49] was the dual frames (PF_DUAL: none since the composite is immediate,
         * A4_IMM); from round 13 the solid fills drawn on the GPU (PF_FILL) */
        h[i++] = pf[PF_FLAT];   h[i++] = pf[PF_ADD];     h[i++] = pf[PF_FILL];
        h[i++] = pf[PF_SHAD];
        h[i++] = a4_gl_rdinfo(3); h[i++] = a4_gl_rdinfo(4);   /* pbuffer config, depth bits */
        h[i++] = pf[PF_M1BAKE];                                /* mode-1 re-bakes (total) */
        h[i++] = a4_gl_rdinfo(5);                              /* textures in the cache */
        h[i++] = pf[PF_M1TEX]; h[i++] = pf[PF_M1T];            /* this window: texels re-baked, ticks */
        /* [57] was the stencil bits (0 on the N95, rounds 10-17); from round 18 this
         * window's ticks grouping and copying the batch (A4_REORDER) */
        h[i++] = pf[PF_REO];
        h[i++] = pf[PF_AFFINE];                                /* flags-1 triangles on the game's affine fallback (total) */
        /* [59] was the alpha bits (8 on the N95, rounds 10-17); from round 18 this
         * window's draw calls before the reorder (PF_RUNS0; [39] is after) */
        h[i++] = pf[PF_RUNS0];
#ifdef A4_MESHCHECK
        h[i++] = pf[60]; h[i++] = pf[61]; h[i++] = pf[62];     /* check: fallback verdicts compared, differing, fallbacks */
#else
        h[i++] = pf[PF_LATE];                                  /* triangles after the frame's composite (total) */
        h[i++] = pf[PF_BB];                                    /* billboards queued on the GPU (total) */
        h[i++] = pf[PF_BBSW];                                  /* billboards left to software (total) */
#endif
#ifdef A4_STEPTIME
        /* [60..63] in a step-timed build (round 30, not with A4_MESHCHECK): this window's logic
         * ticks (the race update, step count to render call), steps, spans, +12D5A ticks */
        i = 60;
        h[i++] = pf[PF_TLOGIC]; h[i++] = pf[PF_NSTEP]; h[i++] = pf[PF_NSPAN];
#ifdef A4_HALFSPEC
        /* [63] in a half-step build (round 32): the speculation's ticks this window (low half)
         * and its longest single run (high half), in place of +12D5A's ticks (0 in every
         * round since 30) */
        h[i++] = (pf[PF_TSPEC] & 0xFFFFu) | ((pf[PF_MSPEC] > 0xFFFFu ? 0xFFFFu : pf[PF_MSPEC]) << 16);
#else
        h[i++] = pf[PF_TCAM];
#endif
#else
        h[i++] = pf[PF_BB2];                                   /* calls of the two-depth billboard blit (total) */
#endif
    }
    while (i < TEL_HDR_WORDS) h[i++] = 0;

    void *f = &t->rfile[t->caps % TEL_RING];
    struct des hd = { DES(sizeof h), h };
    t->dbg_wrc = efs_writep(f, 0, &hd);
    /* a4_fps_state block (fps + the 128-frame delta ring): quantifies choppiness
     * over the preceding ~128 frames, even when the framebuffer is a paused one. */
    struct des pd = { DES(FPS_BYTES), (const void *)A4_FPS_SCRATCH };
    efs_writep(f, (int)sizeof h, &pd);
    if (bb >= 0x00080000u && bb < 0x80000000u) {
        struct des fd = { DES(FB_BYTES), (const void *)bb };
        efs_writep(f, (int)sizeof h + (int)FPS_BYTES, &fd);
    }
#ifdef A4_DUALCAP
    a4_gl_dumpgpu(dual_wr, (int)sizeof h + (int)FPS_BYTES + (int)FB_BYTES);
#ifdef A4_BBRECTS
    {   struct des rd = { DES(125u * 4u), (const void *)0x00412E00u };   /* after the GPU picture */
        efs_writep(f, (int)sizeof h + (int)FPS_BYTES + 2 * (int)FB_BYTES, &rd); }
#endif
#if defined(A4_MESHDUMP)
    {   extern void a4_gl_meshdump(void (*wr)(int, const void *, int), int base);
        a4_gl_meshdump(dual_wr, (int)sizeof h + (int)FPS_BYTES + 2 * (int)FB_BYTES + 125 * 4 + 128 * 4); }
#endif
#if defined(A4_MESHSTATE) || defined(A4_BBSINK) || defined(A4_SHADSINK)
    {   struct des sd = { DES(128u * 4u), (const void *)0x00454000u };   /* a4_gl.c MESH_SC and the sink counters, after the rectangles */
        efs_writep(f, (int)sizeof h + (int)FPS_BYTES + 2 * (int)FB_BYTES + 125 * 4, &sd); }
#endif
#endif
#ifdef A4_ALPHAHIST
    {   struct des ad = { DES(33u * 4u), (const void *)0x00412D00u };
        efs_writep(f, (int)sizeof h + (int)FPS_BYTES + (int)FB_BYTES, &ad); }
#endif
#ifdef A4_M1PROBE
    {   struct des md = { DES(33u * 4u), (const void *)0x00412E00u };
        efs_writep(f, (int)sizeof h + (int)FPS_BYTES + (int)FB_BYTES, &md); }
#endif
#if defined(A4_FLAGHIST) || defined(A4_SWHIST) || defined(A4_BBPROBE) || defined(A4_REMADEPROBE)
    {   /* the (flags, mode) census, a4_gl.c 0x412D00, 122 words, after the framebuffer */
        struct des cd = { DES(122u * 4u), (const void *)0x00412D00u };
        efs_writep(f, (int)sizeof h + (int)FPS_BYTES + (int)FB_BYTES, &cd);
    }
#endif
#ifdef A4_AGG
#if !(defined(A4_FLAGHIST) || defined(A4_SWHIST) || defined(A4_BBPROBE) || defined(A4_REMADEPROBE))
#error "A4_AGG is written after the census tail: build it with A4_BBPROBE"
#endif
    {   /* round 19: this window into its mode's sums (a window of loads and pauses, 0.5 s a
         * render or more, left out), then the three segments after the census tail */
        volatile uint32_t *pf = A4_PERF;
        uint32_t r = pf[PF_RENDERS], w = a4_ntick() - pf[PF_T0];
        if (r && w < r * 500u) {
            uint32_t m = pf[PF_MODE];                /* a4_gl.c AGG_SLOT */
#if defined(A4_ABCYCLE11)
            volatile uint32_t *a = (volatile uint32_t *)0x00421000u + ((m >> 12) & 3u) * 32u;
#elif defined(A4_ABCYCLE10)
            volatile uint32_t *a = (volatile uint32_t *)0x00421000u + ((m >> 10) & 2u) * 32u;
#elif defined(A4_ABCYCLE9)
            volatile uint32_t *a = (volatile uint32_t *)0x00421000u + ((m >> 9) & 2u) * 32u;
#elif defined(A4_ABCYCLE8)
            volatile uint32_t *a = (volatile uint32_t *)0x00421000u + ((m >> 8) & 2u) * 32u;
#elif defined(A4_ABCYCLE7)
            volatile uint32_t *a = (volatile uint32_t *)0x00421000u + (((m >> 7) & 1u) | ((m >> 7) & 2u)) * 32u;
#elif defined(A4_ABCYCLE6)
            volatile uint32_t *a = (volatile uint32_t *)0x00421000u + (((m >> 2) & 1u) | ((m >> 5) & 2u)) * 32u;
#else
            volatile uint32_t *a = (volatile uint32_t *)0x00421000u + (((m >> 2) & 1u) | (((m >> 3) | (m >> 4)) & 2u)) * 32u;
#endif
            a[0] += r; a[1] += w; a[2] += pf[PF_T3D]; a[3] += pf[PF_HOOK]; a[4] += pf[PF_COMP];
            a[5] += pf[PF_READ]; a[6] += pf[PF_DRAW]; a[7] += pf[PF_T2D]; a[8] += pf[PF_TPRES];
            a[9] += pf[PF_REO]; a[10] += pf[PF_CALLS]; a[11] += pf[PF_RUNS0]; a[12] += pf[PF_TRIS];
            a[13] += pf[PF_NPRES]; a[14] += pf[PF_SKIPPED]; a[15]++;
        }
#if defined(A4_ABCYCLE11)
        *(volatile uint32_t *)0x00421600u = 0x41473237u;   /* 'AG27': slot bit 0 AB_PX6, bit 1 AB_PXPLD */
#elif defined(A4_ABCYCLE10)
        *(volatile uint32_t *)0x00421600u = 0x41473236u;   /* 'AG26': slot bit 1 AB_HSPEC */
#elif defined(A4_ABCYCLE9)
        *(volatile uint32_t *)0x00421600u = 0x41473235u;   /* 'AG25': slot bit 1 AB_MESH */
#elif defined(A4_ABCYCLE8)
        *(volatile uint32_t *)0x00421600u = 0x41473234u;   /* 'AG24': slot bit 1 AB_ATLAS */
#elif defined(A4_ABCYCLE7)
        *(volatile uint32_t *)0x00421600u = 0x41473233u;   /* 'AG23': slot bit 0 AB_PLD, bit 1 AB_BIGSTRIP */
#elif defined(A4_ABCYCLE6)
        *(volatile uint32_t *)0x00421600u = 0x41473232u;   /* 'AG22': slot bit 1 is AB_DEFER */
#elif defined(A4_ABCYCLE5)
        *(volatile uint32_t *)0x00421600u = 0x41473231u;   /* 'AG21': slot bit 1 is AB_LEAN */
#else
        *(volatile uint32_t *)0x00421600u = 0x41473230u;   /* 'AG20': slot bit 1 is AB_INDEXED */
#endif
#ifdef A4_STEPTIME
        /* the aggregates, their format word, then the step block (a4_gl.c A4_GL_STEP, 0x421700) */
        struct des ad = { DES(0x700u + 3u * 32u * 4u), (const void *)0x00421000u };
#else
        struct des ad = { DES(3u * 4u * 32u * 4u + 4u), (const void *)0x00421000u };
#endif
        efs_writep(f, (int)sizeof h + (int)FPS_BYTES + (int)FB_BYTES + 122 * 4, &ad);
#if defined(A4_SLOWREC) && defined(A4_STEPTIME)
        {   /* round 34: the slowest frames and the lossy ones (a4_gl.c A4_GL_SLOW), after the step block */
            struct des sd = { DES(0x680u), (const void *)0x00455800u };
            efs_writep(f, (int)sizeof h + (int)FPS_BYTES + (int)FB_BYTES + 122 * 4 + 0x700 + 3 * 32 * 4, &sd);
        }
#endif
#if defined(A4_FRAMEREC) && defined(A4_SLOWREC) && defined(A4_STEPTIME)
        {   /* round 41: the frame record (a4_gl.c A4_GL_FREC, 0x456000), after the slow block */
            struct des rd = { DES(16u + 256u * 32u), (const void *)0x00456000u };
            efs_writep(f, (int)sizeof h + (int)FPS_BYTES + (int)FB_BYTES + 122 * 4 + 0x700 + 3 * 32 * 4 + 0x680, &rd);
        }
#if defined(A4_KINEDRAW) && !defined(A4_INTERP)
        {   /* engine map 1: FRC2, the heights (a4_gl.c A4_GL_FREC2, 0x45C000), after the frame record */
            struct des r2 = { DES(16u + 255u * 16u), (const void *)0x0045C000u };
            efs_writep(f, (int)sizeof h + (int)FPS_BYTES + (int)FB_BYTES + 122 * 4 + 0x700 + 3 * 32 * 4 + 0x680
                          + 16 + 256 * 32, &r2);
        }
#endif
#endif
        pf[PF_FLAGS] |= 4u;                       /* PFF_WROTE: the next render interval holds this write */
    }
#endif
    efs_flush(f);
    t->caps++;
    {   /* next window: reset the timers after the write, so its cost is in no window */
        volatile uint32_t *pf = A4_PERF;
#if defined(A4_ABCYCLE11)
        /* round 50: AB_PX6 (4096) x AB_PXPLD (8192), one pair a window; native, skip, reorder, lean, list kept
         * (slot 2, the prefetch without the ARMv6 loops, is the C loops again: a second baseline) */
        pf[PF_MODE] = 2u | 4u | 8u | 32u | 1024u | ((((pf[PF_MODE] >> 12) + 1u) & 3u) << 12);
#elif defined(A4_ABCYCLE10)
        /* round 31: AB_HSPEC (2048) alternates; native, skip, reorder, lean, list kept */
        pf[PF_MODE] = 2u | 4u | 8u | 32u | 1024u | ((pf[PF_MODE] & 2048u) ^ 2048u);
#elif defined(A4_ABCYCLE9)
        /* round 26: AB_MESH (1024) alternates; native, skip, reorder, lean kept */
        pf[PF_MODE] = 2u | 4u | 8u | 32u | ((pf[PF_MODE] & 1024u) ^ 1024u);
#elif defined(A4_ABCYCLE8)
        /* round 24: AB_ATLAS (512) alternates; native, skip, reorder, lean kept */
        pf[PF_MODE] = 2u | 4u | 8u | 32u | ((pf[PF_MODE] & 512u) ^ 512u);
#elif defined(A4_ABCYCLE7)
        /* round 23: AB_PLD (128) x AB_BIGSTRIP (256), one pair a window; native, skip, reorder, lean kept */
        pf[PF_MODE] = 2u | 4u | 8u | 32u | ((((pf[PF_MODE] >> 7) + 1u) & 3u) << 7);
#elif defined(A4_ABCYCLE6)
        /* round 22: AB_DEFER (64) alternates; native, skip, reorder and lean kept */
        pf[PF_MODE] = 2u | 4u | 8u | 32u | ((pf[PF_MODE] & 64u) ^ 64u);
#elif defined(A4_ABCYCLE5)
        /* round 21: AB_LEAN (32) alternates; native, skip and reorder kept */
        pf[PF_MODE] = 2u | 4u | 8u | ((pf[PF_MODE] & 32u) ^ 32u);
#elif defined(A4_ABCYCLE4)
        /* round 20: AB_INDEXED (16) alternates; native, skip and reorder kept */
        pf[PF_MODE] = 2u | 4u | 8u | ((pf[PF_MODE] & 16u) ^ 16u);
#elif defined(A4_ABCYCLE3)
        /* round 19: AB_SKIPREP (4) alternates, AB_NATIVE and AB_REORDER (8) kept */
        pf[PF_MODE] = 2u | 8u | ((pf[PF_MODE] & 4u) ^ 4u);
#elif defined(A4_ABCYCLE2)
        /* round 18: AB_SKIPREP (4) x AB_REORDER (8), one pair a window, AB_NATIVE kept */
        pf[PF_MODE] = 2u | ((((pf[PF_MODE] >> 2) + 1u) & 3u) << 2);
#elif defined(A4_ABCYCLE)
        pf[PF_MODE] = (pf[PF_MODE] + 1u) & 3u;    /* AB_FLUSH x AB_565, one mode a window */
#endif
        pf[PF_RENDERS] = 0; pf[PF_COMP] = 0; pf[PF_READ] = 0; pf[PF_DRAW] = 0;
        pf[PF_GLFLUSH] = 0; pf[PF_CALLS] = 0; pf[PF_TRIS] = 0; pf[PF_M1TEX] = 0; pf[PF_M1T] = 0;
        pf[PF_T3D] = 0; pf[PF_TFLUSH] = 0; pf[PF_T2D] = 0; pf[PF_TPRES] = 0; pf[PF_HOOK] = 0;
        pf[PF_REO] = 0; pf[PF_RUNS0] = 0; pf[PF_SKIPPED] = 0; pf[PF_NPRES] = 0;
        pf[PF_MESH] = 0; pf[PF_MKEYS] = 0; pf[PF_MDRAW] = 0;
        pf[PF_TLOGIC] = 0; pf[PF_NSTEP] = 0; pf[PF_NSPAN] = 0; pf[PF_TCAM] = 0; pf[PF_TSPEC] = 0; pf[PF_MSPEC] = 0;
        pf[PF_T0] = a4_ntick();
    }
}

#else
void a4_tel_tick(uint32_t had) { (void)had; }
#endif /* A4_TEL */
