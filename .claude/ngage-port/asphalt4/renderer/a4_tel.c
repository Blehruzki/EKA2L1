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
#define EF(i) (a4_efs_iat_ptr()[i])

#ifndef TEL_EVERY
#define TEL_EVERY 24              /* frames between captures (~1-2 s at race fps) */
#endif
#define TEL_RING  4               /* number of ring files C:\a4tel0..3.dat */
#define A4_GL_SCRATCH 0x00401000u
#define A4_GL_BB      0x00400700u /* holds the game framebuffer pointer (cache hook) */
#define A4_TEL_SCRATCH 0x00400800u
#define VP_W 320
#define VP_H 240
#define FB_STRIDE 320
#define FB_BYTES  ((uint32_t)VP_W * VP_H * 2u)   /* 150 KB: blit-proven mapped */
#define TEL_MAGIC 0x4C546134u     /* "a4TL" */
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
    uint32_t _pad;
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

/* Called once per rendered frame from a4_gl_flush. */
void a4_tel_tick(void)
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
    if ((t->fcount % TEL_EVERY) != 0) return;

    volatile uint32_t *g = (volatile uint32_t *)A4_GL_SCRATCH;
    uint32_t bb = *(volatile uint32_t *)A4_GL_BB;

    /* header: magic, version, geometry, then the glr counters we read on the
     * bench via MEMSNAP (same field order). */
    uint32_t h[32];
    int i = 0;
    h[i++] = TEL_MAGIC;
    h[i++] = 2;                       /* version */
    h[i++] = t->caps;                 /* capture index */
    h[i++] = t->fcount;
    h[i++] = VP_W; h[i++] = VP_H; h[i++] = FB_STRIDE; h[i++] = FB_BYTES;
    h[i++] = bb;
    h[i++] = g[0];  /* up */        h[i++] = g[1];  /* frames */
    h[i++] = g[2];  /* prims */     h[i++] = g[3];  /* errc */
    h[i++] = g[4];  /* ntex */      h[i++] = g[7];  /* bstep */
    h[i++] = g[8];  /* last_prims */h[i++] = g[17]; /* winstep */
    h[i++] = g[18]; /* wvis */      h[i++] = g[19]; /* idle2d */
    h[i++] = g[11]; /* miny */      h[i++] = g[12]; /* maxy */
    h[i++] = g[13]; /* pminy */     h[i++] = g[14]; /* pmaxy */
    while (i < 32) h[i++] = 0;

    void *f = &t->rfile[t->caps % TEL_RING];
    struct des hd = { DES(sizeof h), h };
    t->dbg_wrc = efs_writep(f, 0, &hd);
    if (bb >= 0x00080000u && bb < 0x80000000u) {
        struct des fd = { DES(FB_BYTES), (const void *)bb };
        efs_writep(f, (int)sizeof h, &fd);
    }
    efs_flush(f);
    t->caps++;
}

#else
void a4_tel_tick(void) {}
#endif /* A4_TEL */
