/* a4_gl.c -- MBX stage 6: render the frame's textured 3D on the GPU, RUN-LENGTH
 * batched, no depth buffer.
 *
 * Stage 4 drew each primitive the instant a4_prim.c forwarded it (+0x300B2):
 * bindtex + two pointer sets + glDrawArrays(3), ~490 times a frame. Stage 5
 * sorted the whole frame by texture and used one glDrawElements per texture with
 * a depth buffer to undo the reorder -- but the real MBX renders the depth-sorted
 * scene with wrong occlusion (the road/floor drops out), a divergence the bench's
 * HLE GL never showed.
 *
 * Stage 6 does not reorder anything. It accumulates each triangle (pos x,y +
 * texcoord u,v) in arrival order and, at the 3D->2D boundary, draws it back in
 * that same order -- merging only *consecutive* same-texture triangles into one
 * glDrawArrays. The pixels this produces are byte-for-byte what the per-triangle
 * path produced (same triangles, same order, last-drawn wins), so it cannot
 * occlude wrongly; it just issues ~2.7x fewer GL calls (one per texture *run*,
 * not per triangle). No depth buffer, no sort, nothing for the MBX to get wrong.
 *
 * Round 10: stage 5's phone failure was never diagnosed (whether config 6 even has a
 * depth buffer was not known), and depth is in the game itself: its perspective span
 * (0x333f4, the flags-1 class) DOES depth-test, against a per-frame 1/z buffer; its
 * other spans do not touch it. A4_ZBUF reproduces exactly that: depth = min(ZK/ZW, 1)
 * on the flags-1 triangles, GL_GEQUAL, cleared to 0 each frame, every other class
 * drawn untested in arrival order. Every class a race uses is now taken (KIND_*), so
 * nothing software lands on top of the GPU image out of order.
 *
 * The composite (readback + sentinel-masked blit) is unchanged; on device it is
 * a zero-copy pixmap and A4_NO_BLIT skips the readback for CPU measurement.
 *
 * Function pointers: a4_gl_iat[] (a4_gl.s), filled by the libGLES_CM import. The
 * depth/eglChooseConfig slots [26..29] are left imported but unused here.
 */
#include <stdint.h>

#define A4_GL_SCRATCH 0x00401000u
#define A4_GL_READBUF 0x00403000u        /* one strip: 320*STRIP*4, ends at 0x408000 */
#define A4_GL_BB      0x00400700u        /* the 3D target's pixels: ctx[0x4c] at each frame's first
                                          * triangle (and a4_cachebb_tramp, in the software span) */
#ifndef STRIP
#define STRIP 16                         /* readback strip height (keeps image .bss small);
                                          * -DSTRIP=n overrides, for the per-call-overhead test */
#endif
#define A4_GL_MAGIC   0x6C900DE6u
#define VP_W 320
#define VP_H 240
#define FB_STRIDE VP_W                   /* 320x240 render surface: stride = width (320) */
#ifndef CLIP_TOP
#define CLIP_TOP 11                      /* top rows are the HUD banner, not the 3D viewport
                                          * (the frame's scene fill is x0 y11 w320 h229) --
                                          * the blit must not touch them or GPU scene pixels
                                          * leak into the position/lap/WANTED strip.
                                          * That was when the composite ran at the frame
                                          * end, over the HUD; it now runs at the frame's
                                          * first GPU triangle, before the HUD, and these
                                          * rows show the sky backdrop in a tunnel (the
                                          * "blue banner"). -DCLIP_TOP=0 composites them. */
#endif
#ifndef NCACHE
#define NCACHE 256
#endif
/* The texel pointer in a texture descriptor: +0x10, where every span filler reads it
 * (0x333f4, 0x33964, 0x33d18, 0x33ea8). +0x0C, used until round 10, equals it on every
 * texture the earlier classes draw (A4_TEXPROBE: 144 of 144) but is 0 on the 0x33ea8
 * (alpha-map) textures, which then fell to software. */
#define TS_PIX 4
#ifndef START_AFTER
#define START_AFTER 50000u               /* skip the intro video; reachable on the bench */
#endif

/* Batch buffers, immediately after the readback strip (0x408000). Triangles in
 * arrival order: per vertex a position + UVC int32 texcoords (GL_FIXED), 3 verts a
 * triangle; one uint32 GL texture name a triangle (for run detection). MAXT is the
 * BATCH size, not a frame limit: when the batch fills it is drawn and refilled, so a
 * frame may have any number of triangles. (It used to be a frame cap whose overflow
 * triangles were drawn and then erased by the frame-end clear -- the road "popping
 * out" whenever a frame passed 640 triangles; MBX.md.) Sizes must fit under
 * GL_SCRATCH + GL_SCRATCH_SIZE in injector.py (top 0x413000). */
#ifdef A4_HPERSP
/* A4_HPERSP: perspective-correct texturing through the VERTEX w. Positions are
 * homogeneous GL_FIXED (x*W, y*W, 0, W) with W proportional to ZW (vptr[2], the
 * vertex w: it grows with distance, MBX.md "Measured the perspective input"); the GPU
 * divides the position back to the same screen (x,y) and interpolates the plain
 * (U,V) texcoords with 1/W weighting -- what every 3D GPU does natively. It does not
 * depend on the texcoord q being divided per fragment, which A4_PERSP needs (the bench
 * only does that with EKA2L1_TEXPROJ, and the N95's MBX evidently does not). */
#ifndef MAXT
#define MAXT 512
#endif
#define POSC 4u                         /* int32 (GL_FIXED) x*W, y*W, 0, W */
#define POSB 4u
#define UVC 2u
#else
#ifndef MAXT
#define MAXT 640
#endif
#define POSC 2u                         /* int16 (GL_SHORT) x, y */
#define POSB 2u
/* Texcoord components per vertex: 2 (s,t) affine, or 4 (s,t,0,q) projective
 * (A4_PERSP). GL ES 1.1 samples at (s/q, t/q), so interpolating (u/w, v/w, 1/w) and
 * dividing PER FRAGMENT is perspective-correct -- only where the GL does divide per
 * fragment (the bench with EKA2L1_TEXPROJ). q = 1/ZW; ZW (vptr[2]) is the vertex w. */
#ifdef A4_PERSP
#define UVC 4u
#else
#define UVC 2u
#endif
#endif
#define A4_GL_POS   0x00408000u
#define A4_GL_UV    (A4_GL_POS + (3u*MAXT*POSC*POSB))
#define A4_GL_TEXID (A4_GL_UV  + (3u*MAXT*UVC*4u))    /* uint32 * MAXT */

/* EGL */
#define EGL_WIDTH 0x3057
#define EGL_HEIGHT 0x3056
#define EGL_NONE 0x3038
#define EGL_SURFACE_TYPE 0x3033
#define EGL_PIXMAP_BIT 0x0002            /* probe: does the device offer a pixmap config? */
#define GLES_CONFIG 6u
#ifdef A4_WINDOW
/* The GPU presents through an EGL window surface. The native window must live
 * in the app's OWN window-server session, because libGLES_CM resolves the
 * session internally via CCoeEnv::Static()->WsSession(); a window in a second
 * session is an invalid handle to it (the device's WSERV 6). So we take the
 * app's session and root group from CCoeEnv (cone Static/RootWin) and create
 * our RWindow as a child of the app's root group, in the app's session -- the
 * RWindow object is ours (in scratch), the session and buffer are the app's. */
#define EGL_WINDOW_BIT 0x0004
#define EGL_BUFFER_SIZE 0x3020
#define EGL_ALPHA_SIZE 0x3021
#define EGL_BLUE_SIZE  0x3022
#define EGL_GREEN_SIZE 0x3023
#define EGL_RED_SIZE   0x3024
#define ECOLOR64K 7                      /* TDisplayMode: the game's 565 surface */
#define A4_WS_OBJ 0x00402800u            /* our RWindow object (MWsClientClass + TRect) */
#define A4_CTRL_OBJ 0x00402840u          /* our crafted CCoeControl (client handle); free
                                          * gap between the window object and the readback
                                          * strip (0x403000). iFlags@+0x1C, iWin@+0x20. */
#endif
/* GL enums */
#define GL_TRIANGLES 0x0004
#define GL_FLAT 0x1D00
#define GL_FIXED 0x140C
#define GL_SHORT 0x1402
#define GL_UNSIGNED_BYTE 0x1401
#define GL_UNSIGNED_SHORT 0x1403
#define GL_UNSIGNED_SHORT_5_6_5 0x8363
#define GL_UNSIGNED_SHORT_5_5_5_1 0x8034
#define GL_COLOR_BUFFER_BIT 0x4000
#define GL_ALPHA_TEST 0x0BC0
#define GL_GREATER 0x0204
#define GL_RGB 0x1907
#define GL_RGBA 0x1908
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_NEAREST 0x2600
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_REPEAT 0x2901
#define GL_VERTEX_ARRAY 0x8074
#define GL_TEXTURE_COORD_ARRAY 0x8078
#define GL_PROJECTION 0x1701
#define GL_MODELVIEW 0x1700
#define GL_TEXTURE_ENV 0x2300
#define GL_TEXTURE_ENV_MODE 0x2200
#define GL_REPLACE 0x1E01
#define GL_BLEND 0x0BE2
#define GL_ONE 1
#define GL_IMPLEMENTATION_COLOR_READ_TYPE_OES   0x8B9A
#define GL_IMPLEMENTATION_COLOR_READ_FORMAT_OES 0x8B9B
#define FX 0x10000

/* What a batched triangle is drawn as: the top nibble of its TEXID word (the low
 * bits are the GL texture name, or for a flat triangle its RGB565 colour). A run is
 * a stretch of equal words, so a kind change also starts a new glDrawArrays. */
#define KIND_TEX  0u     /* textured, colour key (flags 1 and the default rasterizer) */
#define KIND_FLAT 1u     /* flat colour, opaque (flags 2: 0x2fbdc -> span 0x33b46) */
#define KIND_ADD  2u     /* textured, saturating add (flags 8: 0x2f358 -> span 0x33d18) */
#define KIND_SHAD 3u     /* untextured darken, dst*12/16 (flags 0x20: 0x2ecb4 -> 0x33f8c) */
#define KIND_TEXZ 4u     /* KIND_TEX, depth-tested and depth-writing (flags 1 under A4_ZBUF) */
#define KIND_ALPHA 5u    /* textured, per-texel 4-bit alpha blend (flags 0x100: 0x2e56c -> 0x33ea8) */
#define KIND_ADDF 6u     /* KIND_ADD at a/16 (the billboard blit's mode 3); a in bits 20..27 */
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_DEPTH_TEST 0x0B71
#define GL_DEPTH_BUFFER_BIT 0x0100
#define GL_GEQUAL 0x0206
#define GL_DEPTH_BITS 0x0D56
#define GL_STENCIL_BITS 0x0D57
#define GL_ALPHA_BITS 0x0D55
#define GL_BGRA 0x80E1                   /* IMG/EXT read_format_bgra: the N95's own read pair */
#define EGL_ALPHA_SIZE_Z 0x3021
#define GL_STENCIL_TEST 0x0B90
#define GL_STENCIL_BUFFER_BIT 0x0400
#define GL_EQUAL 0x0202
#define GL_KEEP 0x1E00
#define GL_INCR 0x1E02
#define EGL_STENCIL_SIZE 0x3026
#define EGL_PBUFFER_BIT 0x0001
#define EGL_DEPTH_SIZE 0x3025
#ifndef EGL_RED_SIZE                     /* (A4_WINDOW defines these too) */
#define EGL_BLUE_SIZE  0x3022
#define EGL_GREEN_SIZE 0x3023
#define EGL_RED_SIZE   0x3024
#endif
#ifndef ZK
#define ZK 128           /* depth = min(ZK/ZW, 1); near road vertices sit at ZW ~200 */
#endif
#define GL_ZERO 0
#define GL_SRC_ALPHA 0x0302
#define GL_SRC_COLOR 0x0300
#define GL_DST_ALPHA 0x0304
#define GL_MODULATE 0x2100
#define GL_NOTEQUAL 0x0205

/* GL phase timers and the per-window A/B mode (round 10). a4_tel.c writes them into
 * each capture's header and starts the next window; ticks are User::NTickCount. */
#define A4_PERF ((volatile uint32_t *)0x00412C00u)
enum { PF_MODE, PF_RENDERS, PF_T0, PF_COMP, PF_READ, PF_DRAW, PF_GLFLUSH,
       PF_CALLS, PF_TRIS, PF_FLAT, PF_ADD, PF_DUAL, PF_SHAD, PF_M1BAKE, PF_M1TEX, PF_M1T, PF_AFFINE, PF_LATE,
       PF_BB, PF_BBSW, PF_BB2, PF_FILL, PF_TEXRE, PF_EVICT, PF_SURF,
       /* round 16 phase timers (this window, nticks): the 3D pass (first triangle to the
        * composite), the composite (the last batch, the read, the blit), the 2D after it
        * (to the present), the game's present call (+1EC: the runtime's convert and post),
        * and our draw_primitive hook (every 4th call timed, x4); then their marks */
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
#define PFF_2D 1u        /* a composite ended this frame and its 2D is being timed */
#define PFF_PRES 2u      /* the game's present call is being timed */
#define PFF_WROTE 4u     /* a4_tel.c wrote a capture since the last render began */
#define PFF_LOGIC 8u     /* A4_STEPTIME: a logic span is open (+15B1C returned, +12992 not yet called) */
_Static_assert(PF_N <= 60, "A4_PERF holds 64 words (0x412C00-0x412D00); 60-63 are A4_MESHCHECK's");
/* integer square root, floor (round 40: the camera's distance from the car) */
static uint32_t __attribute__((unused)) isqrt64(int64_t v)
{
    if (v <= 0) return 0;
    uint64_t x = (uint64_t)v, r = 0, b = (uint64_t)1 << 62;
    while (b > x) b >>= 2;
    while (b) { if (x >= r + b) { x -= r + b; r = (r >> 1) + b; } else r >>= 1; b >>= 2; }
    return (uint32_t)r;
}
#if defined(A4_SPECCAMFOLLOW) && !defined(A4_DROPHOLD)
#error "A4_SPECCAMFOLLOW reads the chase camera's running average A4_DROPHOLD keeps"
#endif
#if defined(A4_DROPHOLD) && !defined(A4_SKIPREPRES)
#error "A4_DROPHOLD holds a frame back in the A4_SKIPREPRES present path"
#endif
#define AB_FLUSH 1u      /* glFlush after the frame's last batch (start the tile render) */
#define AB_NATIVE 2u     /* read back in the implementation's own pair (OES_read_format): BGRA
                          * on the N95 (round 10), RGB565 on a device that names that */
#define AB_SKIPREP 4u    /* skip the race loop's two re-presents (A4_SKIPREPRES, round 18) */
#define AB_REORDER 8u    /* group the batch into fewer draw calls (A4_REORDER, round 18) */
#define AB_INDEXED 16u   /* draw the groups through an index list instead of a copy (round 20) */
#define AB_LEAN 32u      /* the hook's lean path (lean_tail, round 21) */
#define AB_DEFER 64u     /* the hook only records the triangle; the batch converts it (round 22) */
#define AB_PLD 128u      /* prefetch the triangle's game data at the hook's entry (round 23) */
#define AB_BIGSTRIP 256u /* read back in 24-row strips through the reorder's buffer (round 23) */
#define AB_ATLAS 512u    /* in-range triangles of small textures drawn from two shared atlases (round 24) */
#ifndef A4_ABMODE
#define A4_ABMODE 0u     /* the mode when nothing cycles it (no A4_ABCYCLE) */
#endif
extern uint32_t a4_ntick(void);         /* a4_gl.s: User::NTickCount's fast exec */

extern volatile uint32_t *a4_gl_iat_ptr(void);
#ifdef A4_TEL
extern void a4_tel_tick(uint32_t had);   /* a4_tel.c: on-device telemetry/capture */
#endif

struct glr {
    uint32_t up, frames, prims, errc, ntex, seen;
    uint32_t flushed;                 /* GPU world already composited into this frame */
    uint32_t bstep;                   /* furthest EGL/GL bring-up step reached (diag) */
    uint32_t last_prims;              /* prims of the last completed frame (device probe) */
    uint32_t depthcfg;                /* unused in stage 6 (no depth); kept for the probe */
    uint32_t pixmapcap;               /* 1 = device offers a pixmap-capable EGL config */
    uint32_t miny, maxy;              /* screen-row extent the GPU drew this frame (top-down) */
    uint32_t pminy, pmaxy;            /* extent of the PREVIOUS frame -- what the pipelined
                                       * readback composites (it is what the pbuffer holds) */
    uint32_t disp, surf, winstep;     /* A4_WINDOW: display, window surface, how far the window got */
    uint32_t wvis;                    /* A4_WINDOW: our window is visible (hidden while the game is in 2D) */
    uint32_t idle2d;                  /* A4_WINDOW: consecutive present hooks with no 3D primitives */
    uint32_t env, grp, appbuf, appsess; /* A4_WINDOW diag: CCoeEnv, root group, shared RWsBuffer, app session */
    uint32_t appui, istack, stkcount;  /* A4_WINDOW diag: CCoeAppUi, its control stack, #controls on it */
    uint32_t ctrl[8], ctrlwin[8];      /* A4_WINDOW diag: the stack's iControl + each control's iWin (+0x20) */
    uint32_t ctrlh;                    /* A4_WINDOW diag: the CCoeControl* chosen as our window's client handle */
    uint32_t cache_ptr[NCACHE];
    uint32_t cache_id[NCACHE];
    uint32_t cache_tr[NCACHE];         /* 1 = texture is a colorkey (transparent) sprite */
    uint32_t bfill;                    /* triangles in the batch buffers (drawn when MAXT) */
    uint32_t lasthit;                  /* tex_index: cache slot of the previous lookup */
    uint32_t dual;                     /* the first GPU frame: software draws it too (no
                                        * previous GPU image to composite yet) */
    uint32_t rdfmt, rdtype, rderr;     /* OES_read_format probe: the implementation's read
                                        * format/type, and the GL error the probe left */
    uint32_t fmode;                    /* the A/B mode this frame runs in (AB_*) */
    uint32_t zcfg, zbits;              /* A4_ZBUF: the pbuffer config chosen, its depth bits */
    uint32_t sbits;                    /* its stencil bits (0: shadows darken overlaps twice) */
    uint32_t abits;                    /* its alpha bits: >0 -> dst alpha is the drawn-mask (A4_IMM) */
    uint32_t cache_m1r[NCACHE];        /* mode-1 entry: the [lo, hi] of map values it uses */
    uint32_t cache_ck[NCACHE];         /* mode-1 entry: m1_sig of the table at its last bake */
    uint32_t cache_fr[NCACHE];         /* mode-1 entry: the present it was last checked in */
    uint32_t cache_tab[NCACHE];        /* mode-1 entry: the table it was baked from (part of the key) */
    uint32_t rendered;                 /* A4_DROPHOLD: the race made a render since the last real present */
    uint32_t camok;                    /* A4_DROPHOLD: camoff holds the last render's camera offset */
    int32_t camoff[3];                 /* A4_DROPHOLD: the camera less the player car at the last render */
    int32_t goodoff[3];                /* A4_DROPHOLD: the same at the last frame that went out */
    uint32_t droprec;                  /* A4_SLOWREC: 1 + the drop ring entry this frame wrote (0: none) */
    int32_t chaseavg[3];               /* round 40: the camera's distance from the player car on the
                                        * ground [0] and its height [1], running averages x16 (the
                                        * chase camera: ~97% of race renders); [2] unused */
    uint32_t chaseinit;
};

_Static_assert(sizeof(struct glr) <= A4_GL_READBUF - A4_GL_SCRATCH, "glr overlaps the readback strip");

#ifdef A4_MESH
/* The structural step (MBX.md "Structural step, map 2"): the faces the mesh routine
 * +349F8 would insert into the ordering table, taken at the insert (a4_mesh_insert, the
 * BLX at +34ADC) into one list for the frame -- view-space x' y' z' and the face's own
 * texcoord words, GL_SHORT, 6 halfwords a corner -- and drawn at the frame's first
 * triangle with one projection matrix, one draw call a texture, depth-tested and
 * writing. The game's transform and culls run as they did; its projection, ordering
 * table, face draw and our draw_primitive hook do not run for these faces. */
#define AB_MESH 1024u    /* the mesh list (A4_MESH) */
#define AB_HSPEC 2048u   /* round 31: the speculative half-step's frames (A4_HALFSPEC) */
#define GL_CULL_FACE 0x0B44
#define GL_TEXTURE 0x1702
#define GL_DST_COLOR 0x0306
#define GL_ALWAYS 0x0207
#define GL_ONE_MINUS_DST_ALPHA 0x0305
#define A4_GL_MESH 0x00423000u
#ifndef MESH_MAXT
#define MESH_MAXT 2048u
#endif
#define MESH_NK 128u                    /* textures a frame's list may use (key slot 0xFF: dropped) */
#define MESH_GW (VP_W / 16)
#define MESH_GH (VP_H / 16)
struct mesh {
    uint32_t frame;                     /* s->frames the list was gathered in */
    uint32_t n;                         /* triangles in it */
    uint32_t drawn;                     /* drawn (or dropped) this frame: no more taken */
    uint32_t active;                    /* this frame drew mesh triangles: the table's classes depth-test */
    uint32_t cam, F;                    /* the mesh context it was taken under, its focal (cam[1]) */
    uint32_t nk;                        /* keys at the last draw */
    uint32_t culled;                    /* back faces consumed at the insert (total) */
    uint32_t gctx;                      /* the rasterizer context of the last frame's first triangle */
    uint32_t fellback;                  /* perspective faces left to the table by the fallback test (total) */
    uint32_t kid[MESH_NK];              /* a key's GL texture */
    uint16_t kcnt[MESH_NK], koff[MESH_NK];
    uint8_t kws[MESH_NK], khs[MESH_NK]; /* its texcoord shifts */
    uint32_t nearf;                     /* the last frame a TBillboard within MESH_NEAR was drawn (+1) */
    uint16_t qlo, qhi;                  /* round 50: the frame's lowest and highest table bucket (MESH_Q) */
    uint32_t stale;                     /* round 34: lists drawn after a present came between their
                                         * inserts and the walk (dropped before; total) */
    uint16_t grid[MESH_GW * MESH_GH];   /* per 16-pixel tile, the nearest bucket of a drawn list face
                                         * whose box meets it (0xFFFF: none) -- mesh_draw */
};
#define MESH ((volatile struct mesh *)A4_GL_MESH)
/* Per triangle, from the list's base: its corners (36 bytes), texture word, screen box (the
 * game's integer projection, padded as tbox_calc pads), table bucket; at the draw its place
 * in table order, the next of its run, its indices (the bucket counts while sorting), its
 * key slot; then the runs. */
#define MESH_V   ((int16_t *)(A4_GL_MESH + 0x800u))                          /* x y z u v - a corner */
#define MESH_T   ((uint32_t *)(A4_GL_MESH + 0x800u + 36u * MESH_MAXT))       /* texture word | m1 */
#define MESH_B   ((int16_t *)(A4_GL_MESH + 0x800u + 40u * MESH_MAXT))        /* screen box x0 y0 x1 y1 */
#define MESH_Q   ((uint16_t *)(A4_GL_MESH + 0x800u + 48u * MESH_MAXT))       /* table bucket (deepest ZW >> 3) */
#define MESH_ORD ((uint16_t *)(A4_GL_MESH + 0x800u + 50u * MESH_MAXT))       /* table order, back to front */
#define MESH_NX  ((uint16_t *)(A4_GL_MESH + 0x800u + 52u * MESH_MAXT))       /* next in its run */
#define MESH_I   ((uint16_t *)(A4_GL_MESH + 0x800u + 54u * MESH_MAXT))       /* indices, by run (counts first) */
#define MESH_K   ((uint8_t *)(A4_GL_MESH + 0x800u + 60u * MESH_MAXT))        /* key slot (at the draw) */
#ifndef MESH_NR
#define MESH_NR 512u                    /* runs held before they are drawn (a frame's list makes at most ~210
                                         * draw calls in the races benched; 1024 until round 50) */
#endif
#ifndef MESH_NEARF
#define MESH_NEARF 8u                   /* frames the near cut stays on after a near sprite */
#endif
#ifndef MESH_NEAR
#define MESH_NEAR 512                   /* deepest ZW under which a face stays in the table, while a
                                         * near sprite is about (MESH_NEARF) -- r28b had it always on, ~55
                                         * faces a frame, ~3 ms on the phone; 256 lets the flame sink */
#endif
/* a run of the list: its key (texture slot | texz << 8), its faces' ZW range (A4_MESHZ), box, faces */
struct mrun { uint16_t kslot; int16_t zlo; int16_t box[4]; uint16_t head, tail, cnt; int16_t zhi; };
#define MESH_RB  ((A4_GL_MESH + 0x800u + 61u * MESH_MAXT + 3u) & ~3u)        /* word aligned */
#define MESH_R   ((struct mrun *)MESH_RB)
/* round 50: each face's ZW range (its corners' -z', min then max: ZK..32768, so uint16), written at the
 * insert for the run grouping's A4_MESHZ test -- which read the three corners out of MESH_V (45 KB for a
 * full list, past the 32 KB data cache) at the draw */
#define MESH_Z   ((uint16_t *)(MESH_RB + 20u * MESH_NR))
#define MESH_TOP ((MESH_RB + 20u * MESH_NR + 4u * MESH_MAXT + 0xFFFu) & ~0xFFFu)
#define A4_MESH_SCRATCH_TOP 0x00447000u /* injector.py GL_SCRATCH + A4_SCRATCH_SIZE (0x46000) */
_Static_assert(sizeof(struct mrun) == 20u, "run record");
_Static_assert(4096u * 2u <= 6u * MESH_MAXT, "bucket counts in the index area");
_Static_assert(sizeof(struct mesh) <= 0x800u, "mesh header past its block");
_Static_assert(MESH_TOP <= A4_MESH_SCRATCH_TOP, "mesh list past the scratch");
_Static_assert(3u * MESH_MAXT <= 0x10000u, "uint16 indices");
#define MESH_ON() (MESH->active)
#ifdef A4_MESHCHECK
/* Bench (with A4_DUALCAP): the list's faces go to the ordering table as well, so software
 * draws the stock picture; the hook leaves them off the GPU (mesh_has), so the GPU picture
 * of the same frame is the mesh path's. MESH_D: each list triangle's face; MESH_H: an open
 * hash of them (4096 slots, the list at most half fills it), cleared whole each frame. */
#ifndef A4_DUALCAP
#error "A4_MESHCHECK compares against the software picture: build it with A4_DUALCAP"
#endif
#define MESH_D ((uint32_t *)MESH_TOP)
#define MESH_H ((uint32_t *)(MESH_TOP + 4u * MESH_MAXT))
#define MESH_HN 4096u
#define MESH_FH ((uint32_t *)(MESH_TOP + 4u * MESH_MAXT + 4u * MESH_HN))      /* every 0x4001 face seen */
#define MESH_FV ((uint8_t *)(MESH_TOP + 4u * MESH_MAXT + 8u * MESH_HN))       /* 1 + its fallback verdict */
_Static_assert(MESH_TOP + 4u * MESH_MAXT + 9u * MESH_HN <= 0x00452000u, "check tables past the scratch (A4_SCRATCH_SIZE 0x51000)");
#ifdef A4_MESHSTATE
/* bench: does a listed face change between its insert (what the list keeps) and the table's
 * draw of it (what software draws)? The hash slot's list index, and counters written to the
 * capture (a4_tel.c): [0] walk draws of listed faces, [1] texture word differs, [2] a
 * texcoord differs, [3] a vertex's view x'y'z' differs, [4] flags not a listed class, [5]
 * listed faces (all frames), [7] inserts of a face listed already this frame, [8] those at
 * another view position (an instanced mesh), [9] walk draws of a face drawn already, [10]
 * distinct faces listed, [11] distinct faces the walk drew, [12..15] walk draws in span mode
 * 0, 1, 2, 3+ (gctx[0x64]); [16..47] the first 8 cases (face, what | corner << 8, then, now);
 * [48] then 7 of (face | mode, texture) drawn in a mode other than 0 */
#define MESH_HX ((uint16_t *)(MESH_TOP + 4u * MESH_MAXT + 9u * MESH_HN))
#define MESH_SC ((volatile uint32_t *)(MESH_TOP + 4u * MESH_MAXT + 11u * MESH_HN))
_Static_assert(MESH_TOP + 4u * MESH_MAXT + 11u * MESH_HN == 0x00454000u, "a4_tel.c reads MESH_SC at 0x454000");
_Static_assert(MESH_TOP + 4u * MESH_MAXT + 11u * MESH_HN + 256u <= 0x00455000u, "A4_SCRATCH_SIZE 0x54000");
static void mesh_sc_case(uint32_t face, uint32_t what, uint32_t then, uint32_t now)
{
    volatile uint32_t *c = MESH_SC;
    uint32_t k = c[6];
    if (k < 8u) { c[16 + k * 4] = face; c[17 + k * 4] = what; c[18 + k * 4] = then; c[19 + k * 4] = now; c[6] = k + 1u; }
}
#endif
static inline uint32_t mesh_hslot(uint32_t p) { return ((p >> 2) * 2654435761u) >> 20; }
static int mesh_has(uint32_t p)
{
    const uint32_t *H = MESH_H;
    for (uint32_t i = mesh_hslot(p);; i = (i + 1u) & (MESH_HN - 1u)) {
        if (H[i] == p) return 1;
        if (!H[i]) return 0;
    }
}
#endif
#endif

/* sentinel the pbuffer is cleared to each frame: pixels the GPU never drew read
 * back as pure magenta, so the blit can leave them to the software layer. */
#define SENTINEL_R 0xFFu
#define SENTINEL_G 0x00u
#define SENTINEL_B 0xFFu

/* small typed views of the IAT */
#define F(i) (a4_gl_iat_ptr()[i])       /* pc-relative getter; no writable globals */
static int   egl_get_display(int d){ return ((int(*)(int))F(0))(d); }
static int   egl_initialize(int d,int*a,int*b){ return ((int(*)(int,int*,int*))F(1))(d,a,b); }
static int   egl_pbuffer(int d,uint32_t c,int*a) __attribute__((unused));   /* unused in -DA4_WINDOW */
static int   egl_pbuffer(int d,uint32_t c,int*a){ return ((int(*)(int,uint32_t,int*))F(2))(d,c,a); }
static int   egl_context(int d,uint32_t c,int s,int*a){ return ((int(*)(int,uint32_t,int,int*))F(3))(d,c,s,a); }
static int   egl_make_current(int d,int r,int w,int c){ return ((int(*)(int,int,int,int))F(4))(d,r,w,c); }
static void  gl_viewport(int x,int y,int w,int h){ ((void(*)(int,int,int,int))F(5))(x,y,w,h); }
static void  gl_clearcolorx(int r,int g,int b,int a){ ((void(*)(int,int,int,int))F(6))(r,g,b,a); }
static void  gl_clear(uint32_t m){ ((void(*)(uint32_t))F(7))(m); }
static void  gl_finish(void) __attribute__((unused));
static void  gl_finish(void){ ((void(*)(void))F(8))(); }   /* unused since the pipelined readback; kept for A/B builds */
static void  gl_readpixels(int,int,int,int,uint32_t,uint32_t,void*) __attribute__((unused));   /* unused in -DA4_NO_BLIT */
static void  gl_readpixels(int x,int y,int w,int h,uint32_t f,uint32_t t,void*p){ ((void(*)(int,int,int,int,uint32_t,uint32_t,void*))F(9))(x,y,w,h,f,t,p); }
static void  gl_gentex(int n,uint32_t*t){ ((void(*)(int,uint32_t*))F(10))(n,t); }
static void  gl_bindtex(uint32_t tg,uint32_t t){ ((void(*)(uint32_t,uint32_t))F(11))(tg,t); }
static void  gl_teximage(uint32_t tg,int l,int ifmt,int w,int h,int b,uint32_t f,uint32_t t,const void*p){ ((void(*)(uint32_t,int,int,int,int,int,uint32_t,uint32_t,const void*))F(12))(tg,l,ifmt,w,h,b,f,t,p); }
static void  gl_texparamx(uint32_t tg,uint32_t p,int v){ ((void(*)(uint32_t,uint32_t,int))F(13))(tg,p,v); }
static void  gl_enable(uint32_t c){ ((void(*)(uint32_t))F(14))(c); }
static void  gl_matrixmode(uint32_t m){ ((void(*)(uint32_t))F(15))(m); }
static void  gl_loadidentity(void){ ((void(*)(void))F(16))(); }
static void  gl_orthox(int l,int r,int b,int t,int n,int f){ ((void(*)(int,int,int,int,int,int))F(17))(l,r,b,t,n,f); }
static void  gl_enableclient(uint32_t a){ ((void(*)(uint32_t))F(18))(a); }
static void  gl_vertexptr(int s,uint32_t t,int st,const void*p){ ((void(*)(int,uint32_t,int,const void*))F(19))(s,t,st,p); }
static void  gl_texcoordptr(int s,uint32_t t,int st,const void*p){ ((void(*)(int,uint32_t,int,const void*))F(20))(s,t,st,p); }
static void  gl_color4x(int r,int g,int b,int a){ ((void(*)(int,int,int,int))F(21))(r,g,b,a); }
static void  gl_drawarrays(uint32_t m,int f,int c){ ((void(*)(uint32_t,int,int))F(22))(m,f,c); }
static void  gl_drawelements(uint32_t m,int c,uint32_t t,const void*ix) __attribute__((unused));
static void  gl_drawelements(uint32_t m,int c,uint32_t t,const void*ix){ ((void(*)(uint32_t,int,uint32_t,const void*))F(27))(m,c,t,ix); }   /* ord 55 (stage 4, against the ROM) */
static void  gl_shademodel(uint32_t m){ ((void(*)(uint32_t))F(23))(m); }
static void  gl_texenvx(uint32_t tg,uint32_t p,int v){ ((void(*)(uint32_t,uint32_t,int))F(24))(tg,p,v); }
static uint32_t gl_geterror(void){ return ((uint32_t(*)(void))F(25))(); }   /* device probe */
static int   egl_choose_config(int d,const int*a,uint32_t*c,int n,int*num){ return ((int(*)(int,const int*,uint32_t*,int,int*))F(26))(d,a,c,n,num); }
static void  gl_alphafuncx(uint32_t func,int ref){ ((void(*)(uint32_t,int))F(33))(func,ref); }   /* A4_TEL colorkey: alpha test */
static void  gl_disable(uint32_t c){ ((void(*)(uint32_t))F(32))(c); }
static void  gl_blendfunc(uint32_t sf,uint32_t df){ ((void(*)(uint32_t,uint32_t))F(35))(sf,df); }
static void  gl_flush(void){ ((void(*)(void))F(36))(); }
static void  gl_getintegerv(uint32_t pn,int*v){ ((void(*)(uint32_t,int*))F(37))(pn,v); }
static void  gl_stencilfunc(uint32_t f,int r,uint32_t m){ ((void(*)(uint32_t,int,uint32_t))F(38))(f,r,m); }
static void  gl_stencilop(uint32_t a,uint32_t b,uint32_t c){ ((void(*)(uint32_t,uint32_t,uint32_t))F(39))(a,b,c); }
static void  gl_clearstencil(int v){ ((void(*)(int))F(40))(v); }
/* imported, deliberately unused: a masked channel is the round-11 slowdown on the N95 */
static void  gl_colormask(uint32_t r,uint32_t g,uint32_t b,uint32_t a) __attribute__((unused));
static void  gl_colormask(uint32_t r,uint32_t g,uint32_t b,uint32_t a){ ((void(*)(uint32_t,uint32_t,uint32_t,uint32_t))F(41))(r,g,b,a); }
static void  gl_depthfunc(uint32_t f) __attribute__((unused));
static void  gl_depthfunc(uint32_t f){ ((void(*)(uint32_t))F(28))(f); }
static void  gl_cleardepthx(int d) __attribute__((unused));
static void  gl_cleardepthx(int d){ ((void(*)(int))F(29))(d); }
static void  gl_loadmatrixx(const int*m) __attribute__((unused));
static void  gl_loadmatrixx(const int*m){ ((void(*)(const int*))F(42))(m); }       /* ord 84 (A4_MESH) */
static void  gl_depthmask(uint32_t f) __attribute__((unused));
static void  gl_depthmask(uint32_t f){ ((void(*)(uint32_t))F(43))(f); }              /* ord 49 (A4_MESH) */
static void  gl_texsubimage(uint32_t tg,int l,int xo,int yo,int w,int h,uint32_t f,uint32_t t,const void*p){ ((void(*)(uint32_t,int,int,int,int,int,uint32_t,uint32_t,const void*))F(34))(tg,l,xo,yo,w,h,f,t,p); }
#ifdef A4_WINDOW
extern volatile uint32_t *a4_ws_iat_ptr(void);
extern volatile uint32_t *a4_cone_iat_ptr(void);
#define W(i) (a4_ws_iat_ptr()[i])
#define C(i) (a4_cone_iat_ptr()[i])
static void *coe_static(void){ return ((void*(*)(void))C(0))(); }                 /* CCoeEnv::Static() */

static int  ws_flush(void *w){ return ((int(*)(void*))W(2))(w); }
static int  rw_construct(void *r, void *parent, uint32_t h) __attribute__((unused));
static int  rw_construct(void *r, void *parent, uint32_t h){ return ((int(*)(void*,void*,uint32_t))W(8))(r, parent, h); }
static int  rw_construct_backup(void *r, void *parent, int dmode, uint32_t h) __attribute__((unused));
static int  rw_construct_backup(void *r, void *parent, int dmode, uint32_t h){ return ((int(*)(void*,void*,int,uint32_t))W(15))(r, parent, dmode, h); }
static void rw_setextent(void *r, const int *pt, const int *sz){ ((void(*)(void*,const int*,const int*))W(9))(r, pt, sz); }
static void rw_activate(void *r){ ((void(*)(void*))W(10))(r); }
static void rw_setvisible(void *r, int v){ ((void(*)(void*,int))W(11))(r, v); }
static int  rw_setdispmode(void *r, int m) __attribute__((unused));
static int  rw_setdispmode(void *r, int m){ return ((int(*)(void*,int))W(12))(r, m); }
static void rw_nontransparent(void *r){ ((void(*)(void*))W(13))(r); }
static int  egl_window_surface(int d, uint32_t c, void *win, const int *a){ return ((int(*)(int,uint32_t,void*,const int*))F(30))(d, c, win, a); }
static int  egl_swap(int d, int sf){ return ((int(*)(int,int))F(31))(d, sf); }
#endif

/* what the frame's clear covers: colour, and depth/stencil when the pbuffer has them */
static uint32_t clear_bits(volatile struct glr *s)
{
    return GL_COLOR_BUFFER_BIT | (s->zbits ? GL_DEPTH_BUFFER_BIT : 0u) | (s->sbits ? GL_STENCIL_BUFFER_BIT : 0u);
}

static int bringup(volatile struct glr *s)
{
    s->bstep = 1;                                                  /* entered bring-up */
    int disp = egl_get_display(0);                 if (!disp) return 0; s->bstep = 2;
    int a=0,b=0; if (!egl_initialize(disp,&a,&b))   return 0;       s->bstep = 3;
    /* PROBE: does this device's GL offer a pixmap-capable config? That decides
     * whether the zero-copy "render straight into the backbuffer" path is possible
     * on the N95 (vs. the current glReadPixels readback). */
    { int pca[] = { EGL_SURFACE_TYPE, EGL_PIXMAP_BIT, EGL_NONE };
      uint32_t pcfg = 0; int pnum = 0;
      if (egl_choose_config(disp, pca, &pcfg, 1, &pnum) && pnum > 0) s->pixmapcap = 1; }
#ifdef A4_WINDOW
    /* The native window must be in the app's own session. Take the session and
     * root group from CCoeEnv; create our RWindow as a child of the root group,
     * sharing the app's RWsBuffer. The window object is ours (scratch), so the
     * app's framework never sees it, but the server and EGL do, in the right
     * session. The app's own window/DSA region is occluded where ours sits -- on
     * the device the runtime's DSA copy then stops there (CLAUDE.md gap: the
     * bench draws the DSA regardless, so the bench cannot show this). */
    /* The native window must live in the app's own window-server session, the
     * one libGLES_CM resolves via CCoeEnv::Static()->WsSession(). Get CCoeEnv
     * (cone Static, 182, resolves), then find iWsSession and iRootWin INSIDE it
     * by content, not by a fixed offset -- the layout differs across S60
     * editions (SYMBIAN.md), and the bench ROM (RM-409 = Nokia 5320, 3.2) is a
     * different edition from the N95 (3.1), so a measured offset would be wrong
     * on the phone. Both members are MWsClientClass {iWsHandle, iBuffer}; the
     * session has its RSessionBase kernel handle at word+2, the group has a
     * window-server handle at word+0 and the same iBuffer. */
    if (C(0) < 0x10000u) { s->winstep = 0xC0; return 0; }
    void *env = coe_static();           s->winstep = 1; if (!env) return 0;
    s->env = (uint32_t)env;
    volatile uint32_t *e = (volatile uint32_t *)env;
    uint32_t appbuf = 0, appsess = 0, grp = 0;
    /* pass 1: the RWsSession -- word[1] a heap pointer, word[2] a kernel handle */
    for (int o = 0; o + 3 <= 0x80; o++) {
        uint32_t b = e[o + 1], hk = e[o + 2];
        if (b >= 0x00080000u && b < 0x10000000u && hk >= 0x40000000u) { appsess = (uint32_t)env + 4 * o; appbuf = b; break; }
    }
    if (!appsess) { s->winstep = 0xC2; return 0; }
    /* pass 2: the RWindowGroup -- a different member whose iBuffer is the same,
     * preceded by a window-server handle (nonzero, not a pointer). */
    for (int o = 0; o + 2 <= 0x80; o++) {
        uint32_t h = e[o], b = e[o + 1];
        if (b == appbuf && h != 0 && h < 0x00080000u && ((uint32_t)env + 4 * o) != appsess) { grp = (uint32_t)env + 4 * o; break; }
    }
    if (!grp) { s->winstep = 0xC3; return 0; }
    s->appbuf = appbuf; s->appsess = appsess; s->grp = grp;   s->winstep = 3;
    /* build our RWindow object: MWsClientClass {iWsHandle=0, iBuffer=appbuf} */
    uint8_t *wsb = (uint8_t *)A4_WS_OBJ; void *win = wsb;
    for (int i = 0; i < 0x20; i++) wsb[i] = 0;
    ((volatile uint32_t *)win)[1] = appbuf;
#ifdef A4_BACKUP
    /* A BACKED-UP window (window_type backed_up). The server maintains its bitmap
     * and redraws it itself, so the client gets NO redraw events -- the app's cone
     * CCoeRedrawer never sees our window, dissolving the client-handle dilemma
     * (cone wanted 0-or-a-real-control, the window server wants non-zero: WSERV 4).
     * EGL still accepts it (same window_kind::client). Construct takes the display
     * mode. The handle here is a deliberate POISON value (unmapped, aligned,
     * non-zero): if a backed-up window truly bypasses cone, nothing dereferences
     * it and the race runs; if cone ever does touch it, the fault address is this
     * value, proving it does not. */
    if (rw_construct_backup(win, (void *)grp, ECOLOR64K, 0x7FFFFFFCu) != 0) return 0;   s->winstep = 4;
#else
    /* A REDRAW window (it presents; the device rejects a null handle at Construct,
     * WSERV 4) with a CRAFTED minimal CCoeControl as the client handle. The app is
     * a DSA title whose cone control stack is empty (proven: iStack iCount=0,
     * iBase=NULL), so there is no real control to borrow. cone's CCoeRedrawer::RunL
     * asserts (handle&3)==0 (CONE 46), then does window=(CCoeControl*)handle; if
     * (window) window->HandleRedrawEvent(rect). HandleRedrawEvent is
     *   Window().BeginRedraw(r); if(IsReadyToDraw()){...Draw()...} Window().EndRedraw();
     * and IsReadyToDraw() is ((iFlags&(EInvisible|EActivated))==EActivated) ||
     * (iFlags&EBackedUpWindow) -- so iFlags==0 makes it FALSE and the whole draw
     * block (ActivateGc, the virtual Draw, DrawComponents) is skipped. What is left
     * is BeginRedraw/EndRedraw on the control's iWin(+0x20): point it at OUR OWN
     * window and the redraw of our window is a no-op that VALIDATES it (no redraw
     * storm) and dereferences nothing else. No virtual is ever dispatched on this
     * control (Draw is skipped; the keypad-only N95 has no pointer events, the sole
     * aDestination-deref path in CCoeAppUi::HandleWsEventL), so the vtable is unused.
     * This is exactly what the earlier crafted control got wrong -- its iFlags/iWin
     * drove it into the Draw block on a redraw-during-orientation. */
    { uint32_t appui = e[0x24 / 4];   s->appui = appui;   /* diagnostic: confirm empty */
      if (appui >= 0x00080000u && appui < 0x10000000u) {
          volatile uint32_t *au = (volatile uint32_t *)appui;
          uint32_t stk = au[0x0C / 4];   s->istack = stk;
          if (stk >= 0x00080000u && stk < 0x10000000u)
              s->stkcount = ((volatile uint32_t *)stk)[0x04 / 4];
      } }
    uint8_t *cb = (uint8_t *)A4_CTRL_OBJ;
    for (int i = 0; i < 0x28; i++) cb[i] = 0;             /* zeroed: vptr 0, iFlags 0 */
    ((volatile uint32_t *)cb)[0x20 / 4] = (uint32_t)win;  /* iWin -> our own window */
    s->ctrlh = (uint32_t)cb;
    if (rw_construct(win, (void *)grp, (uint32_t)cb) != 0) return 0;   s->winstep = 4;
    rw_setdispmode(win, ECOLOR64K);
#endif
    { int pt[2] = { 0, 0 }, sz[2] = { VP_W, VP_H }; rw_setextent(win, pt, sz); }
    rw_nontransparent(win);
    rw_activate(win); rw_setvisible(win, 1);
    ws_flush((void *)appsess);          s->winstep = 5;
    /* Ask for an OPAQUE 565 config explicitly. Requesting only EGL_BUFFER_SIZE 16
     * lets the device pick a 16-bit config WITH alpha (RGBA4444/5551); the N95
     * window server then screen-door (dither) composites our window -- the
     * checkerboard glitch seen on hardware. EGL_ALPHA_SIZE 0 + 5/6/5 forces the
     * native no-alpha surface, so the window composites opaque. (The bench always
     * composited opaque, so it never showed this; keep 565 explicit for parity.) */
    int ca[] = { EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                 EGL_RED_SIZE, 5, EGL_GREEN_SIZE, 6, EGL_BLUE_SIZE, 5,
                 EGL_ALPHA_SIZE, 0, EGL_BUFFER_SIZE, 16, EGL_NONE };
    uint32_t cfg = 0; int ncfg = 0;
    if (!egl_choose_config(disp, ca, &cfg, 1, &ncfg) || ncfg < 1) return 0; s->winstep = 6;
    int surf = egl_window_surface(disp, cfg, win, 0); if (!surf) return 0; s->bstep = 4; s->winstep = 7;
    int ctx  = egl_context(disp, cfg, 0, 0);          if (!ctx)  return 0; s->bstep = 5;
    s->disp = (uint32_t)disp; s->surf = (uint32_t)surf; s->wvis = 1;
#else
    int pb[] = { EGL_WIDTH, VP_W, EGL_HEIGHT, VP_H, EGL_NONE, 0, 0 };
    uint32_t pcfg = GLES_CONFIG;
#ifdef A4_ZBUF
    /* A pbuffer config WITH a depth buffer: the game depth-tests its perspective class
     * (span 0x333f4 keeps a per-frame 1/z buffer). Config 6, used until now, is a fixed
     * index whose depth on the N95 was never known -- stage 5's depth build lost the road
     * on the phone and was abandoned without that being checked. Falls back to config 6;
     * glGetIntegerv(GL_DEPTH_BITS) below then says whether depth is really there. */
    /* Depth always; then, best first, alpha (the drawn-mask, A4_IMM) and stencil (shadows
     * darken once, KIND_SHAD): depth+alpha+stencil, depth+alpha, depth alone. The N95 has
     * no stencil config (round 10); whether it has alpha is what zcfg/abits will say. */
    { int za[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RED_SIZE, 5, EGL_GREEN_SIZE, 6,
                   EGL_BLUE_SIZE, 5, EGL_DEPTH_SIZE, 16, EGL_ALPHA_SIZE_Z, 1, EGL_STENCIL_SIZE, 1,
                   EGL_NONE };
      uint32_t c = 0; int n = 0;
      if (egl_choose_config(disp, za, &c, 1, &n) && n > 0) pcfg = c;
      else {
          za[12] = EGL_NONE;                        /* drop the stencil request */
          if (egl_choose_config(disp, za, &c, 1, &n) && n > 0) pcfg = c;
          else {
              za[10] = EGL_NONE;                    /* and the alpha request */
              if (egl_choose_config(disp, za, &c, 1, &n) && n > 0) pcfg = c;
          }
      } }
#endif
    s->zcfg = pcfg;
    int surf = egl_pbuffer(disp, pcfg, pb);  if (!surf) return 0; s->bstep = 4;
    int ctx  = egl_context(disp, pcfg, 0, 0);if (!ctx)  return 0; s->bstep = 5;
#endif
    if (!egl_make_current(disp, surf, surf, ctx))   return 0;       s->bstep = 6;
    gl_viewport(0,0,VP_W,VP_H);
    gl_enable(GL_TEXTURE_2D);
    gl_shademodel(GL_FLAT);
    gl_texenvx(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    gl_enable(GL_ALPHA_TEST);                         /* colorkey: discard texel-0 (alpha 0) */
    gl_alphafuncx(GL_GREATER, 0);                     /* keep fragments with alpha > 0 */
    gl_color4x(FX,FX,FX,FX);
    gl_enableclient(GL_VERTEX_ARRAY);
    gl_enableclient(GL_TEXTURE_COORD_ARRAY);
    gl_matrixmode(GL_PROJECTION); gl_loadidentity();
    gl_orthox(0, VP_W*FX, VP_H*FX, 0, -FX, FX);      /* top-left origin, 2D (no depth) */
    gl_matrixmode(GL_MODELVIEW); gl_loadidentity();
    gl_clearcolorx(FX, 0, FX, FX);                   /* sentinel-clear once, so the first pipelined
                                                      * readback (before any draw) is all-sentinel */
    gl_clear(GL_COLOR_BUFFER_BIT);
    s->pminy = VP_H; s->pmaxy = 0;                   /* nothing to read back yet */
    { uint32_t e = gl_geterror(); if (s->errc == 0) s->errc = e; }  /* catch a bring-up GL error */
    /* The read format the implementation prefers besides RGBA/UNSIGNED_BYTE (GL ES 1.1,
     * OES_read_format). The composite reads 565 only where this says RGB/565 (AB_565). Its
     * own error is kept apart, so an implementation without the query cannot set errc. */
    { int f = 0, t = 0;
      gl_getintegerv(GL_IMPLEMENTATION_COLOR_READ_FORMAT_OES, &f);
      gl_getintegerv(GL_IMPLEMENTATION_COLOR_READ_TYPE_OES, &t);
      s->rdfmt = (uint32_t)f; s->rdtype = (uint32_t)t; s->rderr = gl_geterror(); }
#ifdef A4_ZBUF
    /* The game's test passes when the pixel's 1/z is >= the stored one, the buffer zeroed
     * each frame (0x31c70 -> 0x2def4): GL_GEQUAL on depth = min(ZK/ZW, 1), cleared to 0.
     * No depth bits -> zbits 0 -> the flags-1 triangles draw untested, as before. */
    { int zb = 0, sb = 0, ab = 0; gl_getintegerv(GL_DEPTH_BITS, &zb); gl_getintegerv(GL_STENCIL_BITS, &sb);
      gl_getintegerv(GL_ALPHA_BITS, &ab);
#ifdef A4_NOSTENCIL
      sb = 0;                                       /* bench: the N95's state (no stencil) */
#endif
      s->zbits = (uint32_t)zb; s->sbits = (uint32_t)sb;
#ifdef A4_IMM
      s->abits = (uint32_t)ab;
#endif
      gl_cleardepthx(0); gl_depthfunc(GL_GEQUAL);
      if (sb > 0) { gl_clearstencil(0); gl_stencilfunc(GL_EQUAL, 0, 0xFFu); gl_stencilop(GL_KEEP, GL_KEEP, GL_INCR); }
      gl_clear(clear_bits(s));
      { uint32_t e = gl_geterror(); if (s->errc == 0) s->errc = e; } }
#endif
    return 1;
}

/* texture cache: returns the cache INDEX for desc+0x30, uploading on a miss and
 * flagging whether the texture is a colorkey (transparent) sprite -- the game's
 * software filler treats texel value 0 as transparent, so a texture with many 0
 * texels is a sprite we must leave to software (the GPU would draw its black
 * background opaque). -1 on failure (caller falls through to software). */
/* Each cache entry's provenance (round 13): the texels it was made from, their size, a
 * sampled hash of them, and the last present it was used in. The cache was keyed by the
 * descriptor address alone, and a descriptor freed and reallocated for another texture
 * -- the garage's next car -- kept the old one's GL texture (stretched, by its UVs
 * normalised for the new size); and it never let an entry go, so after the garage it was
 * full and every new texture went to software. Above the batch scratch, to 0x414000. */
#define A4_GL_TEXC 0x00413000u
struct texc { uint32_t src[NCACHE], wh[NCACHE], hs[NCACHE], lu[NCACHE]; };
#define TX ((volatile struct texc *)A4_GL_TEXC)
#define A4_GL_SCRATCH_TOP 0x00423000u              /* injector.py GL_SCRATCH + GL_SCRATCH_SIZE */
_Static_assert(A4_GL_TEXC + sizeof(struct texc) <= A4_GL_SCRATCH_TOP, "texc past the scratch");
/* tex_index's hint (round 18): a slot + 1 for each hash of (key, table, texels), checked
 * in full before use, so a stale or colliding hint only costs the linear search it
 * replaced (20% of the hook's instructions in round 16's bench profile) */
#define A4_GL_THINT 0x00420800u
#define THINT ((uint16_t *)A4_GL_THINT)
#define THINT_N 1024u
_Static_assert(A4_GL_THINT + 2u * THINT_N <= 0x00421000u, "texture hints past the aggregates");
#ifdef A4_ATLAS
/* Round 24: two shared textures, 1024x512 -- [0] RGB565 for opaque textures, [1] RGBA5551 for
 * colour-keyed ones, as their own textures are (tex_make's tr) -- that a triangle may draw
 * from instead of its texture's own, so consecutive runs of different textures become one
 * draw call (the census: reorder alone 210 calls a frame, with an atlas ~110; a call is
 * ~31 us on the N95). Only where nothing changes: kinds TEX, TEXZ, ADD (ALPHA's texture is
 * RGBA8888, mode 1 is re-baked), textures up to 128x128, and triangles whose texcoords all
 * lie inside [0, 1] of the texture. Each copy is padded by one texel of the opposite edge,
 * so a sample a texel outside (the edge of [0, 1]) reads what GL_REPEAT reads on the
 * texture itself. Shelves by padded height; a full atlas places no more (the texture's own
 * is always there). Per cache slot: where its copy is and in which state. */
#define ATL_W 1024u
#define ATL_SH 48u
#define ATL_PQ 32u               /* copies waiting for the end of a frame */
#ifndef ATL_PER_FRAME
#define ATL_PER_FRAME 4u         /* copies uploaded after one frame's composite, at most */
#endif
struct atl { uint32_t name[2], ok, nsh[2], top[2], h, vs;   /* h: 1024, or 512 if the larger was refused; vs: log2(h) */
             uint16_t shy[2][ATL_SH], shh[2][ATL_SH], shx[2][ATL_SH];
             uint32_t xy[NCACHE]; uint16_t wh[NCACHE]; uint8_t st[NCACHE], ai[NCACHE];
             uint32_t npend, pts[ATL_PQ]; uint16_t pslot[ATL_PQ]; };
#define A4_GL_ATL 0x00422000u
#ifndef A4_REORDER
#error "A4_ATLAS builds its padded copies in the reorder's arrays"
#endif
#define ATL ((volatile struct atl *)A4_GL_ATL)
_Static_assert(A4_GL_ATL + sizeof(struct atl) <= A4_GL_SCRATCH_TOP, "atlas state past the scratch");
#define ATLST_NONE 0u
#define ATLST_OK 1u
#define ATLST_STALE 2u           /* placed, its texture remade: upload again before use */
#define ATLST_FULL 3u            /* refused (no room): not asked again until the slot is remade */
#define ATLST_PENDING 4u         /* placed, waiting for the end of the frame to be uploaded */
#endif

#if defined(A4_STEPTIME) && !defined(A4_AGG)
#error "A4_STEPTIME keeps its sums beside the aggregates: build it with A4_AGG"
#endif
#ifdef A4_AGG
/* Round 19: the race's numbers by A/B mode, over whole segments instead of the ring's last
 * 8 windows. A segment ends at a gap of 1.5 s or more between two renders (a load, a
 * pause); seg[0] is the current one, seg[1] and seg[2] the two before. Per segment and
 * mode ((mode >> 2) & 3): [0..15] the window sums a4_tel.c adds at each capture (renders,
 * ticks, 3D, hook, composite, read, batch draw, 2D, present, grouping, calls, calls before
 * grouping, triangles, presents, skipped, windows), [16..31] the render-to-render times in
 * 5 ms bins ([16] < 60 ms, [17] 60-64, ... [30] 125-129, [31] >= 130), the render after a
 * capture's write left out. Written after the census tail in every capture. */
#define A4_GL_AGG 0x00421000u
struct agg { uint32_t seg[3][4][32]; };
#define AGG ((volatile struct agg *)A4_GL_AGG)
#ifdef A4_STEPTIME
/* Round 30: the game's logic per step, by segment like the aggregates (rotated with them).
 * A span is one call of the race update +1670E: from +15B1C's return (the step count) to
 * the render call +12992 -- the n logic steps, the m - n slow-motion updates and +12D5A.
 * Per segment, 32 words: [0] spans, [1] steps (sum of n), [2] span ticks, [3] +12D5A ticks,
 * [4] the longest span, [5] sum of m - n, [6] +15A52 ticks, [7] its calls, [8..13] spans by
 * n = 0..5, [14] spans of 500 ms or more (a load: left out of everything else), [15] 'STP1',
 * [16..27] spans with n = 1 by length, 4 ms bins ([27] >= 44 ms); then the render call
 * +12992 itself (under 500 ms): [28] its ticks, [29] its ticks before the frame's first
 * triangle, [30] calls, [31] calls with no triangle. Ticks are NTickCount. */
#ifdef A4_FRAMEREC
/* Round 41 (the owner's held-steering jitter, not reproduced on the bench by any measure): the
 * phone's own frames. For each of the last FREC_N renders: the ms it started (a4_ntick), the ms
 * its frame went out (the next made present; 0 if none), the speculation's fraction q16 | the
 * camera mode (race+0x94) << 20 | bit 31 a fraction computed, the player car's x and z, its
 * heading (+0x678) | the camera's yaw (+0x68) << 16, the camera's x and z -- as drawn. [0]
 * 'FRC1', [1] renders recorded; entries of 8 words from [4]. a4_tel.c writes it after the slow
 * block in every capture; tools/frec.py reads it. */
#define A4_GL_FREC 0x00456000u
#define FREC_N 256u
#if defined(A4_KINEDRAW) && !defined(A4_INTERP)
/* engine map 1 (round 47 on the phone: cars flew, and the frame record had no height): a second
 * ring, 'FRC2', in A4_INTERP's block (unused without it): per render [0] the same ms as FRC1's
 * entry, [1] the player's drawn y | the camera's y << 16, [2] the step's y | its +0D0 << 16,
 * [3] the step's x less the drawn x | the step's z less the drawn z << 16 (16 bits each, signed).
 * a4_tel.c appends it after FRC1; tools/frec2.py reads both. */
#define A4_GL_FREC2 0x0045C000u
#define FREC2_N 255u
_Static_assert(A4_GL_FREC2 + 16u + FREC2_N * 16u <= 0x0045D000u, "FRC2 past the block before HP");
#endif
_Static_assert(A4_GL_FREC >= 0x00455800u + 0x680u && A4_GL_FREC + 16u + FREC_N * 32u <= 0x00460000u, "the frame record");
#endif
#define A4_GL_STEP 0x00421700u
#define STEPB ((volatile uint32_t *)A4_GL_STEP)
_Static_assert(A4_GL_STEP >= A4_GL_AGG + sizeof(struct agg) + 4u && A4_GL_STEP + 3u * 32u * 4u <= 0x00422000u,
               "the step block between the aggregates' format word and the atlas");
#endif
/* the slot of a mode: bit 0 AB_SKIPREP, bit 1 AB_INDEXED (round 20; round 19 used
 * (mode >> 2) & 3, its files carry no format word after the segments) */
#if defined(A4_ABCYCLE10)
#define AGG_SLOT(m) (((m) >> 10) & 2u)                                       /* bit 1: AB_HSPEC */
#elif defined(A4_ABCYCLE9)
#define AGG_SLOT(m) (((m) >> 9) & 2u)                                        /* bit 1: AB_MESH */
#elif defined(A4_ABCYCLE8)
#define AGG_SLOT(m) (((m) >> 8) & 2u)                                        /* bit 1: AB_ATLAS */
#elif defined(A4_ABCYCLE7)
#define AGG_SLOT(m) ((((m) >> 7) & 1u) | (((m) >> 7) & 2u))                  /* bit 0: AB_PLD, bit 1: AB_BIGSTRIP */
#elif defined(A4_ABCYCLE6)
#define AGG_SLOT(m) ((((m) >> 2) & 1u) | (((m) >> 5) & 2u))                  /* bit 1: AB_DEFER */
#else
#define AGG_SLOT(m) ((((m) >> 2) & 1u) | ((((m) >> 3) | ((m) >> 4)) & 2u))   /* bit 1: AB_INDEXED or AB_LEAN */
#endif
_Static_assert(A4_GL_AGG + sizeof(struct agg) <= A4_GL_SCRATCH_TOP, "aggregates past the scratch");
static void agg_render(uint32_t now)
{
    uint32_t last = A4_PERF[PF_LASTFIRST];
    A4_PERF[PF_LASTFIRST] = now;
    uint32_t wrote = A4_PERF[PF_FLAGS] & PFF_WROTE;
    A4_PERF[PF_FLAGS] &= ~PFF_WROTE;
    if (!last || wrote) return;
    uint32_t iv = now - last;
    if (iv >= 1500u) {                                  /* a load or a pause: a new segment */
        volatile uint32_t *a = &AGG->seg[0][0][0];
        for (uint32_t k = 0; k < 128u; k++) { a[256u + k] = a[128u + k]; a[128u + k] = a[k]; a[k] = 0; }
#ifdef A4_STEPTIME
        for (uint32_t k = 0; k < 32u; k++) { STEPB[64u + k] = STEPB[32u + k]; STEPB[32u + k] = STEPB[k]; STEPB[k] = 0; }
#endif
        return;
    }
    uint32_t b = iv < 60u ? 0u : iv >= 130u ? 15u : ((iv - 55u) * 13108u) >> 16;   /* /5, exact here */
    AGG->seg[0][AGG_SLOT(A4_PERF[PF_MODE])][16u + b]++;
}
#endif
#ifdef A4_STEPTIME
/* The race update's calls, rerouted through here (injector STEP_HOOKS: the four bl's become
 * BLX). Each calls the game's own function at the placeholder's target; the return value is
 * passed back although +1670E reads none of them. */
extern const int32_t a4_stepcnt, a4_stepcam, a4_steprend, a4_stepmain;   /* a4_gl.s */
extern int a4_gcall1(uint32_t fn, uint32_t a0);    /* a4_gl.s: a game call that keeps r4-r11 */
#define GAMEFN(ph) ((uint32_t)(uintptr_t)&(ph) + (uint32_t)(ph))
#ifdef A4_TRACEARM
/* bench: an empty function entered once, at the A4_TRACEARM-th timed span, so a PC trace
 * (EKA2L1_PCTRACE_START = this function's address) starts mid-race instead of at the boot */
__attribute__((noinline)) void a4_trace_arm(void) { __asm__ volatile("" ::: "memory"); }
#endif
int a4_step_count(void *self, int32_t *m, int32_t *n)           /* +16796: bl +15B1C */
{
#ifdef A4_HALFSPEC
    {   /* a speculative state is put back after the render that draws it, or at the even
         * half-step after it, both inside the update that made it; one still pending here
         * outlived its update (none should: the early returns are in whole steps, after the
         * restore) and may belong to a race that is gone -- dropped, never restored, counted */
        volatile uint32_t *sp = (volatile uint32_t *)0x00447000u;   /* A4_GL_SPEC */
        if (sp[1]) { sp[1] = 0; sp[30]++; }
    }
#endif
#ifdef A4_LOCKSTEP
    {   /* the time the step count reads (+15B1C: t = [[G+0x1114]+0x2C], G from +52CA0, taken
         * here to be [race+0x38] -- the key object K's word 11; last t at [race+0x44]) set to
         * exactly A4_LOCKSTEP periods on, so no reader of that clock sees the host's */
        uint32_t race = (uint32_t)(uintptr_t)self, ctx = *(volatile uint32_t *)(race + 0x38);
        uint32_t K = ctx ? *(volatile uint32_t *)(ctx + 0x1114) : 0;
#ifdef A4_LOCKSTEP_P
        uint32_t P = A4_LOCKSTEP_P;  /* the period each lockstep update advances by */
#else
        uint32_t P = (A4_LOCKSTEP) == 2 ? 10667u : 21333u;
#endif
        if (K) ((volatile uint32_t *)K)[11] = *(volatile uint32_t *)(race + 0x44) + (uint32_t)(A4_LOCKSTEP) * P;
    }
#endif
#ifdef A4_TRACEARM
    if (STEPB[0] == (uint32_t)(A4_TRACEARM)) a4_trace_arm();
#endif
#ifdef A4_BENCH_SLOWMO
    /* bench (round 41): the game's slow motion forced -- the race's time scale (+15B1C reads
     * [race+0x84C], 256 normal) set to A4_BENCH_SLOWMO for 3 s of every 10 (host ms), 256 the
     * rest -- to reproduce on the bench what a takedown's slow motion does to the frames */
    *(volatile uint32_t *)((uint32_t)(uintptr_t)self + 0x84Cu) = ((a4_ntick() / 1000u) % 10u) >= 7u ? (uint32_t)(A4_BENCH_SLOWMO) : 256u;
#endif
    int r = ((int (*)(void *, int32_t *, int32_t *))GAMEFN(a4_stepcnt))(self, m, n);
#ifdef A4_LOCKSTEP
    /* bench (round 31): game time cut loose from the clock -- every race update that would
     * render runs exactly A4_LOCKSTEP steps (1 normal, 2 with the step halved) and then
     * renders, so steps and renders interleave the same way in every run and in both builds
     * (the render feeds the simulation). The game runs slower than real time when behind. */
    if (*m > 0) { *m = (int32_t)(A4_LOCKSTEP); *n = (int32_t)(A4_LOCKSTEP); }
#endif
    volatile uint32_t *pf = A4_PERF;
    pf[PF_LSTEP] = ((uint32_t)*n & 0xFFu) | (((uint32_t)*m & 0xFFu) << 8);
    pf[PF_SCAM] = 0; pf[PF_SMAIN] = 0; pf[PF_NMAIN] = 0;
    pf[PF_FLAGS] |= PFF_LOGIC;
    pf[PF_MLOGIC] = a4_ntick();                                 /* last: the call above is not in it */
    return r;
}
#ifdef A4_CARREC
/* bench (round 31, the faster-step work): each logic step, after the game has taken its
 * keys (+25E8), a record of the step and the player car, handed to an empty function so
 * EKA2L1_PCMEM can write them out (two ats at its address: 0x20 bytes at r0, the car at r1).
 * The record: 'CREC', the step number, the key bits before and now (K+0, K+4, K =
 * [[race+0x38]+0x1114]), the car, the player's index, the race's step period word. */
#define A4_GL_CREC 0x00421900u      /* after STEPB (0x421700 + 3 x 128 = 0x421880) */
__attribute__((noinline)) void a4_rec_emit(const void *hdr, const void *car)
{ __asm__ volatile("" :: "r"(hdr), "r"(car) : "memory"); }
#ifdef A4_RENDREC
/* a second emitter for the per-render dumps of the race and its context (EKA2L1_PCMEM ats
 * at its own address: r0 0x20, r1 and r2 0x4000 each); its body differs so it is not merged */
__attribute__((noinline)) void a4_rec_emit3(const void *hdr, const void *race, const void *ctx)
{ __asm__ volatile("nop\n\tnop" :: "r"(hdr), "r"(race), "r"(ctx) : "memory"); }
#endif
/* round 41 (the oracle with the camera): the header, the player car and the race object's first
 * 0x100 bytes (its camera +0x58..+0x70) -- EKA2L1_PCMEM ats at its address: r0 0x20, r1 0xA00,
 * r2 0x100 */
__attribute__((noinline)) void a4_rec_emit2(const void *hdr, const void *car, const void *race)
{
    __asm__ volatile("" : : "r"(hdr), "r"(car), "r"(race) : "memory");
}
#ifndef A4_AS_START
#define A4_AS_START 260u             /* A4_AUTOSTEER: the step the weave starts (the race starts at ~145) */
#endif
#ifndef A4_AS_ON
#define A4_AS_ON 3u                  /* steps a turn key is held */
#endif
#ifndef A4_AS_OFF
#define A4_AS_OFF 14u                /* steps free after it */
#endif
#ifndef A4_KEY_R
#define A4_KEY_R 0x8u                /* '2' (turn right, landscape 270): a guess -- '5' is bit 6 */
#endif
#ifndef A4_KEY_L
#define A4_KEY_L 0x200u              /* '8' (turn left) */
#endif
#ifdef A4_AUTOPILOT
/* bench (round 45): the autopilot's '5' (K+4 bit 6) released from step A4_AUTOPILOT_REL on, to
 * tell the turbo's words from the rest (the car keeps its speed on its own once moving) */
#ifdef A4_AUTOPILOT_REL
#define A4_AP_KEY(st) ((st) >= (A4_AUTOPILOT_REL) ? 0u : 0x40u)
#elif defined(A4_AUTOPILOT_TAP)
/* or let go for three steps in every A4_AUTOPILOT_TAP: a fresh press of '5' each time (the
 * bench script must not hold the key itself: NOKEY5=1) */
#define A4_AP_KEY(st) ((st) % (A4_AUTOPILOT_TAP) < 3u ? 0u : 0x40u)
#else
#define A4_AP_KEY(st) 0x40u
#endif
#endif
#ifdef A4_AUTOPILOT
#ifndef A4_KEY_BRAKE
#define A4_KEY_BRAKE 0x80u           /* '6' (brakes; reverse at rest): a guess by the pattern above */
#endif
/* the autopilot's keys this step */
static uint32_t ap_key(uint32_t race, uint32_t st)
{
#ifdef A4_AUTOPILOT_UNSTICK
    /* bench (round 45): the bench driver cannot get a stopped car going again -- a car at rest
     * mid-race with '5' held stays dead (+A00 900, speed 0) in every build, stock included (the
     * lockstep records orna, orn, ors). At rest 12 or more steps after its last contact (+0E0):
     * '6' (brakes: reverse) for 20 steps, then '5' with '2' (turn right) for 20, and round again
     * -- one rule for every build, in game steps */
    uint32_t pl = *(volatile uint32_t *)(race + 0x390);
    uint32_t idx = pl ? *(volatile uint32_t *)(pl + 0xC) : 16u;
    uint32_t car = idx < 16u ? *(volatile uint32_t *)(race + 0x394 + 4u * idx) : 0;
    if (car) {
        volatile int32_t *cw = (volatile int32_t *)car;
        int32_t e0 = cw[0xE0u / 4u];
        if (cw[0x7D0u / 4u] == 0 && e0 >= 12)
            return (e0 % 40) < 20 ? A4_KEY_BRAKE : (0x40u | A4_KEY_R);
    }
#else
    (void)race;
#endif
    return A4_AP_KEY(st);
}
#endif
/* what: 1 the keys and the step count (at the whole step), 2 the record (round 43: a half-step
 * build emits it at the odd half-step, the state the whole step left, as a normal build's next
 * step sees it), 3 both */
#ifdef A4_BENCH_INJECT
#include "eng/inject.h"
#endif
static void car_record(uint32_t race, uint32_t what)
{
#ifdef A4_ENG3
    {   /* round 50, the 3x engine: the keys and the record before a whole step only (a4_eng.h's state:
         * magic, race, cnt -- the next race step's substep is cnt % 3), so the autopilot's scripts and the
         * records keep the game's 85.33 ms steps */
        volatile uint32_t *eg = (volatile uint32_t *)0x0045D000u;
        if (eg[0] == 0x33454E47u && (eg[2] % 3u) != 0u) return;
    }
#endif
    volatile uint32_t *r = (volatile uint32_t *)A4_GL_CREC;
    uint32_t ctx = *(volatile uint32_t *)(race + 0x38);
    uint32_t K = ctx ? *(volatile uint32_t *)(ctx + 0x1114) : 0;
    uint32_t pl = *(volatile uint32_t *)(race + 0x390);
    uint32_t idx = pl ? *(volatile uint32_t *)(pl + 0xC) : 0;
    uint32_t car = idx < 16u ? *(volatile uint32_t *)(race + 0x394 + 4u * idx) : 0;
#ifdef A4_AUTOPILOT
    /* bench: the keys replaced by a script in game time (the same in a normal and a
     * halved-step build): accelerate (K+4 bit 6, the '5' key) held from the first step */
#ifdef A4_AUTOSTEER
    /* round 41: and steering in game time -- throttle to A4_AS_START, then right held A4_AS_ON
     * steps, free A4_AS_OFF, left A4_AS_ON, free A4_AS_OFF, round again (r[1]: the steps so far) */
    if (K && (what & 1u)) {
        uint32_t k = 0x40u, st = r[1];
        if (st >= (A4_AS_START)) {
            uint32_t ph = (st - (A4_AS_START)) % (2u * ((A4_AS_ON) + (A4_AS_OFF)));
            k |= ph < (A4_AS_ON) ? A4_KEY_R : (ph >= (A4_AS_ON) + (A4_AS_OFF) && ph < 2u * (A4_AS_ON) + (A4_AS_OFF)) ? A4_KEY_L : 0u;
        }
        ((volatile uint32_t *)K)[0] = k; ((volatile uint32_t *)K)[1] = k;
    }
#else
    if (K && (what & 1u)) {
        uint32_t k = ap_key(race, r[1]);
#if defined(A4_BENCH_SEEK) && defined(A4_SPECTRAFFIC)
        /* bench (r49 on the phone: traffic "passed through"): steer at the nearest live traffic car
         * ahead (its +0x128 bit 0 clear: the collision's own skip), by the game's positions -- to
         * meet traffic far more often than the racing line does. A4_BENCH_SEEK is the sign: 1 or -1
         * (which key turns which way is a guess, settled by whether the bearing closes) */
        uint32_t ta = ((volatile uint32_t *)0x00447000u)[27];   /* A4_GL_SPEC word 27: the traffic array (a4_h_fb12) */
        if (car && ta && r[1] > 200u) {
            const volatile int32_t *pc = (const volatile int32_t *)car;
            int32_t vx = pc[0x10u / 4u], vz = pc[0x18u / 4u], best = 0x7FFFFFFF; int64_t bcr = 0;
            for (uint32_t i = 0; i < 16u; i++) {
                const volatile int32_t *t = (const volatile int32_t *)(ta + i * 0x148u);
                if (t[0x128u / 4u] & 1) continue;
                int32_t dx = t[1] - pc[1], dz = t[3] - pc[3];
                if (dx > 2500 || dx < -2500 || dz > 2500 || dz < -2500) continue;
                int64_t dot = (int64_t)vx * dx + (int64_t)vz * dz;
                int32_t d = (dx < 0 ? -dx : dx) + (dz < 0 ? -dz : dz);
                if (dot <= 0 || d < 150 || d >= best) continue;
                best = d; bcr = (int64_t)vx * dz - (int64_t)vz * dx;
                /* bearing's sine x |v||d|: steer when it is over ~2 degrees of the way */
                int64_t lim = ((int64_t)((vx < 0 ? -vx : vx) + (vz < 0 ? -vz : vz)) * d) / 40;
                if (bcr < lim && bcr > -lim) bcr = 0;
            }
            if (best != 0x7FFFFFFF && bcr) k |= ((bcr > 0) == ((A4_BENCH_SEEK) > 0)) ? A4_KEY_R : A4_KEY_L;
        }
#endif
        ((volatile uint32_t *)K)[0] = k; ((volatile uint32_t *)K)[1] = k;
    }
#endif
#endif
    if (what & 1u) r[1]++;
#ifdef A4_BENCH_SEED
    /* bench (round 50, the engine's lockstep oracle): the C library's random() state set to the same
     * value at the race's first step in every run -- two runs of one build parted at step ~136 because
     * the menus before the race (real-time key presses) draw a different count of random numbers. On
     * the bench's libc (EKA2L1, measured with EKA2L1_WATCH): random()'s 31-word table at 0x50016C, the
     * front pointer at 0x50014C three words ahead of the rear one at 0x500150. Never in a phone build. */
    if ((what & 1u) && r[1] == 1u) {
        volatile uint32_t *tb = (volatile uint32_t *)0x0050016Cu;
        uint32_t x = (uint32_t)(A4_BENCH_SEED);
        for (uint32_t k = 0; k < 31u; k++) { x = x * 1103515245u + 12345u; tb[k] = x; }
        *(volatile uint32_t *)0x00500150u = 0x0050016Cu;
        *(volatile uint32_t *)0x0050014Cu = 0x0050016Cu + 12u;
        /* and rand() -- a Park-Miller generator (16807: its first write after a seed of 1, EKA2L1_WATCH)
         * with its own state at 0x500148: called by the traffic (+F1B0) and the cars' helpers in the step */
        *(volatile uint32_t *)0x00500148u = (uint32_t)(A4_BENCH_SEED) | 1u;
    }
#endif
#ifdef A4_BENCH_STALL
    /* bench (round 43): the player stopped dead at this step -- speed, engine, velocity zeroed --
     * to see whether the game itself gets a stopped car going again */
    if ((what & 1u) && car && r[1] >= (A4_BENCH_STALL) && r[1] < (A4_BENCH_STALL) + 3u) {
        volatile int32_t *cw = (volatile int32_t *)car;
        cw[0x7D0u / 4u] = 0; cw[0x7D8u / 4u] = 0; cw[0x680u / 4u] = 0; cw[0x688u / 4u] = 0;
        cw[0x010u / 4u] = 0; cw[0x014u / 4u] = 0; cw[0x018u / 4u] = 0; cw[0x0B4u / 4u] = 0;
        cw[0x0A8u / 4u] = cw[0x004u / 4u]; cw[0x0ACu / 4u] = cw[0x008u / 4u]; cw[0x0B0u / 4u] = cw[0x00Cu / 4u];
    }
#endif
#ifdef A4_BENCH_INJECT
    /* bench (round 50): at the steps tools/mkinject.py chose, the player car becomes stock's at that step
     * (renderer/eng/inject.h, from a stock run's record; the vtable, owner block and address words stay
     * the live car's), so stock and the 3x engine go on from one state and compare like for like.
     * Before a whole step (the 3x engine's records are taken there too). Never in a phone build. */
    if ((what & 1u) && car) {
        for (uint32_t q = 0; q < A4_INJ_N; q++) {
            if (r[1] != a4_inj_step[q]) continue;
            volatile uint32_t *cw = (volatile uint32_t *)car;
            for (uint32_t k = 0; k < A4_INJ_W; k++)
                if (!((a4_inj_keep[k >> 5] >> (k & 31u)) & 1u)) cw[k] = a4_inj_car[q][k];
#ifdef A4_ENG3
            ((volatile uint32_t *)0x0045D000u)[7] = 1u;   /* a4_eng.h ENG->flush: the lag rings held the old car */
#endif
        }
    }
#endif
    if (!(what & 2u)) return;
    r[0] = 0x43455243u; r[2] = K ? ((volatile uint32_t *)K)[0] : 0; r[3] = K ? ((volatile uint32_t *)K)[1] : 0;
    r[4] = car; r[5] = idx; r[6] = K ? ((volatile uint32_t *)K)[11] : 0; r[7] = *(volatile uint32_t *)(race + 0x44);
    a4_rec_emit2((const void *)r, car ? (const void *)car : (const void *)r, (const void *)race);
#ifdef A4_CARREC_AI
    {   /* bench (round 43): an opponent too (race slot 1, or 2 if the player is 1): 'CREA' */
        volatile uint32_t q[8];          /* on the stack: CREC + 0x40 is A4_GL_HSTEP */
        uint32_t ai = idx == 1u ? 2u : 1u, aic = *(volatile uint32_t *)(race + 0x394 + 4u * ai);
        q[0] = 0x41455243u; q[1] = r[1]; q[2] = r[2]; q[3] = r[3]; q[4] = aic; q[5] = ai; q[6] = r[6]; q[7] = r[7];
        if (aic) a4_rec_emit2((const void *)q, (const void *)aic, (const void *)race);
    }
#endif
}
#endif
int a4_step_main(void *self)                                    /* +167B4: bl +15A52, once a step */
{
#if defined(A4_CARREC) && defined(A4_HALFPHYS)
    car_record((uint32_t)(uintptr_t)self, 1u);
#elif defined(A4_CARREC)
    car_record((uint32_t)(uintptr_t)self, 3u);
#endif
    uint32_t t0 = a4_ntick();
    int r = ((int (*)(void *))GAMEFN(a4_stepmain))(self);

    A4_PERF[PF_SMAIN] += a4_ntick() - t0; A4_PERF[PF_NMAIN]++;
    return r;
}
int a4_step_cam(void *self)                                     /* +16966: bl +12D5A */
{
    uint32_t t0 = a4_ntick();
    int r = a4_gcall1(GAMEFN(a4_stepcam), (uint32_t)(uintptr_t)self);
    A4_PERF[PF_SCAM] += a4_ntick() - t0;
    return r;
}
#ifdef A4_HALFLOGIC
/* Round 31, milestone 1: with the step halved (A4_HALFSTEP), the game's whole step runs on
 * every other half-step only and the others do nothing, so the game keeps its speed; a
 * render whose batch held no whole step is skipped (the render feeds the simulation: the
 * texture animator, per-car flips and countdowns in +12952), so renders fall after whole
 * steps as in the normal build. The plumbing for the half-steps that will move the cars. */
#ifdef A4_STEPTIME
_Static_assert(A4_GL_STEP + 3u * 32u * 4u <= 0x00421900u, "the step block runs into the bench records (round 31: it did, at 0x421800)");
#endif
#define A4_GL_HSTEP 0x00421940u      /* [0] half-steps, [1] whole steps since the last render,
                                      * [2] calls recorded this whole step, [3] replaying,
                                      * [4] calls past HCALL_MAX (not recorded),
                                      * [5] whole steps since a render kept its own changes */
#ifdef A4_HALFMOTION
/* Milestone 2: the whole step's car calls -- the physics loop's +1DE78/+1F1F8 (the player)
 * and +1F1F8/+1FD0E (every other car) and the movement loop's +1EA64/+1FD5C -- are recorded with their arguments as the step makes them
 * and replayed on the odd half-step after it, so the cars move every half-step. Untouched
 * inside, each call still advances a whole step's worth: the comparison against the normal
 * build names what to halve. Up to HCALL_MAX calls (15 car slots, two loops, and room);
 * calls past it are counted at HSTEP[4]. */
#define A4_GL_HCALL 0x00421960u      /* HCALL_MAX x 6 words: which, r0..r3, the stack argument */
#define HCALL_MAX 48u
_Static_assert(A4_GL_HCALL + HCALL_MAX * 6u * 4u <= 0x00422000u, "the recorded car calls past the atlas");
extern const int32_t a4_hc_1de78, a4_hc_1f1f8, a4_hc_1ea64, a4_hc_1fd5c, a4_hc_1fd0e;   /* a4_gl.s */
static void hcall_rec(uint32_t which, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t s0)
{
    volatile uint32_t *h = (volatile uint32_t *)A4_GL_HSTEP;
    if (h[3]) return;
    if (h[2] >= HCALL_MAX) { h[4]++; return; }
    volatile uint32_t *c = (volatile uint32_t *)A4_GL_HCALL + 6u * h[2]++;
    c[0] = which; c[1] = a0; c[2] = a1; c[3] = a2; c[4] = a3; c[5] = s0;
}
typedef int (*fn5)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
typedef int (*fn3)(uint32_t, uint32_t, uint32_t);
typedef int (*fn2)(uint32_t, uint32_t);
typedef int (*fn1)(uint32_t);
#ifdef A4_HALFPHYS
/* Round 42 (owner, after the interpolation: "I think we need to find a way for the game to
 * actually produce these frames with the physics working accordingly"): the game's logic every
 * half-step for real. The step has no time delta (round 30): its amounts are per step. So the
 * game's own code runs every half-step -- the cars' physics and movement and the camera, with the
 * keys of that half-step -- and of what it changes, the continuous quantities are kept half:
 *  - the movement (+1EA64, +1FD5C) is handed half the car's velocity (+010/+014/+018: exactly
 *    the position's change a step, the oracle's stock run) and integrates half a step itself --
 *    walls, the track segment, all its own; the velocity put back after (a collision's change
 *    to it scaled back up);
 *  - the physics (+1DE78, +1F1F8, +1FD0E): its change to each rate field (hp_rate: the speed
 *    words, +7D8, the heading +678, +680/+688, the steering +0B8/+0C0, the slip +0C4/+0CC,
 *    +028, the wheels' phase +020, the step counters +0E0/+820) kept half;
 *  - the camera (+12DEC): its change to the position, angles, transition counter and lag pair
 *    kept half -- unless it is playing a script (a takedown, a crash, an event camera), which
 *    then runs at the whole step only, as the game made it.
 * Halves of integer units carry their remainder (hp_half), so two half-steps add up exactly.
 * Everything else (the race's decisions, timers, the AI's choices, traffic, effects, sounds)
 * runs once a step in the game's own whole step, as now. Nothing is put back: every half-step
 * is the game's state. Only the render after an odd half-step has its own once-a-step changes
 * undone (the per-car flip, the countdown, +0x388), as the game renders once a step. */
#define A4_GL_HP 0x0045D000u
#define HP_CARS 16u
/* Which words each call changes, measured (hpc: the player car copied before each of its calls at
 * an odd half-step, 1,451 calls, the Veyron): +1DE78 moves the position (+004/+00C by the whole
 * velocity on every call) and runs the engine (+7D0, +7D8, +7E4) and counters (+820, +A2C, +A34,
 * +A00, +ADC); +1EA64 changes the velocity (+010/+018), the height (+008, copies +0AC/+0D0), the
 * wheels' phase +020, the revolutions +02C, +028, the speed copy +0B4, the steering and slip
 * (+0B8/+0C0/+0C4/+0CC), the heading +678. Of each such change a half-step keeps half. Left as
 * the call computes them: copies and derived words (the position copies +0A8/+0B0, the
 * orientation matrices, the track segment +01C), flags (+B28), contact forces (+0F0/+0F8), and a
 * call's change to the position in +1EA64 (a wall pushing the car out). Each entry: the offset,
 * its circle (0: none), its quantum (+0B8/+0C0 move only in 12s -- offsets into a table of
 * 12-byte entries, a guess -- and are halved in 12s), the change beyond which it is a jump. */
struct hprate { uint16_t off, circ, q, jump, pos; };
/* round 43: the words each call integrates -- changed by the call AND carrying a nudge through it
 * (A4_HPPROBE sets 1-3, rows in MBX.md) -- are halved: a rate a step becomes half of it a
 * half-step. The words a call recomputes from the others (the probe's "overwritten": +1EA64's
 * velocity +010/+018, height +008, wheels +020, rpm +02C, slip +0B8..+0C0, segment +01C; +1DE78's
 * +07C, +680/+688, +7E4, +7F8, +7FC, +A00, +A18..+A20, +A78, +AF8, +B28) are left as computed:
 * halving one makes it a lag filter. +7D0 (the speed) carries -0.45 of a nudge: recomputed from
 * +7D8, left as computed (a guess). A change bigger than `jump` is an event (a gear change's
 * drop, a bump, a reload, a respawn) and stands whole: halved, it would be half an event. `pos`:
 * only increases are a rate. The census's per-call changes (hpc) set each threshold */
static const struct hprate hp_k1[] = {      /* +1DE78 (the player), and the half of +1F1F8 / +1FD0E that it covers */
    { 0x004, 0, 1, 2048, 0 }, { 0x00C, 0, 1, 2048, 0 },                 /* place: += the velocity */
    { 0x7D8, 0, 1, 64, 0 },                                              /* the engine: +-8/16 a step; a shift -256 */
    { 0x820, 0, 1, 1, 0 }, { 0xA2C, 0, 1, 1, 0 }, { 0xA34, 0, 1, 1, 0 }, { 0xADC, 0, 1, 2, 0 },   /* +1 a step */
    { 0x7C4, 0, 1, 1, 0 }, { 0xAEC, 0, 1, 1, 0 },                       /* countdowns, -1 a step */
    { 0xA24, 0, 1, 64, 0 },                                              /* +6, -2, -34..-46 a step */
    { 0xA7C, 0, 1, 1, 0 },                                               /* a kick of -8 at a steering edge, back by 1 a step */
    { 0xABC, 0, 1, 4096, 0 },                                            /* -4..-16 a step, +240 a step while it fills */
    { 0x808, 0, 1, 4096, 0 }, { 0x80C, 0, 1, 4096, 0 },                 /* filters: 7/8, 3/4 of themselves a step */
    { 0xB20, 0, 1, 1, 1 },                                               /* +1 a step to 48; x3/4 at a steering key's edge */
    { 0x678, 2048, 1, 256, 0 } };                                        /* heading: turned here while a steering key is held
                                                                          * (A4_HPTRACE, -26 /2048 a call) */
static const struct hprate hp_k3[] = {      /* +1EA64 / +1FD5C, and the other half of +1F1F8 / +1FD0E */
    { 0x678, 2048, 1, 256, 0 },                                          /* heading: up to ~50 /2048 a step */
    { 0x0C4, 0, 1, 4096, 0 }, { 0x0C8, 0, 1, 4096, 0 }, { 0x0CC, 0, 1, 4096, 0 },   /* filters: 7/8 a step */
    { 0xA6C, 0, 1, 1, 0 },                                               /* 9..5 by -1 a step, reloaded +4 */
    { 0xB18, 0, 1, 1, 0 } };                                             /* a kick of -4, back by 1 a step (+B98: the top speed, a max) */
#define HP_N1 (sizeof hp_k1 / sizeof hp_k1[0])
#define HP_N3 (sizeof hp_k3 / sizeof hp_k3[0])
#define HP_NR (HP_N1 + HP_N3)
#define HP_NC 9u                     /* the camera words once halved (round 42: +0x58..+0x70, +0xE4, +0xE8) */
struct hp {
    uint32_t race;                   /* the race the carries belong to */
    uint32_t car[HP_CARS];           /* the cars seen, for their carries */
    int32_t rc[HP_CARS][32];         /* hp_k1's then hp_k3's remainders (16.16) */
    int32_t pp[HP_CARS][3];          /* the place +1EA64 measured against at its last call (+0A8/+0AC/+0B0) */
    uint8_t ppv[HP_CARS];
    int32_t cw[6], cwp[6], cwpp[6];  /* the camera as the last three whole steps left it */
    uint32_t started, rstep;         /* the race has started (the player's +0E0 seen above 0); its step count */
    int32_t pw[4];                   /* the player car (+004, +008, +00C, +678) as the whole step left it */
    int32_t pwp[4];                  /* the same at the step before (A4_CAMALL) */
    int32_t pwpp[4];                 /* and two steps before */
    uint32_t pwpps;
    uint32_t hold;                   /* A4_SCRIPTHOLD: this frame drawn at the step (a scripted camera) */
    uint32_t pws, pwps;              /* the steps pw and pwp were taken at */
    uint32_t cwv, camx, cmode;       /* steps of it seen; the odd half-step's camera to put back; its mode */
    uint32_t committed, fwhole;      /* A4_FRACPHYS: the step's fraction the renders have moved the cars;
                                      * what the whole step then moves them (16.16) */
    uint8_t ran[HP_CARS];            /* the car's calls ran at the last odd half-step */
    uint8_t halve[HP_CARS];          /* ... so this whole step is halved for it: else it is a whole step's */
#ifdef A4_KINEPHYS
    int32_t kc[HP_CARS][3];          /* carries (16.16): x, z, heading */
    int32_t ks[HP_CARS][2];          /* this step's move at the renders (x, z) */
    int32_t kerr[HP_CARS][2];        /* the step's move less the game's own (x, z), for +1EA64's measurement */
    int32_t hdl[HP_CARS], turn[HP_CARS], wl[HP_CARS], wr[HP_CARS];
    uint32_t hds[HP_CARS];           /* the whole step hdl/wl were taken at */
    int32_t yW[HP_CARS], dW[HP_CARS], wW[HP_CARS];   /* drawn-only words as the whole step left them */
    uint8_t disp[HP_CARS];
    uint32_t wstep;
#ifdef A4_KINEDRAW
    int32_t xW[HP_CARS], zW[HP_CARS], hW[HP_CARS], aW[HP_CARS], bW[HP_CARS];   /* drawn-only, as the step left them */
    int32_t gW[HP_CARS], dWp[HP_CARS];   /* the ground's change last step, and its height a step back */
    int32_t kE[HP_CARS][4], kD[HP_CARS][4];   /* the corrector (x, z, heading, height; 1/256): offset, last drawn */
    uint32_t kT[HP_CARS];            /* when it was last drawn (whole steps << 16 | q16) */
    uint8_t kV[HP_CARS];
    int32_t cE[4], cD[4];            /* the chase camera's (+0x58, +0x5C, +0x60, +0x68) */
    uint32_t cT, cV;
    uint32_t kn, kbig, kcut;         /* corrections made, of them over 16 units / 16 /2048, reset at a cut */
    int32_t tE[16][2], tD[16][2];    /* the traffic's (x, z) */
    uint32_t tT[16], tstep;
    uint8_t tV[16];
#endif
#endif
    int8_t cc[12];                   /* the camera's */
    uint32_t odd;                    /* the last half-step was odd: the render after it is undone */
    uint32_t camscript;              /* the camera was on a script at the last odd half-step */
    uint32_t replaying;              /* the odd half-step's own calls: the hooks leave them alone */
    uint32_t nodd, nscript;
    uint32_t ntr;                    /* bench (A4_HPTRACE): the calls traced */          /* counts: odd half-steps run, of them with the camera on a script */
};
_Static_assert(HP_NR <= 32u && HP_NC <= 12u, "carry arrays");
_Static_assert(A4_GL_HP >= 0x0045C000u + (16u + 15u * 16u + 8u) * 4u && A4_GL_HP + sizeof(struct hp) <= 0x00460000u, "the half-step physics block");
#define HP ((volatile struct hp *)A4_GL_HP)
#ifdef A4_HPPROBE
#define HP_HALVE 0                   /* bench: the probe's car runs the game's own whole-step dynamics */
#else
/* round 43: a whole step is half a step only for a car whose calls ran at the odd half-step
 * before it. One that did not (before the start, a car just spawned) moves a whole step: halved,
 * it moved half a step in a whole step's time, +1EA64 measured half its speed and the next call
 * took that for a crash (the speed and engine zeroed at the start) */
#define HP_HALVE (!HP->replaying && hp_halve(a0))
#endif
#ifdef A4_FRACPHYS
#define HP_FWHOLE (HP->fwhole)           /* the rest of the step the renders left */
#else
#define HP_FWHOLE 32768u
#endif
/* d x f16/65536, floored, the remainder carried (16.16) so integer units add up over a step's
 * parts (round 43: a half-step is f16 32768; A4_FRACPHYS any fraction) */
static int32_t hp_frac(int32_t d, uint32_t f16, volatile int32_t *c)
{
    int32_t t = d * (int32_t)f16 + *c, a = t >> 16;
    *c = t - a * 65536;
    return a;
}
static uint32_t hp_idx(uint32_t car)
{
    uint32_t i;
    for (i = 0; i < HP_CARS; i++) if (HP->car[i] == car) return i;
    for (i = 0; i < HP_CARS; i++) if (!HP->car[i]) break;
    if (i == HP_CARS) return HP_CARS;
    HP->car[i] = car;
    for (uint32_t f = 0; f < 32u; f++) HP->rc[i][f] = 0;
    HP->ppv[i] = 0; HP->ran[i] = 0; HP->halve[i] = 0;
#ifdef A4_KINEDRAW
    HP->kV[i] = 0; HP->disp[i] = 0;
#endif
    return i;
}
/* round 43: +1EA64 measures the car's velocity (+010/+014/+018, and from it +0B4, +0B8..+0C0,
 * +0C4..+0CC) as its place less +0A8/+0AC/+0B0, then sets those to its place (A4_HPPROBE=2: a
 * nudge of +0A8 moves +010 by -1.00, +0B8 by -12). Every other word the car runs on takes that
 * as a whole step's movement, so a half-step call is measured against the place two half-step
 * calls back: exactly a whole step, the two halves' rounding cancelling; after a whole step's call
 * (one not halved: the start, a car just spawned) its own half doubled. Not across a jump */
static int32_t iabs32(int32_t x);
#ifdef A4_FRACPHYS
/* A4_FRACPHYS: at a render's part-step the velocity stays as the last whole step measured it
 * (+0A8.. = place less it); at the whole step it is measured over the whole step (+0A8.. = the
 * place the last whole step's call left, hp_meas_post). Stock measures once a step too */
static void hp_meas(uint32_t car, uint32_t half) __attribute__((unused));
static void hp_meas(uint32_t car, uint32_t half)
{
    uint32_t i = hp_idx(car);
    if (i >= HP_CARS) return;
    volatile int32_t *c = (volatile int32_t *)car;
    if (HP->replaying) {
        int32_t vx = c[0x10u / 4u], vy = c[0x14u / 4u], vz = c[0x18u / 4u];
        if (iabs32(vx) < 2048 && iabs32(vy) < 2048 && iabs32(vz) < 2048) {
            c[0xA8u / 4u] = c[1] - vx; c[0xACu / 4u] = c[2] - vy; c[0xB0u / 4u] = c[3] - vz;
        }
    } else if (half && HP->ppv[i]) {
        if (iabs32(c[1] - HP->pp[i][0]) < 4096 && iabs32(c[3] - HP->pp[i][2]) < 4096 && iabs32(c[2] - HP->pp[i][1]) < 4096) {
            c[0xA8u / 4u] = HP->pp[i][0]; c[0xACu / 4u] = HP->pp[i][1]; c[0xB0u / 4u] = HP->pp[i][2];
        }
    }
}
static void hp_meas_post(uint32_t car) __attribute__((unused));
static void hp_meas_post(uint32_t car)
{
    uint32_t i = hp_idx(car);
    if (i >= HP_CARS) return;
    volatile int32_t *c = (volatile int32_t *)car;
    HP->pp[i][0] = c[0xA8u / 4u]; HP->pp[i][1] = c[0xACu / 4u]; HP->pp[i][2] = c[0xB0u / 4u]; HP->ppv[i] = 1;
}
#else
static void hp_meas_post(uint32_t car) { (void)car; }
static void hp_meas(uint32_t car, uint32_t half) __attribute__((unused));
static void hp_meas(uint32_t car, uint32_t half)
{
    uint32_t i = hp_idx(car);
    if (i >= HP_CARS) return;
    volatile int32_t *c = (volatile int32_t *)car;
    int32_t p0 = c[0xA8u / 4u], p1 = c[0xACu / 4u], p2 = c[0xB0u / 4u];
    if (half && HP->ppv[i] == 2u) {              /* two half-step calls back: a whole step */
        if (iabs32(c[1] - HP->pp[i][0]) < 4096 && iabs32(c[3] - HP->pp[i][2]) < 4096 && iabs32(c[2] - HP->pp[i][1]) < 4096) {
            c[0xA8u / 4u] = HP->pp[i][0]; c[0xACu / 4u] = HP->pp[i][1]; c[0xB0u / 4u] = HP->pp[i][2];
        }
    } else if (half && HP->ppv[i] == 1u) {       /* the call before was a whole step's: this half doubled */
        if (iabs32(c[1] - p0) < 2048 && iabs32(c[3] - p2) < 2048 && iabs32(c[2] - p1) < 2048) {
            c[0xA8u / 4u] = 2 * p0 - c[1]; c[0xACu / 4u] = 2 * p1 - c[2]; c[0xB0u / 4u] = 2 * p2 - c[3];
        }
    }
    HP->pp[i][0] = p0; HP->pp[i][1] = p1; HP->pp[i][2] = p2; HP->ppv[i] = half ? 2u : 1u;
}
#endif
/* the camera mode (race+0x94; MBX round 41): 2 the chase camera behind the car while racing,
 * 0x17 the start's fly-in, 0x18 the crash camera, 0x2A the helicopter's. Only the chase camera
 * runs at an odd half-step: the others keep timers of their own, which an extra call advances
 * (the fly-in ran 1.5x fast and the race started 15 steps early). A guess that 2 is the only
 * camera that follows the car: the game's other views, if it has any, are not yet seen */
#define HP_CAMMODE(race) (*(volatile uint32_t *)((race) + 0x94u) & 0xFFu)
static uint32_t __attribute__((unused)) hp_halve(uint32_t car)
{
    uint32_t i = hp_idx(car);
    return i < HP_CARS && HP->halve[i];
}
static const struct hprate *hp_ent(uint32_t f) { return f < HP_N1 ? &hp_k1[f] : &hp_k3[f - HP_N1]; }
#ifdef A4_KINEPHYS
/* round 44 (r44 on the phone: jumps short, turbos short, the other cars lagging and weaving --
 * the game's car calls run at every render ran everything the census never saw at the frame
 * rate): no game code at a render. The game's step computes everything as in a normal build;
 * a render moves each car along the velocity that step measured (+010/+018) and turns it at
 * that step's rate, by the time since the last render, and keeps it (nothing taken back); the
 * height and the wheels are moved for the drawing only. The step's own move (+1DE78 / +1F1F8,
 * position and heading, and +1EA64's heading) is then scaled to the rest of the step, and
 * +1EA64 measures the move the game itself made, so its speed model never sees part of a step */
static int32_t hp_wrap(int32_t d, int32_t circ) { d &= circ - 1; return d >= circ / 2 ? d - circ : d; }
static int32_t iabs32(int32_t x);
static void __attribute__((unused)) hp_kmove(uint32_t car, uint32_t f16, uint32_t first, uint32_t qt)
{
    uint32_t i = hp_idx(car);
    if (i >= HP_CARS) return;
    volatile int32_t *c = (volatile int32_t *)car;
    if (first) {
        int32_t hd = c[0x678u / 4u], w = c[0x020u / 4u], t = 0, u = 0;
        if (HP->hds[i] + 1u == HP->wstep) {
            t = hp_wrap(hd - HP->hdl[i], 2048); if (t >= 256 || t <= -256) t = 0;
            u = hp_wrap(w - HP->wl[i], 4096); if (u >= 1024 || u <= -1024) u = 0;
        }
        HP->turn[i] = t; HP->wr[i] = u; HP->hdl[i] = hd; HP->wl[i] = w; HP->hds[i] = HP->wstep;
        HP->yW[i] = c[2]; HP->dW[i] = c[0xD0u / 4u]; HP->wW[i] = w; HP->disp[i] = 1;
    }
    int32_t vx = c[0x10u / 4u], vz = c[0x18u / 4u], vy = c[0x14u / 4u], moved = 0;
    if (iabs32(vx) < 2048 && iabs32(vz) < 2048) {
        int32_t dx = hp_frac(vx, f16, &HP->kc[i][0]), dz = hp_frac(vz, f16, &HP->kc[i][1]);
        c[1] += dx; c[3] += dz; HP->ks[i][0] += dx; HP->ks[i][1] += dz;
        moved = vx || vz;
    }
    if (HP->turn[i]) { c[0x678u / 4u] = (c[0x678u / 4u] + hp_frac(HP->turn[i], f16, &HP->kc[i][2])) & 2047; moved = 1; }
    if (moved) HP->ran[i] = 1;
    if (HP->disp[i]) {               /* drawn only: put back before the whole step */
        int32_t dy = iabs32(vy) < 1024 ? (int32_t)(((int64_t)vy * (int32_t)qt) >> 16) : 0;
        c[2] = HP->yW[i] + dy; c[0xD0u / 4u] = HP->dW[i] + dy;
        c[0x020u / 4u] = (HP->wW[i] + (int32_t)(((int64_t)HP->wr[i] * (int32_t)qt) >> 16)) & 4095;
    }
}
#ifdef A4_KINEDRAW
/* round 46 (r45 on the calibrated bench: its committed moves parted its path from the game's at the
 * first step -- 200 units sideways by the first curve, which it then took ten steps late, wrecking in
 * all three real-time races): the frames are DRAWN only. Each car is drawn from the state its step
 * left -- along that step's velocity (+010/+018: exactly its move, the heading being the velocity's
 * direction, median 1 /2048), turned by A4_KD_TURN of that step's turn (the game turns in pulses of
 * ~20 /2048 a step, 2-4 steps long: 3/4 halves the misses at a pulse's ends, jt_n), the height on
 * its vertical velocity but never under the ground drawn on by its own last change -- and every
 * drawn word is put back before the next step, which runs as in a normal build.
 * The snap fix: where a new step's state disagrees with what the frames drew (a turn pulse's end, a
 * contact, a key let go), the first frame after it continues from where the last frame drew the
 * car, at the new velocity, and the difference fades out over A4_KD_TAU (a step): no frame jumps,
 * and the picture is the game's own again within a step. Not across a cut (a respawn, a reset) */
#ifndef A4_KD_TURN
#ifdef A4_ENGDRAW
#define A4_KD_TURN 65536u            /* engine map 1: the heading turns in whole pulses (0, +-20 a step, run y1:
                                      * the next step repeats the last turn in 87% of steps) -- not 3/4 */
#else
#define A4_KD_TURN 49152u            /* 16.16 */
#endif
#endif
#ifndef A4_KD_TAUSH
#define A4_KD_TAUSH 16u              /* the fade: 1 << A4_KD_TAUSH of 1/65536 step (16: one step) */
#endif
#define KD_CIRC (2048 << 8)
static int32_t kd_fade(int32_t e, uint32_t dt)
{
    if (dt >= (1u << A4_KD_TAUSH)) return 0;
    return (int32_t)(((int64_t)e * (int64_t)((1u << A4_KD_TAUSH) - dt)) >> A4_KD_TAUSH);
}
static int32_t kd_wrap(int32_t d) { d &= KD_CIRC - 1; return d >= KD_CIRC / 2 ? d - KD_CIRC : d; }
/* round 47: (ox, oz) turned by dh (/2048, the heading's convention: h = -atan2(z, x) - 512, so a
 * turn of dh is -dh in x-z), |dh| < 512; sin and cos by their series in Q30, no divide */
static void kd_rot(int32_t ox, int32_t oz, int32_t dh, int32_t *rx, int32_t *rz)
{
    int64_t t = -(int64_t)dh * 3294199;                          /* radians, Q30 (2 pi / 2048 = 0.0030679616) */
    int64_t t2 = (t * t) >> 30, t3 = (t2 * t) >> 30, t4 = (t2 * t2) >> 30, t5 = (t4 * t) >> 30, t6 = (t4 * t2) >> 30;
    int64_t sn = t - ((t3 * 178956971) >> 30) + ((t5 * 8947849) >> 30);                       /* t - t^3/6 + t^5/120 */
    int64_t cs = ((int64_t)1 << 30) - (t2 >> 1) + ((t4 * 44739243) >> 30) - ((t6 * 1491308) >> 30);   /* 1 - t^2/2 + t^4/24 - t^6/720 */
    *rx = (int32_t)(((int64_t)ox * cs - (int64_t)oz * sn) >> 30);
    *rz = (int32_t)(((int64_t)ox * sn + (int64_t)oz * cs) >> 30);
}
/* one frame of the corrector for n words: m the model now, v its velocity a step (both 1/256), w the
 * words on the circle; first: the first frame since a new step; returns 0 at a cut */
static uint32_t kd_correct(volatile int32_t *E, volatile int32_t *D, volatile uint32_t *T, volatile uint8_t *V,
                           int32_t *m, const int32_t *v, uint32_t n, uint32_t circ, uint32_t first, uint32_t tnow,
                           const int32_t *cut)
{
    uint32_t dt = tnow - *T, ok = 1;
#ifdef A4_KD_NOCORR
    first = 0; *V = 0;               /* bench: the corrector off, each frame the model alone */
#endif
    if (!*V || dt > (2u << 16)) {
        for (uint32_t w = 0; w < n; w++) E[w] = 0;
    } else if (first) {
        for (uint32_t w = 0; w < n; w++) {
#ifdef A4_ENGDRAW
            /* engine map 1: the last frame's point continued -- its model part at the new velocity,
             * its offset still fading. Continuing the drawn point whole (as r46-r47) carried the
             * offset across every step whose first frame was its only one, and froze what the
             * velocity missed (a falling car whose +014 is 0) */
            int32_t p = D[w] - E[w] + (int32_t)(((int64_t)v[w] * (int64_t)dt) >> 16) + kd_fade(E[w], dt);
#else
            int32_t p = D[w] + (int32_t)(((int64_t)v[w] * (int64_t)dt) >> 16);
#endif
            int32_t e = p - m[w];
            if ((circ >> w) & 1u) e = kd_wrap(e);
            E[w] = e;
            if (iabs32(e) > cut[w]) ok = 0;
        }
        if (!ok) for (uint32_t w = 0; w < n; w++) E[w] = 0;
        else { HP->kn++; if (iabs32(E[0]) > (16 << 8) || (n > 1u && iabs32(E[1]) > (16 << 8)) || (n > 2u && iabs32(E[2]) > (16 << 8))) HP->kbig++; }
    } else {
        for (uint32_t w = 0; w < n; w++) E[w] = kd_fade(E[w], dt);
    }
    if (!ok) HP->kcut++;
    for (uint32_t w = 0; w < n; w++) { m[w] += E[w]; D[w] = m[w]; }
    *T = tnow; *V = 1;
    return ok;
}
static void hp_kdraw(uint32_t car, uint32_t qt, uint32_t first, uint32_t tnow)
{
    uint32_t i = hp_idx(car);
    if (i >= HP_CARS) return;
    volatile int32_t *c = (volatile int32_t *)car;
    if (first) {                     /* the state the step left: drawn from, and put back before the next step */
        int32_t hd = c[0x678u / 4u], w = c[0x020u / 4u], d = c[0xD0u / 4u], t = 0, u = 0, g = 0;
#ifdef A4_GAPRATE
        /* round 49 (r48 on the phone, the LA climb: two steps between frames, the road up 225, the
         * rates zeroed for not being a step apart, the car held 160 under it): over a gap of n
         * steps a rate is the change over n */
        uint32_t gap = HP->wstep - HP->hds[i];
        if (gap >= 1u && gap <= 3u) {
            int32_t gt = hp_wrap(hd - HP->hdl[i], 2048), gu = hp_wrap(w - HP->wl[i], 4096), gg = d - HP->dWp[i];
            if (gap == 2u) { gt /= 2; gu /= 2; gg /= 2; }
            else if (gap == 3u) { gt = (int32_t)(((int64_t)gt * 21845) >> 16); gu = (int32_t)(((int64_t)gu * 21845) >> 16); gg = (int32_t)(((int64_t)gg * 21845) >> 16); }
            t = gt; if (t >= 256 || t <= -256) t = 0;
            u = gu; if (u >= 1024 || u <= -1024) u = 0;
            g = gg; if (g >= 512 || g <= -512) g = 0;
#else
        if (HP->hds[i] + 1u == HP->wstep) {
            t = hp_wrap(hd - HP->hdl[i], 2048); if (t >= 256 || t <= -256) t = 0;
            u = hp_wrap(w - HP->wl[i], 4096); if (u >= 1024 || u <= -1024) u = 0;
            g = d - HP->dWp[i]; if (g >= 512 || g <= -512) g = 0;
#endif
#ifdef A4_ENGDRAW
            /* a respawn moves the car thousands of units in one step (run y2b, slot 5, step 978:
             * 4,600 in x-z and +452 in y, the road's "slope" then 471 a step): nothing of it is a rate */
#ifdef A4_GAPRATE
            if (iabs32(c[1] - HP->xW[i]) > 2048 * (int32_t)gap || iabs32(c[3] - HP->zW[i]) > 2048 * (int32_t)gap) t = u = g = 0;
            /* round 49 (gap_y5: the player put back on the road after its crash, 708 units in a step with
             * its velocity 0 -- under the 1,024 cut, the corrector drew it at the old place a frame, the
             * camera with it): +010/+018 is the step's own move (exact in 99% of steps), so a step that
             * moved otherwise by over 256 units is a placement, and the frame does not bridge it */
            if (iabs32(c[1] - (HP->xW[i] + c[0x10u / 4u] * (int32_t)gap)) > 256 || iabs32(c[3] - (HP->zW[i] + c[0x18u / 4u] * (int32_t)gap)) > 256) {
                t = u = g = 0; HP->kV[i] = 0;
            }
#else
            if (iabs32(c[1] - HP->xW[i]) > 2048 || iabs32(c[3] - HP->zW[i]) > 2048) t = u = g = 0;
#endif
#endif
        }
        HP->turn[i] = t; HP->wr[i] = u; HP->hdl[i] = hd; HP->wl[i] = w; HP->hds[i] = HP->wstep;
        HP->gW[i] = g; HP->dWp[i] = d;
        HP->xW[i] = c[1]; HP->yW[i] = c[2]; HP->zW[i] = c[3]; HP->hW[i] = hd; HP->dW[i] = d; HP->wW[i] = w;
        HP->aW[i] = c[0xA8u / 4u]; HP->bW[i] = c[0xB0u / 4u];
        HP->disp[i] = 1;
    }
    if (!HP->disp[i]) return;
#ifdef A4_SCRIPTHOLD
    if (HP->hold) { qt = 0; HP->kV[i] = 0; }   /* drawn at the step, the corrector started again */
#endif
    int32_t vx = c[0x10u / 4u], vy = c[0x14u / 4u], vz = c[0x18u / 4u];
    if (iabs32(vx) >= 2048 || iabs32(vz) >= 2048) vx = vz = 0;
#ifdef A4_ENGDRAW
    /* engine map 1 (ENGINE.md): +014 is measured before the vertical logic moves y, so the height
     * moves by the engine's own rule -- in the air y += +A58 next step (+1EE28), on the road it
     * follows +0D0 (its last change, the road's slope under the car) */
    vy = HP->yW[i] > HP->dW[i] ? c[0xA58u / 4u] : HP->gW[i];
#endif
    if (iabs32(vy) >= 1024) vy = 0;
    int32_t tv = (int32_t)(((int64_t)HP->turn[i] * (int32_t)A4_KD_TURN) >> 16);
    int32_t v[4] = { vx << 8, vz << 8, tv << 8, vy << 8 };
    int32_t m[4];
    m[0] = (HP->xW[i] << 8) + (int32_t)(((int64_t)v[0] * (int32_t)qt) >> 16);
    m[1] = (HP->zW[i] << 8) + (int32_t)(((int64_t)v[1] * (int32_t)qt) >> 16);
    m[2] = (HP->hW[i] << 8) + (int32_t)(((int64_t)v[2] * (int32_t)qt) >> 16);
    m[3] = (HP->yW[i] << 8) + (int32_t)(((int64_t)v[3] * (int32_t)qt) >> 16);
    static const int32_t cut[4] = { 1024 << 8, 1024 << 8, 128 << 8, 512 << 8 };
    kd_correct(HP->kE[i], HP->kD[i], &HP->kT[i], &HP->kV[i], m, v, 4u, 4u, first, tnow, cut);
    /* the ground under the drawn car: on it, the car; in the air, drawn on by its own last change */
    int32_t air = HP->yW[i] > HP->dW[i];
#ifdef A4_ENGDRAW
    /* the road drawn on by its own last change; a car in the air lands on it (+1EF0A), a car on the
     * road keeps the step's height above it (the floor is +0D0 - 64, +1EF26) */
    int32_t gnd = (HP->dW[i] << 8) + (int32_t)(((int64_t)(HP->gW[i] << 8) * (int32_t)qt) >> 16);
    if (!air) gnd += (HP->yW[i] - HP->dW[i]) << 8;
    if (air && m[3] < gnd) { m[3] = gnd; HP->kD[i][3] = gnd; }
#else
    int32_t gnd = (HP->dW[i] << 8) + (air ? (int32_t)(((int64_t)(HP->gW[i] << 8) * (int32_t)qt) >> 16) : (m[3] - (HP->yW[i] << 8)));
    if (m[3] < gnd) { m[3] = gnd; HP->kD[i][3] = gnd; }
#endif
    int32_t x = (m[0] + 128) >> 8, z = (m[1] + 128) >> 8;
    c[1] = x; c[3] = z;
    c[0xA8u / 4u] = HP->aW[i] + (x - HP->xW[i]); c[0xB0u / 4u] = HP->bW[i] + (z - HP->zW[i]);
    c[0x678u / 4u] = ((m[2] + 128) >> 8) & 2047;
    c[2] = (m[3] + 128) >> 8; c[0xD0u / 4u] = (gnd + 128) >> 8;
#ifdef A4_ENGDRAW
    {   /* +020 is the car's fraction along its segment (+24524 writes it, clamped; ENGINE.md), read by
         * the render with the segment +01C: past the segment's end it stays at the end -- wrapped,
         * it put the car back at the start of the segment it is leaving */
        int32_t fw = HP->wW[i] + (int32_t)(((int64_t)HP->wr[i] * (int32_t)qt) >> 16);
        c[0x020u / 4u] = fw < 0 ? 0 : fw > 4095 ? 4095 : fw;
    }
#else
    c[0x020u / 4u] = (HP->wW[i] + (int32_t)(((int64_t)HP->wr[i] * (int32_t)qt) >> 16)) & 4095;
#endif
}
#endif
/* the whole step, a car that moved at the renders: before its call */
static void __attribute__((unused)) hp_kpre(uint32_t car, int32_t *k0, uint32_t kind)
{
    uint32_t i = hp_idx(car);
    volatile int32_t *c = (volatile int32_t *)car;
    k0[0] = c[1]; k0[1] = c[3]; k0[2] = c[0x678u / 4u];
    if (kind == 3u && i < HP_CARS) {   /* +1EA64 measures place less +0A8/+0B0: the game's own move */
        c[0xA8u / 4u] += HP->kerr[i][0]; c[0xB0u / 4u] += HP->kerr[i][1];
        HP->kerr[i][0] = HP->kerr[i][1] = 0;
    }
}
static void __attribute__((unused)) hp_kpost(uint32_t car, const int32_t *k0, uint32_t kind, uint32_t f16)
{
    uint32_t i = hp_idx(car);
    if (i >= HP_CARS) return;
    volatile int32_t *c = (volatile int32_t *)car;
    if (kind == 1u || kind == 2u) {  /* the move: the rest of the step's share of it */
        int32_t dx = c[1] - k0[0], dz = c[3] - k0[1];
        if (iabs32(dx) < 2048 && iabs32(dz) < 2048) {
            int32_t sx = hp_frac(dx, f16, &HP->kc[i][0]), sz = hp_frac(dz, f16, &HP->kc[i][1]);
            c[1] = k0[0] + sx; c[3] = k0[1] + sz;
            HP->kerr[i][0] = HP->ks[i][0] + sx - dx; HP->kerr[i][1] = HP->ks[i][1] + sz - dz;
        }
        HP->ks[i][0] = HP->ks[i][1] = 0;
    }
    if (kind <= 3u) {                /* the turn */
        int32_t dh = hp_wrap(c[0x678u / 4u] - k0[2], 2048);
        if (dh < 256 && dh > -256) c[0x678u / 4u] = (k0[2] + hp_frac(dh, f16, &HP->kc[i][2])) & 2047;
    }
}
#endif
static void hp_phys_pre(uint32_t car, int32_t *sv) __attribute__((unused));
static void hp_phys_pre(uint32_t car, int32_t *sv)
{
    const volatile int32_t *c = (const volatile int32_t *)car;
    for (uint32_t f = 0; f < HP_NR; f++) sv[f] = c[hp_ent(f)->off / 4u];
}
/* which: 1 the +1DE78 words, 2 the +1EA64 words, 3 both (+1F1F8 / +1FD0E: not measured, a guess) */
static void hp_phys_post(uint32_t car, const int32_t *sv, uint32_t which, uint32_t f16) __attribute__((unused));
static void hp_phys_post(uint32_t car, const int32_t *sv, uint32_t which, uint32_t f16)
{
    uint32_t i = hp_idx(car);
    if (i >= HP_CARS) return;
    volatile int32_t *c = (volatile int32_t *)car;
    for (uint32_t f = 0; f < HP_NR; f++) {
        if (!(which & (f < HP_N1 ? 1u : 2u))) continue;
        const struct hprate *e = hp_ent(f);
        uint32_t w = e->off / 4u, circ = e->circ;
        int32_t q = (int32_t)e->q, d = c[w] - sv[f];
        if (circ) { d &= (int32_t)circ - 1; if (d >= (int32_t)circ / 2) d -= (int32_t)circ; }
        if (d > (int32_t)e->jump || d < -(int32_t)e->jump) continue;        /* an event (a reset, a respawn): as computed */
        if (e->pos && d <= 0) continue;
        int32_t u = d;               /* d in units of q (1 or 12; no divide in the blob: t/12 = t*43691 >> 19, exact below 2^17) */
        if (q == 12) {
            int32_t t = d < 0 ? -d : d, k = (int32_t)(((uint32_t)t * 43691u) >> 19);
            if (k * 12 != t) continue;                     /* not in its quantum: as computed */
            u = d < 0 ? -k : k;
        }
        int32_t v = sv[f] + q * hp_frac(u, f16, &HP->rc[i][f]);
        c[w] = circ ? (v & ((int32_t)circ - 1)) : v;
    }
}
#endif
#if defined(A4_HALFPHYS) && (defined(A4_HPCENSUS) || defined(A4_HPPROBE) || defined(A4_HPTRACE) || defined(A4_CAMCENSUS))
/* bench (round 42): the census (A4_HPCENSUS), the nudge probe (A4_HPPROBE) and the trace
 * (A4_HPTRACE) emit here: EKA2L1_PCMEM ats at its address, r0 0x10 (a header), r1 0xC7C (the
 * car before), r2 0xC7C (after) */
__attribute__((noinline)) void a4_hp_census(const void *hdr, const void *before, const void *after)
{
    __asm__ volatile("" : : "r"(hdr), "r"(before), "r"(after) : "memory");
}
#endif
#if defined(A4_HALFPHYS) && defined(A4_HPTRACE)
/* bench (round 43): the player car's calls around the start (its +0E0 up to 4), whole steps and
 * odd half-steps both, the car before and after each: [0] 'HPTR', [1] kind | replaying << 8 |
 * the half-step << 16, [2] the call's r1 (the key bits), [3] the car */
#define HP_TRB (A4_GL_HP + 0x1000u)
static uint32_t hp_tr_on(uint32_t car)
{
    uint32_t race = HP->race;
    if (!race || HP->ntr >= 400u) return 0;
    uint32_t pl = *(volatile uint32_t *)(race + 0x390), pidx = pl ? *(volatile uint32_t *)(pl + 0xC) : 99u;
#ifdef A4_HPTR_SLOT                  /* another race slot's car instead of the player's */
    pidx = (A4_HPTR_SLOT);
#endif
    if (pidx >= 16u || *(volatile uint32_t *)(race + 0x394 + 4u * pidx) != car) return 0;
    int32_t e0 = *(volatile int32_t *)(car + 0xE0u), r8 = *(volatile int32_t *)(car + 0x7D8u);
#ifdef A4_HPTR_LO                    /* a window of the player's steps since the start instead */
    (void)r8;
    return e0 >= (A4_HPTR_LO) && e0 <= (A4_HPTR_HI);
#elif defined(A4_HPTR_H0)            /* or of half-steps */
    uint32_t hs = ((volatile uint32_t *)A4_GL_HSTEP)[0];
    (void)e0; (void)r8;
    return hs >= (A4_HPTR_H0) && hs <= (A4_HPTR_H1);
#else
    return e0 <= 4 && (e0 > 0 || r8 != 631);
#endif
}
static void hp_tr_pre(uint32_t car)
{
    for (uint32_t i = 0; i < 0xC7Cu / 4u; i++) ((volatile uint32_t *)HP_TRB)[i] = ((volatile uint32_t *)car)[i];
}
static void hp_tr_post(uint32_t kind, uint32_t car, uint32_t a1)
{
    volatile uint32_t *hd = (volatile uint32_t *)(HP_TRB + 0xC80u);
    hd[0] = 0x52545048u; hd[1] = kind | HP->replaying << 8 | (((volatile uint32_t *)A4_GL_HSTEP)[0] & 0xFFFFu) << 16;
    hd[2] = a1; hd[3] = car;
    a4_hp_census((const void *)hd, (const void *)HP_TRB, (const void *)car);
    HP->ntr++;
}
#define HP_TR_PRE(car) uint32_t tr_ = hp_tr_on(car); if (tr_) hp_tr_pre(car)
#define HP_TR_POST(k, car, a1) if (tr_) hp_tr_post(k, car, a1)
#else
#define HP_TR_PRE(car)
#define HP_TR_POST(k, car, a1)
#endif
#ifdef A4_HALFPHYS
/* the car calls at a whole step: each halved for a car whose calls ran at the odd half-step before
 * (HP_HALVE); `which` the words (1 +1DE78's, 2 +1EA64's, 3 both: +1F1F8 / +1FD0E not measured, a
 * guess). +1EA64's measuring history is kept at every call */
#ifdef A4_KINEDRAW
#define HP_HOOK(kind, which, call)   /* round 46: the step as in a normal build */
#elif defined(A4_KINEPHYS)
#define HP_HOOK(kind, which, call) \
    if (!HP->replaying) { \
        uint32_t hv = HP_HALVE; \
        int32_t k0[3]; \
        if (hv) hp_kpre(a0, k0, kind); \
        int r = call; \
        if (hv) hp_kpost(a0, k0, kind, HP_FWHOLE); \
        return r; \
    }
#else
#define HP_HOOK(kind, which, call) \
    if (!HP->replaying) { \
        HP_TR_PRE(a0); \
        uint32_t hv = HP_HALVE; \
        int32_t sv[HP_NR]; \
        if ((kind) == 3u) hp_meas(a0, hv); \
        if (hv) hp_phys_pre(a0, sv); \
        int r = call; \
        if (hv) hp_phys_post(a0, sv, which, HP_FWHOLE); \
        if ((kind) == 3u) hp_meas_post(a0); \
        HP_TR_POST((kind) | hv << 4, a0, a1); \
        return r; \
    }
#endif
#else
#define HP_HOOK(kind, which, call)
#endif
int a4_h_1de78(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t s0)
{
    hcall_rec(1, a0, a1, a2, a3, s0);
    HP_HOOK(1u, 1u, ((fn5)GAMEFN(a4_hc_1de78))(a0, a1, a2, a3, s0))
    return ((fn5)GAMEFN(a4_hc_1de78))(a0, a1, a2, a3, s0);
}
int a4_h_1f1f8(uint32_t a0, uint32_t a1, uint32_t a2)
{
    hcall_rec(2, a0, a1, a2, 0, 0);
    HP_HOOK(2u, 3u, ((fn3)GAMEFN(a4_hc_1f1f8))(a0, a1, a2))
    return ((fn3)GAMEFN(a4_hc_1f1f8))(a0, a1, a2);
}
int a4_h_1ea64(uint32_t a0, uint32_t a1, uint32_t a2)
{
    hcall_rec(3, a0, a1, a2, 0, 0);
    HP_HOOK(3u, 2u, ((fn3)GAMEFN(a4_hc_1ea64))(a0, a1, a2))
    return ((fn3)GAMEFN(a4_hc_1ea64))(a0, a1, a2);
}
int a4_h_1fd5c(uint32_t a0, uint32_t a1)
{
    hcall_rec(4, a0, a1, 0, 0, 0);
    HP_HOOK(4u, 2u, ((fn2)GAMEFN(a4_hc_1fd5c))(a0, a1))
    return ((fn2)GAMEFN(a4_hc_1fd5c))(a0, a1);
}
int a4_h_1fd0e(uint32_t a0)
{
    uint32_t a1 = 0;
    hcall_rec(5, a0, 0, 0, 0, 0);
    HP_HOOK(5u, 3u, ((fn1)GAMEFN(a4_hc_1fd0e))(a0))
    (void)a1;
    return ((fn1)GAMEFN(a4_hc_1fd0e))(a0);
}
static void hcall_replay(void) __attribute__((unused));
static void hcall_replay(void)
{
    volatile uint32_t *h = (volatile uint32_t *)A4_GL_HSTEP;
    h[3] = 1;
    for (uint32_t i = 0; i < h[2]; i++) {
        volatile uint32_t *c = (volatile uint32_t *)A4_GL_HCALL + 6u * i;
        switch (c[0]) {
        case 1: ((fn5)GAMEFN(a4_hc_1de78))(c[1], c[2], c[3], c[4], c[5]); break;
        case 2: ((fn3)GAMEFN(a4_hc_1f1f8))(c[1], c[2], c[3]); break;
        case 3: ((fn3)GAMEFN(a4_hc_1ea64))(c[1], c[2], c[3]); break;
        case 4: ((fn2)GAMEFN(a4_hc_1fd5c))(c[1], c[2]); break;
        case 5: ((fn1)GAMEFN(a4_hc_1fd0e))(c[1]); break;

        }
    }
    h[3] = 0;
}
#endif
#ifdef A4_HALFSPEC
/* Milestone 2b, the speculative half-step: on an odd half-step every car the whole step
 * touched (the recorded calls' r0) and the key object are saved, the keys taken (+25E8),
 * the step's car calls replayed -- a whole step ahead, with the keys of now -- and each car
 * set half way between the saved state and that one in its position and orientation words;
 * that is what the render after it draws. At the next even half-step everything saved is put
 * back and the game's own step runs as if nothing had happened: whole steps stay exactly the
 * original game's, a key shows half a step sooner, and the screen gets a frame between. */
#define A4_GL_SPEC 0x00447000u       /* [0] cars, [1] pending, [2] K, [3] the race, [4..19] the cars, [24] in the
                                      * speculative camera update (A4_SPECCAMSAFE), [30] stale drops,
                                      * [32..47] K's words, [64..] the race object (0x9EC bytes) */
#define SPEC_RACESZ 0x9ECu           /* the race object (+778A allocates it, constructor +16BDC) */
_Static_assert(A4_GL_SPEC + 0x100u + SPEC_RACESZ <= 0x00448000u, "the race snapshot runs into the cars'");
#define A4_GL_SNAP 0x00448000u       /* the cars, 0xC7C bytes each (every 0xC7C allocation: +17A54 ...) */
#define SPEC_CARSZ 0xC7Cu
#define SPEC_MAXCARS 16u
_Static_assert(A4_GL_SNAP + SPEC_MAXCARS * SPEC_CARSZ <= 0x00460000u, "car snapshots past 0x460000 (A4_SCRATCH_SIZE 0x5F000)");
extern const int32_t a4_hc_25e8, a4_hc_12dec;   /* a4_gl.s: the key handler, the camera's update */
static void words_copy(volatile uint32_t *d, const volatile uint32_t *s, uint32_t n) { while (n--) *d++ = *s++; }
/* round 33: the speculative frame drawn at the fraction of a step the game's clock has reached
 * (q16 of 65536), not at the half: a + (b - a) * q16, angles the short way round */
static int32_t lerp_q16(int32_t a, int32_t b, uint32_t q16) { return a + (int32_t)(((int64_t)(b - a) * (int64_t)q16) >> 16); }
static int32_t lerp_ang(int32_t a, int32_t b, uint32_t q16, uint32_t circle)
{
    int32_t d = (int32_t)((uint32_t)(b - a) & (circle - 1u));
    if (d >= (int32_t)(circle >> 1)) d -= (int32_t)circle;
    return a + (int32_t)(((int64_t)d * (int64_t)q16) >> 16);
}
static int32_t iabs32(int32_t x) { return x < 0 ? -x : x; }
#define SPEC_CUT 1024                /* more than this in x or z in one step: a respawn or a camera cut,
                                      * drawn where it was rather than half way across */
/* The car words the render draws from that the replayed step moves (round 31 phone follow-up:
 * EKA2L1_RWATCH gated to the render, rcar9, against each word's share on the speculative
 * frames, spec7): the position (+004..+00C and its copies +0A8..+0B0, +0D0), the speed +7D0
 * and +7F8 plainly; the heading +678 (the render's matrix +6D4..+718 is exactly it on a 2048
 * circle) and the wheels' spin +020 (0..4095, at most 666 a step at 135 km/h) the short way
 * round. Until this round the list held +04C..+06C as "orientation" -- the collision box,
 * which the replayed calls never move -- and not the heading: every car turned a whole step
 * on the speculative frames and not at all on the next. Also read by the render and moved
 * by the step: +A20/+A24 (-21..40, a steering or lean amount) and +A7C (-8..8, decaying),
 * plainly. Left at the step's value: the track segment +01C, the gear +7BC, the per-render
 * flip +66C, +A78 (0 or 32), +AC0 (4..7), the bits +B28 (indices and flags). The physics-side
 * matrix +6D4..+718 is not read by the render (rone8: it reads +678 at +A44C). */
static const uint16_t spec_mid[] __attribute__((unused)) = { 0x004, 0x008, 0x00C, 0x0A8, 0x0AC, 0x0B0, 0x0D0, 0x7D0, 0x7F8,
                                     0xA20, 0xA24, 0xA7C };   /* read by the render (rone8): signed, small */
/* Round 33: which words the speculation changed (spec_run marks them before the render), so a
 * render whose own side effects are kept -- the first after each whole step, as the game draws
 * once a step -- has only those words put back: the render's per-car flip +66C, its countdown
 * +164, the race's +0x388 advance once a step as in the game. Every other render is undone
 * whole. (spec11: with a render-time speculation in every frame and the whole restore, the
 * flip never advanced -- 167 of 167 steps differing from the normal build.) */
#ifdef A4_SPECTRAFFIC
#define TRAF_OFF   0x6F4Cu           /* the traffic array in G (+14ADE/+14BEA, +FB16: literal 0x6F4C) */
#define TRAF_N     16u               /* its slots (+FAD8, +FB1C, +149CE loops) */
#define TRAF_CARSZ 0x148u            /* a CTrafficCar (stride in every loop over the array) */
#define TRAF_SZ    (TRAF_N * TRAF_CARSZ)
#define A4_GL_TSNAP 0x00458400u      /* the traffic array's snapshot */
#define A4_GL_TBM   (A4_GL_TSNAP + TRAF_SZ)   /* its bitmap, one bit a word */
#define A4_GL_TPREV 0x0045A000u      /* the array just before the last real +FB12 (a4_h_fb12) */
_Static_assert(A4_GL_TSNAP >= 0x00456000u + 16u + 256u * 32u && A4_GL_TBM + (TRAF_SZ / 4u + 31u) / 32u * 4u <= A4_GL_TPREV
               && A4_GL_TPREV + TRAF_SZ <= 0x00460000u, "the traffic snapshot");
#endif
#define A4_GL_SPECBM 0x00455000u     /* bitmaps: per car 25 words (0xC7C / 4 = 799 bits), the race 20 (635), K 1 */
#define BM_CAR 25u
#define BM_RACE 20u
_Static_assert(A4_GL_SNAP + SPEC_MAXCARS * SPEC_CARSZ <= A4_GL_SPECBM, "the bitmaps over the car snapshots");
_Static_assert(A4_GL_SPECBM + (SPEC_MAXCARS * BM_CAR + BM_RACE + 1u) * 4u <= 0x00460000u, "the bitmaps past 0x460000");
static void bm_mark(volatile uint32_t *bm, const volatile uint32_t *now, const volatile uint32_t *was, uint32_t n)
{
    for (uint32_t w = 0; w < n; w += 32u) {
        uint32_t m = 0;
        for (uint32_t b = 0; b < 32u && w + b < n; b++) if (now[w + b] != was[w + b]) m |= 1u << b;
        bm[w >> 5] = m;
    }
}
static void bm_restore(volatile uint32_t *dst, const volatile uint32_t *was, const volatile uint32_t *bm, uint32_t n)
{
    for (uint32_t w = 0; w < n; w += 32u) {
        uint32_t m = bm[w >> 5];
        while (m) { uint32_t b = (uint32_t)__builtin_ctz(m); dst[w + b] = was[w + b]; m &= m - 1u; }
    }
}
static void spec_restore(void)
{
    volatile uint32_t *sp = (volatile uint32_t *)A4_GL_SPEC, *bm = (volatile uint32_t *)A4_GL_SPECBM;
    if (!sp[1]) return;
    if (sp[20]) {                    /* keep the render's own changes: put back only the speculation's */
        for (uint32_t i = 0; i < sp[0]; i++)
            bm_restore((volatile uint32_t *)sp[4 + i], (volatile uint32_t *)(A4_GL_SNAP + i * SPEC_CARSZ), bm + i * BM_CAR, SPEC_CARSZ / 4u);
        if (sp[2]) bm_restore((volatile uint32_t *)sp[2], sp + 32, bm + SPEC_MAXCARS * BM_CAR + BM_RACE, 16);
        if (sp[3]) bm_restore((volatile uint32_t *)sp[3], sp + 64, bm + SPEC_MAXCARS * BM_CAR, SPEC_RACESZ / 4u);
#ifdef A4_SPECTRAFFIC
        if (sp[26]) bm_restore((volatile uint32_t *)sp[26], (volatile uint32_t *)A4_GL_TSNAP, (volatile uint32_t *)A4_GL_TBM, TRAF_SZ / 4u);
#endif
    } else {
        for (uint32_t i = 0; i < sp[0]; i++)
            words_copy((volatile uint32_t *)sp[4 + i], (volatile uint32_t *)(A4_GL_SNAP + i * SPEC_CARSZ), SPEC_CARSZ / 4u);
        if (sp[2]) words_copy((volatile uint32_t *)sp[2], sp + 32, 16);
        if (sp[3]) words_copy((volatile uint32_t *)sp[3], sp + 64, SPEC_RACESZ / 4u);
#ifdef A4_SPECTRAFFIC
        if (sp[26]) words_copy((volatile uint32_t *)sp[26], (volatile uint32_t *)A4_GL_TSNAP, TRAF_SZ / 4u);
#endif
    }
    sp[1] = 0; sp[20] = 0;
}
#ifdef A4_SPECTRAFFIC
/* Round 41 (r40 on the phone: "at high speed is hard to connect or crash against traffic cars"):
 * the traffic. Its 16 cars (CTrafficCar, 0x148 bytes each, an array at G+0x6F4C) are moved once
 * a step by +FB12(G, ...) from +14574, before the car loops -- a call the speculation never
 * made, so on every green frame the traffic stood at the last whole step while the player and
 * the scene were drawn up to a step ahead. +FB12 is not replayed: it also writes the track's
 * spawn records (+FCC6, bench lk43), outside anything saved. Instead the array is copied just
 * before each real +FB12 (A4_GL_TPREV), and a green frame draws each traffic car extrapolated
 * by q of its last whole step (spec_run), put back after the render like the cars. */
extern const int32_t a4_hc_fb12;
int a4_h_fb12(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t s0)
{
    volatile uint32_t *sp = (volatile uint32_t *)A4_GL_SPEC;
    words_copy((volatile uint32_t *)A4_GL_TPREV, (volatile uint32_t *)(a0 + TRAF_OFF), TRAF_SZ / 4u);
    sp[27] = a0 + TRAF_OFF;          /* the array, and that A4_GL_TPREV holds it a step back ... */
    sp[28] = ((volatile uint32_t *)A4_GL_HSTEP)[0];   /* ... as of this half-step (spec_run: only the step just past) */
    return ((fn5)GAMEFN(a4_hc_fb12))(a0, a1, a2, a3, s0);
}
#endif
#ifdef A4_FRAMEMARK
/* round 41, bench only: in each speculation, the player car as the last whole step left it
 * (x, z, heading +0x678, q16) and as the replayed step predicts it a whole step on (x, z,
 * heading, ms), and the camera likewise (saved x, z, predicted x, z; saved yaw, predicted yaw,
 * the cut flag), for EKA2L1_PCTRACE_REGS: the prediction against the next step's own state */
__attribute__((noinline)) void a4_spec_mark(uint32_t x, uint32_t z, uint32_t hd, uint32_t q16)
{
    __asm__ volatile("" : : "r"(x), "r"(z), "r"(hd), "r"(q16) : "memory");
}
__attribute__((noinline)) void a4_spec_mark2(uint32_t x, uint32_t z, uint32_t hd, uint32_t t)
{
    __asm__ volatile("" : : "r"(x), "r"(z), "r"(hd), "r"(t) : "memory");
}
__attribute__((noinline)) void a4_spec_mark3(uint32_t ox, uint32_t oz, uint32_t px, uint32_t pz)
{
    __asm__ volatile("" : : "r"(ox), "r"(oz), "r"(px), "r"(pz) : "memory");
}
__attribute__((noinline)) void a4_spec_mark4(uint32_t oyaw, uint32_t pyaw, uint32_t a, uint32_t b)
{
    __asm__ volatile("" : : "r"(oyaw), "r"(pyaw), "r"(a), "r"(b) : "memory");
}
/* bench (round 41, with A4_SPECGATE): what the speculation saves -- the race object, K, the
 * number of cars, then the cars four at a time -- for a leak check's classifier */
__attribute__((noinline)) void a4_spec_ptrs(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    __asm__ volatile("" : : "r"(a), "r"(b), "r"(c), "r"(d) : "memory");
}
/* bench (round 41, A4_SPECTRAFFIC): a traffic car as drawn -- its slot, x, z, the ms */
__attribute__((noinline)) void a4_traf_mark(uint32_t slot, uint32_t x, uint32_t z, uint32_t t)
{
    __asm__ volatile("" : : "r"(slot), "r"(x), "r"(z), "r"(t) : "memory");
}
#endif
#ifdef A4_INTERP
/* Round 41, after r41 on the phone ("they look like they snap back into position as I get
 * closer ... the same effect as the steering jitter"; the bench at the Veyron's speed with an
 * autopilot steering continuously: every one-frame reversal a frame drawn late in its step
 * toward a prediction the next step did not follow -- a key changed after it was drawn): the
 * frame drawn BETWEEN the last two whole steps instead of between the last one and a
 * prediction. Nothing is replayed (no keys taken, no car calls, no camera update); what a frame
 * shows the game has already done. One step (85 ms) later on screen than the speculation. The
 * state before each whole step is captured here at its even half-step (ip_capture), the state
 * after it is the speculation's snapshot (spec_run), and the save and restore around the render
 * are the speculation's, so the render's own once-a-step changes are kept or undone as before. */
#define A4_GL_IP 0x0045C000u         /* [0] the race, [1..15] the car slots' cars, [16..] 15 x 16 words, then the camera's 6 */
#define IP_NCAR 15u
#define IP_W 16u
_Static_assert(A4_GL_IP + (16u + IP_NCAR * IP_W + 8u) * 4u <= 0x00460000u, "the interpolation buffer");
static const uint16_t ip_words[] = { 0x004, 0x008, 0x00C, 0x0A8, 0x0AC, 0x0B0, 0x0D0, 0x7D0, 0x7F8, 0xA20, 0xA24, 0xA7C,
                                     0x678, 0x020 };   /* spec_mid, then the heading (2048) and the wheels (4096) */
static void ip_capture(uint32_t race)
{
    volatile uint32_t *B = (volatile uint32_t *)A4_GL_IP;
    if (!race) return;
    B[0] = race;
    for (uint32_t i = 0; i < IP_NCAR; i++) {
        const volatile uint32_t *c = *(volatile uint32_t *volatile *)(race + 0x394 + 4u * i);
        B[1 + i] = (uint32_t)(uintptr_t)c;
        if (c) for (uint32_t f = 0; f < sizeof ip_words / sizeof ip_words[0]; f++) B[16 + i * IP_W + f] = c[ip_words[f] / 4u];
    }
    for (uint32_t w = 0; w < 6u; w++) B[16 + IP_NCAR * IP_W + w] = ((volatile uint32_t *)race)[0x58u / 4u + w];
}
#endif
static void __attribute__((unused)) spec_run(uint32_t race, uint32_t q16)
{
    volatile uint32_t *h = (volatile uint32_t *)A4_GL_HSTEP, *sp = (volatile uint32_t *)A4_GL_SPEC;
    uint32_t n = 0;
    for (uint32_t i = 0; i < h[2]; i++) {          /* the cars of the recorded calls, once each */
        uint32_t car = ((volatile uint32_t *)A4_GL_HCALL)[6u * i + 1u], j;

        for (j = 0; j < n && sp[4 + j] != car; j++) ;
        if (j == n && n < SPEC_MAXCARS) sp[4 + n++] = car;
    }
    sp[0] = n;
    for (uint32_t i = 0; i < n; i++)
        words_copy((volatile uint32_t *)(A4_GL_SNAP + i * SPEC_CARSZ), (volatile uint32_t *)sp[4 + i], SPEC_CARSZ / 4u);
    uint32_t ctx = *(volatile uint32_t *)(race + 0x38);
    uint32_t K = ctx ? *(volatile uint32_t *)(ctx + 0x1114) : 0;
    sp[2] = K;
    if (K) words_copy(sp + 32, (volatile uint32_t *)K, 16);
    sp[3] = race;                    /* the race object, its camera (+0x48) with it */
    words_copy(sp + 64, (volatile uint32_t *)race, SPEC_RACESZ / 4u);
#ifdef A4_SPECTRAFFIC
    /* the traffic array, if the whole step just past moved it (a4_h_fb12 runs inside that
     * step's even half-step, after a4_halfstep counted it: h[0] equal, or one more after the
     * odd half-step since), saved */
    sp[26] = (sp[27] && h[0] - sp[28] <= 1u) ? sp[27] : 0u;
    if (sp[26]) words_copy((volatile uint32_t *)A4_GL_TSNAP, (volatile uint32_t *)sp[26], TRAF_SZ / 4u);
#endif
    sp[1] = 1;
#if defined(A4_SPECGATE) && defined(A4_FRAMEMARK)
#ifdef A4_SPECTRAFFIC
    a4_spec_ptrs(race, K, n, sp[26]);
#else
    a4_spec_ptrs(race, K, n, 0);
#endif
    for (uint32_t i = 0; i < n; i += 4u) a4_spec_ptrs(sp[4 + i], sp[5 + i], sp[6 + i], sp[7 + i]);
#endif
#ifdef A4_SPECGATE
    sp[31] = 1;                      /* bench: EKA2L1_WATCHGATE=44707C -- the game's writes from here */
#endif
#ifndef A4_INTERP
    if (K) {
        ((void (*)(uint32_t, uint32_t))GAMEFN(a4_hc_25e8))(K, 0);
#ifdef A4_AUTOPILOT
        {
            uint32_t k = ap_key(race, ((volatile uint32_t *)A4_GL_CREC)[1]);
            ((volatile uint32_t *)K)[0] = k; ((volatile uint32_t *)K)[1] = k;
        }
#endif
    }
    hcall_replay();
#endif
#ifdef A4_INTERP
    {   /* each car, the camera and the traffic between the state before the last whole step
         * (A4_GL_IP, A4_GL_TPREV) and the state it left (now, saved above) */
        volatile uint32_t *B = (volatile uint32_t *)A4_GL_IP;
        if (B[0] == race) {
            for (uint32_t i = 0; i < IP_NCAR; i++) {
                volatile int32_t *c = (volatile int32_t *)B[1 + i];
                if (!c || *(volatile uint32_t *)(race + 0x394 + 4u * i) != B[1 + i]) continue;
                uint32_t j;          /* only a car saved above (put back after the render) */
                for (j = 0; j < n && sp[4 + j] != B[1 + i]; j++) ;
                if (j == n) continue;
                const volatile int32_t *p = (const volatile int32_t *)(B + 16 + i * IP_W);
                if (iabs32(c[1] - p[0]) > SPEC_CUT || iabs32(c[3] - p[2]) > SPEC_CUT) continue;   /* a respawn: as it is */
                for (uint32_t f = 0; f < 12u; f++) { uint32_t w = ip_words[f] / 4u; c[w] = lerp_q16(p[f], c[w], q16); }
                c[0x678u / 4u] = lerp_ang(p[12], c[0x678u / 4u], q16, 2048u);
                c[0x020u / 4u] = lerp_ang(p[13], c[0x020u / 4u], q16, 4096u);
            }
            volatile int32_t *cm = (volatile int32_t *)race;
            const volatile int32_t *pc = (const volatile int32_t *)(B + 16 + IP_NCAR * IP_W);
            uint32_t cut = 0;
            for (uint32_t w = 0; w < 3u; w++) if (iabs32(cm[0x58u / 4u + w] - pc[w]) > SPEC_CUT) cut = 1;
            for (uint32_t w = 3; w < 6u; w++) { uint32_t d = (uint32_t)(cm[0x58u / 4u + w] - pc[w]) & 2047u; if ((d > 1024u ? 2048u - d : d) > 256u) cut = 1; }
            if (!cut) {
                for (uint32_t w = 0; w < 3u; w++) cm[0x58u / 4u + w] = lerp_q16(pc[w], cm[0x58u / 4u + w], q16);
                for (uint32_t w = 3; w < 6u; w++) cm[0x58u / 4u + w] = lerp_ang(pc[w], cm[0x58u / 4u + w], q16, 2048u);
            }
        }
#ifdef A4_SPECTRAFFIC
        if (sp[26]) for (uint32_t i = 0; i < TRAF_N; i++) {
            volatile int32_t *c = (volatile int32_t *)(sp[26] + i * TRAF_CARSZ);
            const volatile int32_t *p = (const volatile int32_t *)(A4_GL_TPREV + i * TRAF_CARSZ);
            if (c[1] == p[1] && c[3] == p[3]) continue;
            if (iabs32(c[1] - p[1]) > SPEC_CUT || iabs32(c[3] - p[3]) > SPEC_CUT || iabs32(c[0x11Cu / 4u] - p[0x11Cu / 4u]) > 512) continue;
            static const uint16_t tw[] = { 0x004, 0x008, 0x00C, 0x04C, 0x050, 0x054, 0x058, 0x05C, 0x060, 0x064, 0x068, 0x06C,
                                           0x0A8, 0x0AC, 0x0B0, 0x11C, 0x120, 0x124 };
            for (uint32_t f = 0; f < sizeof tw / sizeof tw[0]; f++) { uint32_t w = tw[f] / 4u; c[w] = lerp_q16(p[w], c[w], q16); }
            c[0x020u / 4u] = lerp_ang(p[0x020u / 4u], c[0x020u / 4u], q16, 4096u);
        }
#endif
    }
    if (0)
#endif
    {   /* the camera after the cars, as the game's own between-steps update does it in slow
         * motion (+1300C -> +12DEC(race, &0) -> +5968) */
        volatile uint32_t z = 0;
#if !defined(A4_SPECNOCAM) && !defined(A4_SPECCAMFOLLOW)
#ifdef A4_SPECGATECAM
        /* bench (round 40): EKA2L1_WATCHGATE=44707C opened around the speculative camera update
         * alone, with the present count + 1 (the emulator logs the opening value) */
        sp[31] = ((volatile struct glr *)A4_GL_SCRATCH)->frames + 1u;
#endif
#ifdef A4_SPECCAMSAFE
        sp[24] = 1;   /* a4_cs_*_tramp: the camera scripts not played or set up from here */
        sp[25] = 0;   /* ... and each call they skip counted */
#endif
        ((void (*)(uint32_t, volatile uint32_t *))GAMEFN(a4_hc_12dec))(race, &z);
#ifdef A4_SPECCAMSAFE
        sp[24] = 0;
#endif
#ifdef A4_SPECGATECAM
        sp[31] = 0;
#endif
#else
        /* round 40: not run speculatively. The bench at the phone's rhythm: with it, the
         * empty render at the hill in every race (r40c, c7); without it (A4_SPECNOCAM), none in
         * two races. It allocates and frees list nodes outside the saved objects (round 31's
         * leak6: +5912 -> +5205A), the camera's zones or events moved to where the car will be,
         * which the render then culls by and nothing restores. A4_SPECCAMFOLLOW moves the
         * chase camera with the player car instead, below. */
        (void)z;
#endif
    }
#ifdef A4_SPECGATE
    sp[31] = 0;                      /* ... to here: the keys, the replayed car calls, the camera */
#endif
#ifdef A4_FRAMEMARK
    {   uint32_t pl = *(volatile uint32_t *)(race + 0x390);
        uint32_t idx = pl ? *(volatile uint32_t *)(pl + 0xC) : 0;
        uint32_t pcar = idx < 16u ? *(volatile uint32_t *)(race + 0x394 + 4u * idx) : 0;
        for (uint32_t i = 0; i < n && pcar; i++) {
            if (sp[4 + i] != pcar) continue;
            const volatile uint32_t *c = (const volatile uint32_t *)pcar;
            const volatile uint32_t *o = (const volatile uint32_t *)(A4_GL_SNAP + i * SPEC_CARSZ);
            const volatile uint32_t *cm = (const volatile uint32_t *)race;
            a4_spec_mark(o[1], o[3], o[0x678u / 4u], q16);
            a4_spec_mark2(c[1], c[3], c[0x678u / 4u], a4_ntick());
            a4_spec_mark3(sp[64 + 0x58u / 4u], sp[64 + 0x60u / 4u], cm[0x58u / 4u], cm[0x60u / 4u]);
            a4_spec_mark4(sp[64 + 0x68u / 4u], cm[0x68u / 4u], sp[25], cm[0x94u / 4u]);   /* skipped script calls, the camera mode */
            break;
        }
    }
#endif
#ifndef A4_INTERP
    {   /* the camera half way too, in the words the render draws it from (rcam8: the render
         * reads +0x58..+0x6C, +0x70 and +0x388 of these; EKA2L1_RWATCH gated to the render):
         * the position +0x58/+0x5C/+0x60 plainly, the angles +0x64/+0x68/+0x6C the short way
         * round a 2048 circle (round 31 phone follow-up: +0x68 fitted against the camera's
         * direction vector, 2048 not 4096 -- the 4096 guess turned the camera half round on a
         * wrap), +0x70 (37..48, with the speed) plainly. +0x388 (0..100, also read) is the
         * render's own counter, advanced in the render and undone by the restore (spec10: 1.00
         * with a midpoint written before the render) -- left alone */
        volatile int32_t *cm = (volatile int32_t *)race;
        const volatile int32_t *co = (const volatile int32_t *)(sp + 64);
        /* a cut: more than SPEC_CUT on any axis -- height too (round 39: the owner's
         * "helicopter position for a frame or two" before the news helicopter event; x and
         * z alone let a camera that rose to an event view be drawn half way up) -- or an
         * angle turned more than an eighth of the circle in one step */
        uint32_t cut = 0;
        for (uint32_t w = 0x58u / 4u; w <= 0x60u / 4u; w++) if (iabs32(cm[w] - co[w]) > SPEC_CUT) cut = 1;
        for (uint32_t w = 0x64u / 4u; w <= 0x6Cu / 4u; w++) {
            uint32_t d = (uint32_t)(cm[w] - co[w]) & 2047u;
            if ((d > 1024u ? 2048u - d : d) > 256u) cut = 1;
        }
#if defined(A4_SPECSCRIPTHOLD) && defined(A4_SPECCAMSAFE)
        /* round 41 (r40 on the phone: "the cars online lag during takedown cameras"): a camera
         * driven by a script (a takedown, a crash, an event) has its script player skipped in
         * the speculation (A4_SPECCAMSAFE), so the speculated camera stays at the whole step
         * while the cars are drawn up to a step ahead -- they slide forward and snap back each
         * step. Such a frame is drawn at the whole step: camera and cars as the game left them */
        if (sp[25]) {
            cut = 1;
#ifdef A4_SLOWREC
            if ((((volatile uint32_t *)0x00455800u)[14] & 0xFFFFu) != 0xFFFFu) ((volatile uint32_t *)0x00455800u)[14]++;   /* low half: frames drawn at the whole step for it */
#endif
        }
#endif
        if (cut) {
            for (uint32_t w = 0x58u / 4u; w <= 0x70u / 4u; w++) cm[w] = co[w];   /* a cut: the camera as it was */
        } else {
            for (uint32_t w = 0x58u / 4u; w <= 0x60u / 4u; w++) cm[w] = lerp_q16(co[w], cm[w], q16);
            for (uint32_t w = 0x64u / 4u; w <= 0x6Cu / 4u; w++) {
                cm[w] = lerp_ang(co[w], cm[w], q16, 2048u);
#ifdef A4_ANGWRAP
                /* bench (round 40): the game keeps these words in 0..4095; an interpolation across
                 * the wrap can hand it 4096 and over or below 0 */
                cm[w] &= 4095;
#endif
            }
#ifdef A4_KEEP70
            /* round 40: +0x70 kept at the whole step's (a camera-transition counter, 125 on the
             * chase camera, reset where a transition starts: not a quantity to interpolate). The bench at the phone's rhythm: at each
             * empty render +0x70 (normally 125) had dropped to 43-65 while pitch, roll, yaw and
             * the position were steady -- the speculated camera update computes a low +0x70 at the
             * hill and the interpolation drew half way to it; the render culls by it */
            cm[0x70u / 4u] = co[0x70u / 4u];
#else
            cm[0x70u / 4u] = lerp_q16(co[0x70u / 4u], cm[0x70u / 4u], q16);
#endif
        }
    }
    for (uint32_t i = 0; i < n; i++) {
        volatile int32_t *c = (volatile int32_t *)sp[4 + i];
        const volatile int32_t *o = (const volatile int32_t *)(A4_GL_SNAP + i * SPEC_CARSZ);
        if (iabs32(c[0x004u / 4u] - o[0x004u / 4u]) > SPEC_CUT || iabs32(c[0x00Cu / 4u] - o[0x00Cu / 4u]) > SPEC_CUT
#if defined(A4_SPECSCRIPTHOLD) && defined(A4_SPECCAMSAFE)
            || sp[25]
#endif
            ) {
            words_copy((volatile uint32_t *)c, (const volatile uint32_t *)o, SPEC_CARSZ / 4u);   /* a respawn: as it was */
            continue;
        }
        for (uint32_t f = 0; f < sizeof spec_mid / sizeof spec_mid[0]; f++) {
            uint32_t w = spec_mid[f] / 4u;
            c[w] = lerp_q16(o[w], c[w], q16);
        }
        c[0x678u / 4u] = lerp_ang(o[0x678u / 4u], c[0x678u / 4u], q16, 2048u);
        c[0x020u / 4u] = lerp_ang(o[0x020u / 4u], c[0x020u / 4u], q16, 4096u);
    }
#ifdef A4_SPECTRAFFIC
#ifndef A4_TRAFNOX
    if (sp[26]) for (uint32_t i = 0; i < TRAF_N; i++) {
        /* each traffic car carried on by q of its last whole step (now less a step back), in
         * the words the bench saw move (tfm: a car's +004..+00C and their copies +04C..+054
         * and +0A8..+0B0, the rotation +058..+06C in 4096ths, the heading +11C, the direction
         * +120/+124, the wheels +020); one that jumped (a respawn, past SPEC_CUT) or turned
         * hard in that step is drawn as the step left it */
        volatile int32_t *c = (volatile int32_t *)(sp[26] + i * TRAF_CARSZ);
        const volatile int32_t *p = (const volatile int32_t *)(A4_GL_TPREV + i * TRAF_CARSZ);
        if (c[1] == p[1] && c[3] == p[3]) continue;   /* not moved (or not in use) */
        uint32_t keep = iabs32(c[1] - p[1]) > SPEC_CUT || iabs32(c[3] - p[3]) > SPEC_CUT;
        for (uint32_t w = 0x058u / 4u; w <= 0x06Cu / 4u && !keep; w++) if (iabs32(c[w] - p[w]) > 1024) keep = 1;
        if (iabs32(c[0x11Cu / 4u] - p[0x11Cu / 4u]) > 512) keep = 1;
        if (keep) continue;
        static const uint16_t tmid[] = { 0x004, 0x008, 0x00C, 0x04C, 0x050, 0x054, 0x058, 0x05C, 0x060, 0x064, 0x068, 0x06C,
                                         0x0A8, 0x0AC, 0x0B0, 0x11C, 0x120, 0x124 };
        for (uint32_t f = 0; f < sizeof tmid / sizeof tmid[0]; f++) {
            uint32_t w = tmid[f] / 4u;
            c[w] += (int32_t)(((int64_t)(c[w] - p[w]) * (int64_t)q16) >> 16);
        }
        {   int32_t d = (int32_t)(((uint32_t)(c[0x020u / 4u] - p[0x020u / 4u]) + 2048u) & 4095u) - 2048;   /* the wheels round 4096 */
            c[0x020u / 4u] = (c[0x020u / 4u] + (int32_t)(((int64_t)d * (int64_t)q16) >> 16)) & 4095; }
    }
#endif
#endif
#endif /* !A4_INTERP */
#ifdef A4_SPECCAMFOLLOW
    {   /* the chase camera moved with the player car by the car's interpolated motion (the
         * camera words are the whole step's here, the game's update not run); any other camera
         * (a crash, a takedown, an event: its offset off the running average by more than 128
         * on an axis) stays where the whole step put it */
        volatile struct glr *g = (volatile struct glr *)A4_GL_SCRATCH;
        uint32_t pl = *(volatile uint32_t *)(race + 0x390);
        uint32_t idx = pl ? *(volatile uint32_t *)(pl + 0xC) : 0;
        uint32_t pcar = idx < 16u ? *(volatile uint32_t *)(race + 0x394 + 4u * idx) : 0;
        for (uint32_t i = 0; i < n && pcar && g->chaseinit; i++) {
            if (sp[4 + i] != pcar) continue;
            volatile int32_t *c = (volatile int32_t *)sp[4 + i];
            const volatile int32_t *o = (const volatile int32_t *)(A4_GL_SNAP + i * SPEC_CARSZ);
            volatile int32_t *cm = (volatile int32_t *)race;
            int32_t ox = cm[0x58 / 4] - o[1], oy = cm[0x5C / 4] - o[2], oz = cm[0x60 / 4] - o[3];
            int32_t dd = (int32_t)isqrt64((int64_t)ox * ox + (int64_t)oz * oz) - g->chaseavg[0] / 16, dy = oy - g->chaseavg[1] / 16;
            uint32_t chase = dd <= 64 && dd >= -64 && dy <= 64 && dy >= -64;
            if (chase) for (int a = 0; a < 3; a++) cm[0x58 / 4 + a] += c[1 + a] - o[1 + a];
        }
    }
#endif
#ifdef A4_SPECLEAN
    /* bench (round 40: the empty render is the speculation's -- r40ns, the half-step logic
     * alone, at the phone's rhythm, had none): every word the replayed half-step changed that
     * is NOT drawn half way put back to the whole step's for the render. A4_SPECLEAN bits:
     * 1 the cars (spec_mid, the heading +0x678 and the wheels +0x020 kept), 2 the race (the
     * camera +0x58..+0x70 kept), 4 K (the keys) */
    if (A4_SPECLEAN & 1) for (uint32_t i = 0; i < n; i++) {
        volatile uint32_t *c = (volatile uint32_t *)sp[4 + i];
        const volatile uint32_t *o = (const volatile uint32_t *)(A4_GL_SNAP + i * SPEC_CARSZ);
        for (uint32_t w = 0; w < SPEC_CARSZ / 4u; w++) {
            uint32_t keep = (w == 0x678u / 4u || w == 0x020u / 4u);
            for (uint32_t f = 0; f < sizeof spec_mid / sizeof spec_mid[0] && !keep; f++) if (spec_mid[f] / 4u == w) keep = 1;
            if (!keep) c[w] = o[w];
        }
    }
    if (A4_SPECLEAN & 2) for (uint32_t w = 0; w < SPEC_RACESZ / 4u; w++)
        if (w < 0x58u / 4u || w > 0x70u / 4u) ((volatile uint32_t *)race)[w] = sp[64 + w];
    if ((A4_SPECLEAN & 4) && K) words_copy((volatile uint32_t *)K, sp + 32, 16);
#endif
    {   /* what the speculation changed, for a restore that keeps the render's own changes */
        volatile uint32_t *bm = (volatile uint32_t *)A4_GL_SPECBM;
        for (uint32_t i = 0; i < n; i++)
            bm_mark(bm + i * BM_CAR, (volatile uint32_t *)sp[4 + i], (volatile uint32_t *)(A4_GL_SNAP + i * SPEC_CARSZ), SPEC_CARSZ / 4u);
        bm_mark(bm + SPEC_MAXCARS * BM_CAR, (volatile uint32_t *)race, sp + 64, SPEC_RACESZ / 4u);
        if (K) bm_mark(bm + SPEC_MAXCARS * BM_CAR + BM_RACE, (volatile uint32_t *)K, sp + 32, 16);
#ifdef A4_SPECTRAFFIC
        if (sp[26]) bm_mark((volatile uint32_t *)A4_GL_TBM, (volatile uint32_t *)sp[26], (volatile uint32_t *)A4_GL_TSNAP, TRAF_SZ / 4u);
#endif
    }
}
#endif
#ifdef A4_HALFPHYS
/* the game's own camera updates (the whole step's +14EEC, slow motion's +1306C), halved too */
int a4_h_12dec(uint32_t race, uint32_t p)
{
    /* round 43: the whole step's camera as the game computes it. Halving the chase camera's
     * update (a filter toward a point behind the car) left it half a step's travel further back
     * (crec: 398 units from the car against 319); the odd half-step draws it extrapolated */
#ifdef A4_CAMCENSUS
    {   /* bench (round 43): every race word the camera update writes -- the race object before
         * and after, through a4_hp_census (r0 0x10, r1 0x9EC, r2 0x9EC) */
        volatile uint32_t *b = (volatile uint32_t *)(A4_GL_HP + 0x1000u), *hd = b + 0x9ECu / 4u + 4u;
        for (uint32_t i = 0; i < 0x9ECu / 4u; i++) b[i] = ((volatile uint32_t *)race)[i];
        int r = ((fn2)GAMEFN(a4_hc_12dec))(race, p);
        hd[0] = 0x4D414348u; hd[1] = HP_CAMMODE(race); hd[2] = ((volatile uint32_t *)A4_GL_HSTEP)[0]; hd[3] = race;
        a4_hp_census((const void *)hd, (const void *)b, (const void *)race);
        return r;
    }
#endif
    return ((fn2)GAMEFN(a4_hc_12dec))(race, p);
}
/* the odd half-step: the whole step's car calls again (each halved as in the hooks), the camera
 * (the chase camera only, halved; on a script or another camera it waits for the whole step).
 * The census (A4_HPCENSUS) and the probe (A4_HPPROBE) emit through a4_hp_census, above */
static void hp_sub(uint32_t race, uint32_t f16, uint32_t first)
{
    volatile uint32_t *h = (volatile uint32_t *)A4_GL_HSTEP, *sp = (volatile uint32_t *)A4_GL_SPEC;
    if (HP->race != race) {
        HP->race = race;
        for (uint32_t i = 0; i < HP_CARS; i++) HP->car[i] = 0;
        for (uint32_t f = 0; f < 12u; f++) HP->cc[f] = 0;
        HP->camscript = 0; HP->cwv = 0; HP->camx = 0; HP->started = 0;
    }
    uint32_t ctx = race ? *(volatile uint32_t *)(race + 0x38) : 0;
    uint32_t K = ctx ? *(volatile uint32_t *)(ctx + 0x1114) : 0;
    if (!race) return;
    HP->replaying = 1; h[3] = 1;
    /* round 43: not the keys (+25E8): it is the key queue, K[0] = K[1] and the presses since,
     * so a call here took a press's edge before the whole step saw it. The car calls replay
     * the whole step's arguments; the keys stay as the whole step read them */
    (void)K;
    uint32_t pl = *(volatile uint32_t *)(race + 0x390), pidx = pl ? *(volatile uint32_t *)(pl + 0xC) : 99u;
    uint32_t pcar = pidx < 16u ? *(volatile uint32_t *)(race + 0x394 + 4u * pidx) : 0;
    /* round 43: the car calls replay the whole step's arguments, whose key bits (+1DE78's r1,
     * built at +14748..+14930) the step forces to 8, the brake, until the start: replayed after
     * the start's step they braked the launch to a stall. Before the start nothing moves, so no
     * car call runs at an odd half-step until the player's +0E0 (its steps since the start) counts */
    /* once a race: +0E0 counts the player's steps since its last contact (0 for a few steps
     * after a hit, crec_orn step 1076), so gating on it froze the cars at every contact */
    {   uint32_t rs = *(volatile uint32_t *)(race + 0x30u);   /* steps since the race loaded: back to 0 on a restart */
        if (rs < HP->rstep) HP->started = 0;
        HP->rstep = rs;
    }
    if (pcar && *(volatile int32_t *)(pcar + 0xE0u) > 0) HP->started = 1;
    uint32_t started = pcar && HP->started;
    if (pcar && first) {
        for (uint32_t f = 0; f < 4u; f++) { HP->pwpp[f] = HP->pwp[f]; HP->pwp[f] = HP->pw[f]; }
        HP->pwpps = HP->pwps; HP->pwps = HP->pws; HP->pws = HP->wstep;
        HP->pw[0] = ((volatile int32_t *)pcar)[1]; HP->pw[1] = ((volatile int32_t *)pcar)[2];
        HP->pw[2] = ((volatile int32_t *)pcar)[3]; HP->pw[3] = ((volatile int32_t *)pcar)[0x678u / 4u];
    }
#ifdef A4_HPPROBE
    /* the probe changes nothing at an odd half-step: the player car and the race object as they
     * were put back after its calls (slot 2 of the snapshot area, the race in sp + 64) */
    volatile uint32_t *s2 = (volatile uint32_t *)(A4_GL_SNAP + 2u * SPEC_CARSZ);
    uint32_t saved = 0;
    words_copy(sp + 64, (volatile uint32_t *)race, SPEC_RACESZ / 4u);
#endif
#ifdef A4_KINEPHYS
    {   /* the cars the whole step moved (+1DE78, +1F1F8), each once: along its own velocity */
        uint32_t seen = 0;
        uint32_t qt = HP->committed + f16;
        for (uint32_t i = 0; i < h[2]; i++) {
            volatile uint32_t *c = (volatile uint32_t *)A4_GL_HCALL + 6u * i;
            if (c[0] != 1u && c[0] != 2u) continue;
            uint32_t ci = hp_idx(c[1]);
            if (ci >= HP_CARS || (seen >> ci) & 1u) continue;
            seen |= 1u << ci;
#ifdef A4_KINEDRAW
            hp_kdraw(c[1], qt, first, (HP->wstep << 16) + qt);
#else
            hp_kmove(c[1], f16, first, qt);
#endif
        }
    }
    for (uint32_t i = 0; i < 0u; i++) {
#else
    for (uint32_t i = 0; i < h[2] && started; i++) {
#endif
        volatile uint32_t *c = (volatile uint32_t *)A4_GL_HCALL + 6u * i;
        uint32_t car = c[1];
#ifdef A4_HPPROBE
        if (car != pcar) continue;   /* the other cars: not run at all */
        if (!saved) { words_copy(s2, (volatile uint32_t *)car, SPEC_CARSZ / 4u); saved = 1; }
        if (car == pcar) {
            /* bench (round 42): which words are the car's own state and which are recomputed. The
             * call run from the same start twice -- as it is, and with one word nudged first (the
             * words in turn, one an odd half-step) -- both results emitted: a nudge that carries
             * through to the result is state (an integrator); one the call overwrites is derived.
             * The car then left with the plain result. EKA2L1_PCMEM ats at a4_hp_census: r0 0x10
             * ([0] 'HPPR', [1] kind | word << 8, [2] the nudge, [3] the car), r1 0xC7C (plain),
             * r2 0xC7C (nudged) */
#if A4_HPPROBE == 2                 /* round 43: what the measured velocity is measured against */
            static const uint16_t pw[] = { 0x04C, 0x050, 0x054, 0x0A8, 0x0AC, 0x0B0, 0x680, 0x688, 0x7E4, 0x0B4, 0x9FC, 0x0D0 };
#elif A4_HPPROBE == 3               /* round 43: the rest of the census's words (the low bit: nudge by 1) */
            static const uint16_t pw[] = { 0x07C, 0x7C4, 0x7FC, 0x808, 0x80C, 0xA18, 0xA1C, 0xA20, 0xA78,       /* not +034/+048: */
                                           0xAEC, 0xAF8, 0xB20, 0x01C | 1, 0x0BC, 0x0C8,                          /* pointers, a nudge */
                                           0x0E8, 0x0EC, 0xA40, 0xA44, 0xA58, 0xA5C, 0xA6C, 0xA70, 0xA74,         /* faulted the game */
                                           0xB18, 0xB28 | 1, 0xC1C };
#else
            static const uint16_t pw[] = { 0x004, 0x008, 0x00C, 0x010, 0x014, 0x018, 0x020, 0x028, 0x02C, 0x0B4, 0x0B8, 0x0C0,
                                           0x0C4, 0x0CC, 0x0D4, 0x0E0, 0x678, 0x680, 0x688, 0x7D0, 0x7D8, 0x7E4, 0x7F8, 0x820,
                                           0xA00, 0xA24, 0xA2C, 0xA34, 0xA7C, 0xABC, 0xADC, 0xB98, 0x0F0, 0x0F8, 0x164, 0x15C };
#endif
            volatile uint32_t *hd = (volatile uint32_t *)(A4_GL_SNAP + SPEC_MAXCARS * SPEC_CARSZ - 0x10u);
            volatile uint32_t *s0 = (volatile uint32_t *)A4_GL_SNAP, *s1 = (volatile uint32_t *)(A4_GL_SNAP + SPEC_CARSZ);
            uint32_t pwi = pw[(HP->nodd / 2u) % (sizeof pw / sizeof pw[0])], wi = pwi / 4u;
            int32_t nudge = (pwi & 1u) ? 1 : (wi == 0x0B8u / 4u || wi == 0x0C0u / 4u) ? 48 : (wi == 0x0E0u / 4u || wi == 0x820u / 4u || wi == 0xA2Cu / 4u || wi == 0xA34u / 4u || wi == 0x164u / 4u || wi == 0x15Cu / 4u) ? 3 : 64;
            for (uint32_t run = 0; run < 2u; run++) {
                if (run == 0) words_copy(s0, (volatile uint32_t *)car, SPEC_CARSZ / 4u);
                else { words_copy((volatile uint32_t *)car, s0, SPEC_CARSZ / 4u); ((volatile int32_t *)car)[wi] += nudge; }
                switch (c[0]) {
                case 1: ((fn5)GAMEFN(a4_hc_1de78))(c[1], c[2], c[3], c[4], c[5]); break;
                case 2: ((fn3)GAMEFN(a4_hc_1f1f8))(c[1], c[2], c[3]); break;
                case 3: ((fn3)GAMEFN(a4_hc_1ea64))(c[1], c[2], c[3]); break;
                case 4: ((fn2)GAMEFN(a4_hc_1fd5c))(c[1], c[2]); break;
                case 5: ((fn1)GAMEFN(a4_hc_1fd0e))(c[1]); break;
                }
                if (run == 0) words_copy(s1, (volatile uint32_t *)car, SPEC_CARSZ / 4u);
            }
            hd[0] = 0x52505048u; hd[1] = c[0] | (wi * 4u) << 8; hd[2] = (uint32_t)nudge; hd[3] = car;
            a4_hp_census((const void *)hd, (const void *)s1, (const void *)car);
            words_copy((volatile uint32_t *)car, s1, SPEC_CARSZ / 4u);   /* the plain result, for the next call */
            continue;
        }
#endif
#ifdef A4_HPCENSUS
        if (car == pcar) {           /* the call unhalved, the car before and after it */
            volatile uint32_t *hd = (volatile uint32_t *)(A4_GL_SNAP + SPEC_MAXCARS * SPEC_CARSZ - 0x10u);
            words_copy((volatile uint32_t *)A4_GL_SNAP, (volatile uint32_t *)car, SPEC_CARSZ / 4u);
            switch (c[0]) {
            case 1: ((fn5)GAMEFN(a4_hc_1de78))(c[1], c[2], c[3], c[4], c[5]); break;
            case 2: ((fn3)GAMEFN(a4_hc_1f1f8))(c[1], c[2], c[3]); break;
            case 3: ((fn3)GAMEFN(a4_hc_1ea64))(c[1], c[2], c[3]); break;
            case 4: ((fn2)GAMEFN(a4_hc_1fd5c))(c[1], c[2]); break;
            case 5: ((fn1)GAMEFN(a4_hc_1fd0e))(c[1]); break;
            }
            hd[0] = 0x4E435048u; hd[1] = c[0]; hd[2] = HP->nodd; hd[3] = car;
            a4_hp_census((const void *)hd, (const void *)A4_GL_SNAP, (const void *)car);
            continue;
        }
#endif
        if (c[0] >= 1u && c[0] <= 5u) {
            int32_t sv[HP_NR];
            HP_TR_PRE(car);
            { uint32_t ci = hp_idx(car); if (ci < HP_CARS) HP->ran[ci] = 1; }
            if (c[0] == 3u) hp_meas(car, 1u);
            hp_phys_pre(car, sv);
            switch (c[0]) {
            case 1: ((fn5)GAMEFN(a4_hc_1de78))(c[1], c[2], c[3], c[4], c[5]); break;
            case 2: ((fn3)GAMEFN(a4_hc_1f1f8))(c[1], c[2], c[3]); break;
            case 3: ((fn3)GAMEFN(a4_hc_1ea64))(c[1], c[2], c[3]); break;
            case 4: ((fn2)GAMEFN(a4_hc_1fd5c))(c[1], c[2]); break;
            case 5: ((fn1)GAMEFN(a4_hc_1fd0e))(c[1]); break;
            }
            hp_phys_post(car, sv, c[0] == 1u ? 1u : (c[0] == 3u || c[0] == 4u) ? 2u : 3u, f16);
            HP_TR_POST(c[0], car, c[2]);
        }
    }
#ifdef A4_HPPROBE
    if (saved) words_copy((volatile uint32_t *)pcar, s2, SPEC_CARSZ / 4u);
    words_copy((volatile uint32_t *)race, sp + 64, SPEC_RACESZ / 4u);
    if (0)
#endif
    {   /* round 43: the camera at an odd half-step. The whole step's camera is the game's own
         * (a4_h_12dec: halving it left the chase camera half a step's travel further back). Here
         * the chase camera (mode 2) is moved with the player car's own half-step -- its place by
         * the car's, its yaw (+0x68) by 3/4 of the car's turn (the game's chase camera takes about
         * 3/4 of a turn in the step it happens, crec_orna, then closes the rest slowly) -- and
         * put back before the next whole step, so the game's camera runs exactly as in a normal
         * build. Extrapolating the camera's own last step instead overshot where a turn eased
         * (camera yaw reversals 8 in 417 frames). Not on another camera (held) */
        volatile int32_t *cm = (volatile int32_t *)race;
        uint32_t qt = HP->committed + f16;   /* the step's fraction drawn (A4_FRACPHYS; a half-step 32768) */
#ifndef A4_FRACPHYS
        qt = 32768u;
#endif
        if (first) {
            for (uint32_t f = 0; f < 6u; f++) { HP->cwpp[f] = HP->cwp[f]; HP->cwp[f] = HP->cw[f]; HP->cw[f] = cm[0x58u / 4u + f]; }
            if (HP->cwv < 3u) HP->cwv++;
            if (HP_CAMMODE(race) != HP->cmode) HP->cwv = 1;   /* a new camera: its steps start again */
            HP->cmode = HP_CAMMODE(race);
        }
        HP->camx = 1;
        if (HP_CAMMODE(race) == 2u && pcar && started) {
            const volatile int32_t *pc = (const volatile int32_t *)pcar;
            int32_t dx = pc[1] - HP->pw[0], dy = pc[2] - HP->pw[1], dz = pc[3] - HP->pw[2];
            int32_t dh = (pc[0x678u / 4u] - HP->pw[3]) & 2047; if (dh >= 1024) dh -= 2048;
            if (dx < 2048 && dx > -2048 && dz < 2048 && dz > -2048 && dy < 2048 && dy > -2048 && dh < 256 && dh > -256) {
                /* the height on its own vertical move, not the car's: following the car down
                 * over a crest put the camera inside the hill (round 37's empty renders: the
                 * terrain's faces turned away and culled; r44t held two such frames) */
                int32_t vy = HP->cwv >= 2u ? HP->cw[1] - HP->cwp[1] : 0;
                if (vy > 256) vy = 256; else if (vy < -256) vy = -256;
                (void)dy;
                cm[0x58u / 4u] = HP->cw[0] + dx; cm[0x60u / 4u] = HP->cw[2] + dz;
                cm[0x5Cu / 4u] = HP->cw[1] + (int32_t)(((int64_t)vy * (int32_t)qt) >> 16);
                cm[0x68u / 4u] = (HP->cw[4] + (3 * dh) / 4) & 2047;
#ifdef A4_KINEDRAW
                {   /* round 47 (r46 on the phone: "the camera doesn't follow the car appropriately" --
                     * the car's bearing in the view spread 35/76/91 /2048 against r42's 2/6/23; the
                     * bench's own frames showed it, unread: jt_d 10/84/122, the Veyron 75/144/214, the
                     * corrector off 0/6/17): the camera's position had its own corrector, so in a curve,
                     * where the game's camera orbits the car step by step, it was held off its orbit and
                     * the car slid across the view. Now the camera is RIGID on the drawn car: the game's
                     * own offset from the car at the step (always straight behind along its yaw: offset
                     * direction yaw - 1024, spread 0-2 /2048 on r42ph, r44ph, r45t) turned by the yaw the
                     * frame draws -- the game's camera yaw and 3/4 of the car's drawn turn, as round 45
                     * (the camera's turn is the game's: no corrector of its own) -- and set on the drawn
                     * car. The framing is the game's by construction; the height keeps its own rate */
                    int32_t yaw = cm[0x68u / 4u];
#ifdef A4_ENGDRAW
                    /* engine map 1 (+4B40, run y1): the game's chase camera is rigid on the car at
                     * every step -- yaw = the car's heading (1,238 steps, within -1..0: the shake of
                     * +49C4), height = the car's y + 52 (+A8C) + 53 + a slope term -- so the frame's
                     * camera turns with the whole drawn turn and keeps its height above the drawn car */
                    yaw = (HP->cw[4] + dh) & 2047;
                    cm[0x5Cu / 4u] = pc[2] + (HP->cw[1] - HP->pw[1]);
#endif
#ifdef A4_KD_CAMYAWCORR              /* bench (round 47, r47a): the yaw through the corrector -- off: a large
                                      * offset faded out across one sparse frame (a 100 /2048 jump), and the car's
                                      * heading against the camera's spread 22/93/128 (r42ph 7/25/59) */
                    {
                        uint32_t pi = hp_idx(pcar);
                        int32_t tv = pi < HP_CARS ? (int32_t)(((int64_t)HP->turn[pi] * (int32_t)A4_KD_TURN) >> 16) : 0;
                        int32_t v[1] = { ((3 * tv) / 4) << 8 };
                        int32_t m[1] = { yaw << 8 };
                        static const int32_t cut[1] = { 256 << 8 };
                        volatile uint8_t cv = (uint8_t)HP->cV;
                        kd_correct(HP->cE + 3, HP->cD + 3, &HP->cT, &cv, m, v, 1u, 1u, first, (HP->wstep << 16) + qt, cut);
                        HP->cV = cv;
                        yaw = ((m[0] + 128) >> 8) & 2047;
                    }
#endif
                    int32_t rx, rz;
                    kd_rot(HP->cw[0] - HP->pw[0], HP->cw[2] - HP->pw[2], hp_wrap(yaw - HP->cw[4], 2048), &rx, &rz);
                    cm[0x58u / 4u] = pc[1] + rx; cm[0x60u / 4u] = pc[3] + rz;
                    cm[0x68u / 4u] = yaw;
                }
#endif
            } else {
#ifdef A4_KINEDRAW
                HP->cV = 0;          /* the car jumped (a respawn): the camera's corrector starts again */
#endif
            }
        }
#ifdef A4_CAMALL
#ifdef A4_SCRIPTHOLD
        else if (HP->hold) { }       /* the game's own camera, as the step (or its slow motion) left it */
#endif
        else if (pcar && started) {
            /* round 49 (r48 on the phone: "back and forth" in the takedown, the crash, the helicopter
             * view and the race's end -- the camera modes other than the chase): every camera the step
             * places follows the player car (+12DEC takes it from +52206); the chase family 1/2/3/0x2A
             * rigidly (+4B40), the others by their own rules (+490A, +50EC, the scripts +57AC/+554C).
             * The frame keeps the camera's offset from the player car in the car's own frame and
             * moves it on by its own last change: the drawn car + R(drawn turn) (o_k + (o_k - R(dh_k)
             * o_k-1) q) -- exact for a camera fixed to the car (the change 0), exact for one standing
             * still while the car drives by (the change minus the car's move), near for an orbit
             * (the race's end turns ~8 /2048 a step). Its height above the car and its yaw less the
             * car's heading likewise; pitch and roll by their own change. Up to r48 these modes moved
             * the camera on its own path only while that path repeated, else held it at the step
             * while the cars moved on: the back and forth. A new mode, a jump or a respawn: held to
             * the car at the step's offset, no change carried */
            const volatile int32_t *pc = (const volatile int32_t *)pcar;
            int32_t dx = pc[1] - HP->pw[0], dy = pc[2] - HP->pw[1], dz = pc[3] - HP->pw[2];
            int32_t dh = hp_wrap(pc[0x678u / 4u] - HP->pw[3], 2048);
#ifdef A4_KINEDRAW
            HP->cV = 0;
#endif
            if (iabs32(dx) < 2048 && iabs32(dz) < 2048 && iabs32(dy) < 2048 && iabs32(dh) < 256) {
                int32_t ox = HP->cw[0] - HP->pw[0], oz = HP->cw[2] - HP->pw[2], oy = HP->cw[1] - HP->pw[1];
                int32_t yo = hp_wrap(HP->cw[4] - HP->pw[3], 2048);
                int32_t gx = 0, gz = 0, gy = 0, gyo = 0, gp = 0, gr = 0;
                int32_t dhk = hp_wrap(HP->pw[3] - HP->pwp[3], 2048);
                if (HP->cwv >= 2u && HP->pwps + 1u == HP->pws && iabs32(dhk) < 256
                    && iabs32(HP->pw[0] - HP->pwp[0]) < 2048 && iabs32(HP->pw[2] - HP->pwp[2]) < 2048) {
                    int32_t px, pz;
                    kd_rot(HP->cwp[0] - HP->pwp[0], HP->cwp[2] - HP->pwp[2], dhk, &px, &pz);
                    gx = ox - px; gz = oz - pz;
                    gy = oy - (HP->cwp[1] - HP->pwp[1]);
                    gyo = hp_wrap(yo - hp_wrap(HP->cwp[4] - HP->pwp[3], 2048), 2048);
                    gp = HP->cw[3] - HP->cwp[3]; gr = hp_wrap(HP->cw[5] - HP->cwp[5], 2048);
                    if (iabs32(gx) > 512 || iabs32(gz) > 512 || iabs32(gy) > 512 || iabs32(gyo) > 128
                        || iabs32(gp) > 128 || iabs32(gr) > 128) gx = gz = gy = gyo = gp = gr = 0;   /* a cut */
                    /* only a steady change is carried (the finish's orbit, +8 a step): the step before's
                     * change is found the same way, and a component that differs from it by over half
                     * itself (plus a little) is noise -- the game's own shake at speed (+49C4: the yaw +-6,
                     * x and y a few units, at random each step: r49t's helicopter view) -- and not carried */
                    if (HP->cwv >= 3u && HP->pwpps + 1u == HP->pwps) {
                        int32_t dhp = hp_wrap(HP->pwp[3] - HP->pwpp[3], 2048);
                        if (iabs32(dhp) < 256) {
                            int32_t ppx, ppz;
                            kd_rot(HP->cwpp[0] - HP->pwpp[0], HP->cwpp[2] - HP->pwpp[2], dhp, &ppx, &ppz);
                            int32_t hx = (HP->cwp[0] - HP->pwp[0]) - ppx, hz = (HP->cwp[2] - HP->pwp[2]) - ppz;
                            int32_t hy = (HP->cwp[1] - HP->pwp[1]) - (HP->cwpp[1] - HP->pwpp[1]);
                            int32_t hyo = hp_wrap(hp_wrap(HP->cwp[4] - HP->pwp[3], 2048) - hp_wrap(HP->cwpp[4] - HP->pwpp[3], 2048), 2048);
                            int32_t hp_ = HP->cwp[3] - HP->cwpp[3], hr = hp_wrap(HP->cwp[5] - HP->cwpp[5], 2048);
                            if (iabs32(gx - hx) > iabs32(gx) / 2 + 4) gx = 0;
                            if (iabs32(gz - hz) > iabs32(gz) / 2 + 4) gz = 0;
                            if (iabs32(gy - hy) > iabs32(gy) / 2 + 4) gy = 0;
                            if (iabs32(gyo - hyo) > iabs32(gyo) / 2 + 2) gyo = 0;
                            if (iabs32(gp - hp_) > iabs32(gp) / 2 + 2) gp = 0;
                            if (iabs32(gr - hr) > iabs32(gr) / 2 + 2) gr = 0;
                        } else gx = gz = gy = gyo = gp = gr = 0;
                    } else gx = gz = gy = gyo = gp = gr = 0;
                }
                int32_t qi = (int32_t)qt;
                ox += (int32_t)(((int64_t)gx * qi) >> 16); oz += (int32_t)(((int64_t)gz * qi) >> 16);
                int32_t rx, rz;
                kd_rot(ox, oz, dh, &rx, &rz);
                cm[0x58u / 4u] = pc[1] + rx; cm[0x60u / 4u] = pc[3] + rz;
                cm[0x5Cu / 4u] = pc[2] + oy + (int32_t)(((int64_t)gy * qi) >> 16);
                cm[0x64u / 4u] = HP->cw[3] + (int32_t)(((int64_t)gp * qi) >> 16);
                cm[0x68u / 4u] = (pc[0x678u / 4u] + yo + (int32_t)(((int64_t)gyo * qi) >> 16)) & 2047;
                cm[0x6Cu / 4u] = (HP->cw[5] + (int32_t)(((int64_t)gr * qi) >> 16)) & 2047;
            }
        }
#else
        else if (HP_CAMMODE(race) != 2u && HP->cwv >= 3u) {
#ifdef A4_KINEDRAW
            HP->cV = 0;
#endif
            /* another camera (the fly-in, the crash camera, the helicopter's): its own path,
             * drawn on by its last whole step's move -- only where that move is the one before
             * it again (a steady path); the crash camera jumps step to step (yaw 1166, 1019,
             * 1180, r44t) and is held. Not across a cut */
            int32_t dv[6]; uint32_t cut = 0;
            for (uint32_t f = 0; f < 6u; f++) {
                int32_t d = HP->cw[f] - HP->cwp[f], e = HP->cwp[f] - HP->cwpp[f];
                if (f >= 3u) { d &= 2047; if (d >= 1024) d -= 2048; e &= 2047; if (e >= 1024) e -= 2048; }
                if (d > 1024 || d < -1024) cut = 1;
                if (iabs32(d - e) > iabs32(d) / 2 + 8) cut = 1;
                dv[f] = d;
            }
            if (!cut) for (uint32_t f = 0; f < 6u; f++) {
                int32_t v = HP->cw[f] + (int32_t)(((int64_t)dv[f] * (int32_t)qt) >> 16);
                cm[0x58u / 4u + f] = f >= 3u ? (v & 2047) : v;
            }
        }
#endif
        (void)sp;
    }
    h[3] = 0; HP->replaying = 0; HP->nodd++;
}
/* before the render after an odd half-step: what the render changes is put back after it (the
 * speculation's save and whole restore), and the traffic drawn half a step on */
static void hp_snap(uint32_t race) __attribute__((unused));
static void hp_snap(uint32_t race)
{
    volatile uint32_t *h = (volatile uint32_t *)A4_GL_HSTEP, *sp = (volatile uint32_t *)A4_GL_SPEC;
    uint32_t n = 0;
    for (uint32_t i = 0; i < h[2]; i++) {
        uint32_t car = ((volatile uint32_t *)A4_GL_HCALL)[6u * i + 1u], j;
        for (j = 0; j < n && sp[4 + j] != car; j++) ;
        if (j == n && n < SPEC_MAXCARS) sp[4 + n++] = car;
    }
    sp[0] = n;
    for (uint32_t i = 0; i < n; i++)
        words_copy((volatile uint32_t *)(A4_GL_SNAP + i * SPEC_CARSZ), (volatile uint32_t *)sp[4 + i], SPEC_CARSZ / 4u);
    uint32_t ctx = *(volatile uint32_t *)(race + 0x38);
    uint32_t K = ctx ? *(volatile uint32_t *)(ctx + 0x1114) : 0;
    sp[2] = K;
    if (K) words_copy(sp + 32, (volatile uint32_t *)K, 16);
    sp[3] = race;
    words_copy(sp + 64, (volatile uint32_t *)race, SPEC_RACESZ / 4u);
#ifdef A4_SPECTRAFFIC
    sp[26] = (sp[27] && h[0] - sp[28] <= 2u) ? sp[27] : 0u;
    if (sp[26]) {
        words_copy((volatile uint32_t *)A4_GL_TSNAP, (volatile uint32_t *)sp[26], TRAF_SZ / 4u);
        for (uint32_t i = 0; i < TRAF_N; i++) {
            volatile int32_t *c = (volatile int32_t *)(sp[26] + i * TRAF_CARSZ);
            const volatile int32_t *p = (const volatile int32_t *)(A4_GL_TPREV + i * TRAF_CARSZ);
            if (c[1] == p[1] && c[3] == p[3]) continue;
            if (iabs32(c[1] - p[1]) > SPEC_CUT || iabs32(c[3] - p[3]) > SPEC_CUT || iabs32(c[0x11Cu / 4u] - p[0x11Cu / 4u]) > 512) continue;
            static const uint16_t tw[] = { 0x004, 0x008, 0x00C, 0x04C, 0x050, 0x054, 0x058, 0x05C, 0x060, 0x064, 0x068, 0x06C,
                                           0x0A8, 0x0AC, 0x0B0, 0x11C, 0x120, 0x124 };
            for (uint32_t f = 0; f < sizeof tw / sizeof tw[0]; f++) { uint32_t w = tw[f] / 4u; c[w] += (c[w] - p[w]) / 2; }
        }
    }
#endif
    sp[1] = 1; sp[20] = 0;
}
#ifdef A4_KINEDRAW
/* round 46 (lockstep against stock, crec_ord / crec_ors0: every physics word identical for 499
 * steps, but the render's own counters -- +15C, two +B28 flag bits, then +AD8/+ADC one off -- ran
 * once a frame, not once a step: since round 43 every frame kept the render's own changes): a
 * frame after the step's first leaves nothing behind. The cars the step moved and the race
 * object are saved after the frame is drawn on and put back whole at the next half-step
 * (spec_restore, with the traffic hp_tsnap saved), as rounds 33-42 undid every frame but the
 * first. The first frame keeps its own changes, as the game renders once a step */
static void kd_snapall(uint32_t race)
{
    volatile uint32_t *h = (volatile uint32_t *)A4_GL_HSTEP, *sp = (volatile uint32_t *)A4_GL_SPEC;
    uint32_t n = 0;
    for (uint32_t i = 0; i < h[2]; i++) {
        uint32_t car = ((volatile uint32_t *)A4_GL_HCALL)[6u * i + 1u], j;
        for (j = 0; j < n && sp[4 + j] != car; j++) ;
        if (j == n && n < SPEC_MAXCARS) sp[4 + n++] = car;
    }
    sp[0] = n;
    for (uint32_t i = 0; i < n; i++)
        words_copy((volatile uint32_t *)(A4_GL_SNAP + i * SPEC_CARSZ), (volatile uint32_t *)sp[4 + i], SPEC_CARSZ / 4u);
    sp[2] = 0;                       /* not K: the keys that arrive before the next half-step are the player's */
    sp[3] = race;
    words_copy(sp + 64, (volatile uint32_t *)race, SPEC_RACESZ / 4u);
    sp[1] = 1; sp[20] = 0;           /* put back whole, the traffic's snapshot (hp_tsnap) with it */
}
#endif
#ifdef A4_FRACPHYS
/* A4_FRACPHYS, before a render: the traffic drawn q16 of a step past the whole step that moved
 * it (each car by that much of its last step), put back after the render -- only the words
 * moved, the render's own changes kept (spec_restore with SPEC word 20 set) */
static void hp_tsnap(uint32_t race, uint32_t q16)
{
    volatile uint32_t *h = (volatile uint32_t *)A4_GL_HSTEP, *sp = (volatile uint32_t *)A4_GL_SPEC;
    (void)race;
    sp[0] = 0; sp[2] = 0; sp[3] = 0;
#ifdef A4_SPECTRAFFIC
#ifdef A4_KINEDRAW
    sp[26] = (sp[27] && h[0] - sp[28] <= 2u) ? sp[27] : 0u;   /* round 46: every frame (the corrector at q 0 too) */
#ifdef A4_SCRIPTHOLD
    if (HP->hold) { q16 = 0; for (uint32_t i = 0; i < TRAF_N; i++) HP->tV[i] = 0; }
#endif
    uint32_t tfirst = sp[28] != HP->tstep;
    HP->tstep = sp[28];
#else
    sp[26] = (sp[27] && h[0] - sp[28] <= 2u && q16) ? sp[27] : 0u;
#endif
    if (sp[26]) {
        words_copy((volatile uint32_t *)A4_GL_TSNAP, (volatile uint32_t *)sp[26], TRAF_SZ / 4u);
        for (uint32_t i = 0; i < TRAF_N; i++) {
            volatile int32_t *c = (volatile int32_t *)(sp[26] + i * TRAF_CARSZ);
            const volatile int32_t *p = (const volatile int32_t *)(A4_GL_TPREV + i * TRAF_CARSZ);
#ifdef A4_KINEDRAW
            if (iabs32(c[1] - p[1]) > SPEC_CUT || iabs32(c[3] - p[3]) > SPEC_CUT || iabs32(c[0x11Cu / 4u] - p[0x11Cu / 4u]) > 512) { HP->tV[i] = 0; continue; }
#else
            if (c[1] == p[1] && c[3] == p[3]) continue;
            if (iabs32(c[1] - p[1]) > SPEC_CUT || iabs32(c[3] - p[3]) > SPEC_CUT || iabs32(c[0x11Cu / 4u] - p[0x11Cu / 4u]) > 512) continue;
#endif
            static const uint16_t tw[] = { 0x004, 0x008, 0x00C, 0x04C, 0x050, 0x054, 0x058, 0x05C, 0x060, 0x064, 0x068, 0x06C,
                                           0x0A8, 0x0AC, 0x0B0, 0x11C, 0x120, 0x124 };
#ifdef A4_KINEDRAW
            /* round 46: the traffic through the corrector (r41 on the phone: traffic "snap back into
             * position as I get closer" -- a car that braked or swerved meeting its extrapolation).
             * Its offset goes on the position (+004, +00C) and on each word that moved exactly as the
             * position did this step and sits near it (its copies), found here rather than assumed */
            int32_t dx = c[1] - p[1], dz = c[3] - p[3];
            uint32_t xc = 0, zc = 0;
            for (uint32_t f = 0; f < sizeof tw / sizeof tw[0]; f++) {
                uint32_t w = tw[f] / 4u;
                if (c[w] - p[w] == dx && iabs32(c[w] - c[1]) <= 64) xc |= 1u << f;
                else if (c[w] - p[w] == dz && iabs32(c[w] - c[3]) <= 64) zc |= 1u << f;
            }
#endif
            for (uint32_t f = 0; f < sizeof tw / sizeof tw[0]; f++) { uint32_t w = tw[f] / 4u; c[w] += (int32_t)(((int64_t)(c[w] - p[w]) * (int32_t)q16) >> 16); }
#ifdef A4_KINEDRAW
            {
                int32_t m[2] = { c[1] << 8, c[3] << 8 }, v[2] = { dx << 8, dz << 8 };
                static const int32_t cut[2] = { 1024 << 8, 1024 << 8 };
                kd_correct(HP->tE[i], HP->tD[i], &HP->tT[i], &HP->tV[i], m, v, 2u, 0u, tfirst, (HP->wstep << 16) + q16, cut);
                int32_t ex = ((m[0] + 128) >> 8) - c[1], ez = ((m[1] + 128) >> 8) - c[3];
                for (uint32_t f = 0; f < sizeof tw / sizeof tw[0]; f++) {
                    uint32_t w = tw[f] / 4u;
                    if ((xc >> f) & 1u) c[w] += ex; else if ((zc >> f) & 1u) c[w] += ez;
                }
            }
#endif
        }
        bm_mark((volatile uint32_t *)A4_GL_TBM, (volatile uint32_t *)sp[26], (volatile uint32_t *)A4_GL_TSNAP, TRAF_SZ / 4u);
    }
    sp[1] = sp[26] ? 1u : 0u; sp[20] = 1;
#else
    (void)q16; (void)h;
#endif
}
#endif
#endif
int a4_halfstep(uint32_t race)
{
    volatile uint32_t *h = (volatile uint32_t *)A4_GL_HSTEP;
    (void)race;
    uint32_t k = h[0]++;
    if (k & 1u) {                    /* odd */
#if defined(A4_FRACPHYS)
        spec_restore();              /* the traffic drawn on at the last render put back */
        h[1]++;                      /* round 43: the cars move at the renders, by the time each is at */
#elif defined(A4_HALFPHYS)
        spec_restore();
#ifdef A4_CARREC
        if (race) car_record(race, 2u);   /* bench: the state the whole step left, as a normal build records it */
#endif
        hp_sub(race, 32768u, 1u);    /* round 42: the game's cars and camera, half a step, for real */
        HP->odd = 1;
        h[1]++;
#elif defined(A4_HALFSPEC)
#ifdef A4_ABCYCLE10
        if (A4_PERF[PF_MODE] & AB_HSPEC)  /* the A/B: off, the odd half-step is empty and draws nothing */
#endif
            h[1]++;                  /* round 33: worth a render -- the speculation runs at the render, at
                                      * the fraction of a step the clock has reached (a4_step_render) */
#elif defined(A4_HALFMOTION)
        hcall_replay();
        h[1]++;                      /* the cars moved: worth a render */
#endif
        return 1;
    }
#ifdef A4_HALFSPEC
    spec_restore();                  /* the game's own step from the saved state */
    h[5]++;                          /* a whole step: the next render keeps its own changes */
#endif
#ifdef A4_INTERP
    ip_capture(race);                /* the state before this whole step, to draw the next frames from */
#endif
#ifdef A4_HALFPHYS
    HP->odd = 0;
    if (HP->race != race) { HP->race = race; for (uint32_t i = 0; i < HP_CARS; i++) HP->car[i] = 0; HP->camscript = 0; HP->cwv = 0; HP->camx = 0; HP->started = 0; }
    for (uint32_t i = 0; i < HP_CARS; i++) { HP->halve[i] = HP->ran[i]; HP->ran[i] = 0; }
#ifdef A4_KINEPHYS
    for (uint32_t i = 0; i < HP_CARS; i++) {
        volatile int32_t *c = (volatile int32_t *)HP->car[i];
        if (HP->disp[i] && c) { c[2] = HP->yW[i]; c[0xD0u / 4u] = HP->dW[i]; c[0x020u / 4u] = HP->wW[i]; }
#ifdef A4_KINEDRAW
        if (HP->disp[i] && c) {      /* round 46: every drawn word back as the step left it */
            c[1] = HP->xW[i]; c[3] = HP->zW[i]; c[0x678u / 4u] = HP->hW[i];
            c[0xA8u / 4u] = HP->aW[i]; c[0xB0u / 4u] = HP->bW[i];
        }
#endif
        HP->disp[i] = 0;
        if (!HP->halve[i]) { HP->ks[i][0] = HP->ks[i][1] = 0; HP->kerr[i][0] = HP->kerr[i][1] = 0; }
    }
    HP->wstep++;
#endif
    HP->fwhole = HP->committed < 65536u ? 65536u - HP->committed : 0u;   /* A4_FRACPHYS */
    HP->committed = 0;
    if (HP->camx && race) {          /* the camera the last whole step left, for the game's own step */
        for (uint32_t f = 0; f < 6u; f++) ((volatile int32_t *)race)[0x58u / 4u + f] = HP->cw[f];
        HP->camx = 0;
    }
#endif
#ifdef A4_HALFMOTION
    h[2] = 0;                        /* the whole step about to run records its car calls */
#endif
    h[1]++;
    return 0;                        /* even: the game's own step */
}
#endif
#ifdef A4_FRAMEMARK
/* round 37, bench only: called just before each render call (after the speculation moved
 * the camera), for EKA2L1_PCTRACE_REGS:
 * r0, r1, r2 = the camera's position (race +0x58, +0x5C, +0x60), r3 = its angles +0x64 and
 * +0x68 (12 bits each) and HSTEP[1] << 24. (r37n: the object at [race+0x38], whose bytes 4
 * and 9 make +12992 skip its 3D, read 0 at every render's entry.) */
__attribute__((noinline)) void a4_render_mark(uint32_t x, uint32_t y, uint32_t z, uint32_t ang)
{
    __asm__ volatile("" : : "r"(x), "r"(y), "r"(z), "r"(ang) : "memory");
}
/* and the player car's position (+0x04/+0x08/+0x0C) and heading (+0x678) */
__attribute__((noinline)) void a4_render_mark2(uint32_t x, uint32_t y, uint32_t z, uint32_t hd)
{
    __asm__ volatile("" : : "r"(x), "r"(y), "r"(z), "r"(hd) : "memory");
}
/* and the camera's pitch +0x64, roll +0x6C and +0x70 as the render reads them (round 40) */
__attribute__((noinline)) void a4_render_mark3(uint32_t pitch, uint32_t roll, uint32_t w70, uint32_t yaw)
{
    __asm__ volatile("" : : "r"(pitch), "r"(roll), "r"(w70), "r"(yaw) : "memory");
}
#endif
int a4_step_render(void *self)                                  /* +1696C: bl +12992 */
{
    volatile uint32_t *pf = A4_PERF;
#if defined(A4_FRAMEMARK) || defined(A4_FRAMEREC)
    uint32_t mark_q16 = 0;   /* the speculation's fraction this render, bit 31 = computed */
#endif

#ifdef A4_HALFLOGIC
    {   volatile uint32_t *h = (volatile uint32_t *)A4_GL_HSTEP;
        if (!h[1]) {                 /* no whole step since the last render: skipped, its half-steps counted */
            if (pf[PF_FLAGS] & PFF_LOGIC) pf[PF_NSTEP] += pf[PF_LSTEP] & 0xFFu;
            pf[PF_FLAGS] &= ~PFF_LOGIC;
#ifdef A4_SLOWREC
            ((volatile uint32_t *)0x00455800u)[25]++;  /* a render skipped (no half-step since the last) */
#endif
            return 0; }
        h[1] = 0; }
#endif
    if (pf[PF_FLAGS] & PFF_LOGIC) {
        uint32_t t = a4_ntick() - pf[PF_MLOGIC], ns = pf[PF_LSTEP];
        uint32_t n = ns & 0xFFu, m = ns >> 8;
        pf[PF_FLAGS] &= ~PFF_LOGIC;
        if (t >= 500u) STEPB[14]++;                             /* a load inside the update */
        else {
            pf[PF_TLOGIC] += t; pf[PF_NSTEP] += n; pf[PF_NSPAN]++; pf[PF_TCAM] += pf[PF_SCAM];
            STEPB[0]++; STEPB[1] += n; STEPB[2] += t;
            if (t > STEPB[4]) STEPB[4] = t;
            STEPB[5] += m > n ? m - n : 0u;
#ifndef A4_HALFSPEC
            STEPB[3] += pf[PF_SCAM]; STEPB[6] += pf[PF_SMAIN]; STEPB[7] += pf[PF_NMAIN];
#endif
            STEPB[8u + (n > 5u ? 5u : n)]++;
            if (n == 1u) STEPB[16u + (t >= 44u ? 11u : t >> 2)]++;
        }
        STEPB[15] = 0x31505453u;                                /* 'STP1' */
    }
#ifdef A4_HALFSPEC
    /* Round 33: the speculative frame at the render, at the game time the update ran (round 32's
     * frames showed the game at half-step instants -- one, two or three half-steps a frame for
     * ~65 ms of real time each, unevenly: the phone's step block, n = 1 / 2 / 3+ in 39 / 52 /
     * 9% of renders). The state is the last whole step; the clock t (K+0x2C, what +15B1C
     * counted from) is past it by t mod 10667 plus a half-step if the last half-step was odd.
     * That fraction of a step (q16) is how far between the whole step and its prediction the
     * frame is drawn. The telemetry: header [63] (this window's ticks and longest run) and, per
     * segment, STEPB[3] the ticks, [6] the runs, [7] the longest run | runs of 8 ms or more << 16. */
#ifdef A4_ABCYCLE10
    if (A4_PERF[PF_MODE] & AB_HSPEC)
#endif
    {   uint32_t race = (uint32_t)(uintptr_t)self, ctx = *(volatile uint32_t *)(race + 0x38);
        uint32_t K = ctx ? *(volatile uint32_t *)(ctx + 0x1114) : 0;
        volatile uint32_t *h = (volatile uint32_t *)A4_GL_HSTEP;
        if (K && h[0]) {
            uint32_t t = ((volatile uint32_t *)K)[11];
            uint32_t q = (uint32_t)(((uint64_t)t * 0x312684D0u) >> 43);   /* t / 10667, exact below 2^31 */
            uint32_t ph = t - q * 10667u + (((h[0] - 1u) & 1u) ? 10667u : 0u);   /* 0 .. 21333 */
            uint32_t q16 = (uint32_t)(((uint64_t)ph * 201320u) >> 16);         /* ph * 65536 / 21334 */
#ifdef A4_SLOWMOQ
            {   /* round 41 (r40 on the phone: "the cars online lag during takedown cameras"):
                 * the logic half-step's period is the race's time scale times 10667 >> 8
                 * (+15B1C: [race+0x84C], 256 at normal speed, more in slow motion), not 10667.
                 * At 10667 in slow motion the fraction swept a whole step more than once per
                 * step: the cars drawn up to a step ahead, then back, then ahead again */
                uint32_t sc = *(volatile uint32_t *)(race + 0x84Cu);
                if (sc != 256u && sc > 0u && sc <= 0x4000u) {
                    uint32_t P = (sc * 10667u) >> 8, r = 0, D, f = 0;
                    for (int b = 31; b >= 0; b--) { r = (r << 1) | ((t >> b) & 1u); if (r >= P) r -= P; }   /* t mod P */
                    ph = r + (((h[0] - 1u) & 1u) ? P : 0u);   /* 0 .. 2P */
                    D = 2u * P;
                    for (int b = 0; b < 16; b++) { ph <<= 1; f <<= 1; if (ph >= D) { ph -= D; f |= 1u; } }   /* ph / 2P, 16 bits */
                    q16 = f;
#ifdef A4_SLOWREC
                    ((volatile uint32_t *)0x00455800u)[14] += 0x10000u;   /* high half: speculations in slow motion */
#endif
                }
            }
#endif
#if defined(A4_FRAMEMARK) || defined(A4_FRAMEREC)
            mark_q16 = q16 | 0x80000000u;
#endif
            if (q16 > 65535u) q16 = 65535u;
#if defined(A4_FRACPHYS)
            /* round 43: the cars moved by the game's own calls to the time this render is at
             * (q16 of the step past the whole step) and kept -- every frame the game's state at its
             * own moment, no prediction to take back. The traffic (moved once a step, +FB12 not
             * replayed) drawn q16 on and put back after. The render's own changes kept, as r41 */
            if (1) {
                uint32_t s0 = a4_ntick();
#ifdef A4_CARREC
                if (HP->committed == 0u) car_record(race, 2u);   /* bench: the state the whole step and its camera left */
#endif
#ifdef A4_KINEDRAW
                {   /* round 46: every frame drawn (the corrector needs each one), at its own q16 */
                    uint32_t f = q16 > HP->committed ? q16 - HP->committed : 0u, first = HP->committed == 0u;
#ifdef A4_SCRIPTHOLD
                    {   /* round 49: a scripted camera (the crash 0x18 and the race's other event cameras,
                         * +554C/+57AC's keyframe paths and their kin) turns where no step's change can say,
                         * and the frames drew it back and forth (the crash: 3-5 reversals in ~45 frames on
                         * the bench after A4_CAMALL, 13 before). In those, the frame is the game's step --
                         * the camera and every car as it left them -- as round 40's A4_SPECSCRIPTHOLD did */
                        uint32_t cmode = HP_CAMMODE(race);
                        HP->hold = cmode == 0x14u || (cmode >= 0x17u && cmode <= 0x1Du);
#ifdef A4_BENCH_HOLDTEST
                        HP->hold |= ((a4_ntick() / 1000u) % 10u) >= 7u;   /* bench: the hold forced 3 s in 10, to test it */
#endif
                    }
#endif
                    hp_sub(race, f, first);
#ifdef A4_CARREC
                    if (first) {   /* bench: the corrections the step's first frame made, 'CRKD' */
                        uint32_t pl = *(volatile uint32_t *)(race + 0x390), pidx = pl ? *(volatile uint32_t *)(pl + 0xC) : 99u;
                        uint32_t pcar = pidx < 16u ? *(volatile uint32_t *)(race + 0x394 + 4u * pidx) : 0;
                        uint32_t pi = pcar ? hp_idx(pcar) : HP_CARS;
                        volatile uint32_t q[8];
                        q[0] = 0x444B5243u; q[1] = ((volatile uint32_t *)A4_GL_CREC)[1]; q[2] = HP->kn; q[3] = HP->kbig; q[4] = HP->kcut;
                        q[5] = pi < HP_CARS ? (uint32_t)HP->kE[pi][0] : 0; q[6] = pi < HP_CARS ? (uint32_t)HP->kE[pi][2] : 0;
                        q[7] = (uint32_t)HP->cE[3];
                        a4_rec_emit2((const void *)q, (const void *)A4_GL_HP, (const void *)race);
                    }
#ifdef A4_YREC
                    /* bench (engine map, after round 47 on the phone: cars fly off and snap back): every
                     * car as this frame draws it, with the step's own values beside it -- 'FRKD', the
                     * step, q16 | car slot << 24 | first << 31, the step's y, +0D0, x, z, heading */
                    {   /* and the camera as this frame draws it: 'FRKC', the step, q16 | first << 31, the camera
                         * mode, the slow-motion scale race+0x84C (256 normal), the race's own step race+0x30;
                         * the player car and the race object (camera +0x48..) beside it */
                        uint32_t pl = *(volatile uint32_t *)(race + 0x390), pidx = pl ? *(volatile uint32_t *)(pl + 0xC) : 99u;
                        uint32_t pcar = pidx < 16u ? *(volatile uint32_t *)(race + 0x394 + 4u * pidx) : 0;
                        volatile uint32_t q[8];
                        q[0] = 0x434B5246u; q[1] = ((volatile uint32_t *)A4_GL_CREC)[1];
                        q[2] = (q16 & 0xFFFFFFu) | (first << 31); q[3] = HP_CAMMODE(race);
                        q[4] = *(volatile uint32_t *)(race + 0x84Cu); q[5] = *(volatile uint32_t *)(race + 0x30u);
                        q[6] = HP->wstep; q[7] = pcar;
                        if (pcar) a4_rec_emit2((const void *)q, (const void *)pcar, (const void *)race);
                    }
                    for (uint32_t ci = 0; ci < HP_CARS; ci++) {
                        if (!HP->car[ci] || !HP->disp[ci]) continue;
                        volatile uint32_t q[8];
                        q[0] = 0x444B5246u; q[1] = ((volatile uint32_t *)A4_GL_CREC)[1];
                        q[2] = (q16 & 0xFFFFFFu) | (ci << 24) | (first << 31);
                        q[3] = (uint32_t)HP->yW[ci]; q[4] = (uint32_t)HP->dW[ci]; q[5] = (uint32_t)HP->xW[ci];
                        q[6] = (uint32_t)HP->zW[ci]; q[7] = (uint32_t)HP->hW[ci];
                        a4_rec_emit2((const void *)q, (const void *)HP->car[ci], (const void *)race);
                    }
#endif
#endif
                    HP->committed += f; if (!HP->committed) HP->committed = 1u;   /* the step's first frame done */
                    hp_tsnap(race, q16);
#if defined(A4_YREC) && defined(A4_SPECTRAFFIC)
                    {   /* bench (r49 on the phone: traffic "passed through", no hit): every traffic car near
                         * the player as this frame draws it, with the game's own position beside it --
                         * 'FRKT', the step, q16 | slot << 24 | first << 31, drawn x, z, the step's x, z,
                         * the drawn heading +11C (the game's: the snapshot when the frame moved it) */
                        volatile uint32_t *sp = (volatile uint32_t *)A4_GL_SPEC;
                        uint32_t arr = sp[26] ? sp[26] : sp[27];
                        uint32_t pl = *(volatile uint32_t *)(race + 0x390), pidx = pl ? *(volatile uint32_t *)(pl + 0xC) : 99u;
                        uint32_t pcar = pidx < 16u ? *(volatile uint32_t *)(race + 0x394 + 4u * pidx) : 0;
                        for (uint32_t i = 0; arr && pcar && i < TRAF_N; i++) {
                            const volatile int32_t *c = (const volatile int32_t *)(arr + i * TRAF_CARSZ);
                            const volatile int32_t *s = sp[26] ? (const volatile int32_t *)(A4_GL_TSNAP + i * TRAF_CARSZ) : c;
                            const volatile int32_t *pc = (const volatile int32_t *)pcar;
                            if (iabs32(c[1] - pc[1]) > 4000 || iabs32(c[3] - pc[3]) > 4000) continue;
                            volatile uint32_t q[8];
                            q[0] = 0x544B5246u; q[1] = ((volatile uint32_t *)A4_GL_CREC)[1];
                            q[2] = (q16 & 0xFFFFFFu) | (i << 24) | (first << 31);
                            q[3] = (uint32_t)c[1]; q[4] = (uint32_t)c[3]; q[5] = (uint32_t)s[1]; q[6] = (uint32_t)s[3];
                            q[7] = (uint32_t)c[0x11Cu / 4u];
                            a4_rec_emit2((const void *)q, (const void *)c, (const void *)race);
                        }
                    }
#endif
                    if (!first) kd_snapall(race);
                }
#else
                if (q16 > HP->committed + 256u) { hp_sub(race, q16 - HP->committed, HP->committed == 0u); HP->committed = q16; }
                hp_tsnap(race, HP->committed);
#endif
#elif defined(A4_HALFPHYS)
            /* every half-step is the game's own state: drawn as it is. After an odd half-step the
             * render's own once-a-step changes are undone (and the traffic, moved once a step,
             * drawn half a step on) */
            (void)q16;
#if defined(A4_FRAMEMARK) || defined(A4_FRAMEREC)
            mark_q16 = (HP->odd ? 32768u : 0u) | 0x80000000u;
#endif
            if (HP->odd) {
                hp_snap(race);
#elif defined(A4_INTERP)
            if (1) {                 /* every frame: at q 0 it shows the step before, not this one */
#else
            if (q16 >= 2048u) {      /* under 1/32 of a step (2.7 ms): the whole step as it is */
#endif
#if defined(A4_FRACPHYS)
#elif !defined(A4_HALFPHYS)
                uint32_t s0 = a4_ntick();
                spec_run(race, q16);
                ((volatile uint32_t *)A4_GL_SPEC)[20] = h[5] ? 1u : 0u;   /* the first render since a whole step keeps its own changes */
#else
                uint32_t s0 = a4_ntick();
#endif
                uint32_t dt = a4_ntick() - s0;
                A4_PERF[PF_TSPEC] += dt; if (dt > A4_PERF[PF_MSPEC]) A4_PERF[PF_MSPEC] = dt;
                STEPB[3] += dt; STEPB[6]++;
                if (dt > (STEPB[7] & 0xFFFFu)) STEPB[7] = (STEPB[7] & 0xFFFF0000u) | (dt > 0xFFFFu ? 0xFFFFu : dt);
                if (dt >= 8u) STEPB[7] += 0x10000u;
            }
        }
    }
#endif
    /* the render call itself, and its part before the first triangle (the game's own
     * transforms and sort, which the 3D pass timer -- first triangle to composite -- misses) */
    uint32_t t0 = a4_ntick();
#ifdef A4_CLOSECAM
    /* bench (round 33, the owner's crash photos: the car in pieces in the close crash camera,
     * green off): the camera pulled to 1/A4_CLOSECAM of its distance from the player car for
     * the render and put back after, so a check build's same-frame captures (software against
     * GPU) show what a camera that close does to the GPU picture */
    int32_t cc_save[3]; uint32_t cc_on = 0;
    {   uint32_t race = (uint32_t)(uintptr_t)self, pl = *(volatile uint32_t *)(race + 0x390);
        uint32_t idx = pl ? *(volatile uint32_t *)(pl + 0xC) : 0;
        uint32_t car = idx < 16u ? *(volatile uint32_t *)(race + 0x394 + 4u * idx) : 0;
        if (car) {
            volatile int32_t *cm = (volatile int32_t *)race;
            const volatile int32_t *c = (const volatile int32_t *)car;
            for (int i = 0; i < 3; i++) {
                cc_save[i] = cm[0x58 / 4 + i];
                cm[0x58 / 4 + i] = c[1 + i] + (cm[0x58 / 4 + i] - c[1 + i]) / (A4_CLOSECAM);
            }
            cc_on = 1;
        }
    }
#endif
#ifdef A4_RENDGATE
    ((volatile uint32_t *)0x00447000u)[31] = 1;     /* bench: EKA2L1_WATCHGATE=44707C -- what the render reads */
#endif
#ifdef A4_FRAMEMARK
    {   volatile uint32_t *c = (volatile uint32_t *)self;
        volatile uint32_t *h = (volatile uint32_t *)A4_GL_HSTEP;
        uint32_t race = (uint32_t)(uintptr_t)self, pl = *(volatile uint32_t *)(race + 0x390);
        uint32_t idx = pl ? *(volatile uint32_t *)(pl + 0xC) : 0;
        volatile uint32_t *car = idx < 16u ? *(volatile uint32_t *volatile *)(race + 0x394 + 4u * idx) : 0;
        /* round 40: r3 = the camera's yaw (12 bits) | the speculation's fraction q16 >> 4
         * (12 bits) << 12 | HSTEP[5] (renders since the last whole step... as counted) << 24;
         * the car mark: x, z, its heading (+0x678), and the real time (a4_ntick, 1 ms) */
        a4_render_mark(c[0x58u / 4u], c[0x5Cu / 4u], c[0x60u / 4u],
                       (c[0x68u / 4u] & 0xFFFu) | ((mark_q16 >> 4) & 0xFFFu) << 12 | (mark_q16 >> 31) << 31 | (h[5] & 0x7Fu) << 24);
        a4_render_mark2(car ? car[1] : 0, car ? car[3] : 0, car ? car[0x678u / 4u] : 0, a4_ntick());
        a4_render_mark3(c[0x64u / 4u], c[0x6Cu / 4u], c[0x70u / 4u], c[0x68u / 4u]);
#ifdef A4_SPECTRAFFIC
        {   volatile uint32_t *sp = (volatile uint32_t *)A4_GL_SPEC;
            if (sp[27]) for (uint32_t i = 0; i < TRAF_N; i++) {
                const volatile int32_t *tc = (const volatile int32_t *)(sp[27] + i * TRAF_CARSZ);
                const volatile int32_t *tp = (const volatile int32_t *)(A4_GL_TPREV + i * TRAF_CARSZ);
                if (tc[1] != tp[1] || tc[3] != tp[3]) { a4_traf_mark(i, (uint32_t)tc[1], (uint32_t)tc[3], a4_ntick()); break; }
            }
        }
#endif
    }
#endif
#ifdef A4_FRAMEREC
    {   /* round 41: the frame record (see A4_GL_FREC) -- this render's entry */
        volatile uint32_t *F = (volatile uint32_t *)A4_GL_FREC, *c = (volatile uint32_t *)self;
        uint32_t race = (uint32_t)(uintptr_t)self, pl = *(volatile uint32_t *)(race + 0x390);
        uint32_t idx = pl ? *(volatile uint32_t *)(pl + 0xC) : 0;
        volatile uint32_t *car = idx < 16u ? *(volatile uint32_t *volatile *)(race + 0x394 + 4u * idx) : 0;
        if (F[0] != 0x31435246u) { F[0] = 0x31435246u; F[1] = 0; }   /* 'FRC1' */
        volatile uint32_t *e = F + 4u + (F[1] % FREC_N) * 8u;
        e[0] = a4_ntick(); e[1] = 0;
        e[2] = mark_q16 | (c[0x94u / 4u] & 0xFFu) << 20;   /* q16, the camera mode, bit 31 = a fraction computed */
        e[3] = car ? car[1] : 0; e[4] = car ? car[3] : 0;
        e[5] = (car ? car[0x678u / 4u] & 0xFFFFu : 0u) | (c[0x68u / 4u] & 0xFFFFu) << 16;
        e[6] = c[0x58u / 4u]; e[7] = c[0x60u / 4u];
#ifdef A4_GL_FREC2
        {
            volatile uint32_t *F2 = (volatile uint32_t *)A4_GL_FREC2;
            if (F2[0] != 0x32435246u) { F2[0] = 0x32435246u; F2[1] = 0; }   /* 'FRC2' */
            volatile uint32_t *e2 = F2 + 4u + (F2[1] % FREC2_N) * 4u;
            uint32_t pi = HP_CARS;   /* a lookup only: hp_idx would register a car it does not know */
            for (uint32_t k = 0; car && k < HP_CARS; k++) if (HP->car[k] == (uint32_t)(uintptr_t)car) { pi = k; break; }
            int32_t dy = car ? (int32_t)car[2] : 0, sy = dy, sd = car ? (int32_t)car[0xD0u / 4u] : 0, ex = 0, ez = 0;
            if (pi < HP_CARS && HP->disp[pi]) {
                sy = HP->yW[pi]; sd = HP->dW[pi];
                ex = HP->xW[pi] - (int32_t)car[1]; ez = HP->zW[pi] - (int32_t)car[3];
            }
            e2[0] = e[0];
            e2[1] = ((uint32_t)dy & 0xFFFFu) | (c[0x5Cu / 4u] & 0xFFFFu) << 16;
            e2[2] = ((uint32_t)sy & 0xFFFFu) | ((uint32_t)sd & 0xFFFFu) << 16;
            e2[3] = ((uint32_t)ex & 0xFFFFu) | ((uint32_t)ez & 0xFFFFu) << 16;
            F2[1]++;
        }
#endif
        F[1]++;
    }
#endif
    int r = a4_gcall1(GAMEFN(a4_steprend), (uint32_t)(uintptr_t)self);   /* the render: span trampolines inside */
#ifdef A4_DROPHOLD
    {   /* a render that draws nothing is a frame too; and where the camera stood from the
         * player car as it drew (after the speculation moved both): a camera cut changes
         * that, the empty render does not (r37o: steady to ~15 units a render through it) */
        volatile struct glr *g = (volatile struct glr *)A4_GL_SCRATCH;
        uint32_t race = (uint32_t)(uintptr_t)self, pl = *(volatile uint32_t *)(race + 0x390);
        uint32_t idx = pl ? *(volatile uint32_t *)(pl + 0xC) : 0;
        const volatile int32_t *car = idx < 16u ? *(const volatile int32_t *volatile *)(race + 0x394 + 4u * idx) : 0;
        const volatile int32_t *cm = (const volatile int32_t *)race;
        g->rendered = 1;
        g->camok = car != 0;
        if (car) for (int i = 0; i < 3; i++) g->camoff[i] = cm[0x58 / 4 + i] - car[1 + i];
        if (car) {   /* the running average of the offset's distance on the ground and height -- what does
                      * not turn with the car (r40cf: the world-frame offset turns in every bend, and the
                      * chase test failed there) -- the player's chase camera, whatever the car or view */
            int32_t v[2] = { (int32_t)isqrt64((int64_t)g->camoff[0] * g->camoff[0] + (int64_t)g->camoff[2] * g->camoff[2]), g->camoff[1] };
            if (!g->chaseinit) { g->chaseavg[0] = v[0] * 16; g->chaseavg[1] = v[1] * 16; g->chaseinit = 1; }
            else {   /* quickly while near it (within 64), slowly (1/256) while off it: a long crash
                      * camera does not drag it away, a view the player switches to for good does */
                uint32_t near = 1;
                for (int i = 0; i < 2; i++) { int32_t d = v[i] - g->chaseavg[i] / 16; if (d > 64 || d < -64) near = 0; }
                for (int i = 0; i < 2; i++)
                    g->chaseavg[i] += near ? v[i] - g->chaseavg[i] / 16 : (v[i] * 16 - g->chaseavg[i]) / 256;
            }
        }
    }
#endif
#ifdef A4_SLOWREC
    ((volatile uint32_t *)0x00455800u)[24]++; ((volatile uint32_t *)0x00455800u)[19] = 1;   /* renders; one since the last present */
#endif
#ifdef A4_CLOSECAM
    if (cc_on) { volatile int32_t *cm = (volatile int32_t *)self; for (int i = 0; i < 3; i++) cm[0x58 / 4 + i] = cc_save[i]; }
#endif
#ifdef A4_RENDGATE
    ((volatile uint32_t *)0x00447000u)[31] = 0;
#endif
#if defined(A4_CARREC) && defined(A4_RENDREC)
    {   /* bench: after each render, the mesh context ('CRNC') and the player car ('CRNP'), with
         * the half-step count, through the recorder's emitter */
        volatile uint32_t *q = (volatile uint32_t *)(A4_GL_CREC + 0x20u);
        uint32_t race = (uint32_t)(uintptr_t)self, pl = *(volatile uint32_t *)(race + 0x390);
        uint32_t idx = pl ? *(volatile uint32_t *)(pl + 0xC) : 0;
        uint32_t car = idx < 16u ? *(volatile uint32_t *)(race + 0x394 + 4u * idx) : 0;
        uint32_t cam = MESH->cam;
#ifdef A4_HALFLOGIC
        uint32_t hs = ((volatile uint32_t *)A4_GL_HSTEP)[0];
#else
        uint32_t hs = 0;
#endif
        q[0] = 0x434E5243u; q[1] = hs; q[2] = cam; q[3] = car; q[4] = q[5] = q[6] = q[7] = 0;
        if (cam) a4_rec_emit((const void *)q, (const void *)cam);
        q[0] = 0x504E5243u;
        if (car) a4_rec_emit((const void *)q, (const void *)car);
        q[0] = 0x524E5243u;          /* 'CRNR': the race object and its context */
        a4_rec_emit3((const void *)q, (const void *)race, (const void *)*(volatile uint32_t *)(race + 0x38));
#ifdef A4_HALFSPEC
        /* round 31 phone follow-up: 'CRNS' the step's call log (HSTEP + HCALL) and the
         * speculation's header (which cars it saved), 'CRNA' every car slot (race+0x394, 15):
         * each car's own split on the speculative frames, not only the player's */
        q[0] = 0x534E5243u;
        a4_rec_emit3((const void *)q, (const void *)A4_GL_HSTEP, (const void *)A4_GL_SPEC);
        for (uint32_t i = 0; i < 15u; i++) {
            uint32_t c = *(volatile uint32_t *)(race + 0x394 + 4u * i);
            if (!c) continue;
            q[0] = 0x414E5243u; q[2] = i; q[3] = c;
            a4_rec_emit((const void *)q, (const void *)c);
        }
#endif
    }
#endif
#ifdef A4_HALFSPEC
    /* the speculative state has been drawn: put everything back now, before the next race
     * update's own bookkeeping runs (+15B1C writes the last time to race+0x44; restored at the
     * next even half-step instead, that time went back and the steps were counted twice) */
    {   volatile uint32_t *sp = (volatile uint32_t *)A4_GL_SPEC;
        if (!sp[1] || sp[20]) ((volatile uint32_t *)A4_GL_HSTEP)[5] = 0;   /* this render's own changes stood */
    }
    spec_restore();
#endif
    uint32_t t1 = a4_ntick(), pre = pf[PF_MFIRST] - t0;
    if (t1 - t0 < 500u) {
        STEPB[28] += t1 - t0; STEPB[30]++;
        if (pre <= t1 - t0) STEPB[29] += pre;                   /* MFIRST set inside this call */
        else STEPB[31]++;                                       /* a render with no triangle */
    }
    return r;
}
#endif

static inline uint32_t thint_of(uint32_t key, uint32_t tabk, uint32_t src)
{
    return ((key >> 2) ^ (key >> 12) ^ (tabk >> 4) ^ (src >> 3) ^ (src >> 13)) & (THINT_N - 1u);
}

#ifdef A4_REORDER
/* The batch reorder (round 18). A GL draw call costs ~32 us on the N95 whatever it draws
 * (round 16's windows: batch draw against calls and triangles), and the game changes
 * texture every ~4 triangles. A triangle may be drawn earlier than its turn, joining an
 * earlier run of its own texture and kind, when its screen box misses every run it moves
 * past: triangles that share no pixel draw the same pixels in either order, depth and
 * blending included, so the image is the same. Each triangle's box (int16 x0 y0 x1 y1,
 * one pixel of slack for the fill rule, clamped to the viewport) is kept beside the batch;
 * batch_draw groups, copies the vertices into run order and draws a call a run. Bench
 * census (A4_SEQDUMP, 80 race frames): 345 calls a frame -> 210. */
#define A4_GL_TBOX 0x00414000u
struct reo { int32_t pos[3u * MAXT * POSC]; int32_t uv[3u * MAXT * UVC]; uint32_t key[MAXT];
             int16_t box[MAXT * 4u]; uint16_t head[MAXT], tail[MAXT], cnt[MAXT], next[MAXT]; };
#define A4_GL_REO 0x00415000u
#define RO ((struct reo *)A4_GL_REO)
#ifndef A4_HPERSP
#error "A4_REORDER copies HPERSP vertices (int32 x4)"
#endif
_Static_assert(A4_GL_TBOX + 8u * MAXT <= A4_GL_REO, "triangle boxes past the reorder");
_Static_assert(A4_GL_REO + sizeof(struct reo) <= 0x00420800u, "reorder past the texture hints");
_Static_assert(VP_H % 24 == 0 && 24u * VP_W * 4u <= sizeof(((struct reo *)0)->pos) + sizeof(((struct reo *)0)->uv), "24-row strips fit the reorder's arrays");
_Static_assert((A4_GL_POS & 7u) == 0 && (A4_GL_UV & 7u) == 0 && (A4_GL_REO & 7u) == 0
               && ((3u * MAXT * POSC * 4u) & 7u) == 0, "the grouping copies doublewords");
#ifndef REO_SCAN
#define REO_SCAN 32u             /* runs a triangle may look back over (bench: 32 ~ unlimited) */
#endif
static inline void tbox_calc(int16_t *b, int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    x0 -= 1; y0 -= 1; x1 += 1; y1 += 1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > VP_W - 1) x1 = VP_W - 1;
    if (y1 > VP_H - 1) y1 = VP_H - 1;
    if (x0 > x1 || y0 > y1) { x0 = y0 = 0x7FFF; x1 = y1 = -0x8000; }   /* off screen: misses all */
    b[0] = (int16_t)x0; b[1] = (int16_t)y0; b[2] = (int16_t)x1; b[3] = (int16_t)y1;
}
static void tbox_set(uint32_t n, int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    tbox_calc((int16_t *)A4_GL_TBOX + n * 4u, x0, y0, x1, y1);
}
#endif

#ifdef A4_SEQDUMP
/* Bench diagnostic: every triangle a frame puts into the batch, in order, as 4 words --
 * TEXID, x0 | y0 << 16, x1 | y1 << 16 (its screen bounding box, clamped), and
 * inrange | (w << 1) | (h << 12) (inrange: every texcoord inside [0, 1], so the texture
 * could sit in an atlas; w, h its size) -- into a frame buffer at 0x423010; at the frame's
 * flush it is copied to the completed buffer at 0x42F010 under a sequence number for
 * EKA2L1_MEMSNAP (0x423000: [0] seq, 0 while the copy runs; [1] triangles; [2] the
 * running count). Needs A4_SCRATCH_SIZE=0x3B000 (the scratch then ends at 0x43C000). */
#define SEQ_HDR  ((volatile uint32_t *)0x00423000u)
#define SEQ_CUR  ((uint32_t *)0x00423010u)
#define SEQ_DONE ((uint32_t *)0x0042F010u)
#define SEQ_MAX  3072u
static void seq_add(uint32_t id, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t info)
{
    uint32_t n = SEQ_HDR[2];
    if (n >= SEQ_MAX) return;
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x1 > VP_W - 1) x1 = VP_W - 1; if (y1 > VP_H - 1) y1 = VP_H - 1;
    uint32_t *e = SEQ_CUR + n * 4u;
    e[0] = id; e[1] = (uint32_t)(x0 & 0xFFFF) | ((uint32_t)y0 << 16);
    e[2] = (uint32_t)(x1 & 0xFFFF) | ((uint32_t)y1 << 16); e[3] = info;
    SEQ_HDR[2] = n + 1u;
}
static void seq_frame_done(void)
{
    uint32_t n = SEQ_HDR[2], seq = SEQ_HDR[3] + 1u;
    SEQ_HDR[0] = 0;
    for (uint32_t k = 0; k < n * 4u; k++) SEQ_DONE[k] = SEQ_CUR[k];
    SEQ_HDR[1] = n; SEQ_HDR[3] = seq; SEQ_HDR[0] = seq; SEQ_HDR[2] = 0;
}
#endif

/* 16 texels spread over the texture, and its size: a texture loaded where another was
 * differs here (one changed in place since round 10's probe found none in a race). 64
 * until round 18: each is a likely cache miss, once a present for every texture used. */
static uint32_t tex_hash(const uint16_t *px, uint32_t n, uint32_t seed)
{
    uint32_t h = 0x811C9DC5u ^ seed ^ n, step = n >> 4;
    if (!step) step = 1;
    for (uint32_t k = 0; k < n; k += step) h = (h ^ px[k]) * 16777619u;
    return h;
}


/* Mode 1 of the default span (0x33964 with ctx[0x64] == 1: the 0x104000 class, ~15% of
 * calls). Per texel: T = tex[i] (0 = transparent); M = map2[i] (ctx[0x5c], the texture's
 * own layout); if M, the texel is T plus a quarter of table[M] (ctx[0x60], 16-bit colours),
 * summed with the span's own carry trick (0x33a3c). A function of texture space alone, so
 * it is baked into the GL texture. A black base whose lit parts come from the table is why
 * the speed-camera signs drew as black boxes (round 10); the table can change between
 * frames (a speed display), so each present the table entries the texture uses are hashed
 * and a change re-bakes it. */
#ifndef M1_BIG
#define M1_BIG 16384u            /* texels: a mode-1 texture this big re-bakes at most ... */
#endif
#ifndef M1_BIG_EVERY
#define M1_BIG_EVERY 8u          /* ... every this many presents (~3 a second in a race) */
#endif
static inline uint16_t m1_texel(uint32_t T, uint32_t M, const uint16_t *tab)
{
    if (!M) return (uint16_t)T;
    uint32_t q = tab[M];
    q = (((q & 0xF7DEu) >> 1) & 0xF7DEu) >> 1;
    uint32_t sum = T + q;
    uint32_t cy = (T ^ sum ^ q) & 0x10820u;
    return (uint16_t)((cy - (cy >> 4)) | sum);
}

/* the table the span would use: ctx[0x60], moved on 80 bytes a pixel of width when the
 * frame is not wider than tall (0x339ec; the 320x240 race frame is not moved) */
static const uint16_t *m1_table(const uint32_t *gctx)
{
    const uint32_t *base = (const uint32_t *)((uintptr_t)gctx + 0x40100u);
    uint32_t fw = *(const uint32_t *)base[0x10 / 4], fh = *(const uint32_t *)base[0x14 / 4];
    uintptr_t t = gctx[0x60 / 4];
    if (fw <= fh) t += fw * 80u;
    return (const uint16_t *)t;
}

/* Hash of what a mode-1 bake read from the table, plus the map's address: every table[M]
 * for M in [lo, hi] when that range is small, else 64 texels' worth. The table's own
 * address is left out -- contents decide, so a table that moves buffer unchanged does not
 * re-bake. */
static uint32_t m1_sig(const uint16_t *map2, const uint16_t *tab, uint32_t lohi, uint32_t n)
{
    uint32_t c = 0x811C9DC5u ^ (uint32_t)(uintptr_t)map2;
    uint32_t lo = lohi & 0xFFFFu, hi = lohi >> 16;
    if (lo > hi) return c;                          /* no M at all: nothing to track */
    if (hi - lo <= 1024u) {
        for (uint32_t m = lo; m <= hi; m++) c = (c ^ tab[m]) * 16777619u;
    } else {
        uint32_t step = n >> 6; if (!step) step = 1;
        for (uint32_t k = 0; k < n; k += step) c = (c ^ tab[map2[k]]) * 16777619u;
    }
    return c;
}

/* Upload texture ts into GL texture id (bound here). tr: 5551 with alpha 0 on the zero
 * texels (else 565); alloc: (re)specify the level first. map2/tab non-null: the mode-1
 * bake. Converted in row strips through the readback scratch (free here: composite_prev
 * ran at the frame's first triangle). Returns the [lo, hi] range of M seen (lo > hi when
 * none). No integer divide: the freestanding blob has no __aeabi_uidiv. */
static uint32_t tex_upload(uint32_t id, const uint32_t *ts, uint32_t tr, int alloc,
                           const uint16_t *map2, const uint16_t *tab)
{
    uint32_t w = ts[1] + 1, h = ts[2] + 1;
    const uint16_t *px16 = (const uint16_t *)ts[TS_PIX];
    uint32_t lo = 0xFFFFu, hi = 0;
    gl_bindtex(GL_TEXTURE_2D, id);
    uint32_t fmt = tr ? GL_RGBA : GL_RGB, typ = tr ? GL_UNSIGNED_SHORT_5_5_5_1 : GL_UNSIGNED_SHORT_5_6_5;
    if (!tr && !map2) {                             /* plain opaque: straight from the game */
        gl_teximage(GL_TEXTURE_2D, 0, (int)fmt, (int)w, (int)h, 0, fmt, typ, px16);
        return 1;                                   /* lo > hi */
    }
    if (alloc) gl_teximage(GL_TEXTURE_2D, 0, (int)fmt, (int)w, (int)h, 0, fmt, typ, 0);
    uint16_t *conv = (uint16_t *)A4_GL_READBUF;   /* 0x403000..0x408000 = 10240 u16 */
    uint32_t y0 = 0, nrow = 0;
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            uint32_t i = y * w + x, t16 = px16[i];
            if (map2 && t16) {
                uint32_t M = map2[i];
                if (M) { if (M < lo) lo = M; if (M > hi) hi = M; }
                t16 = m1_texel(t16, M, tab);
            }
            conv[nrow * w + x] = tr ? (uint16_t)((t16 & 0xF800) | (((t16 >> 6) & 0x1F) << 6)
                                                 | ((t16 & 0x1F) << 1) | (px16[i] ? 1u : 0u))
                                    : (uint16_t)t16;
        }
        nrow++;
        if ((nrow + 1) * w > 10240u || y == h - 1) {  /* strip full, or last row */
            gl_texsubimage(GL_TEXTURE_2D, 0, 0, (int)y0, (int)w, (int)nrow, fmt, typ, conv);
            y0 = y + 1; nrow = 0;
        }
    }
    return lo | (hi << 16);
}

/* The 0x33ea8 texture: RGBA8888, colour from the 565 texel, alpha (a>>4)*16 from the byte
 * map at desc+0x2c -- the span's a/16 (to 1/255). Row strips through the readback scratch
 * as tex_upload; returns 1 (no mode-1 range). */
static uint32_t tex_upload_alpha(uint32_t id, const uint32_t *ts)
{
    uint32_t w = ts[1] + 1, h = ts[2] + 1;
#ifdef A4_ALPHAHIST
    {   /* diagnosis: uploads [16], and the first descriptor's 16 words at [17..32] */
        volatile uint32_t *ah = (volatile uint32_t *)0x00412D00u;
        if (ah[16]++ == 0) for (int k = 0; k < 16; k++) ah[17 + k] = ts[k];
    }
#endif
    const uint16_t *px16 = (const uint16_t *)ts[TS_PIX];
    const uint8_t *am = (const uint8_t *)ts[11];
    gl_bindtex(GL_TEXTURE_2D, id);
    gl_teximage(GL_TEXTURE_2D, 0, GL_RGBA, (int)w, (int)h, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
    uint32_t *conv = (uint32_t *)A4_GL_READBUF;   /* 5120 RGBA texels */
    uint32_t y0 = 0, nrow = 0;
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            uint32_t i = y * w + x, t = px16[i];
            uint32_t r = ((t >> 11) & 31u) << 3, g = ((t >> 5) & 63u) << 2, b = (t & 31u) << 3;
            uint32_t a = am ? (uint32_t)(am[i] >> 4) << 4 : 0xFFu;
#ifdef A4_ALPHAHIST
            /* diagnosis: the alpha nibbles of 0x33ea8 textures, at 0x412D00 [0..15] */
            if (am) ((volatile uint32_t *)0x00412D00u)[am[i] >> 4]++;
#endif
            /* premultiplied (KIND_ALPHA blends GL_DST_ALPHA, GL_ONE_MINUS_SRC_ALPHA):
             * c*a/255 as (x + 1 + (x >> 8)) >> 8, exact for x = c*a < 65535, no divide */
            { uint32_t x;
              x = r * a; r = (x + 1u + (x >> 8)) >> 8;
              x = g * a; g = (x + 1u + (x >> 8)) >> 8;
              x = b * a; b = (x + 1u + (x >> 8)) >> 8; }
            conv[nrow * w + x] = r | (g << 8) | (b << 16) | (a << 24);
        }
        nrow++;
        if ((nrow + 1) * w > 5120u || y == h - 1) {
            gl_texsubimage(GL_TEXTURE_2D, 0, 0, (int)y0, (int)w, (int)nrow, GL_RGBA, GL_UNSIGNED_BYTE, conv);
            y0 = y + 1; nrow = 0;
        }
    }
    return 1;
}

/* A slot for a new entry: the next unused one, else the entry used longest ago -- never
 * one used in this present (its GL name is in the batch, not yet drawn). *id is its GL
 * name (made here for an unused slot). -1 when all are in use this present. */
static int cache_slot(volatile struct glr *s, uint32_t *id)
{
    if (s->ntex < NCACHE) {
        *id = 0;
        gl_gentex(1, id);
        return (int)s->ntex++;
    }
    int v = -1; uint32_t age = 0;
    for (uint32_t k = 0; k < NCACHE; k++) {
        uint32_t a = s->frames - TX->lu[k];
        if (a > age) { age = a; v = (int)k; }
    }
    if (v < 0) return -1;                           /* every entry drawn this present */
    *id = s->cache_id[v];
    A4_PERF[PF_EVICT]++;
    return v;
}

/* (Re)make slot i's GL texture from ts: the alpha-map texture (var 2), the mode-1 bake
 * (var 1) or the plain/colour-key one, and record what it was made from. */
static void tex_make(volatile struct glr *s, int i, uint32_t id, const uint32_t *ts, const uint16_t *m1map, const uint16_t *m1tab, int var)
{
    uint32_t w = ts[1] + 1, h = ts[2] + 1, total = w * h;
    const uint16_t *px16 = (const uint16_t *)ts[TS_PIX];
    /* The game's software filler treats texel value 0 (pure black) as transparent.
     * A texture with ANY such texel, GPU-drawn opaque, shows black where software
     * shows through (the "black background trees"). Handle it ON the GPU: a colorkey
     * texture is uploaded as 5551 with alpha=0 on the zero texels, and the alpha
     * test (enabled in bring-up) discards them -- matching software exactly, and
     * keeping the sprite on the GPU (no slow software fallback). Opaque textures
     * (no zero texels) stay 565; GL_REPLACE on an RGB texture yields alpha 1, so the
     * alpha test passes and they are unaffected. */
    uint32_t tr = 0;
    for (uint32_t k = 0; k < total; k++) if (px16[k] == 0) { tr = 1; break; }
    const uint16_t *map2 = 0, *tab = 0;
    if (var == 1) { map2 = m1map; tab = m1tab; }   /* ctx[0x5c] and m1_table(ctx), taken by the caller */
    uint32_t r = var == 2 ? tex_upload_alpha(id, ts) : tex_upload(id, ts, tr, 1, map2, tab);
    gl_texparamx(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl_texparamx(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl_texparamx(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    gl_texparamx(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    s->cache_id[i] = id; s->cache_tr[i] = tr;
#ifdef A4_ATLAS
    ATL->st[i] = (ATL->st[i] == ATLST_OK || ATL->st[i] == ATLST_STALE) ? ATLST_STALE : ATLST_NONE;   /* the copy is of the old texels */
    /* (a pending copy becomes NONE: its queue entry finds that and lets the place go) */
#endif
    s->cache_m1r[i] = r; s->cache_fr[i] = s->frames;
    s->cache_ck[i] = var == 1 ? m1_sig(map2, tab, r, total) : 0;
    uint32_t am = var == 2 ? ts[11] : 0u;           /* the alpha map is part of what it was made from */
    TX->src[i] = ts[TS_PIX]; TX->wh[i] = ts[1] | (ts[2] << 16);
    TX->hs[i] = tex_hash(px16, total, am); TX->lu[i] = s->frames;
}

/* texture cache: the cache INDEX for desc+0x30 (and, for a mode-1 triangle, its map and
 * table), uploading on a miss. A mode-1 entry is keyed desc|1 and the alpha-map one desc|2
 * (descriptors are word aligned), so a texture drawn several ways has a GL texture each.
 * The texel pointer is part of the key; a hit is checked against what the entry was made
 * from -- the size on every hit, a sampled hash of the texels once a present -- and
 * remade if the descriptor now holds another texture. -1 on failure (the caller falls
 * through to software). */
/* m1map, m1tab: ctx[0x5c] and m1_table(ctx) for a mode-1 triangle (var 1), read by the
 * caller when the triangle came in (round 22: the deferred path calls this later, when the
 * game's context may describe another object) */
static int tex_index(volatile struct glr *s, uint32_t tsaddr, const uint16_t *m1map, const uint16_t *m1tab, int var)
{
    uint32_t *ts = (uint32_t *)tsaddr;
    int m1 = var == 1;                              /* 1: mode-1 bake; 2: the alpha-map texture */
    uint32_t key = tsaddr | (uint32_t)var;
    /* a mode-1 entry is also keyed by its table: objects sharing a texture can each bring
     * their own (ctx[0x60]) -- the tunnel-exit gantry's signs drew black with one image a
     * texture (round 11) */
    uint32_t tabk = m1 ? (uint32_t)(uintptr_t)m1tab : 0u;
    uint32_t w = ts[1] + 1, h = ts[2] + 1, n = w * h;
    const void *pix = (const void *)ts[TS_PIX];
    if (!pix || w > 1024 || h > 1024) return -1;
    int i = -1;
    /* consecutive triangles usually share a texture: try the previous slot first
     * (the linear search below runs per triangle over up to NCACHE entries) */
    /* The texel pointer is part of the key too: some textures animate by the game swapping
     * it among a few frames (a 64x64 alpha-map one and a 32x32 one in the bench race,
     * round 14 -- frozen on their first frame until then), and each frame keeps its entry. */
    uint32_t src = ts[TS_PIX];
    uint32_t hh = thint_of(key, tabk, src);
    { uint32_t li = s->lasthit; if (li < s->ntex && s->cache_ptr[li] == key && s->cache_tab[li] == tabk && TX->src[li] == src) i = (int)li; }
    if (i < 0) {
        uint32_t li = THINT[hh];
        if (li && --li < s->ntex && s->cache_ptr[li] == key && s->cache_tab[li] == tabk && TX->src[li] == src) { s->lasthit = li; i = (int)li; }
    }
    for (uint32_t k = 0; i < 0 && k < s->ntex; k++)
        if (s->cache_ptr[k] == key && s->cache_tab[k] == tabk && TX->src[k] == src) { s->lasthit = k; i = (int)k; THINT[hh] = (uint16_t)(k + 1u); }
    if (i >= 0) {
        if (TX->wh[i] != (ts[1] | (ts[2] << 16))
            || (TX->lu[i] != s->frames && TX->hs[i] != tex_hash((const uint16_t *)pix, n, var == 2 ? ts[11] : 0u))) {
            A4_PERF[PF_TEXRE]++;                    /* another texture here now */
#ifdef A4_REMADEPROBE
            {   /* diagnosis, 0x412D00 (A4_FLAGHIST's tail): [0] count, then a ring of 20
                 * (key, why: 2 size | 4 hash, or 8 either while used this present, w | h << 16,
                 * old pointer, new pointer, present) */
                volatile uint32_t *rp = (volatile uint32_t *)0x00412D00u;
                uint32_t k = rp[0] % 20u, why = 0u
                    | (TX->wh[i] != (ts[1] | (ts[2] << 16)) ? 2u : 0u) | (TX->lu[i] == s->frames ? 8u : 4u);
                rp[1+k*6] = key; rp[2+k*6] = why; rp[3+k*6] = w | (h << 16);
                rp[4+k*6] = TX->src[i]; rp[5+k*6] = ts[TS_PIX]; rp[6+k*6] = s->frames; rp[0]++;
            }
#endif
            if (TX->lu[i] == s->frames) {
                /* already drawn with in this present: its GL name is in the batch, so the
                 * new texture takes another slot and this key is retired */
                uint32_t id;
                int j = cache_slot(s, &id);
                if (j < 0) return -1;
                s->cache_ptr[i] = 0xFFFFFFFFu;      /* no descriptor or sprite has this key */
                s->cache_ptr[j] = key; s->cache_tab[j] = tabk;
                tex_make(s, j, id, ts, m1map, m1tab, var);
                s->lasthit = (uint32_t)j; THINT[hh] = (uint16_t)(j + 1);
                return j;
            }
            tex_make(s, i, s->cache_id[i], ts, m1map, m1tab, var);
            return i;
        }
        TX->lu[i] = s->frames;
        /* once a present: has the table moved? A big texture at most every M1_BIG_EVERY
         * presents: the one that re-bakes all the time is the player's car body (256x256,
         * its paint reflection, a table the game rewrites every frame), and a re-bake is
         * the whole texture through the CPU and glTexSubImage2D. */
        if (m1 && s->cache_fr[i] != s->frames && (n < M1_BIG || s->frames - s->cache_fr[i] >= M1_BIG_EVERY)) {
            s->cache_fr[i] = s->frames;
            const uint16_t *map2 = m1map, *tab = m1tab;
            if (m1_sig(map2, tab, s->cache_m1r[i], n) != s->cache_ck[i]) {
                uint32_t t0 = a4_ntick();
                s->cache_m1r[i] = tex_upload(s->cache_id[i], ts, s->cache_tr[i], 0, map2, tab);
                s->cache_ck[i] = m1_sig(map2, tab, s->cache_m1r[i], n);
                A4_PERF[PF_M1BAKE]++; A4_PERF[PF_M1TEX] += n; A4_PERF[PF_M1T] += a4_ntick() - t0;
#ifdef A4_M1PROBE
                {   /* diagnosis: a ring of the last 8 re-bakes at 0x412E00: [0] count, then
                     * (desc, w | h<<16, lo | hi<<16, table entries that differ from the
                     * previous re-bake's copy at 0x412F00, up to 64 entries) */
                    volatile uint32_t *mp = (volatile uint32_t *)0x00412E00u;
                    volatile uint16_t *cp = (volatile uint16_t *)0x00412F00u;
                    uint32_t k = mp[0] & 7u, lo = s->cache_m1r[i] & 0xFFFFu, hi = s->cache_m1r[i] >> 16, dif = 0;
                    for (uint32_t m = lo; m <= hi && m - lo < 64u; m++) { if (cp[m - lo] != tab[m]) dif++; cp[m - lo] = tab[m]; }
                    mp[1+k*4] = tsaddr; mp[2+k*4] = (ts[1] + 1) | ((ts[2] + 1) << 16);
                    mp[3+k*4] = s->cache_m1r[i]; mp[4+k*4] = dif; mp[0]++;
                }
#endif
            }
        }
        return i;
    }
    uint32_t id;
    int idx = cache_slot(s, &id);
    if (idx < 0) return -1;
    s->cache_ptr[idx] = key; s->cache_tab[idx] = tabk;
    tex_make(s, idx, id, ts, m1map, m1tab, var);
    s->lasthit = (uint32_t)idx; THINT[hh] = (uint16_t)(idx + 1);
    return idx;
}

/* returns 1 if the primitive was taken by the GPU path (the caller skips the
 * software rasteriser), 0 to fall through. The triangle is accumulated in arrival
 * order and drawn later, in a4_gl_flush; an overflow past MAXT is drawn now. */
static void composite_prev(volatile struct glr *s) __attribute__((unused));
static void batch_draw(uint32_t nt);
#ifdef A4_HPERSP
static inline int bitlen(uint32_t v) { return v ? 32 - __builtin_clz(v) : 0; }   /* CLZ (ARMv5) */
#endif

#ifdef A4_PERSP
/* Unsigned 32/32 divide by restoring shift-subtract -- the renderer is
 * freestanding (no libgcc), so neither hardware divide nor a compiler __aeabi
 * helper is available; this uses only shifts, compares and subtracts. Used for
 * the per-vertex reciprocal 1/w (2^22 / ZW). ~32 iterations; a few per triangle,
 * well under 2% of the frame. */
static uint32_t a4_udiv(uint32_t num, uint32_t den) __attribute__((unused));
static uint32_t a4_udiv(uint32_t num, uint32_t den)
{
    if (den == 0) return 0xFFFFFFFFu;
    uint32_t q = 0, r = 0;
    for (int i = 31; i >= 0; --i) {
        r = (r << 1) | ((num >> i) & 1u);
        if (r >= den) { r -= den; q |= (1u << i); }
    }
    return q;
}
#endif

#ifdef A4_ZBUF
/* The perspective rasterizer's own affine fallback (0x2f6fc, read from the image): with
 * its vertices ordered by y as 0x2ff60 orders them, if the top vertex's ZW is over 500 and
 * the reciprocal-table values (RT[ZW], ctx+0x8C, 32768 words, 0x522c8) of the other two
 * differ from the top's by dq1, dq2 with dq1*dq1 + dq2*dq2 < 100 (32-bit, as the game does
 * it), the triangle goes to 0x2e1c8 instead -- affine, colour key, the ctx[0x64] modes,
 * and NO depth test or write. So the game's distant flat-depth triangles are painter's
 * order, and only the others depth-test. */
static int32_t persp_rt(const uint32_t *gctx, int32_t z)
{
    const int32_t *rt = (const int32_t *)((uintptr_t)gctx + 0x8Cu);
    return rt[z < 0 ? 0 : z >= 32768 ? 32767 : z];
}
static int persp_fallback(const uint32_t *gctx, const uint32_t *desc)
{
    const int32_t *v0 = (const int32_t *)desc[0x0C / 4], *v1 = (const int32_t *)desc[0x18 / 4],
                  *v2 = (const int32_t *)desc[0x24 / 4];
    int32_t y0 = v0[1], y1 = v1[1], y2 = v2[1];
    const int32_t *top;                             /* 0x2ff60's first output, branch for branch */
    if (y0 <= y1) top = (y1 <= y2 || y0 <= y2) ? v0 : v2;
    else          top = (y2 >= y0 || y2 > y1) ? v1 : v2;
    if (top[2] <= 500) return 0;
    const int32_t *a = top == v0 ? v1 : v0, *b = top == v2 ? v1 : v2;
    int32_t q = persp_rt(gctx, top[2]);
    uint32_t d1 = (uint32_t)(persp_rt(gctx, a[2]) - q), d2 = (uint32_t)(persp_rt(gctx, b[2]) - q);
    return (int32_t)(d1 * d1 + d2 * d2) < 100;
}
#endif

#if defined(A4_LEAN) && defined(A4_HPERSP) && defined(A4_REORDER) && defined(A4_IMM)
/* Round 21: the hook's work after the class is known, in one pass and written straight
 * into the batch slot -- the same words as the path in draw_prim_take (A4_LEANCHECK
 * compares them triangle by triangle). The phone's hook costs ~7 us a triangle outside
 * its GL calls (round 18-20 windows, least squares), ~600 instructions on the bench. What
 * changes: the three vertices read once (positions, texcoords, depths, extents together),
 * no local copies of the outputs, the triangle's box from the same pass, the frame's row
 * extent in locals, and tex_index's commonest outcome inline -- the texture of the last
 * hit, already used in this present, nothing to re-check (tex_index's own conditions for
 * returning that slot at once). Writes pos[12], uv[6], *idp, box[4], yr[2]; 0 if the
 * texture fails (as the path it replaces returns 0 then). */
/* the triangle's texture: its TEXID word, and the texcoord shifts; tex_index's commonest
 * outcome inline -- the last hit, already used in this present, nothing to re-check (the
 * conditions on which tex_index returns that slot untouched). -1: the texture fails. */
#ifdef A4_ATLAS
/* the padded copy of slot i's texture (w x h, tr) into atlas a at inner origin (x, y): rows
 * and columns -1 and w (h) are the opposite edge's, built in the reorder's arrays (free in
 * the hook; 130 x 130 x 2 = 33.8 KB of 36) */
static void atl_upload(int a, uint32_t x, uint32_t y, const uint32_t *ts, uint32_t w, uint32_t h, uint32_t tr)
{
    const uint16_t *px16 = (const uint16_t *)ts[TS_PIX];
    uint16_t *buf = (uint16_t *)RO->pos;
    uint32_t pw = w + 2u;
    for (uint32_t py = 0; py < h + 2u; py++) {
        uint32_t sy = py == 0 ? h - 1u : py == h + 1u ? 0u : py - 1u;
        const uint16_t *row = px16 + sy * w;
        uint16_t *dst = buf + py * pw;
        for (uint32_t pxx = 0; pxx < pw; pxx++) {
            uint32_t sx = pxx == 0 ? w - 1u : pxx == w + 1u ? 0u : pxx - 1u;
            uint32_t t16 = row[sx];
            dst[pxx] = tr ? (uint16_t)((t16 & 0xF800) | (((t16 >> 6) & 0x1F) << 6) | ((t16 & 0x1F) << 1) | (t16 ? 1u : 0u))
                          : (uint16_t)t16;
        }
    }
    gl_bindtex(GL_TEXTURE_2D, ATL->name[a]);
    gl_texsubimage(GL_TEXTURE_2D, 0, (int)x - 1, (int)y - 1, (int)pw, (int)(h + 2u),
                   tr ? GL_RGBA : GL_RGB, tr ? GL_UNSIGNED_SHORT_5_5_5_1 : GL_UNSIGNED_SHORT_5_6_5, buf);
}
/* slot i's atlas copy, placed and uploaded if need be: its inner origin x | y << 16, or 0
 * when it has none (the atlas could not be made, or is full for this size) */
static uint32_t atl_get(volatile struct glr *s, int i, const uint32_t *ts, uint32_t w, uint32_t h)
{
    volatile struct atl *A = ATL;
    if (A->ok != 1u) {
        if (A->ok) return 0;                        /* making it failed once: never again */
        uint32_t nm[2] = { 0, 0 }, e = 0, hh = 1024u;
        gl_gentex(2, nm);
        (void)gl_geterror();                        /* a draw's error is flush's to report; not ours */
        for (int tries = 0; tries < 2; tries++, hh = 512u) {
            for (int a = 0; a < 2; a++) {
                gl_bindtex(GL_TEXTURE_2D, nm[a]);
                gl_teximage(GL_TEXTURE_2D, 0, a ? GL_RGBA : GL_RGB, (int)ATL_W, (int)hh, 0,
                            a ? GL_RGBA : GL_RGB, a ? GL_UNSIGNED_SHORT_5_5_5_1 : GL_UNSIGNED_SHORT_5_6_5, 0);
                gl_texparamx(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                gl_texparamx(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                A->name[a] = nm[a]; A->nsh[a] = 0; A->top[a] = 0;
            }
            e = gl_geterror();
            if (!e) break;                          /* 2 MB each refused (out of memory): 1 MB each */
        }
        /* how the making went: 1 = 1024x1024 each, 3 = 1024x512 each, else the GL error */
        ((volatile uint32_t *)0x00412D00u)[118] = e ? e : hh == 1024u ? 1u : 3u;
        if (e) { A->ok = 2u; return 0; }
        A->h = hh; A->vs = hh == 1024u ? 10u : 9u;
        A->ok = 1u;
    }
    uint32_t a = s->cache_tr[i] ? 1u : 0u, wh = w | (h << 8);
    if (A->st[i] != ATLST_NONE && (A->ai[i] != a || A->wh[i] != wh)) A->st[i] = ATLST_NONE;   /* another size: leave the old place */
    if (A->st[i] == ATLST_OK) return A->xy[i];
    if (A->st[i] == ATLST_FULL || A->st[i] == ATLST_PENDING) return 0;
    /* Round 25: no upload here. Uploaded mid-frame, into a texture that this frame's
     * submitted draws use, each copy cost the N95 a stall (round 24: the race's start, 36
     * renders over 130 ms); queued instead, it goes up after the frame's composite
     * (atl_flush_pending), and the triangle draws from its own texture until then. */
    if (A->npend >= ATL_PQ) return 0;               /* the queue is full: ask again later */
    if (A->st[i] == ATLST_STALE) {
        A->pslot[A->npend] = (uint16_t)i; A->pts[A->npend] = (uint32_t)(uintptr_t)ts; A->npend++;
        A->st[i] = ATLST_PENDING;
        return 0;
    }
    uint32_t pw = w + 2u, ph = h + 2u, k, n = A->nsh[a];
    for (k = 0; k < n; k++) if (A->shh[a][k] == ph && A->shx[a][k] + pw <= ATL_W) break;
    if (k == n) {
        if (n == ATL_SH || A->top[a] + ph > A->h) { A->st[i] = ATLST_FULL; ((volatile uint32_t *)0x00412D00u)[117]++; return 0; }
        A->shy[a][k] = (uint16_t)A->top[a]; A->shh[a][k] = (uint16_t)ph; A->shx[a][k] = 0;
        A->top[a] += ph; A->nsh[a] = n + 1u;
    }
    uint32_t x = A->shx[a][k] + 1u, y = A->shy[a][k] + 1u;
    A->shx[a][k] = (uint16_t)(A->shx[a][k] + pw);
    A->xy[i] = x | (y << 16); A->wh[i] = (uint16_t)wh; A->ai[i] = (uint8_t)a; A->st[i] = ATLST_PENDING;
    A->pslot[A->npend] = (uint16_t)i; A->pts[A->npend] = (uint32_t)(uintptr_t)ts; A->npend++;
    ((volatile uint32_t *)0x00412D00u)[116]++;
    return 0;
}
/* After a frame's composite (its glReadPixels has finished the frame's GPU work, so no draw
 * holds the atlas): up to ATL_PER_FRAME queued copies uploaded, oldest first. An entry whose
 * slot was remade or reused since (no longer PENDING, or another texture's texels) is
 * dropped; its place is left. */
static void atl_flush_pending(volatile struct glr *s)
{
    volatile struct atl *A = ATL;
    uint32_t n = A->npend, done = 0, k = 0;
    if (!n || A->ok != 1u) return;
    while (k < n && done < ATL_PER_FRAME) {
        uint32_t i = A->pslot[k];
        const uint32_t *ts = (const uint32_t *)A->pts[k];
        k++;
        if (A->st[i] != ATLST_PENDING) continue;
        uint32_t w = A->wh[i] & 0xFFu, h = A->wh[i] >> 8;
        if (TX->src[i] != ts[TS_PIX] || ts[1] + 1u != w || ts[2] + 1u != h
            || (s->cache_tr[i] ? 1u : 0u) != A->ai[i]) { A->st[i] = ATLST_NONE; continue; }
        atl_upload((int)A->ai[i], A->xy[i] & 0xFFFFu, A->xy[i] >> 16, ts, w, h, A->ai[i]);
        A->st[i] = ATLST_OK;
        done++;
    }
    for (uint32_t j = k; j < n; j++) { A->pslot[j - k] = A->pslot[j]; A->pts[j - k] = A->pts[j]; }
    A->npend = n - k;
    ((volatile uint32_t *)0x00412D00u)[119] += done;
}
#endif
static inline int tri_tex(volatile struct glr *s, uint32_t kind, int m1, uint32_t tsaddr,
                          const uint16_t *m1map, const uint16_t *m1tab, uint32_t *idp, int *wshp, int *hshp)
{   /* returns the cache slot (>= 0), -2 for an untextured kind, -1 when the texture fails */
    if (kind == KIND_SHAD) { *idp = KIND_SHAD << 28; *wshp = *hshp = 0; return -2; }
    if (kind == KIND_FLAT) { *idp = (KIND_FLAT << 28) | (tsaddr & 0xFFFFu); *wshp = *hshp = 0; return -2; }
    const uint32_t *ts = (const uint32_t *)tsaddr;
    uint32_t tw = ts[1] + 1, th = ts[2] + 1;
    *wshp = tw ? 31 - __builtin_clz(tw) : 0;
    *hshp = th ? 31 - __builtin_clz(th) : 0;
    int var = kind == KIND_ALPHA ? 2 : m1, ti = -1;
    uint32_t li = s->lasthit;
    if (li < s->ntex) {
        uint32_t key = tsaddr | (uint32_t)var;
        uint32_t tabk = var == 1 ? (uint32_t)(uintptr_t)m1tab : 0u;
        if (s->cache_ptr[li] == key && s->cache_tab[li] == tabk && TX->src[li] == ts[TS_PIX]
            && TX->wh[li] == (ts[1] | (ts[2] << 16)) && TX->lu[li] == s->frames
            && (var != 1 || s->cache_fr[li] == s->frames)) ti = (int)li;
    }
    if (ti < 0) ti = tex_index(s, tsaddr, var == 1 ? m1map : 0, var == 1 ? m1tab : 0, var);
    if (ti < 0) return -1;
    *idp = (kind << 28) | s->cache_id[ti];
    return ti;
}
/* positions (x*W, y*W, -min(zq, W), W) and texcoords of one triangle from its screen
 * points (int16, as the game's), depths and texcoord words; xlo..yhi its extents */
static inline void tri_conv(uint32_t kind, int wsh, int hsh, int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                            int32_t x2, int32_t y2, int32_t za, int32_t zb, int32_t zc, const uint32_t *uvw,
                            int32_t xlo, int32_t xhi, int32_t ylo, int32_t yhi, int32_t *pos, int32_t *uv)
{
    if (kind == KIND_TEX || kind == KIND_TEXZ || kind == KIND_ADD || kind == KIND_ALPHA) {
        uv[0] = ((int32_t)uvw[0] * 4096) >> wsh; uv[1] = ((int32_t)uvw[1] * 4096) >> hsh;
        uv[2] = ((int32_t)uvw[2] * 4096) >> wsh; uv[3] = ((int32_t)uvw[3] * 4096) >> hsh;
        uv[4] = ((int32_t)uvw[4] * 4096) >> wsh; uv[5] = ((int32_t)uvw[5] * 4096) >> hsh;
    } else { uv[0] = uv[1] = uv[2] = uv[3] = uv[4] = uv[5] = 0; }
    /* the extents bound |x|, |y|: max(-lo, hi) over both axes, at least 1 */
    int32_t m = 1;
    if (-xlo > m) m = -xlo;
    if (xhi > m) m = xhi;
    if (-ylo > m) m = -ylo;
    if (yhi > m) m = yhi;
    uint32_t w0 = (uint32_t)(za > 0 ? za : 1), w1 = (uint32_t)(zb > 0 ? zb : 1), w2 = (uint32_t)(zc > 0 ? zc : 1);
    uint32_t zmax = w0 > w1 ? w0 : w1;
    if (w2 > zmax) zmax = w2;
    int shift = (26 - bitlen((uint32_t)m)) - bitlen(zmax);
    int32_t zq = 0;
    if (kind == KIND_TEXZ) zq = shift >= 0 ? (ZK << shift) : (ZK >> -shift);
#ifdef A4_MESH
    else if (MESH_ON()) {
        /* a mesh frame: every class tests its true depth against the list's (draw_run); a
         * shadow 1/64 nearer, so the road it lies on does not hide it where both write */
        int32_t zk = kind == KIND_SHAD ? ZK + (ZK >> 6) : ZK;
        zq = shift >= 0 ? (zk << shift) : (zk >> -shift);
    }
#endif
    int32_t W0 = (int32_t)(shift >= 0 ? (w0 << shift) : (w0 >> -shift));
    int32_t W1 = (int32_t)(shift >= 0 ? (w1 << shift) : (w1 >> -shift));
    int32_t W2 = (int32_t)(shift >= 0 ? (w2 << shift) : (w2 >> -shift));
    if (W0 < 1) W0 = 1;
    if (W1 < 1) W1 = 1;
    if (W2 < 1) W2 = 1;
    pos[0] = x0 * W0; pos[1]  = y0 * W0; pos[2]  = -(zq < W0 ? zq : W0); pos[3]  = W0;
    pos[4] = x1 * W1; pos[5]  = y1 * W1; pos[6]  = -(zq < W1 ? zq : W1); pos[7]  = W1;
    pos[8] = x2 * W2; pos[9]  = y2 * W2; pos[10] = -(zq < W2 ? zq : W2); pos[11] = W2;
}
static inline void ext3(int32_t a, int32_t b, int32_t c, int32_t *lo, int32_t *hi)
{
    int32_t l = a, h = a;
    if (b < l) l = b;
    if (b > h) h = b;
    if (c < l) l = c;
    if (c > h) h = c;
    *lo = l; *hi = h;
}
#if defined(A4_MESH) && !defined(MESH_TRIALL)
/* Round 28: in a list frame the table's classes other than the perspective one -- glows,
 * cut-outs, flat slabs, shadows, the default span -- were all depth-tested, against the
 * table's road as well; the game tests none of them, painting each over whatever it draws
 * before. Such a triangle needs the test only where a list face the table would draw after
 * it -- a strictly nearer bucket -- covers it: by the tile grid (mesh_draw), conservative
 * at 16 pixels. Its bucket as +349B0 takes it: TFace's key (+3121E, the deepest ZW), >> 3,
 * -16 for flag 0x10000, +16 for 0x20000; a TFakeZFace's key is 1 (+522C2), drawn after
 * everything; a clipped copy (+35D3A's pool, no record of its own) by the deepest ZW. */
static int mesh_tri_covered(const uint32_t *desc, int32_t za, int32_t zb, int32_t zc,
                            int32_t xlo, int32_t ylo, int32_t xhi, int32_t yhi)
{
    extern const int32_t a4_tfvt, a4_tfzvt;
    uint32_t vt = desc[0];
    if (vt == (uint32_t)(uintptr_t)&a4_tfzvt + (uint32_t)a4_tfzvt) return 0;
    int32_t key = za > zb ? za : zb;
    if (zc > key) key = zc;
    int32_t b = key >> 3;
    if (vt == (uint32_t)(uintptr_t)&a4_tfvt + (uint32_t)a4_tfvt) {
        if (desc[1] & 0x10000u) b -= 16;
        if (desc[1] & 0x20000u) b += 16;
    }
    if (b <= 0) return 0;
    if (xhi < 0 || yhi < 0 || xlo >= VP_W || ylo >= VP_H) return 0;
    int32_t tx0 = xlo < 0 ? 0 : xlo >> 4, tx1 = xhi >= VP_W ? MESH_GW - 1 : xhi >> 4;
    int32_t ty0 = ylo < 0 ? 0 : ylo >> 4, ty1 = yhi >= VP_H ? MESH_GH - 1 : yhi >> 4;
    const uint16_t *g = (const uint16_t *)MESH->grid;
    for (int32_t ty = ty0; ty <= ty1; ty++)
        for (int32_t tx = tx0; tx <= tx1; tx++)
            if ((int32_t)g[ty * MESH_GW + tx] < b) return 1;
    return 0;
}
#endif
#if defined(A4_SHADSINK) && defined(A4_MESH) && defined(A4_MESHCHECK)
/* Bench (round 33, owner: "the car's shadow would start clipping through the road"; check
 * build: software draws the frame, its 1/z buffer holds every perspective face drawn so far --
 * the road, the player's car): at each shadow triangle of a list frame, its pixels where the
 * game's buffer is nearer than the shadow -- pixels a depth-tested GPU shadow loses that the
 * game, which never tests a shadow, paints. Split by whether lean_conv tests it (covered by a
 * nearer list face) or draws it untested at window depth 1. 0x454100: [0] shadow triangles,
 * [1] covered ones, [2] their pixels, [3..8] of those hidden at the shadow's depth brought
 * nearer by 0, 1/64 (the build's bias), 1/32, 1/16, 1/8, 1/4; [9] the uncovered ones' pixels,
 * [10..15] hidden likewise (never tested: for scale). Per-pixel depth by barycentrics without
 * a divide (no libgcc): hidden when zbuf * area * 64 > sum(w_i z_i) * (64 + 64 b). */
static void shad_sink(const uint32_t *gctx, const uint32_t *desc)
{
    if (!MESH_ON()) return;
    volatile uint32_t *k = (volatile uint32_t *)0x00454100u;
    const int32_t *zbuf = (const int32_t *)gctx[0x40118 / 4];
    const int32_t *rt = (const int32_t *)((uintptr_t)gctx + 0x8Cu);
    if (!zbuf) return;
    const uint32_t *v0 = (const uint32_t *)desc[3], *v1 = (const uint32_t *)desc[6], *v2 = (const uint32_t *)desc[9];
    int32_t x0 = (int16_t)(int32_t)v0[0], y0 = (int16_t)(int32_t)v0[1], x1 = (int16_t)(int32_t)v1[0], y1 = (int16_t)(int32_t)v1[1];
    int32_t x2 = (int16_t)(int32_t)v2[0], y2 = (int16_t)(int32_t)v2[1];
    int32_t wa = (int32_t)v0[2], wb = (int32_t)v1[2], wc = (int32_t)v2[2];
    if (wa < 1 || wb < 1 || wc < 1 || wa > 32767 || wb > 32767 || wc > 32767) return;
    int64_t z0 = rt[wa], z1 = rt[wb], z2 = rt[wc];
    int32_t xlo, xhi, ylo, yhi;
    ext3(x0, x1, x2, &xlo, &xhi); ext3(y0, y1, y2, &ylo, &yhi);
    int cov = mesh_tri_covered(desc, wa, wb, wc, xlo, ylo, xhi, yhi);
    k[0]++; if (cov) k[1]++;
    int64_t area = (int64_t)(x1 - x0) * (y2 - y0) - (int64_t)(y1 - y0) * (x2 - x0);
    if (area == 0) return;
    int64_t sg = area > 0 ? 1 : -1, aa = area * sg;
    static const int32_t fac[6] = { 64, 65, 66, 68, 72, 80 };
    if (xlo < 0) xlo = 0;
    if (ylo < 0) ylo = 0;
    if (xhi > VP_W - 1) xhi = VP_W - 1;
    if (yhi > VP_H - 1) yhi = VP_H - 1;
    for (int32_t y = ylo; y <= yhi; y++)
        for (int32_t x = xlo; x <= xhi; x++) {
            int64_t e0 = ((int64_t)(x2 - x1) * (y - y1) - (int64_t)(y2 - y1) * (x - x1)) * sg;
            int64_t e1 = ((int64_t)(x0 - x2) * (y - y2) - (int64_t)(y0 - y2) * (x - x2)) * sg;
            int64_t e2 = ((int64_t)(x1 - x0) * (y - y0) - (int64_t)(y1 - y0) * (x - x0)) * sg;
            if (e0 < 0 || e1 < 0 || e2 < 0) continue;
            int64_t S = e0 * z0 + e1 * z1 + e2 * z2;          /* the shadow's depth * area */
            int64_t Z = (int64_t)zbuf[y * VP_W + x] * aa * 64;
            k[cov ? 2 : 9]++;
            for (int q = 0; q < 6; q++) if (Z > S * fac[q]) k[(cov ? 3 : 10) + q]++;
        }
}
#endif
static int lean_conv(volatile struct glr *s, uint32_t *gctx, const uint32_t *desc, uint32_t kind, int m1,
                     uint32_t tsaddr, int32_t *pos, int32_t *uv, uint32_t *idp, int16_t *box, int32_t *yr)
{
    int wsh, hsh;
    int mv = m1 && kind != KIND_ALPHA;
    int ti = tri_tex(s, kind, m1, tsaddr, mv ? (const uint16_t *)gctx[0x5c / 4] : 0, mv ? m1_table(gctx) : 0,
                     idp, &wsh, &hsh);
    if (ti == -1) return 0;
    const uint32_t *v0 = (const uint32_t *)desc[3], *v1 = (const uint32_t *)desc[6], *v2 = (const uint32_t *)desc[9];
    int32_t x0 = (int16_t)(int32_t)v0[0], y0 = (int16_t)(int32_t)v0[1];
    int32_t x1 = (int16_t)(int32_t)v1[0], y1 = (int16_t)(int32_t)v1[1];
    int32_t x2 = (int16_t)(int32_t)v2[0], y2 = (int16_t)(int32_t)v2[1];
    int32_t xlo, xhi, ylo, yhi;
    ext3(x0, x1, x2, &xlo, &xhi); ext3(y0, y1, y2, &ylo, &yhi);
    tbox_calc(box, xlo, ylo, xhi, yhi);
    yr[0] = ylo < 0 ? 0 : ylo >= VP_H ? VP_H - 1 : ylo;
    yr[1] = yhi < 0 ? 0 : yhi >= VP_H ? VP_H - 1 : yhi;
    const uint32_t uvw[6] = { desc[4], desc[5], desc[7], desc[8], desc[10], desc[11] };
    tri_conv(kind, wsh, hsh, x0, y0, x1, y1, x2, y2, (int32_t)v0[2], (int32_t)v1[2], (int32_t)v2[2], uvw,
             xlo, xhi, ylo, yhi, pos, uv);
#if defined(A4_MESH) && !defined(MESH_TRIALL)       /* MESH_TRIALL: bench, round 27's rule */
    if (MESH_ON() && kind != KIND_TEXZ
        && !mesh_tri_covered(desc, (int32_t)v0[2], (int32_t)v1[2], (int32_t)v2[2], xlo, ylo, xhi, yhi)) {
        pos[2] = -pos[3]; pos[6] = -pos[7]; pos[10] = -pos[11];   /* window depth 1: untested */
    }
#endif
#ifdef A4_ATLAS
    if ((s->fmode & AB_ATLAS) && ti >= 0 && !mv && (kind == KIND_TEX || kind == KIND_TEXZ || kind == KIND_ADD)
        && wsh <= 7 && hsh <= 7 && ((const uint32_t *)tsaddr)[1] + 1u == (1u << wsh)
        && ((const uint32_t *)tsaddr)[2] + 1u == (1u << hsh)) {          /* a power of two each way, as GL ES 1.x textures are */
        /* texcoord words are texels * 16 (tri_conv: x 4096 >> log2 size = / 16 / size); in [0, 1]:
         * every U in [0, 16 w], V in [0, 16 h] */
        int32_t uw = 16 << wsh, vh = 16 << hsh;
        const int32_t *q = (const int32_t *)uvw;
        if (q[0] >= 0 && q[0] <= uw && q[2] >= 0 && q[2] <= uw && q[4] >= 0 && q[4] <= uw
            && q[1] >= 0 && q[1] <= vh && q[3] >= 0 && q[3] <= vh && q[5] >= 0 && q[5] <= vh) {
            uint32_t xy = atl_get(s, ti, (const uint32_t *)tsaddr, 1u << wsh, 1u << hsh);
            if (xy) {
                /* normalised GL_FIXED over the atlas: (16 * origin + U) * 65536 / (16 * 1024) = << 2,
                 * (16 * origin + V) * 65536 / (16 * h) = << (16 - 4 - log2 h): 2 for 1024, 3 for 512 */
                int32_t ox = (int32_t)(xy & 0xFFFFu) << 4, oy = (int32_t)(xy >> 16) << 4;
                int vsh = 12 - (int)ATL->vs;
                uv[0] = (ox + q[0]) << 2; uv[1] = (oy + q[1]) << vsh;
                uv[2] = (ox + q[2]) << 2; uv[3] = (oy + q[3]) << vsh;
                uv[4] = (ox + q[4]) << 2; uv[5] = (oy + q[5]) << vsh;
                *idp = (kind << 28) | ATL->name[s->cache_tr[ti] ? 1 : 0];
                ((volatile uint32_t *)0x00412D00u)[115]++;
            }
        }
    }
#endif
    return 1;
}
static int lean_tail(volatile struct glr *s, uint32_t *gctx, const uint32_t *desc, uint32_t kind, int m1, uint32_t tsaddr)
{
    uint32_t n = s->bfill;
    if (n >= MAXT) {                                /* batch full: draw it, then refill */
        uint32_t t0 = a4_ntick(); batch_draw(n); A4_PERF[PF_DRAW] += a4_ntick() - t0;
        n = 0; s->bfill = 0;
    }
    int32_t yr[2];
    if (!lean_conv(s, gctx, desc, kind, m1, tsaddr, (int32_t *)A4_GL_POS + n * 12u, (int32_t *)A4_GL_UV + n * 6u,
                   (uint32_t *)A4_GL_TEXID + n, (int16_t *)A4_GL_TBOX + n * 4u, yr)) return 0;
    if ((uint32_t)yr[0] < s->miny) s->miny = (uint32_t)yr[0];
    if ((uint32_t)yr[1] > s->maxy) s->maxy = (uint32_t)yr[1];
    s->bfill = n + 1;
    s->prims++;
    if (kind == KIND_FLAT) A4_PERF[PF_FLAT]++;
    else if (kind == KIND_ADD) A4_PERF[PF_ADD]++;
    else if (kind == KIND_SHAD) A4_PERF[PF_SHAD]++;
    return (kind == KIND_ADD || kind == KIND_ALPHA) ? 0 : 1;
}
#elif defined(A4_LEAN)
#error "A4_LEAN is written for A4_HPERSP + A4_REORDER + A4_IMM"
#endif

#ifdef A4_DEFER
/* Round 22: the hook only records. Its phone cost is ~2,400 cycles a triangle for ~440
 * instructions (rounds 18-21) -- stalls, not work; one cause that fits the size is the
 * hook's ~1.8 KB of code evicted from the 16 KB instruction cache between the game's
 * per-triangle calls, which a tight loop over a whole batch would not suffer (a guess:
 * the A/B against the lean path is the measurement). defer_take writes the triangle's raw
 * record (16 words) into its batch slot -- the POS/UV region, unused in this mode -- and
 * batch_draw converts every record of the batch in one pass, then writes each run's
 * vertices straight into the reorder's arrays (no copy). Triangle record: [0] kind |
 * m1 << 4, [1] the texture word, [2] [3] ctx[0x5c] and m1_table(ctx) for mode 1 (the
 * context describes another object by the time the batch is drawn), [4..6] x | y << 16
 * (int16, as the game's), [7..9] the depths, [10..15] the texcoord words. Quad record
 * (quad_push): [0] DEF_QUAD, [1] its TEXID, [2] [3] its box (x | y << 16, inclusive),
 * [4..9] the three vertices' (x, y), [10..15] their (s, t). */
#define DEF_W 16u
#define DEF_QUAD 0x20u
#define DEF_DROP 0xFFFFFFFFu     /* a record whose texture failed at the batch: not drawn */
#define DEFR(n) ((uint32_t *)A4_GL_POS + (n) * DEF_W)
_Static_assert(DEF_W * 4u * MAXT <= 3u * MAXT * (POSC * POSB + UVC * 4u), "records past the POS/UV region");
static int defer_take(volatile struct glr *s, uint32_t *gctx, const uint32_t *desc, uint32_t kind, int m1, uint32_t tsaddr)
{
    if (kind != KIND_SHAD && kind != KIND_FLAT) {      /* what tex_index refuses at once: software */
        const uint32_t *ts = (const uint32_t *)tsaddr;
        if (!ts[TS_PIX] || ts[1] + 1 > 1024u || ts[2] + 1 > 1024u) return 0;
    }
    uint32_t n = s->bfill;
    if (n >= MAXT) {
        uint32_t t0 = a4_ntick(); batch_draw(n); A4_PERF[PF_DRAW] += a4_ntick() - t0;
        n = 0; s->bfill = 0;
    }
    uint32_t *r = DEFR(n);
    const uint32_t *v0 = (const uint32_t *)desc[3], *v1 = (const uint32_t *)desc[6], *v2 = (const uint32_t *)desc[9];
    r[0] = kind | ((uint32_t)m1 << 4);
    r[1] = tsaddr;
    if (m1 && kind != KIND_ALPHA) { r[2] = gctx[0x5c / 4]; r[3] = (uint32_t)(uintptr_t)m1_table(gctx); }
    else r[2] = r[3] = 0;
    r[4] = (v0[0] & 0xFFFFu) | (v0[1] << 16); r[5] = (v1[0] & 0xFFFFu) | (v1[1] << 16); r[6] = (v2[0] & 0xFFFFu) | (v2[1] << 16);
    r[7] = v0[2]; r[8] = v1[2]; r[9] = v2[2];
    r[10] = desc[4]; r[11] = desc[5]; r[12] = desc[7]; r[13] = desc[8]; r[14] = desc[10]; r[15] = desc[11];
#ifdef A4_DEFERCHECK
    {   /* bench: what the lean path makes of this triangle now, for batch_draw to compare */
        int32_t yr[2];
        uint32_t *sh = (uint32_t *)0x00423000u + n * 24u;    /* pos 12, uv 6, id, box 2, ok, rows 2 */
        sh[19] = lean_conv(s, gctx, desc, kind, m1, tsaddr, (int32_t *)sh, (int32_t *)sh + 12, sh + 18,
                           (int16_t *)(sh + 20), yr);
        sh[22] = (uint32_t)yr[0]; sh[23] = (uint32_t)yr[1];
    }
#endif
    s->bfill = n + 1;
    s->prims++;
    return (kind == KIND_ADD || kind == KIND_ALPHA) ? 0 : 1;
}
#endif

#ifdef A4_MESH
extern const int32_t a4_otins, a4_tfvt;      /* a4_gl.s: target - (the word), patched by the injector */
#define MESH_FITS(a) ((uint32_t)((int32_t)(a) + 32768) < 65536u)
/* A face the mesh routine passed its culls, into the frame's list: 1 = taken (the ordering
 * table never sees it). Taken: a TFace (its vtable; not a TFakeZFace), in one of the three
 * classes the mesh routine gives (map 2: 0x4001 perspective, 0x4000 default, 0x104000
 * default in span mode 1 -- all three with the 2D back-face bit, which the list's draw
 * does with GL_CULL_FACE; anything else, the near-clip variants among them, goes to the
 * table), a texture tex_index would take, its three vertices at z' <= -ZK (so the GPU
 * never clips at the near plane: z_ndc = ZK/-z' <= 1) and every coordinate in int16. The
 * mode is the window's (A4_PERF), not s->fmode, which the frame's first triangle sets
 * after these inserts. */
static int mesh_fallback(const uint32_t *gctx, int32_t F, const int32_t *v0, const int32_t *v1, const int32_t *v2) __attribute__((unused));
static void mesh_scr(const uint32_t *gctx, int32_t F, int32_t cx, int32_t cy, int32_t x, int32_t y, int32_t z,
                     int32_t *sx, int32_t *sy);
#ifdef A4_MESHMEMO
/* Round 50: each face's three corners were projected (mesh_scr, and for the perspective class mesh_sy
 * and persp_rt again) at every face that shares them -- each a read of a 128 KB table of the game's,
 * a data-cache miss more often than not (the cycle profile's hottest loads). Both are pure functions of
 * the corner's view x' y' z' and the frame's projection (gctx, F, the screen centre: fixed while a
 * frame's list is gathered, mesh_take refuses another camera), so a corner's are kept, keyed by its
 * exact coordinates and the call of the mesh routine it was met in (MC1: kept for the frame it was not
 * exact -- the game's tables behind the projection change between meshes; within a call of +349F8 nothing
 * changes them): the same numbers, worked out once. 128 entries of 32 bytes at 0x452000 (free in a 3x-engine
 * build: the half-step machinery that uses it cannot be built with it); the call count at 0x451FFC, bumped
 * by the lifted +349F8 at its entry (edits.py ALWAYS), so the memo needs the lifted mesh routine. */
#if defined(A4_HALFSPEC) || defined(A4_MESHSTATE) || defined(A4_MESHCHECK)
#error "A4_MESHMEMO keeps its table at 0x451FFC-0x453000, which that uses"
#endif
#if !defined(A4_ENGINE) || !(defined(A4_ENG_ALL) || defined(A4_ENG_349F8))
#error "A4_MESHMEMO is kept per call of the lifted mesh routine: build it with A4_ENGINE and A4_ENG_349F8 (or ALL)"
#endif
struct mvtx { int32_t x, y, z, sx, sy, prt; uint32_t stamp, hasprt; };
#define MVT ((struct mvtx *)0x00452000u)
#define MVN 128u
static int32_t persp_rt(const uint32_t *gctx, int32_t z);
static void mesh_scr(const uint32_t *gctx, int32_t F, int32_t cx, int32_t cy, int32_t x, int32_t y, int32_t z,
                     int32_t *sx, int32_t *sy);
/* A corner's kept numbers are copied out (struct mpt): MC1 and MC2 found the kept ones wrong on 1-3% of
 * faces, and the cause was here -- a pointer into the table, overwritten by the next corner of the same
 * face when the two hash to one slot. */
struct mpt { int32_t z, sx, sy; struct mvtx *e; };
static inline struct mvtx *mesh_vtx_e(const uint32_t *gctx, int32_t F, int32_t cx, int32_t cy, const int32_t *v, uint32_t stamp)
{
    const int32_t x = v[3], y = v[4], z = v[5];
    struct mvtx *e = MVT + ((((uint32_t)x * 0x9E3779B1u) ^ ((uint32_t)y * 0x85EBCA77u) ^ ((uint32_t)z * 0xC2B2AE3Du)) >> 25);
    if (e->stamp != stamp || e->x != x || e->y != y || e->z != z) {
        e->x = x; e->y = y; e->z = z; e->stamp = stamp; e->hasprt = 0;
        mesh_scr(gctx, F, cx, cy, x, y, z, &e->sx, &e->sy);
    }
#ifdef A4_MEMOCHECK
    else ((volatile uint32_t *)0x00412D00u)[103]++;
#endif
    return e;
}
static inline struct mpt mesh_vtx(const uint32_t *gctx, int32_t F, int32_t cx, int32_t cy, const int32_t *v, uint32_t stamp)
{
    struct mvtx *e = mesh_vtx_e(gctx, F, cx, cy, v, stamp);
    struct mpt p = { v[5], e->sx, e->sy, e };
    return p;
}
/* a corner's perspective term: kept with it while its entry still holds it (same coordinates), else
 * worked out (the entry may have gone to another corner since) */
static inline int32_t mesh_vprt(const uint32_t *gctx, const struct mpt *p, const int32_t *v)
{
    struct mvtx *e = p->e;
    if (e->x == v[3] && e->y == v[4] && e->z == v[5]) {
        if (!e->hasprt) { e->prt = persp_rt(gctx, -e->z); e->hasprt = 1; }
        return e->prt;
    }
    return persp_rt(gctx, -p->z);
}
/* mesh_fallback on the kept corners: the same rule, the same numbers */
static int mesh_fallback_m(const uint32_t *gctx, const struct mpt *e0, const struct mpt *e1, const struct mpt *e2,
                           const int32_t *v0, const int32_t *v1, const int32_t *v2)
{
    int32_t y0 = e0->sy, y1 = e1->sy, y2 = e2->sy;
    const struct mpt *et, *ea, *eb;                 /* 0x2ff60's first output, branch for branch */
    const int32_t *vt, *va, *vb;
    if (y0 <= y1) { if (y1 <= y2 || y0 <= y2) { et = e0; ea = e1; eb = e2; vt = v0; va = v1; vb = v2; } else { et = e2; ea = e0; eb = e1; vt = v2; va = v0; vb = v1; } }
    else { if (y2 >= y0 || y2 > y1) { et = e1; ea = e0; eb = e2; vt = v1; va = v0; vb = v2; } else { et = e2; ea = e0; eb = e1; vt = v2; va = v0; vb = v1; } }
    if (-et->z <= 500) return 0;
    int32_t q = mesh_vprt(gctx, et, vt);
    uint32_t d1 = (uint32_t)(mesh_vprt(gctx, ea, va) - q), d2 = (uint32_t)(mesh_vprt(gctx, eb, vb) - q);
    return (int32_t)(d1 * d1 + d2 * d2) < 100;
}
#endif
static int mesh_take(uint32_t *cam, const uint32_t *face)
{
    volatile struct glr *s = (volatile struct glr *)A4_GL_SCRATCH;
    volatile struct mesh *m = MESH;
    if ((A4_PERF[PF_MODE] & (AB_MESH | AB_LEAN | AB_DEFER)) != (AB_MESH | AB_LEAN)
        || s->up != A4_GL_MAGIC || !s->zbits || !s->abits) return 0;
    if (m->frame != s->frames) {                    /* the frame's first insert */
#ifdef A4_MESHCHECK
        for (uint32_t j = 0; j < MESH_HN; j++) { MESH_H[j] = 0; MESH_FH[j] = 0; }   /* the last frame's out of
                                                     * the hashes (all of it: a deletion would cut probe chains) */
#endif
        m->frame = s->frames; m->n = 0; m->drawn = 0;
        m->cam = (uint32_t)(uintptr_t)cam; m->F = cam[1];
        m->qlo = 0xFFFFu; m->qhi = 0;
    }
    uint32_t n = m->n;
    if (m->drawn || m->cam != (uint32_t)(uintptr_t)cam || n >= MESH_MAXT) return 0;
    if (face[0] != (uint32_t)(uintptr_t)&a4_tfvt + (uint32_t)a4_tfvt) return 0;
    uint32_t fl = face[1], m1;
    if (fl == 0x4001u || fl == 0x4000u) m1 = 0;
#ifdef A4_MESH_M1
    else if (fl == 0x104000u) m1 = 1;
#endif
    else return 0;
    /* The mode-1 class (0x104000) stays in the table unless A4_MESH_M1: its "table" is the
     * render target itself (ctx[0x60] == ctx[0x4c]), so its bake samples the frame as it
     * stands when the face is drawn -- late in the walk for the player's car, whose paint
     * and window reflect the scenery drawn by then. Baked at the list's draw (the walk's
     * start) the reflection was gone (MBX.md, mesh bench 1). */
#ifdef MESH_EXCL
    /* bench: classes left to the table (1: 0x4001, 2: 0x4000, 4: 0x104000) */
    if ((MESH_EXCL & 1) && fl == 0x4001u) return 0;
    if ((MESH_EXCL & 2) && fl == 0x4000u) return 0;
    if ((MESH_EXCL & 4) && fl == 0x104000u) return 0;
#endif
    const uint32_t *ts = (const uint32_t *)face[12];
    if (!ts || !ts[TS_PIX] || ts[1] + 1u > 1024u || ts[2] + 1u > 1024u) return 0;
    const int32_t *v0 = (const int32_t *)face[3], *v1 = (const int32_t *)face[6], *v2 = (const int32_t *)face[9];
    int32_t z0 = v0[5], z1 = v1[5], z2 = v2[5];     /* view z' at +0x14 */
    if (z0 > -ZK || z1 > -ZK || z2 > -ZK || !MESH_FITS(z0) || !MESH_FITS(z1) || !MESH_FITS(z2)) return 0;
    /* Round 28: a face whose deepest vertex is within MESH_NEAR stays in the table, drawn in
     * the walk's order with the sprites around it -- the player's car's plain parts (~55
     * faces a frame, 6.6% of the list): the nitro flame (quads at 191-230) is painted over
     * the road and under those parts by order, which no depth test reproduces (MBX.md). */
    if (-z0 < MESH_NEAR && -z1 < MESH_NEAR && -z2 < MESH_NEAR
        && m->nearf && s->frames - (m->nearf - 1u) < MESH_NEARF) return 0;
    const uint32_t *gctx = (const uint32_t *)m->gctx;
    if (!gctx) return 0;                            /* no frame drawn yet: no projection */
    uint32_t texz = 0;
#ifdef A4_MESHMEMO
    const int32_t mF = (int32_t)m->F, mcx = *(const int32_t *)gctx[0x40110 / 4] >> 1, mcy = *(const int32_t *)gctx[0x40114 / 4] >> 1;
    const uint32_t mst = *(volatile uint32_t *)0x00451FFCu | 0x80000000u;   /* the mesh routine's call: never the zeroed table's 0 */
    const struct mpt e0 = mesh_vtx(gctx, mF, mcx, mcy, v0, mst), e1 = mesh_vtx(gctx, mF, mcx, mcy, v1, mst),
                     e2 = mesh_vtx(gctx, mF, mcx, mcy, v2, mst);
#endif
    if (fl == 0x4001u) {
#ifdef A4_MESHMEMO
        int fb = mesh_fallback_m(gctx, &e0, &e1, &e2, v0, v1, v2);
#ifdef A4_MEMOCHECK
        {   /* bench: the kept corners against the projection worked out again ([100] faces, [101] differing
             * fallback verdicts, [102] differing screen points; [103] corners found kept) */
            volatile uint32_t *ck = (volatile uint32_t *)0x00412D00u;
            ck[100]++;
            if (fb != mesh_fallback(gctx, (int32_t)m->F, v0, v1, v2)) ck[101]++;
        }
#endif
#else
        int fb = mesh_fallback(gctx, (int32_t)m->F, v0, v1, v2);
#endif
#ifdef A4_MESHCHECK
        {   uint32_t j = mesh_hslot((uint32_t)(uintptr_t)face);
            while (MESH_FH[j] && MESH_FH[j] != (uint32_t)(uintptr_t)face) j = (j + 1u) & (MESH_HN - 1u);
            MESH_FH[j] = (uint32_t)(uintptr_t)face; MESH_FV[j] = (uint8_t)(1 + fb); }
#endif
        /* The list is painted in table order (mesh_draw), as the game paints everything; the
         * default class and the perspective class's affine fallback untested, a 0x4001 face
         * that keeps its perspective span depth-tested and writing (the game's 1/z buffer):
         * texz, bit 1 of its texture word. */
        if (fb) m->fellback++;
        else texz = 2u;
    }
    int32_t x0 = v0[3], y0 = v0[4], x1 = v1[3], y1 = v1[4], x2 = v2[3], y2 = v2[4];
    if (!(MESH_FITS(x0) && MESH_FITS(y0) && MESH_FITS(x1) && MESH_FITS(y1) && MESH_FITS(x2) && MESH_FITS(y2))) return 0;
    uint32_t a0 = face[4], b0 = face[5], a1 = face[7], b1 = face[8], a2 = face[10], b2 = face[11];
    if (!(MESH_FITS(a0) && MESH_FITS(b0) && MESH_FITS(a1) && MESH_FITS(b1) && MESH_FITS(a2) && MESH_FITS(b2))) return 0;
    {   /* The game's 2D back-face test, done before projecting: it draws where the screen
         * area (x1-x0)(y2-y0) - (x2-x0)(y1-y0) < 0. With sx = cx + F x'/w, sy = cy - F y'/w,
         * w = -z' > 0, that area is F^2 det[x' y' z'] / (w0 w1 w2), the same sign as the
         * determinant of the three view-space vertices: drawn where det < 0. Taken here,
         * a back face is consumed (the table would have dropped it at its draw). int16
         * coordinates (MESH_FITS, all nine): each 2x2 minor is within 2^31 - 32768, so it
         * is exact in 32 bits (round 50: wrapping products, 6 MULs); only the three terms
         * of up to 2^47 need 64 bits (SMULL). Before, every product was 64-bit: 6 SMULLs
         * and three 64x64 multiplies, 2.2% of the race's modelled cycles (pct19). Checked
         * against the all-64-bit form on the host: 2e8 random and 4e7 corner cases. */
        const int32_t ma = (int32_t)((uint32_t)y1 * (uint32_t)z2 - (uint32_t)z1 * (uint32_t)y2);
        const int32_t mb = (int32_t)((uint32_t)z1 * (uint32_t)x2 - (uint32_t)x1 * (uint32_t)z2);
        const int32_t mc = (int32_t)((uint32_t)x1 * (uint32_t)y2 - (uint32_t)y1 * (uint32_t)x2);
        int64_t d = (int64_t)x0 * ma + (int64_t)y0 * mb + (int64_t)z0 * mc;
        if (d >= 0) { MESH->culled++; return 1; }
    }
    int16_t *c = MESH_V + n * 18u;
    c[0]  = (int16_t)x0; c[1]  = (int16_t)y0; c[2]  = (int16_t)z0; c[3]  = (int16_t)a0; c[4]  = (int16_t)b0; c[5]  = 0;
    c[6]  = (int16_t)x1; c[7]  = (int16_t)y1; c[8]  = (int16_t)z1; c[9]  = (int16_t)a1; c[10] = (int16_t)b1; c[11] = 0;
    c[12] = (int16_t)x2; c[13] = (int16_t)y2; c[14] = (int16_t)z2; c[15] = (int16_t)a2; c[16] = (int16_t)b2; c[17] = 0;
    MESH_T[n] = (uint32_t)(uintptr_t)ts | m1 | texz; /* descriptors are word aligned */
    {   /* where the table would put it: its screen box from the game's own projection (for
         * the run grouping's overlap test) and its bucket, +349B0's (key >> 3, TFace's key
         * +3121E = the deepest vertex's ZW; these classes carry neither of the +-16 bits) */
        int32_t F = (int32_t)m->F, cx = *(const int32_t *)gctx[0x40110 / 4] >> 1, cy = *(const int32_t *)gctx[0x40114 / 4] >> 1;
        int32_t sx0, sy0, sx1, sy1, sx2, sy2, xlo, xhi, ylo, yhi;
#ifdef A4_MESHMEMO
        sx0 = e0.sx; sy0 = e0.sy; sx1 = e1.sx; sy1 = e1.sy; sx2 = e2.sx; sy2 = e2.sy;
        (void)F; (void)cx; (void)cy;
#ifdef A4_MEMOCHECK
        {   volatile uint32_t *ck = (volatile uint32_t *)0x00412D00u;
            int32_t a0, b0, a1, b1, a2, b2;
            mesh_scr(gctx, F, cx, cy, x0, y0, z0, &a0, &b0);
            mesh_scr(gctx, F, cx, cy, x1, y1, z1, &a1, &b1);
            mesh_scr(gctx, F, cx, cy, x2, y2, z2, &a2, &b2);
#ifdef A4_MEMOCHECK_MUTANT
            a0 ^= 1;
#endif
            if (a0 != sx0 || b0 != sy0 || a1 != sx1 || b1 != sy1 || a2 != sx2 || b2 != sy2) ck[102]++;
        }
#endif
#else
        mesh_scr(gctx, F, cx, cy, x0, y0, z0, &sx0, &sy0);
        mesh_scr(gctx, F, cx, cy, x1, y1, z1, &sx1, &sy1);
        mesh_scr(gctx, F, cx, cy, x2, y2, z2, &sx2, &sy2);
#endif
        ext3(sx0, sx1, sx2, &xlo, &xhi); ext3(sy0, sy1, sy2, &ylo, &yhi);
        tbox_calc(MESH_B + n * 4u, xlo, ylo, xhi, yhi);
        int32_t zw = -z0, zl = -z0;
        if (-z1 > zw) zw = -z1;
        if (-z2 > zw) zw = -z2;
        if (-z1 < zl) zl = -z1;
        if (-z2 < zl) zl = -z2;
        const uint32_t q = (uint32_t)((zw >> 3) > 4095 ? 4095 : (zw >> 3));
        MESH_Q[n] = (uint16_t)q;
        if (q < m->qlo) m->qlo = (uint16_t)q;
        if (q > m->qhi) m->qhi = (uint16_t)q;
        MESH_Z[2u * n] = (uint16_t)zl; MESH_Z[2u * n + 1u] = (uint16_t)zw;
    }
#ifdef A4_MESHCHECK
    MESH_D[n] = (uint32_t)(uintptr_t)face;
    {   uint32_t j = mesh_hslot((uint32_t)(uintptr_t)face);
        while (MESH_H[j] && MESH_H[j] != (uint32_t)(uintptr_t)face) j = (j + 1u) & (MESH_HN - 1u);
#ifdef A4_MESHSTATE
        if (MESH_H[j]) {                            /* listed already this frame */
            uint32_t o = MESH_HX[j] & 0x7FFFu;
            const int16_t *ov = MESH_V + o * 18u, *nv = MESH_V + n * 18u;
            MESH_SC[7]++;
            if (ov[2] != nv[2] || ov[0] != nv[0] || ov[1] != nv[1]) { MESH_SC[8]++; mesh_sc_case((uint32_t)(uintptr_t)face, 5u, (uint16_t)ov[2], (uint16_t)nv[2]); }
        } else MESH_SC[10]++;                       /* distinct faces listed */
        MESH_HX[j] = (uint16_t)n; MESH_SC[5]++;
#endif
        MESH_H[j] = (uint32_t)(uintptr_t)face;
    }
#endif
    m->n = n + 1;
    return 1;
}

/* The screen point +51E44 would give a vertex (map 2): its integer arithmetic, 32-bit wrap
 * included; the near ranges pre-shift by 4-7 bits as +51F08 does. RT2 = gctx+0x2008C. */
static void mesh_scr(const uint32_t *gctx, int32_t F, int32_t cx, int32_t cy, int32_t x, int32_t y, int32_t z,
                     int32_t *sx, int32_t *sy)
{
    const uint32_t *base = (const uint32_t *)((uintptr_t)gctx + 0x40100u);
    const int32_t *rt2 = (const int32_t *)((uintptr_t)gctx + 0x2008Cu);
    int m = z > -64 ? 7 : z > -128 ? 6 : z > -256 ? 5 : z > -512 ? 4 : 0;
    int32_t lz = m ? (int32_t)((uint32_t)(-z) << m) : (int32_t)((uint32_t)(-z) & 0x7FFFu), r;
    if (lz <= 0) r = (int32_t)base[0x0c / 4];
    else if (lz >= 0x8020) r = (int32_t)base[0x08 / 4];
    else r = rt2[lz];
    r = (int32_t)((uint32_t)r * (uint32_t)F) >> 6;
    int32_t xx = m ? (int32_t)((uint32_t)x << m) : x, yy = m ? (int32_t)((uint32_t)y << m) : y;
    *sx = cx + ((int32_t)((uint32_t)xx * (uint32_t)r + 0x8000u) >> 16);
    *sy = cy - ((int32_t)((uint32_t)yy * (uint32_t)r + 0x8000u) >> 16);
}
static int32_t mesh_sy(const uint32_t *gctx, int32_t F, int32_t cy, int32_t y, int32_t z)
{
    int32_t sx, sy;
    mesh_scr(gctx, F, 0, cy, 0, y, z, &sx, &sy);
    return sy;
}
/* The perspective rasterizer's affine fallback (persp_fallback's rule, on the vertices the
 * projection would make): such a face neither tests nor writes the game's depth -- it is
 * painted in table order, over whatever is behind it -- so the list, which depth-tests,
 * cannot stand in for it (round 26: the exit gantry's signs hang behind its beam). */
static int mesh_fallback(const uint32_t *gctx, int32_t F, const int32_t *v0, const int32_t *v1, const int32_t *v2)
{
    int32_t cy = *(const int32_t *)gctx[0x40114 / 4] >> 1;
    int32_t y0 = mesh_sy(gctx, F, cy, v0[4], v0[5]), y1 = mesh_sy(gctx, F, cy, v1[4], v1[5]);
    int32_t y2 = mesh_sy(gctx, F, cy, v2[4], v2[5]);
    int32_t w0 = -v0[5], w1 = -v1[5], w2 = -v2[5];
    int32_t wt, wa, wb;                             /* 0x2ff60's first output, branch for branch */
    if (y0 <= y1) { if (y1 <= y2 || y0 <= y2) { wt = w0; wa = w1; wb = w2; } else { wt = w2; wa = w0; wb = w1; } }
    else { if (y2 >= y0 || y2 > y1) { wt = w1; wa = w0; wb = w2; } else { wt = w2; wa = w0; wb = w1; } }
    if (wt <= 500) return 0;
    int32_t q = persp_rt(gctx, wt);
    uint32_t d1 = (uint32_t)(persp_rt(gctx, wa) - q), d2 = (uint32_t)(persp_rt(gctx, wb) - q);
    return (int32_t)(d1 * d1 + d2 * d2) < 100;
}

/* +34ADC's BLX (r0 = cam, r1 = face, as the bl to +349B0 had them); returns to +34AE0. */
void a4_mesh_insert(uint32_t *cam, uint32_t *face)
{
#ifdef A4_LIFTCHECK
    {   /* bench: the mesh routine run twice (a4_eng.c a4_engv_349F8) -- its inserts listed, not made */
        volatile uint32_t *lk = (volatile uint32_t *)0x00460000u;
        uint32_t md = lk[0];
        if (md == 1u || md == 2u) {
            uint32_t n = lk[md];
            volatile uint32_t *L = (volatile uint32_t *)(md == 1u ? 0x00460010u : 0x00464010u);
            if (n < 2048u) { L[2u * n] = (uint32_t)(uintptr_t)cam; L[2u * n + 1u] = (uint32_t)(uintptr_t)face; }
            lk[md] = n + 1u;
            return;
        }
    }
#endif
#ifdef A4_MESHCHECK
    mesh_take(cam, face);                           /* listed, and to the table as well (software draws it) */
#else
    if (mesh_take(cam, face)) return;
#endif
    ((void (*)(uint32_t *, uint32_t *))((uint32_t)(uintptr_t)&a4_otins + (uint32_t)a4_otins))(cam, face);
}

/* The frame's list, at its first triangle (draw_prim_take, after the clear): each
 * triangle's texture resolved now, with the context the ordering table's faces would
 * have been drawn under (ctx[0x5c] and the mode-1 table for the 0x104000 class), grouped
 * by texture through an index list, and drawn with the frame's projection -- x_c =
 * 2F/W x' + (1 - 2cx/W) z', y_c = 2F/H y' + (2cy/H - 1) z', z_c = ZK, w_c = -z' (MBX.md
 * map 2: sx = cx + F x'/-z', sy = cy - F y'/-z'; window depth 0.5 + ZK/ZW/2 less 1/64,
 * the hook's KIND_TEXZ set back a little) -- texcoords scaled by a texture matrix (words are texels * 16), the
 * depth test on and writing, back faces culled (the game's 2D test: drawn where the
 * screen-space area is negative, i.e. counter-clockwise once GL's y points up). */
static void mesh_draw(volatile struct glr *s, uint32_t *gctx)
{
    volatile struct mesh *m = MESH;
    m->active = 0;
    m->gctx = (uint32_t)(uintptr_t)gctx;            /* for the next frame's fallback tests */
    uint32_t n = m->n;
    if (m->frame != s->frames) {
        /* Round 34: a list gathered before a present and not drawn since is this walk's (its
         * faces were taken from the game: the table no longer holds them). Until now it was
         * dropped -- the scenery and the list's car parts missing for that frame, a candidate
         * for the owner's empty frame and the crash camera's car in pieces (a guess; the lossy
         * frame recorder, A4_SLOWREC, says whether it happens). A list with no inserts since
         * the last walk (drawn already, or empty) is no list. */
        if (m->drawn || !n) { m->frame = s->frames; m->n = 0; m->drawn = 1; return; }
        m->frame = s->frames; m->stale++;
    }
    if (m->drawn) return;
    m->drawn = 1;
    if (!n) return;
    uint32_t t0 = a4_ntick();
    const uint16_t *m1map = (const uint16_t *)gctx[0x5c / 4];
    const uint16_t *m1tab = m1_table(gctx);
    uint32_t kid[MESH_NK];
    uint8_t kws[MESH_NK], khs[MESH_NK];
    uint32_t nk = 0, lastw = 0xFFFFFFFFu, lastk = 0xFFu, lost = 0;
    uint8_t *K = MESH_K;
    const uint32_t *T = MESH_T;
    for (uint32_t i = 0; i < n; i++) {               /* each triangle's texture, now (ctx as the table's draw sees it) */
        uint32_t w = T[i] & ~2u;                     /* (texz is the run's, not the texture's) */
        if (w != lastw) {
            uint32_t id; int wsh, hsh;
            lastw = w; lastk = 0xFFu;
            if (tri_tex(s, KIND_TEX, (int)(w & 1u), w & ~3u, m1map, m1tab, &id, &wsh, &hsh) >= 0) {
                uint32_t k = 0;
                while (k < nk && kid[k] != id) k++;
                if (k == nk && nk < MESH_NK) { kid[k] = id; kws[k] = (uint8_t)wsh; khs[k] = (uint8_t)hsh; nk++; }
                if (k < nk) lastk = k;
            }
        }
        K[i] = (uint8_t)lastk;
        if (lastk == 0xFFu) lost++;
    }
#ifdef A4_BBSINK
    {   volatile uint32_t *k = (volatile uint32_t *)0x00454100u;   /* bench: [47] frames with the near cut, [63] list frames */
        k[63]++; if (m->nearf && s->frames - (m->nearf - 1u) < MESH_NEARF) k[47]++; }
#endif
    {   /* the tile grid of nearest list buckets, for the table's untested classes (lean_conv) */
        uint16_t *g = (uint16_t *)m->grid;
        const int16_t *B = MESH_B;
        const uint16_t *Q = MESH_Q;
        for (uint32_t j = 0; j < MESH_GW * MESH_GH; j++) g[j] = 0xFFFFu;
        for (uint32_t i = 0; i < n; i++) {
            if (K[i] == 0xFFu) continue;
            const int16_t *b = B + i * 4u;
            if (b[0] > b[2]) continue;               /* off screen */
            uint32_t q = Q[i];
            for (int32_t ty = b[1] >> 4; ty <= (b[3] >> 4); ty++)
                for (int32_t tx = b[0] >> 4; tx <= (b[2] >> 4); tx++)
                    if (q < g[ty * MESH_GW + tx]) g[ty * MESH_GW + tx] = (uint16_t)q;
        }
    }
    /* Table order: buckets far to near; in a bucket the last inserted first (+349B0 pushes
     * on the bucket's head and the walk starts there). A counting sort, the counts in the
     * index area (rewritten below). */
    {   uint16_t *c = MESH_I, *ord = MESH_ORD;
        const uint16_t *Q = MESH_Q;
        /* over the buckets the frame's faces use (mesh_take keeps the range): the same order */
        const int32_t qlo = m->qlo, qhi = m->qhi;
        for (int32_t b = qlo; b <= qhi; b++) c[b] = 0;
        for (uint32_t i = 0; i < n; i++) c[Q[i]]++;
        uint32_t o = 0;
        for (int32_t b = qhi; b >= qlo; b--) { uint32_t k = c[b]; c[b] = (uint16_t)o; o += k; }
        for (int32_t i = (int32_t)n - 1; i >= 0; i--) ord[c[Q[i]]++] = (uint16_t)i;
#ifdef A4_LISTCHECK
        {   /* bench (round 50): the order against the full 4,096-bucket sort, made in the grid's spare
             * 0x460000 area, and each kept ZW range against the corners -- census [74] faces whose place
             * differs, [75] ranges that differ, [76] lists checked */
            volatile uint32_t *ck = (volatile uint32_t *)0x00412D00u;
            uint16_t *c2 = (uint16_t *)0x00460000u, *o2 = (uint16_t *)0x00462000u;
            for (uint32_t b = 0; b < 4096u; b++) c2[b] = 0;
            for (uint32_t i = 0; i < n; i++) c2[Q[i]]++;
            uint32_t oo = 0;
            for (int32_t b = 4095; b >= 0; b--) { uint32_t k = c2[b]; c2[b] = (uint16_t)oo; oo += k; }
            for (int32_t i = (int32_t)n - 1; i >= 0; i--) o2[c2[Q[i]]++] = (uint16_t)i;
#ifdef A4_LISTCHECK_MUTANT
            if (n > 1) { uint16_t t = o2[0]; o2[0] = o2[1]; o2[1] = t; MESH_Z[0] ^= 1u; }
#endif
            for (uint32_t i = 0; i < n; i++) {
                if (o2[i] != ord[i]) ck[74]++;
                const int16_t *fv = MESH_V + i * 18u;
                int32_t fl = -fv[2], fh = -fv[2];
                if (-fv[8] < fl) fl = -fv[8];
                if (-fv[8] > fh) fh = -fv[8];
                if (-fv[14] < fl) fl = -fv[14];
                if (-fv[14] > fh) fh = -fv[14];
                if (fl != (int32_t)MESH_Z[2u * i] || fh != (int32_t)MESH_Z[2u * i + 1u]) ck[75]++;
            }
            ck[76]++;
        }
#endif
    }
    int32_t F = (int32_t)m->F;
    if (F < 1) F = 1;
    if (F > 16383) F = 16383;                        /* F << 17 in int32 */
    int32_t cx = *(const int32_t *)gctx[0x40110 / 4] >> 1, cy = *(const int32_t *)gctx[0x40114 / 4] >> 1;
    int P[16];                                       /* column-major GL_FIXED (no memset: no libgcc) */
    for (int i = 0; i < 16; i++) P[i] = 0;
    P[0] = (F << 17) / VP_W;
    P[5] = (F << 17) / VP_H;
    P[8] = FX - (cx << 17) / VP_W;
    P[9] = (cy << 17) / VP_H - FX;
    P[11] = -FX;
    P[14] = ZK * FX - ((ZK * FX) >> 6);              /* 1/64 farther than the table's ZK/ZW: a
                                                      * face the table draws in the same plane
                                                      * (a lit sign, a decal) wins the tie, as it
                                                      * did in painter's order (round 26) */
    gl_matrixmode(GL_PROJECTION); gl_loadmatrixx(P);
    gl_matrixmode(GL_TEXTURE);
    /* Painted as the game paints them -- in table order, the perspective span's faces tested
     * (GEQUAL) and the others not -- all writing depth, which the table's own triangles then
     * test against (draw_run). */
    gl_enable(GL_DEPTH_TEST); gl_depthfunc(GL_ALWAYS); gl_enable(GL_CULL_FACE);
    gl_vertexptr(3, GL_SHORT, 12, MESH_V);
    gl_texcoordptr(2, GL_SHORT, 12, MESH_V + 3);
    int TM[16];
    for (int i = 0; i < 16; i++) TM[i] = 0;
    TM[10] = FX; TM[15] = FX;
    int lw = -1, lh = -1;
    uint32_t calls = 0, lastid = 0xFFFFFFFFu, lastz = 0;   /* depth func ALWAYS set above */
    /* Runs of one texture in table order; a triangle joins an earlier run of its texture when
     * its box misses every run it passes (the reorder's rule, the same picture as drawing in
     * order). Drawn whenever MESH_NR runs are held, and at the end. */
    struct mrun *R = MESH_R;
    uint16_t *NX = MESH_NX, *I = MESH_I;
    const uint16_t *ord = MESH_ORD;
    const int16_t *B = MESH_B;
    uint32_t nr = 0, t = 0;
    while (t <= n) {
        int flush = t == n;
        if (!flush) {
            uint32_t i = ord[t], k = K[i];
            if (k != 0xFFu) {
                k |= (T[i] & 2u) << 7;               /* run key: texture slot | texz << 8 */
                const int16_t *b = B + i * 4u;
                int32_t r = (int32_t)nr - 1, found = -1;
#ifdef A4_MESHZ
                /* The face's ZW range (its corners' -z'; window depth falls as ZW grows). Every
                 * face of the list writes depth; the perspective class (texz) tests GEQUAL,
                 * the rest are painted (ALWAYS). Where boxes meet, drawing a face before a run
                 * leaves the same colour and depth when: both are tested and their ranges are
                 * apart (the nearer wins either way); the face is tested and wholly farther
                 * than a painted run (the run covers it either way); the face is painted and a
                 * tested run is wholly farther (the face covers it either way). Two painted
                 * ones keep their order. "Apart" with a margin of 1/256 of ZW, many times the
                 * 24-bit buffer's step, so the depths cannot tie once quantised (r50: the list
                 * made 209 draws a render, ~6 faces each, 10.9 ms on the N95 model). */
                const int32_t fl = MESH_Z[2u * i], fh = MESH_Z[2u * i + 1u];   /* mesh_take's */
                const uint32_t tz = (k >> 8) & 1u, zok = s->zbits >= 24u;
#define MESHZ_FARTHER(a, b) ((int32_t)(a) - (int32_t)(b) > ((int32_t)(b) >> 8) + 1)
#endif
                for (uint32_t steps = 0; r >= 0 && steps <= REO_SCAN; r--, steps++) {
                    if (R[r].kslot == k) { found = r; break; }
                    const int16_t *rb = R[r].box;
                    if (b[2] < rb[0] || rb[2] < b[0] || b[3] < rb[1] || rb[3] < b[1]) continue;   /* apart */
#ifdef A4_MESHZ
                    if (zok) {
                        const uint32_t rz = R[r].kslot >> 8;
                        if (tz && rz && (MESHZ_FARTHER(fl, R[r].zhi) || MESHZ_FARTHER(R[r].zlo, fh))) continue;
                        if (tz && !rz && MESHZ_FARTHER(fl, R[r].zhi)) continue;
                        if (!tz && rz && MESHZ_FARTHER(R[r].zlo, fh)) continue;
                    }
#endif
                    break;                          /* shares pixels: stays after it */
                }
                if (found < 0) {
                    if (nr == MESH_NR) { flush = 1; goto draw; }   /* draw what is held, then retake this one */
                    found = (int32_t)nr++;
                    R[found].kslot = (uint16_t)k; R[found].head = R[found].tail = (uint16_t)i; R[found].cnt = 0;
                    R[found].box[0] = b[0]; R[found].box[1] = b[1]; R[found].box[2] = b[2]; R[found].box[3] = b[3];
#ifdef A4_MESHZ
                    R[found].zlo = (int16_t)fl; R[found].zhi = (int16_t)fh;
#endif
                } else {
#ifdef A4_MESHZ
                    if (fl < R[found].zlo) R[found].zlo = (int16_t)fl;
                    if (fh > R[found].zhi) R[found].zhi = (int16_t)fh;
#endif
                    NX[R[found].tail] = (uint16_t)i; R[found].tail = (uint16_t)i;
                    int16_t *rb = R[found].box;
                    if (b[0] < rb[0]) rb[0] = b[0];
                    if (b[1] < rb[1]) rb[1] = b[1];
                    if (b[2] > rb[2]) rb[2] = b[2];
                    if (b[3] > rb[3]) rb[3] = b[3];
                }
                R[found].cnt++;
            }
            t++;
            continue;
        }
draw:
        {   uint32_t o = 0;
            for (uint32_t r = 0; r < nr; r++) {
                uint32_t i = R[r].head, first = o;
                for (uint32_t c = R[r].cnt; c; c--) {
                    I[o] = (uint16_t)(i * 3u); I[o + 1] = (uint16_t)(i * 3u + 1u); I[o + 2] = (uint16_t)(i * 3u + 2u);
                    o += 3u; i = NX[i];
                }
                uint32_t k = R[r].kslot & 0xFFu, z = R[r].kslot >> 8;
                if (z != lastz) { lastz = z; gl_depthfunc(z ? GL_GEQUAL : GL_ALWAYS); }
                if (kws[k] != lw || khs[k] != lh) {
                    lw = kws[k]; lh = khs[k];
                    TM[0] = 4096 >> lw; TM[5] = 4096 >> lh;  /* (texels * 16) / (16 * size), GL_FIXED */
                    gl_loadmatrixx(TM);
                }
                if (kid[k] != lastid) { lastid = kid[k]; gl_bindtex(GL_TEXTURE_2D, kid[k] & 0x0FFFFFFFu); }
#ifdef MESH_VIS
                gl_disable(GL_TEXTURE_2D);              /* bench: the list's faces flat, by class */
                if (z) gl_color4x(FX, 0, 0, FX); else gl_color4x(0, FX, 0, FX);   /* texz red, painted green */
#endif
                gl_drawelements(GL_TRIANGLES, (int)(o - first), GL_UNSIGNED_SHORT, I + first);
                calls++;
            }
            nr = 0;
            if (t == n) break;                       /* the end */
        }
    }
#ifdef MESH_VIS
    gl_enable(GL_TEXTURE_2D); gl_color4x(FX, FX, FX, FX);
#endif
    gl_loadidentity();                               /* the texture matrix */
    gl_matrixmode(GL_PROJECTION); gl_loadidentity();
    gl_orthox(0, VP_W*FX, VP_H*FX, 0, -FX, FX);      /* bring-up's */
    gl_matrixmode(GL_MODELVIEW);
    gl_depthfunc(GL_GEQUAL);                         /* bring-up's */
    gl_disable(GL_CULL_FACE); gl_disable(GL_DEPTH_TEST);
    m->active = n > lost;
    m->nk = nk;
#ifdef A4_ROWSTAT
    {   /* bench (round 50): census [70] the list's texture keys (texture x texz) a frame, [71] its draw
         * calls, [72] frames, [73] faces drawn */
        volatile uint32_t *ck = (volatile uint32_t *)0x00412D00u;
        uint32_t keys = 0, seen[2 * MESH_NK / 32];
        for (uint32_t j = 0; j < 2 * MESH_NK / 32; j++) seen[j] = 0;
        for (uint32_t i = 0; i < n; i++) {
            if (K[i] == 0xFFu) continue;
            uint32_t k = K[i] | ((T[i] & 2u) << 6);
            if (!(seen[k >> 5] & (1u << (k & 31u)))) { seen[k >> 5] |= 1u << (k & 31u); keys++; }
        }
        ck[70] += keys; ck[71] += calls; ck[72]++; ck[73] += n - lost;
    }
#endif
    s->prims += n - lost;                            /* the frame has begun: no second clear */
    s->miny = 0; s->maxy = VP_H - 1;                 /* rows unknown without projecting: all */
    A4_PERF[PF_MESH] += n - lost; A4_PERF[PF_MKEYS] += calls; A4_PERF[PF_MLOST] += lost;
#if defined(A4_RUNDIAG) || defined(A4_TEL)
    ((volatile uint32_t *)0x00412000u)[4] += calls;  /* draw calls this frame */
#endif
    A4_PERF[PF_MDRAW] += a4_ntick() - t0;
}
#endif

static void quad_push(volatile struct glr *s, int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                      int32_t S0, int32_t T0, int32_t S1, int32_t T1, uint32_t id, int32_t zw);
static int draw_prim_take(uint32_t *gctx, uint32_t *desc, uint32_t flags)
{
    volatile uint32_t *iat = a4_gl_iat_ptr();
#ifdef A4_FLAGHIST
    /* mode census: every draw_primitive by (flags, ctx[0x64] -- the per-object span
     * mode the transparent/opaque spans switch on). Table at 0x412D00 (clear of the
     * telemetry's draw-call census at 0x412000; a4_tel.c appends it to each capture):
     * [0]=n, then n entries (flags, mode, count), max 40; [121] = calls that found the
     * table full. Counted before anything else, so taken and software calls alike. */
    {
        volatile uint32_t *fh = (volatile uint32_t *)0x00412D00u;
        uint32_t md = gctx[0x64 / 4], nn = fh[0], i;
        for (i = 0; i < nn; i++) if (fh[1+i*3] == flags && fh[2+i*3] == md) break;
        if (i == nn) { if (nn < 40u) { fh[1+i*3] = flags; fh[2+i*3] = md; fh[3+i*3] = 0; fh[0] = nn + 1; } else i = 99; }
        if (i < 40u) fh[3+i*3]++; else fh[121]++;
    }
#endif
    if (iat[0] < 0x10000u) return 0;
#ifdef A4_PLD
    /* round 23: the hook's phone time is mostly its first touch of the game's per-triangle
     * data (round 22). Ask for the three vertex records and the texture descriptor at once,
     * so their misses overlap; desc+0x30 may be a flat colour (PLD never faults). */
    if (((volatile struct glr *)A4_GL_SCRATCH)->fmode & AB_PLD) {
        __builtin_prefetch((const void *)desc[3]); __builtin_prefetch((const void *)desc[6]);
        __builtin_prefetch((const void *)desc[9]); __builtin_prefetch((const void *)desc[12]);
    }
#endif
    /* Which rasterizer the game would use: draw_primitive (0x300AE) tests flags in the
     * order 0x2, 0x1, 0x8, 0x10, 0x20, 0x40, 0x100, else the default (MBX.md). Anything
     * left to software is drawn ON TOP of the previous frame's GPU image (it lands after
     * the composite), whatever the game's draw order says -- so every class taken here
     * is one less source of things showing through walls. */
    uint32_t kind;
    int m1 = 0;                                     /* default span in its mode 1 (tex_index) */
    if (flags & 2u) {
        /* 0x2fbdc: untextured, the colour in desc+0x30 (its low half; a zero word draws
         * nothing). Solid when neither 0x4 nor 0x8 (span 0x33b46); 0x4 is a 50% blend
         * (0x33b86), 0x8 a stipple (0x33c44) -- those two stay software: blending on the
         * GPU would mix with the sentinel where the GPU drew nothing. A race uses only
         * the solid one (0x20002, 1.6% of calls: the grey/khaki "slabs", round 9). */
#ifdef A4_TAKEFLAT
        if (flags & 0xCu) return 0;
        kind = KIND_FLAT;
#else
        return 0;
#endif
    } else if (flags & 1u) {
        /* 0x2f6fc, perspective textured. Its span depth-tests (see A4_ZBUF); the other
         * classes neither test nor write the game's 1/z buffer. */
#ifdef A4_ZBUF
        kind = KIND_TEXZ;
        if (persp_fallback(gctx, desc)) { kind = KIND_TEX; m1 = gctx[0x64 / 4] == 1u; A4_PERF[PF_AFFINE]++; }
#ifdef A4_MESHCHECK
        {   /* bench: mesh_fallback's verdict at the insert against this one at the draw --
             * A4_PERF [60] compared, [61] differing (must be 0), [62] fallbacks among them
             * (past PF_N; a4_tel.c puts them in header words 60-62 in a check build) */
            uint32_t p = (uint32_t)(uintptr_t)desc, j = mesh_hslot(p);
            while (MESH_FH[j] && MESH_FH[j] != p) j = (j + 1u) & (MESH_HN - 1u);
            if (MESH_FH[j] == p) {
                volatile uint32_t *ck = A4_PERF + 60 - 96;
                int fb = kind == KIND_TEX;
                ck[96]++; if ((MESH_FV[j] - 1) != fb) ck[97]++; if (fb) ck[98]++;
            }
        }
#endif
#else
        kind = KIND_TEX;
#endif
    } else if (flags & 8u) {
        /* 0x2f358 -> span 0x33d18: affine textured, dst + texel per channel, saturating
         * (0x10008, 0.2%: glows). GL_ONE/GL_ONE is the same sum -- but over the sentinel
         * (pixels whose content is the software layer, e.g. the sky) it adds to magenta. */
#ifdef A4_TAKEADD
        kind = KIND_ADD;
#else
        return 0;
#endif
    } else if ((flags & 0x30u) == 0x20u) {
        /* 0x2ecb4 -> span 0x33f8c: shadows, dst*12/16 (22% of calls). The span keeps a
         * screen-sized byte map so an overlap darkens once; GL_ZERO/GL_SRC_ALPHA at alpha
         * 12/16 darkens again where two shadow triangles overlap. */
#ifdef A4_TAKESHADOW
        kind = KIND_SHAD;
#ifdef A4_SHADSINK
        shad_sink(gctx, desc);
#endif
#else
        return 0;
#endif
    } else if ((flags & 0x150u) == 0x100u) {
        /* 0x2e56c -> span 0x33ea8: affine textured, dst + (T - dst) * a/16 with a the top
         * nibble of a byte alpha map at desc+0x2c (per texel; no colour key). 0x14100,
         * 1.1% of a full race (the short round-9 census missed it): the roadside chevron
         * boards. RGBA8888 with alpha a*16 and SRC_ALPHA blending -- over the sentinel a
         * part-transparent texel mixes with magenta (leak.py measures it). */
#ifdef A4_TAKEALPHA
        kind = KIND_ALPHA;
#else
        return 0;
#endif
    } else if (flags & 0x150u) {
        return 0;                                    /* 0x10/0x40: none in a race */
    } else {
#ifdef A4_TAKEDEF
        /* The game's DEFAULT rasterizer (0x2e1c8): 0x4000, 0x104000, 0x20000, 0x10000 --
         * ~47% of all triangles in a race. Its span (0x33964) is a colour key -- texel 0
         * leaves the pixel -- which the 5551 upload + alpha test already reproduce; span
         * mode 1 (ctx[0x64]==1) also adds 1/4 of a per-scanline table colour indexed by a
         * second map, not reproduced (drawn as the plain key). Same descriptor layout as
         * flags 1. Mode 1 (ctx[0x64] == 1) is baked into its texture (m1_texel). */
        kind = KIND_TEX;
        m1 = gctx[0x64 / 4] == 1u;
#else
        return 0;
#endif
    }
    volatile struct glr *s = (volatile struct glr *)A4_GL_SCRATCH;
    if (++s->seen < START_AFTER) return 0;           /* past the intro video + menu */
    if (s->up != A4_GL_MAGIC) {
        if (!bringup(s)) return 0;                   /* glr is bss-zeroed */
        s->up = A4_GL_MAGIC;
#ifndef A4_IMM
        s->dual = 1;                                 /* see the return below */
#endif
    }
#ifdef A4_IMM
    /* the frame was composited already (a4_gl_hud: a billboard or the HUD has begun): a
     * triangle now is drawn after it, and the GPU's image of this frame is spent --
     * software, in the game's own order (~25 a frame: shadows and sparks among the
     * billboards, round 12) */
    if (s->flushed) { A4_PERF[PF_LATE]++; return 0; }
#endif
    if (s->prims != 0 && gctx[0x4c / 4] != *(volatile uint32_t *)A4_GL_BB) {
        A4_PERF[PF_SURF]++;                             /* a second 3D target in one frame: software */
        return 0;
    }
    if (s->prims == 0) {                                /* first prim of the frame */
        s->flushed = 0;
        /* The surface this frame's 3D renders into (ctx[0x4c], what the software span
         * reads): the composite writes there. It was taken only from the span itself
         * (a4_cachebb_tramp), which the GPU path stops running -- so after a race the
         * menus' garage, rendered offscreen and copied onto the screen at the start of
         * the next frame, was composited onto the SCREEN, over the 2D drawn before it
         * (round 13-14: the HOME panel and the garage's stats box and bar). */
        *(volatile uint32_t *)A4_GL_BB = gctx[0x4c / 4];
        A4_PERF[PF_MFIRST] = a4_ntick();                /* the 3D pass begins */
#ifdef A4_AGG
        agg_render(A4_PERF[PF_MFIRST]);
#endif
#if defined(A4_TEL) && defined(A4_ABCYCLE10)
        /* round 31: a4_tel.c alternates AB_HSPEC per window; native, skip, reorder, lean, list on */
        A4_PERF[PF_MODE] |= AB_NATIVE | AB_SKIPREP | AB_REORDER | AB_LEAN | AB_MESH;
        s->fmode = A4_PERF[PF_MODE];
#elif defined(A4_TEL) && defined(A4_ABCYCLE9)
        /* round 26: a4_tel.c alternates AB_MESH per window; native, skip, reorder, lean on */
        A4_PERF[PF_MODE] |= AB_NATIVE | AB_SKIPREP | AB_REORDER | AB_LEAN;
        s->fmode = A4_PERF[PF_MODE];
#elif defined(A4_TEL) && defined(A4_ABCYCLE8)
        /* round 24: a4_tel.c alternates AB_ATLAS per window; native, skip, reorder, lean on */
        A4_PERF[PF_MODE] |= AB_NATIVE | AB_SKIPREP | AB_REORDER | AB_LEAN;
        s->fmode = A4_PERF[PF_MODE];
#elif defined(A4_TEL) && defined(A4_ABCYCLE7)
        /* round 23: a4_tel.c steps AB_PLD x AB_BIGSTRIP per window; native, skip, reorder, lean on */
        A4_PERF[PF_MODE] |= AB_NATIVE | AB_SKIPREP | AB_REORDER | AB_LEAN;
        s->fmode = A4_PERF[PF_MODE];
#elif defined(A4_TEL) && defined(A4_ABCYCLE6)
        /* round 22: a4_tel.c alternates AB_DEFER per capture window; native, skip, reorder, lean on */
        A4_PERF[PF_MODE] |= AB_NATIVE | AB_SKIPREP | AB_REORDER | AB_LEAN;
        s->fmode = A4_PERF[PF_MODE];
#elif defined(A4_TEL) && defined(A4_ABCYCLE5)
        /* round 21: a4_tel.c alternates AB_LEAN per capture window; native, skip, reorder on */
        A4_PERF[PF_MODE] |= AB_NATIVE | AB_SKIPREP | AB_REORDER;
        s->fmode = A4_PERF[PF_MODE];
#elif defined(A4_TEL) && defined(A4_ABCYCLE4)
        /* round 20: a4_tel.c alternates AB_INDEXED per capture window; native, skip, reorder on */
        A4_PERF[PF_MODE] |= AB_NATIVE | AB_SKIPREP | AB_REORDER;
        s->fmode = A4_PERF[PF_MODE];
#elif defined(A4_TEL) && defined(A4_ABCYCLE3)
        /* round 19: a4_tel.c alternates AB_SKIPREP per capture window; native, reorder on */
        A4_PERF[PF_MODE] |= AB_NATIVE | AB_REORDER;
        s->fmode = A4_PERF[PF_MODE];
#elif defined(A4_TEL) && defined(A4_ABCYCLE2)
        /* a4_tel.c steps AB_SKIPREP x AB_REORDER per capture window; native readback always */
        A4_PERF[PF_MODE] |= AB_NATIVE;
        s->fmode = A4_PERF[PF_MODE];
#elif defined(A4_TEL) && defined(A4_ABCYCLE)
        s->fmode = A4_PERF[PF_MODE];                   /* a4_tel.c steps it per capture window */
#elif defined(A4_ABFRAME)
        /* bench: AB_MESH flips every A4_ABFRAME frames (from the next frame's inserts; this
         * frame draws whatever list its inserts made), for side-by-side frames of a video */
        {   uint32_t m = A4_ABMODE & ~AB_MESH;
            if ((s->frames / A4_ABFRAME) & 1u) m |= AB_MESH;
            s->fmode = m; A4_PERF[PF_MODE] = m; }
#else
        s->fmode = A4_ABMODE;
        A4_PERF[PF_MODE] = A4_ABMODE;                  /* so the capture header names it */
#endif
        s->miny = VP_H; s->maxy = 0;                    /* empty drawn-row extent */
        /* Composite the previous frame's GPU image NOW, before this frame's
         * software draws (the car, the trees, the HUD) land on top of it in
         * painter's order. The old 3D->2D boundary was the frame's first
         * translucent fill, which a race frame does not have (draw-order trace,
         * MBX.md): the flush fell to the frame end and overwrote the car and
         * the bottom HUD -- the device glitch. The pbuffer still holds the
         * frame drawn at the previous frame's end. */
#ifndef A4_IMM
        { uint32_t t0 = a4_ntick(); composite_prev(s); A4_PERF[PF_COMP] += a4_ntick() - t0; }
#endif
        /* Clear for THIS frame now, after the readback of the previous one, so a
         * batch drawn mid-frame (when the batch buffers fill) is kept. The clear used
         * to be at the frame end, which erased every triangle past the batch size.
         * Alpha 0: with dst alpha (A4_IMM) only an opaque triangle sets it, so it marks
         * what the GPU covered (see composite). */
        gl_clearcolorx(FX, 0, FX, 0);                    /* magenta sentinel (see SENTINEL_*) */
        gl_clear(clear_bits(s));
        s->bfill = 0;
#ifdef A4_BBRECTS
        *(volatile uint32_t *)0x00412E00u = 0;          /* a new frame's sprite rectangles */
#endif
#ifdef A4_MESH
        mesh_draw(s, gctx);                             /* before any of the table's triangles */
#ifdef A4_ABFRAME
        if (MESH_ON()) quad_push(s, 0, 0, 8, 8, 0, 0, 0, 0, (KIND_FLAT << 28) | 0x07E0u, 0);   /* bench: a list frame's mark */
#endif
        /* A4_ABMARK's square is no longer pushed here: see mark_draw (a4_gl_frame) */
#endif
    }

#ifdef A4_MESHCHECK
#ifdef A4_MESHSTATE
    if (MESH_ON()) {
        uint32_t p = (uint32_t)(uintptr_t)desc, j = mesh_hslot(p);
        while (MESH_H[j] && MESH_H[j] != p) j = (j + 1u) & (MESH_HN - 1u);
        if (MESH_H[j] == p) {
            volatile uint32_t *c = MESH_SC;
            uint32_t n = MESH_HX[j] & 0x7FFFu;
            const int16_t *cv = MESH_V + n * 18u;
            c[0]++;
            {   uint32_t md = gctx[0x64 / 4];       /* the span mode the table draws it in */
                c[12 + (md < 3u ? md : 3u)]++;
                if (md && c[48] < 7u) { c[49 + c[48] * 2] = p | (md & 3u); c[50 + c[48] * 2] = desc[12]; c[48]++; } }
            if (MESH_HX[j] & 0x8000u) c[9]++;       /* the walk drew it already this frame */
            else { MESH_HX[j] |= 0x8000u; c[11]++; }  /* distinct faces the walk drew */
            uint32_t tw = MESH_T[n] & ~3u;
            if (desc[12] != tw) { c[1]++; mesh_sc_case(p, 1u, tw, desc[12]); }
            if (desc[1] != 0x4001u && desc[1] != 0x4000u && desc[1] != 0x104000u) { c[4]++; mesh_sc_case(p, 4u, 0, desc[1]); }
            for (uint32_t k = 0; k < 3u; k++) {
                const int32_t *v = (const int32_t *)desc[3 + k * 3];
                if ((int16_t)desc[4 + k * 3] != cv[k * 6 + 3] || (int16_t)desc[5 + k * 3] != cv[k * 6 + 4]) {
                    c[2]++; mesh_sc_case(p, 2u | (k << 8), (uint16_t)cv[k * 6 + 3] | ((uint32_t)(uint16_t)cv[k * 6 + 4] << 16), (desc[4 + k * 3] & 0xFFFFu) | (desc[5 + k * 3] << 16));
                    break;
                }
                if ((int16_t)v[3] != cv[k * 6] || (int16_t)v[4] != cv[k * 6 + 1] || (int16_t)v[5] != cv[k * 6 + 2]) {
                    c[3]++; mesh_sc_case(p, 3u | (k << 8), (uint16_t)cv[k * 6 + 2], (uint32_t)v[5]);
                    break;
                }
            }
        }
    }
#endif
    if (MESH_ON() && mesh_has((uint32_t)(uintptr_t)desc)) return 0;   /* the list drew it on the GPU */
#endif
#ifdef A4_SWONLY
    /* reference build: GL up and the frame counted (so telemetry captures it), but
     * nothing drawn on the GPU -- every triangle is the game's own software, i.e. the
     * stock picture, for comparing captures against */
    s->prims++;
    return 0;
#endif
    uint32_t tsaddr = desc[0x30 / 4];               /* texture descriptor, or flat colour */
    if (!tsaddr && kind != KIND_SHAD) return 0;
#if defined(A4_DEFER) && !defined(A4_M1VIS) && !defined(A4_SEQDUMP) && !defined(A4_VTXDUMP)
    if ((s->fmode & (AB_DEFER | AB_REORDER)) == (AB_DEFER | AB_REORDER)) return defer_take(s, gctx, desc, kind, m1, tsaddr);
#endif
#if defined(A4_LEAN) && !defined(A4_M1VIS) && !defined(A4_SEQDUMP) && !defined(A4_VTXDUMP)
#ifdef A4_DUALCAP
    if (s->fmode & AB_LEAN) { lean_tail(s, gctx, desc, kind, m1, tsaddr); return 0; }   /* software draws it all */
#else
    if (s->fmode & AB_LEAN) return lean_tail(s, gctx, desc, kind, m1, tsaddr);
#endif
#endif
    int wsh = 0, hsh = 0;
    uint32_t id;                                    /* the triangle's TEXID word */
#ifdef A4_M1VIS
    /* diagnosis: mode-1 triangles of a texture of 64K texels or more, flat red */
    if (m1 && (((uint32_t *)tsaddr)[1] + 1) * (((uint32_t *)tsaddr)[2] + 1) >= 65536u) kind = KIND_FLAT, tsaddr = 0xF800u;
#endif
    if (kind == KIND_SHAD) {
        id = KIND_SHAD << 28;                       /* desc+0x30 unused by 0x33f8c */
    } else if (kind == KIND_FLAT) {
        id = (KIND_FLAT << 28) | (tsaddr & 0xFFFFu);
    } else {
        uint32_t *ts = (uint32_t *)tsaddr;
        uint32_t tw = ts[1] + 1, th = ts[2] + 1;
        wsh = tw ? 31 - __builtin_clz(tw) : 0;      /* floor(log2), as the shift loops it replaces */
        hsh = th ? 31 - __builtin_clz(th) : 0;
        int ti = tex_index(s, tsaddr, m1 && kind != KIND_ALPHA ? (const uint16_t *)gctx[0x5c / 4] : 0,
                           m1 && kind != KIND_ALPHA ? m1_table(gctx) : 0, kind == KIND_ALPHA ? 2 : m1);
        if (ti < 0) return 0;
        /* colorkey sprites are GPU-drawn with alpha test (5551 upload), so no software
         * fallback -- s->cache_tr is kept for diagnostics only. */
        id = (kind << 28) | s->cache_id[ti];
    }

    int16_t p[6];
    int32_t uv[3 * UVC];
    const uint32_t voff[3] = { 0x0C, 0x18, 0x24 };
    for (int i = 0; i < 3; i++) {
        uint32_t *vp = (uint32_t *)desc[voff[i] / 4];
        p[i*2]   = (int16_t)(int32_t)vp[0];
        p[i*2+1] = (int16_t)(int32_t)vp[1];
        int32_t un = 0, vn = 0;                     /* flat/shadow triangles sample nothing */
        if (kind == KIND_TEX || kind == KIND_TEXZ || kind == KIND_ADD || kind == KIND_ALPHA) {
            un = ((int32_t)desc[voff[i]/4 + 1] * 4096) >> wsh;      /* U_norm * 65536 */
            vn = ((int32_t)desc[voff[i]/4 + 2] * 4096) >> hsh;      /* V_norm * 65536 */
        }
#if defined(A4_PERSP) && !defined(A4_HPERSP)
        /* q = 1/w (ZW is w: it grows with distance, measured on road triangles --
         * MBX.md "Measured the perspective input"). s = U_norm*q, t = V_norm*q
         * (GL_FIXED); a GL that divides (s/q, t/q) PER FRAGMENT is then
         * perspective-correct. The absolute scale of q cancels in the division, so any
         * fixed multiple of 1/ZW works; 2^22 keeps q and s*q in range for ZW in
         * ~[8, 2^16]. (Round 7 briefly used vp[2] directly as q on a sky/building
         * sample that does not track depth -- wrong; reverted.) */
        int32_t zw = (int32_t)vp[2];
        uint32_t iw = a4_udiv(1u << 22, (uint32_t)(zw > 0 ? zw : 1));
        uv[i*UVC + 0] = (int32_t)(((int64_t)un * (int32_t)iw) >> 16);
        uv[i*UVC + 1] = (int32_t)(((int64_t)vn * (int32_t)iw) >> 16);
        uv[i*UVC + 2] = 0;
        uv[i*UVC + 3] = (int32_t)iw;
#else
        uv[i*UVC + 0] = un;
        uv[i*UVC + 1] = vn;
#endif
    }
    /* grow the frame's drawn-row extent, so the readback covers only the rows
     * the GPU touched (the readback, not the blit, is the composite's cost). */
    {
        int32_t ylo = p[1], yhi = p[1];
        for (int i = 1; i < 3; i++) {
            if (p[i*2+1] < ylo) ylo = p[i*2+1];
            if (p[i*2+1] > yhi) yhi = p[i*2+1];
        }
        if (ylo < 0) ylo = 0; else if (ylo >= VP_H) ylo = VP_H - 1;
        if (yhi < 0) yhi = 0; else if (yhi >= VP_H) yhi = VP_H - 1;
        if ((uint32_t)ylo < s->miny) s->miny = (uint32_t)ylo;
        if ((uint32_t)yhi > s->maxy) s->maxy = (uint32_t)yhi;
    }

#ifdef A4_HPERSP
    /* homogeneous positions (x*W, y*W, 0, W), W proportional to ZW. One shift for the
     * whole triangle -- the perspective weighting depends only on the ratios of W
     * across it -- chosen so |x|*W, |y|*W < 2^26 (GL_FIXED, headroom for a fixed-point
     * vertex pipeline). Bit lengths, not a 64-bit multiply: the blob has no libgcc. */
    int32_t P[12];
    {
        uint32_t zw[3], zmax = 1, m = 1;
        for (int i = 0; i < 3; i++) {
            int32_t z = (int32_t)((uint32_t *)desc[voff[i] / 4])[2];
            zw[i] = (uint32_t)(z > 0 ? z : 1);
            if (zw[i] > zmax) zmax = zw[i];
            uint32_t ax = (uint32_t)(p[i*2]   < 0 ? -p[i*2]   : p[i*2]);
            uint32_t ay = (uint32_t)(p[i*2+1] < 0 ? -p[i*2+1] : p[i*2+1]);
            if (ax > m) m = ax;
            if (ay > m) m = ay;
        }
        int shift = (26 - bitlen(m)) - bitlen(zmax);
        /* depth (A4_ZBUF, flags 1 only): z_ndc = ZK/ZW, the game's 1/z up to scale. The
         * ortho maps z_clip = -z, so z = -ZK*2^shift over w = ZW*2^shift; clamped to w so
         * z_ndc <= 1 and no vertex is ever clipped by the depth range (a near vertex just
         * saturates). Window depth is interpolated linearly on screen, as 1/z is. */
        int32_t zq = 0;
        if (kind == KIND_TEXZ) zq = shift >= 0 ? (ZK << shift) : (ZK >> -shift);
        for (int i = 0; i < 3; i++) {
            int32_t W = (int32_t)(shift >= 0 ? (zw[i] << shift) : (zw[i] >> -shift));
            if (W < 1) W = 1;
            P[i*4+0] = (int32_t)p[i*2]   * W;
            P[i*4+1] = (int32_t)p[i*2+1] * W;
            P[i*4+2] = -(zq < W ? zq : W);
            P[i*4+3] = W;
        }
    }
#endif
#ifdef A4_VTXDUMP
    /* perspective diagnosis: record each triangle's 3 (screen-y, vp[2]) pairs so we can
     * see offline whether vp[2] grows or shrinks toward the bottom of the screen (z vs
     * 1/z). Table at 0x400B00: [0]=count, then count*6 words (y0,zw0,y1,zw1,y2,zw2).
     * Reset at the frame's first prim (prims==0). Read via MEMSNAP. */
    {
        volatile uint32_t *vt = (volatile uint32_t *)0x00400B00u;
        uint32_t vn2 = vt[0];                /* accumulate across frames: catches ground tris */
        if (vn2 < 80u) {
            for (int i = 0; i < 3; i++) {
                uint32_t *vpd = (uint32_t *)desc[voff[i] / 4];
                vt[1 + vn2*6 + i*2 + 0] = (uint32_t)(int32_t)(int16_t)(int32_t)vpd[1];
                vt[1 + vn2*6 + i*2 + 1] = vpd[2];
            }
            vt[0] = vn2 + 1;
        }
    }
#endif
#ifdef A4_SEQDUMP
    {
        int32_t x0 = p[0], x1 = p[0], y0 = p[1], y1 = p[1];
        uint32_t inr = 1, tw = 0, th = 0;
        for (int i = 1; i < 3; i++) {
            if (p[i*2] < x0) x0 = p[i*2]; if (p[i*2] > x1) x1 = p[i*2];
            if (p[i*2+1] < y0) y0 = p[i*2+1]; if (p[i*2+1] > y1) y1 = p[i*2+1];
        }
        for (int i = 0; i < 3; i++)
            if (uv[i*UVC] < 0 || uv[i*UVC] > 65536 || uv[i*UVC+1] < 0 || uv[i*UVC+1] > 65536) inr = 0;
        if (kind != KIND_SHAD && kind != KIND_FLAT) { tw = ((uint32_t *)tsaddr)[1] + 1; th = ((uint32_t *)tsaddr)[2] + 1; }
        else inr = 0;
        seq_add(id, x0, y0, x1, y1, inr | (tw << 1) | (th << 12));
    }
#endif
    if (s->bfill >= MAXT) {                         /* batch full: draw it, then refill */
        uint32_t t0 = a4_ntick(); batch_draw(s->bfill); A4_PERF[PF_DRAW] += a4_ntick() - t0;
        s->bfill = 0;
    }
    uint32_t n = s->bfill;
    uint32_t vb = n * 3;                            /* first vertex of this triangle */
#ifdef A4_REORDER
    {
        int32_t x0 = p[0], x1 = p[0], y0 = p[1], y1 = p[1];
        for (int i = 1; i < 3; i++) {
            if (p[i*2] < x0) x0 = p[i*2];
            if (p[i*2] > x1) x1 = p[i*2];
            if (p[i*2+1] < y0) y0 = p[i*2+1];
            if (p[i*2+1] > y1) y1 = p[i*2+1];
        }
        tbox_set(n, x0, y0, x1, y1);
    }
#endif
#ifdef A4_HPERSP
    int32_t  *POS = (int32_t  *)A4_GL_POS;
    for (int k = 0; k < 12; k++) POS[vb*4 + k] = P[k];
#else
    int16_t  *POS = (int16_t  *)A4_GL_POS;
    for (int k = 0; k < 6; k++) POS[vb*2 + k] = p[k];
#endif
    int32_t  *UV  = (int32_t  *)A4_GL_UV;
    uint32_t *TEXID = (uint32_t *)A4_GL_TEXID;
    for (uint32_t k = 0; k < 3u*UVC; k++) UV[vb*UVC + k] = uv[k];
    TEXID[n] = id;
    s->bfill = n + 1;
    s->prims++;                                     /* the frame's total (not capped) */
#if defined(A4_LEANCHECK) && defined(A4_LEAN)
    {   /* bench check: the lean path's words for this triangle against what this path just
         * wrote into slot n. 0x412D00 [106] triangles differing, [107] checked, [108] the
         * first differing word's index (0-11 pos, 12-17 uv, 18 id, 19-22 box, 23-24 rows) */
        volatile uint32_t *ck = (volatile uint32_t *)0x00412D00u;
        int32_t lp[12], lu[6], yr[2]; uint32_t lid = 0; int16_t lb[4];
        int ok = lean_conv(s, gctx, desc, kind, m1, tsaddr, lp, lu, &lid, lb, yr);
        const int16_t *ob = (const int16_t *)A4_GL_TBOX + n * 4u;
        int bad = -1;
        for (int k = 0; k < 12 && bad < 0; k++) if (lp[k] != POS[vb*4 + k]) bad = k;
        for (int k = 0; k < 6 && bad < 0; k++) if (lu[k] != UV[vb*UVC + k]) bad = 12 + k;
        if (bad < 0 && lid != id) bad = 18;
        for (int k = 0; k < 4 && bad < 0; k++) if (lb[k] != ob[k]) bad = 19 + k;
        if (bad < 0 && ((uint32_t)yr[0] < s->miny || (uint32_t)yr[1] > s->maxy)) bad = 23;
        if (!ok) bad = 99;
        if (bad >= 0) { if (!ck[106]) ck[108] = (uint32_t)bad; ck[106]++; }
        ck[107]++;
    }
#endif
    if (kind == KIND_FLAT) A4_PERF[PF_FLAT]++;
    else if (kind == KIND_ADD) A4_PERF[PF_ADD]++;
    else if (kind == KIND_SHAD) A4_PERF[PF_SHAD]++;
    /* The first GPU frame has no previous GPU image to composite, so its GPU-taken
     * triangles would be missing for a frame (the countdown flash: 76% of the scene,
     * round 9). Let software draw that frame as well; the GPU still draws it, and the
     * next frame composites it as usual. */
#ifdef A4_DUALCAP
    return 0;                                       /* bench instrument: software draws it all */
#elif defined(A4_IMM)
    /* Blended classes are drawn by software as well: the GPU's version is right where it
     * blends over GPU-drawn pixels, software's where it blends over the backdrop -- the
     * composite takes the GPU's only where an opaque GPU triangle set dst alpha (the
     * blends keep it, draw_run). */
    return (kind == KIND_ADD || kind == KIND_ALPHA) ? 0 : 1;
#else
    return s->dual ? 0 : 1;
#endif
}

int a4_gl_draw_prim(uint32_t *gctx, uint32_t *desc, uint32_t flags)
{
#ifdef A4_TEL
    /* every 4th call timed (NTickCount is 1 ms: summed over many calls the count of ticks
     * crossed is an unbiased measure of the time inside), x4 */
    int r;
    if ((++A4_PERF[PF_HOOKN] & 3u) == 0 && ((volatile struct glr *)A4_GL_SCRATCH)->up == A4_GL_MAGIC) {
        uint32_t t0 = a4_ntick();
        r = draw_prim_take(gctx, desc, flags);
        A4_PERF[PF_HOOK] += (a4_ntick() - t0) * 4u;
    } else
        r = draw_prim_take(gctx, desc, flags);
#else
    int r = draw_prim_take(gctx, desc, flags);
#endif
#ifdef A4_SWHIST
    /* diagnosis: the census of the calls left to SOFTWARE (returned 0), by (flags,
     * ctx[0x64]), in A4_FLAGHIST's table format at 0x412D00 -- under A4_IMM what software
     * draws before the composite is overwritten wherever the GPU drew. The blended
     * classes (0x8, 0x100) are there too: software draws them by design. */
    if (!r && ((volatile struct glr *)A4_GL_SCRATCH)->up == A4_GL_MAGIC) {   /* in the race */
        volatile uint32_t *fh = (volatile uint32_t *)0x00412D00u;
        uint32_t md = gctx[0x64 / 4], nn = fh[0], i;
        for (i = 0; i < nn; i++) if (fh[1+i*3] == flags && fh[2+i*3] == md) break;
        if (i == nn) { if (nn < 40u) { fh[1+i*3] = flags; fh[2+i*3] = md; fh[3+i*3] = 0; fh[0] = nn + 1; } else i = 99; }
        if (i < 40u) fh[3+i*3]++; else fh[121]++;
    }
#endif
    return r;
}

/* A screen rectangle [x0,x1) x [y0,y1) into the frame's batch as two triangles, after
 * whatever came before it -- the 2D things the game draws into its surface (billboards,
 * fills) keep their place among the triangles. (S0,T0) at (x0,y0) to (S1,T1) at (x1,y1),
 * normalised GL_FIXED texcoords; a flat id ignores them. */
static void quad_push(volatile struct glr *s, int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                      int32_t S0, int32_t T0, int32_t S1, int32_t T1, uint32_t id, int32_t zw) __attribute__((unused));
static void quad_push(volatile struct glr *s, int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                      int32_t S0, int32_t T0, int32_t S1, int32_t T1, uint32_t id, int32_t zw)
{
    /* zw: the quad's distance (a TBillboard's, a4_gl_bb), 0 for none. In a mesh frame a quad
     * with one tests it -- positions (x zw, y zw, -min(ZK, zw), zw): depth ZK/zw, no divide,
     * the same w at all four corners so the texture stays affine -- and one without sits at
     * window depth 1, passing every test as it was drawn untested before. */
    int32_t qw = 4096, qz = 0;
#ifdef A4_MESH
    if (MESH_ON()) {
        if (zw > 0 && zw < 32768) { qw = zw; qz = -(zw < ZK ? zw : ZK); }
        else qz = -4096;
    }
#else
    (void)zw;
#endif
    (void)qw; (void)qz;                             /* used by the A4_HPERSP positions only */
    int32_t vx[4] = { x0, x1, x1, x0 }, vy[4] = { y0, y0, y1, y1 };
    int32_t vs[4] = { S0, S1, S1, S0 }, vt[4] = { T0, T0, T1, T1 };
    int ord[6] = { 0, 1, 2, 0, 2, 3 };
    for (int t = 0; t < 2; t++) {
        if (s->bfill >= MAXT) {
            uint32_t t0 = a4_ntick(); batch_draw(s->bfill); A4_PERF[PF_DRAW] += a4_ntick() - t0;
            s->bfill = 0;
        }
        uint32_t n = s->bfill, vb = n * 3;
#ifdef A4_DEFER
        if ((s->fmode & (AB_DEFER | AB_REORDER)) == (AB_DEFER | AB_REORDER)) {   /* batch_draw converts only under the reorder */
            uint32_t *r = DEFR(n);
            int32_t bx0 = x0 < x1 ? x0 : x1, by0 = y0 < y1 ? y0 : y1;
            int32_t bx1 = (x0 < x1 ? x1 : x0) - 1, by1 = (y0 < y1 ? y1 : y0) - 1;
            r[0] = DEF_QUAD; r[1] = id;
            r[2] = ((uint32_t)bx0 & 0xFFFFu) | ((uint32_t)by0 << 16); r[3] = ((uint32_t)bx1 & 0xFFFFu) | ((uint32_t)by1 << 16);
            for (int k = 0; k < 3; k++) {
                int q = ord[t * 3 + k];
                r[4 + k * 2] = (uint32_t)vx[q]; r[5 + k * 2] = (uint32_t)vy[q];
                r[10 + k * 2] = (uint32_t)vs[q]; r[11 + k * 2] = (uint32_t)vt[q];
            }
#ifdef A4_DEFERCHECK
            {   uint32_t *sh = (uint32_t *)0x00423000u + n * 24u;
                for (int k = 0; k < 3; k++) {
                    int q = ord[t * 3 + k];
                    sh[k*4] = (uint32_t)(vx[q] * 4096); sh[k*4+1] = (uint32_t)(vy[q] * 4096); sh[k*4+2] = 0; sh[k*4+3] = 4096;
                    sh[12 + k*2] = (uint32_t)vs[q]; sh[13 + k*2] = (uint32_t)vt[q];
                }
                sh[18] = id; sh[19] = 1; tbox_calc((int16_t *)(sh + 20), bx0, by0, bx1, by1); sh[22] = 0; sh[23] = 0; }
#endif
            s->bfill = n + 1;
            s->prims++;
            continue;
        }
#endif
#ifdef A4_REORDER
        /* the quad covers [x0, x1) x [y0, y1); every caller has x0 < x1, y0 < y1, but a
         * reversed quad still draws, and a box read as empty would let it pass anything */
        tbox_set(n, x0 < x1 ? x0 : x1, y0 < y1 ? y0 : y1, (x0 < x1 ? x1 : x0) - 1, (y0 < y1 ? y1 : y0) - 1);
#endif
        for (int k = 0; k < 3; k++) {
            int q = ord[t * 3 + k];
#ifdef A4_HPERSP
            int32_t *POS = (int32_t *)A4_GL_POS;                /* (x*W, y*W, 0, W), W = 4096 */
            POS[(vb + k) * 4 + 0] = vx[q] * qw; POS[(vb + k) * 4 + 1] = vy[q] * qw;
            POS[(vb + k) * 4 + 2] = qz;         POS[(vb + k) * 4 + 3] = qw;
#else
            int16_t *POS = (int16_t *)A4_GL_POS;
            POS[(vb + k) * 2 + 0] = (int16_t)vx[q]; POS[(vb + k) * 2 + 1] = (int16_t)vy[q];
#endif
            int32_t *UV = (int32_t *)A4_GL_UV;
            UV[(vb + k) * UVC + 0] = vs[q]; UV[(vb + k) * UVC + 1] = vt[q];
#if UVC == 4
            UV[(vb + k) * UVC + 2] = 0; UV[(vb + k) * UVC + 3] = FX;
#endif
        }
        ((uint32_t *)A4_GL_TEXID)[n] = id;
        s->bfill = n + 1;
        s->prims++;
    }
#ifdef A4_SEQDUMP
    seq_add(id, x0, y0, x1 - 1, y1 - 1, 0u);        /* one entry, two triangles in the batch */
    seq_add(id, x0, y0, x1 - 1, y1 - 1, 0u);
#endif
    int32_t ya = y0 < 0 ? 0 : y0, yb = y1 > VP_H ? VP_H - 1 : y1 - 1;
    if ((uint32_t)ya < s->miny) s->miny = (uint32_t)ya;
    if (yb >= 0 && (uint32_t)yb > s->maxy) s->maxy = (uint32_t)yb;
}

/* The billboard blit's texture (sprite +0x10: 565 texels, rows of 1 << sprite[0x1c]
 * texels, the row index masked by sprite[8]): cached under sprite|3 (descriptors are
 * word aligned and keyed |0 |1 |2), uploaded by tex_upload through a made-up
 * descriptor -- 5551 with alpha 0 on texel 0 when it has any (mode 0 keys it out; the
 * additive modes add nothing for it either way). -1: software draws it. */
static int bb_tex(volatile struct glr *s, const uint32_t *spr) __attribute__((unused));
static int bb_tex(volatile struct glr *s, const uint32_t *spr)
{
    uint32_t key = (uint32_t)(uintptr_t)spr | 3u;
    uint32_t vsh = spr[0x1c / 4], rows = spr[8 / 4] + 1u;
    const uint16_t *px = (const uint16_t *)spr[0x10 / 4];
    if (!px || vsh > 10u || rows > 1024u || (rows & (rows - 1u))) return -1;
    uint32_t pitch = 1u << vsh, total = pitch * rows;
    uint32_t fd[5] = { 0, pitch - 1u, rows - 1u, 0, (uint32_t)(uintptr_t)px };
    for (uint32_t k = 0; k < s->ntex; k++)
        if (s->cache_ptr[k] == key && TX->src[k] == fd[4]) {      /* the texels' address keys it too */
            if (TX->wh[k] != (fd[1] | (fd[2] << 16))
                || (TX->lu[k] != s->frames && TX->hs[k] != tex_hash(px, total, 0))) {
                tex_make(s, (int)k, s->cache_id[k], fd, 0, 0, 0);    /* another sprite here now */
                A4_PERF[PF_TEXRE]++;
            }
            TX->lu[k] = s->frames;
            return (int)k;
        }
    uint32_t id;
    int idx = cache_slot(s, &id);
    if (idx < 0) return -1;
    s->cache_ptr[idx] = key; s->cache_tab[idx] = 0;
    tex_make(s, idx, id, fd, 0, 0, 0);
    return idx;
}

void a4_gl_flush(void);

/* The scaled billboard blit (+3015E) at +30252 (a4_bbq_tramp): r = r0..r7 there, f = its
 * sp. Its clip is done: the screen rectangle is [f+0x34, r4) x [r6, f+0x30); u starts at
 * r0 and steps r5 a pixel, v starts at r1 and steps -[f+0x28] a row (8.8 texels); the
 * mode is stack argument 1 (f+0x94: 0 key copy, 1 stipple, 2 saturating add, 3 add at
 * a/16 with a stack argument 2, f+0x98); the surface is ctx[0x4c] (ctx = f+0x6c), its
 * width f+0x64. Queued as a quad in the frame's batch -- so in draw order with the
 * triangles around it -- and 1 returned (the blit returns at once), or 0 (it runs). */
#if defined(A4_MESH) && defined(A4_TAKEBB)
/* The depth a TBillboard drawn over [x0, x1) x [y0, y1) is tested at, or 0 (untested):
 * the game paints it after every face of a farther bucket and under every face of a nearer
 * one. Its bucket as +349B0 takes it (key +31368: bb+0x18, less 0x20 for flag bit 13; key
 * >> 3, -16 for flag 0x10000, +16 for 0x20000); the list's faces of strictly nearer buckets
 * (a tie's order is the insertion's, unknown here) are its tail in table order (MESH_ORD,
 * back to front), found by binary search, and those whose box (MESH_B, padded) meets the
 * rectangle cover it. None: 0. Some: the far edge of the deepest such bucket -- every
 * covering face is nearer than that, while the road behind them, which a test at the
 * sprite's own distance lost to, mostly is not (round 28). b1 = bb + 0x10. */
static int32_t mesh_bb_depth(const int32_t *b1, int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    uint32_t fl = (uint32_t)b1[-3];
    int32_t sb = (b1[2] - ((fl >> 13) & 1u ? 0x20 : 0)) >> 3;
    if (fl & 0x10000u) sb -= 16;
    if (fl & 0x20000u) sb += 16;
    if (sb <= 0) return 0;                          /* nothing nearer than bucket 0 */
    {   /* the tile grid first (mesh_draw): no tile under the rectangle holds a nearer list
         * bucket, no cover -- most sprites, without walking the list's tail */
        if (x1 <= 0 || y1 <= 0 || x0 >= VP_W || y0 >= VP_H) return 0;
        int32_t tx0 = x0 < 0 ? 0 : x0 >> 4, tx1 = x1 > VP_W ? MESH_GW - 1 : (x1 - 1) >> 4;
        int32_t ty0 = y0 < 0 ? 0 : y0 >> 4, ty1 = y1 > VP_H ? MESH_GH - 1 : (y1 - 1) >> 4;
        const uint16_t *g = (const uint16_t *)MESH->grid;
        int near = 0;
        for (int32_t ty = ty0; ty <= ty1 && !near; ty++)
            for (int32_t tx = tx0; tx <= tx1; tx++)
                if ((int32_t)g[ty * MESH_GW + tx] < sb) { near = 1; break; }
        if (!near) return 0;
    }
    const uint16_t *ord = MESH_ORD, *Q = MESH_Q;
    const uint8_t *K = MESH_K;
    const int16_t *B = MESH_B;
    uint32_t n = MESH->n, lo = 0, hi = n;
    while (lo < hi) {                               /* the first face of a bucket below sb */
        uint32_t mid = (lo + hi) >> 1;
        if ((int32_t)Q[ord[mid]] >= sb) lo = mid + 1; else hi = mid;
    }
    int32_t deep = -1;                              /* the deepest covering bucket */
    for (uint32_t t = lo; t < n; t++) {
        uint32_t i = ord[t];
        if (K[i] == 0xFFu) continue;                /* not drawn (its texture failed) */
        const int16_t *b = B + i * 4u;
        if (b[2] < x0 || b[0] >= x1 || b[3] < y0 || b[1] >= y1) continue;
#ifdef A4_BBSINK
        {   /* bench: the covers found -- 0x454100 [48] count, then 3 of (bucket | sprite bucket
             * << 16, flags, face box x0 y0, x1 y1) */
            volatile uint32_t *k = (volatile uint32_t *)0x00454100u;
            uint32_t c = k[48];
            if (c < 3u) { k[49 + c * 4] = Q[i] | ((uint32_t)sb << 16); k[50 + c * 4] = fl;
                          k[51 + c * 4] = (uint16_t)b[0] | ((uint32_t)(uint16_t)b[1] << 16); k[52 + c * 4] = (uint16_t)b[2] | ((uint32_t)(uint16_t)b[3] << 16); }
            k[48] = c + 1u;
        }
#endif
        deep = Q[i];                                /* the tail runs deep to near: the first */
        break;                                      /* cover found is the deepest */
    }
    return deep < 0 ? 0 : (deep + 1) * 8;
}
#endif
int a4_gl_bb(const uint32_t *r, const uint32_t *f)
{
    const uint32_t *spr = (const uint32_t *)r[7];
    int32_t x0 = (int32_t)f[0x34 / 4], x1 = (int32_t)r[4], y0 = (int32_t)r[6], y1 = (int32_t)f[0x30 / 4];
    uint32_t mode = f[0x94 / 4], fade = f[0x98 / 4];
#ifdef A4_BBPROBE
    {   /* diagnosis, 0x412D00 (A4_FLAGHIST's tail): [0] sprites seen, [1] calls, [2..5]
         * calls by mode 0-3, [6] another surface or width, [7] empty rectangles, [8] [9]
         * widest and tallest rectangle; then 12 sprites of 9 words: address, w | h << 16,
         * u mask (+4), v mask (+8), row shift (+0x1c), texels (+0x10), mode | a << 8,
         * calls, the size shifts +0x14 | +0x18 << 16 -- only sprites drawn in a mode
         * other than 2 (round 16: all 9,566 bench calls were mode 2); [120] composites
         * forced by a billboard left to software, [121] outlined fills drawn on the GPU */
        volatile uint32_t *bp = (volatile uint32_t *)0x00412D00u;
        const uint32_t *ctx = (const uint32_t *)f[0x6c / 4];
        bp[1]++; if (mode < 4u) bp[2 + mode]++;
        if (ctx[0x4c / 4] != *(volatile uint32_t *)A4_GL_BB || f[0x64 / 4] != (uint32_t)VP_W) bp[6]++;
        if (x0 >= x1 || y0 >= y1) bp[7]++;
        if ((uint32_t)(x1 - x0) > bp[8] && x1 > x0) bp[8] = (uint32_t)(x1 - x0);
        if ((uint32_t)(y1 - y0) > bp[9] && y1 > y0) bp[9] = (uint32_t)(y1 - y0);
        uint32_t nn = bp[0], i;
        for (i = 0; i < nn; i++) if (bp[10 + i*9] == (uint32_t)(uintptr_t)spr) break;
        if (i == nn && nn < 12u && mode != 2u) {
            volatile uint32_t *e = bp + 10 + i*9;
            e[0] = (uint32_t)(uintptr_t)spr; e[1] = spr[0]; e[2] = spr[1]; e[3] = spr[2];
            e[4] = spr[7]; e[5] = spr[4]; e[6] = mode | (fade << 8); e[7] = 0; e[8] = spr[5] | (spr[6] << 16);
            bp[0] = nn + 1;
        }
        if (i < nn) bp[10 + i*9 + 7]++;
    }
#endif
#ifdef A4_TAKEBB
    volatile uint32_t *iat = a4_gl_iat_ptr();
    if (iat[0] < 0x10000u) return 0;
    volatile struct glr *s = (volatile struct glr *)A4_GL_SCRATCH;
    if (s->up != A4_GL_MAGIC || s->prims == 0) return 0;      /* no GPU frame begun */
    if (s->flushed) { A4_PERF[PF_BBSW]++; return 0; }          /* composited already: software, in order */
    const uint32_t *ctx = (const uint32_t *)f[0x6c / 4];
    if (ctx[0x4c / 4] != *(volatile uint32_t *)A4_GL_BB || f[0x64 / 4] != (uint32_t)VP_W
        || x0 >= x1 || y0 >= y1) { A4_PERF[PF_BBSW]++; return 0; }  /* not into this frame's 3D */
    int ti = (mode == 0u || mode == 2u || mode == 3u) ? bb_tex(s, spr) : -1;
    if (ti < 0) {
        /* Drawn by software into the 3D surface (mode 1, the stipple, or a texture the
         * cache refused): composite first, so it lands on the frame's GPU image instead
         * of under it -- before, it survived only where the GPU drew nothing (the sky:
         * round 15's band at the lap line, inferred). What follows is software, in order. */
        A4_PERF[PF_BBSW]++;
#ifdef A4_BBPROBE
        ((volatile uint32_t *)0x00412D00u)[120]++;
#endif
#ifdef A4_IMM
        a4_gl_flush();
#endif
        return 0;
    }
    /* The blit's caller: TBillboard's draw (+312E6) returns to +31364, and passed r1 =
     * bb + 0x10, whose bb[0x18] is its distance (its sort key, +31368; ZW units, MBX.md
     * round 26). The blit pushed {r0-r7, lr} under its frame: r1 at f+0x70, lr at f+0x8C.
     * Other callers' quads keep no depth. */
    int32_t bbz = 0, bbzd = 0;                     /* the depth used, the TBillboard's (detected) */
#if defined(A4_MESH)
    {   extern const int32_t a4_bbret;
        if (f[0x8c / 4] == (uint32_t)(uintptr_t)&a4_bbret + (uint32_t)a4_bbret) {
            const int32_t *b1 = (const int32_t *)f[0x70 / 4];   /* bb + 0x10 */
            bbzd = b1[2];
            if (bbzd < MESH_NEAR + 16 * 8) MESH->nearf = s->frames + 1u;   /* a near sprite (its bucket may
                                                     * be up to 16 farther, flag 0x20000): the near cut on */
#ifndef MESH_NOBBZ                                  /* MESH_NOBBZ: bench, round 26's sprites (no depth) */
            /* Round 28: the game paints a sprite over everything drawn before it, and its
             * distance word is not where it shows -- the nitro flame is a trail of quads at
             * 191-230 over road rows far nearer (MBX.md). Tested, it sank into the table's
             * road, which writes depth in a list frame. So test it only where the game would
             * cover it: a list face of a nearer bucket over its rectangle (round 26's car
             * lights); otherwise window depth 1, untested, as in a frame without the list. */
#ifdef MESH_BBALL
            bbz = bbzd;                             /* bench: round 27's rule, every sprite tested */
#else
            if (MESH_ON()) {
                int32_t dz = mesh_bb_depth(b1, x0, y0, x1, y1);
                if (dz) bbz = dz < bbzd ? dz : bbzd;
            }
#endif
#endif
        }
    }
#endif
    (void)bbzd;
#ifdef A4_BBSINK
    if (bbzd > 0 && bbzd < 32768) {
        /* bench (check build: software draws the frame, its 1/z buffer holds every
         * perspective face drawn so far): the sprite's opaque pixels where something drawn
         * BEFORE it is nearer than its depth -- the game paints a sprite over all of that, a
         * depth-tested quad loses there. [0] sprites, [1] opaque pixels, [2..6] hidden at
         * depth zw * (1 - f), f = 0, 1/16, 1/8, 1/4, 1/2; [7] rows counted, [8..11] hidden
         * at f = 0 by quarter of the sprite's height, top to bottom; then 8 sprites of
         * (texture, opaque, hidden at 0, opaque of the depth-tested ones); [44..46] the
         * depth-tested sprites, their opaque pixels, those hidden at f = 0 */
        volatile uint32_t *k = (volatile uint32_t *)0x00454100u;
        const uint32_t *gc = (const uint32_t *)MESH->gctx;
        const int32_t *zb = gc ? (const int32_t *)gc[0x40118 / 4] : 0;
        if (zb) {
            const int32_t *rt = (const int32_t *)((uintptr_t)gc + 0x8Cu);
            int32_t zt[5];
            for (int q = 0; q < 5; q++) {
                int32_t w = q == 0 ? bbzd : q == 1 ? bbzd - (bbzd >> 4) : q == 2 ? bbzd - (bbzd >> 3)
                          : q == 3 ? bbzd - (bbzd >> 2) : bbzd - (bbzd >> 1);
                zt[q] = rt[w < 1 ? 1 : w];
            }
            const uint16_t *tex = (const uint16_t *)spr[0x10 / 4];
            uint32_t um = spr[4 / 4], vm = spr[8 / 4], vs = spr[0x1c / 4];
            int32_t uu0 = (int32_t)r[0], vv0 = (int32_t)r[1], ddu = (int32_t)r[5], ddv = (int32_t)f[0x28 / 4];
            int32_t cx0 = x0 < 0 ? 0 : x0, cx1 = x1 > VP_W ? VP_W : x1, cy0 = y0 < 0 ? 0 : y0, cy1 = y1 > VP_H ? VP_H : y1;
            uint32_t op = 0, hid0 = 0, hid2 = 0, h = (uint32_t)(y1 - y0);
            k[0]++;
            for (int32_t y = cy0; y < cy1; y++) {
                int32_t v = vv0 - (y - y0) * ddv;
                uint32_t dy4 = (uint32_t)(y - y0) * 4u;  /* the quarter, without a divide */
                uint32_t qr = (dy4 >= h) + (dy4 >= 2u * h) + (dy4 >= 3u * h);
                for (int32_t x = cx0; x < cx1; x++) {
                    int32_t u = uu0 + (x - x0) * ddu;
                    if (!tex[(((uint32_t)v >> 8) & vm) << vs | (((uint32_t)u >> 8) & um)]) continue;
                    int32_t z = zb[y * VP_W + x];
                    op++;
                    for (int q = 0; q < 5; q++) if (z > zt[q]) k[2 + q]++;
                    if (z > zt[0]) { hid0++; if (qr < 4u) k[8 + qr]++; }
                    (void)hid2;
                }
            }
            k[1] += op;
            if (bbz) {                              /* depth-tested ones: hidden at the depth used */
                int32_t zu = rt[bbz < 1 ? 1 : bbz > 32767 ? 32767 : bbz];
                uint32_t hu = 0;
                for (int32_t y = cy0; y < cy1; y++) {
                    int32_t v = vv0 - (y - y0) * ddv;
                    for (int32_t x = cx0; x < cx1; x++) {
                        int32_t u = uu0 + (x - x0) * ddu;
                        if (tex[(((uint32_t)v >> 8) & vm) << vs | (((uint32_t)u >> 8) & um)] && zb[y * VP_W + x] > zu) hu++;
                    }
                }
                k[44]++; k[45] += op; k[46] += hu;
                if (bbzd < 512) { k[60]++; k[61] += op; k[62] += hu; }   /* the near ones (the flame) */
            }
            uint32_t i;
            for (i = 0; i < 8u; i++) if (k[12 + i * 4] == (uint32_t)(uintptr_t)tex || !k[12 + i * 4]) break;
            if (i < 8u) { k[12 + i * 4] = (uint32_t)(uintptr_t)tex; k[13 + i * 4] += op; k[14 + i * 4] += hid0; if (bbz) k[15 + i * 4] += op; }
        }
    }
#endif
#ifdef A4_BBRECTS
    {   /* bench: this frame's TBillboard sprites, for comparing the two pictures inside them --
         * 0x412E00: [0] count, then up to 31 of (x0 | y0 << 16, x1 | y1 << 16, zw, mode) */
        volatile uint32_t *br = (volatile uint32_t *)0x00412E00u;
        uint32_t nb = br[0];
        if (bbzd > 0 && nb < 31u) {
            br[1 + nb * 4] = ((uint32_t)x0 & 0xFFFFu) | ((uint32_t)y0 << 16);
            br[2 + nb * 4] = ((uint32_t)x1 & 0xFFFFu) | ((uint32_t)y1 << 16);
            br[3 + nb * 4] = (uint32_t)bbzd; br[4 + nb * 4] = mode;
            br[0] = nb + 1;
        }
    }
#endif
    uint32_t kind = mode == 0u ? KIND_TEX : mode == 2u ? KIND_ADD : KIND_ADDF;
    uint32_t id = (kind << 28) | s->cache_id[ti];
    if (kind == KIND_ADDF) id |= (fade > 16u ? 16u : fade) << 20;
    /* u at pixel x0 + k is u0 + k*du; v at row y0 + j is v0 - j*dv (the sprite runs
     * bottom-up). GL samples at pixel centres, so the quad's edges sit half a step out;
     * plus 1/32 texel, so a texel boundary floors as the blit's >> 8 does. Normalised
     * GL_FIXED: c(8.8) * 65536 / (256 * size) = c * 2^(8 - log2 size). */
    int32_t u0 = (int32_t)r[0], v0 = (int32_t)r[1], du = (int32_t)r[5], dv = (int32_t)f[0x28 / 4];
    int32_t sl = u0 - du / 2 + 8, sr = sl + (x1 - x0) * du;
    int32_t tt = v0 + dv / 2 + 8, tb = tt - (y1 - y0) * dv;
    int wl = (int)spr[0x1c / 4], hl = 0;
    for (uint32_t k = spr[8 / 4] + 1u; k > 1u; k >>= 1) hl++;
    int32_t S0 = wl <= 8 ? sl * (1 << (8 - wl)) : sl >> (wl - 8), S1 = wl <= 8 ? sr * (1 << (8 - wl)) : sr >> (wl - 8);
    int32_t T0 = hl <= 8 ? tt * (1 << (8 - hl)) : tt >> (hl - 8), T1 = hl <= 8 ? tb * (1 << (8 - hl)) : tb >> (hl - 8);
    quad_push(s, x0, y0, x1, y1, S0, T0, S1, T1, id, bbz);
    A4_PERF[PF_BB]++;
#ifdef A4_DUALCAP
    return 0;                                       /* bench instrument: software draws it all */
#endif
    /* the additive modes are drawn by software too, as KIND_ADD triangles are: its glow
     * is the one that stays where the GPU drew nothing (the sky) */
    return mode == 0u ? 1 : 0;
#else
    (void)spr; (void)x0; (void)x1; (void)y0; (void)y1; (void)mode; (void)fade;
    return 0;
#endif
}

/* The game's rectangle fill (+2AB08: surface, x, y, w, h, colour, border; 0x1000 is
 * "none"), at +2AB0C (a4_fill_tramp), f = its sp: f[1..4] = r0..r3 as pushed, f[10..12]
 * the three stack arguments. The takedown and race-end letterbox bars are its solid
 * black fills (+125EA, +127EE), drawn after the 3D pass and before the HUD -- so before
 * the composite, which covered them wherever the GPU drew (round 12: the bars only over
 * the sky). A fill into this frame's 3D surface goes to the batch as flat quads and
 * returns 1 (the fill returns at once); another surface, or a fill after the composite,
 * 0 (it runs). No clip: the fill has none either. */
int a4_gl_fill(const uint32_t *f)
{
#ifdef A4_TAKEFILL
    volatile uint32_t *iat = a4_gl_iat_ptr();
    if (iat[0] < 0x10000u) return 0;
    volatile struct glr *s = (volatile struct glr *)A4_GL_SCRATCH;
    if (s->up != A4_GL_MAGIC || s->prims == 0 || s->flushed) return 0;
    const uint32_t *surf = (const uint32_t *)f[1];
    int32_t x = (int32_t)f[2], y = (int32_t)f[3], w = (int32_t)f[4], h = (int32_t)f[10];
    uint32_t col = f[11], border = f[12];
    if (surf[1] != *(volatile uint32_t *)A4_GL_BB || ((const uint16_t *)surf)[4] != (uint16_t)VP_W
        || w <= 0 || h <= 0 || (border == 0x1000u && col == 0x1000u)) return 0;
    if (border == 0x1000u) {
        quad_push(s, x, y, x + w, y + h, 0, 0, 0, 0, (KIND_FLAT << 28) | (col & 0xFFFFu), 0);
    } else {
        /* the outlined fill (+2AB50): its edge pixels in the border colour, the inside in
         * the fill colour unless that is "none" -- as four edge quads and one inner one,
         * so it keeps its place among the triangles too (software drew it before the
         * composite, which covered it wherever the GPU drew) */
        uint32_t bid = (KIND_FLAT << 28) | (border & 0xFFFFu);
        int32_t x1 = x + w, y1 = y + h;
        quad_push(s, x, y, x1, y + 1, 0, 0, 0, 0, bid, 0);
        if (h > 1) quad_push(s, x, y1 - 1, x1, y1, 0, 0, 0, 0, bid, 0);
        if (h > 2) {
            quad_push(s, x, y + 1, x + 1, y1 - 1, 0, 0, 0, 0, bid, 0);
            if (w > 1) quad_push(s, x1 - 1, y + 1, x1, y1 - 1, 0, 0, 0, 0, bid, 0);
            if (w > 2 && col != 0x1000u)
                quad_push(s, x + 1, y + 1, x1 - 1, y1 - 1, 0, 0, 0, 0, (KIND_FLAT << 28) | (col & 0xFFFFu), 0);
        }
#ifdef A4_BBPROBE
        ((volatile uint32_t *)0x00412D00u)[121]++;
#endif
    }
    A4_PERF[PF_FILL]++;
#ifdef A4_DUALCAP
    return 0;                                       /* bench instrument: software draws it all */
#endif
    return 1;
#else
    (void)f;
    return 0;
#endif
}

/* The blit's two-depth sibling (+3059E, a4_bb2_tramp): not reproduced, and never called
 * in the bench race. Its first call composites the frame (what follows is software, in
 * order: right, if slow), and the calls are counted for the capture header. */
void a4_gl_bb2(void)
{
    if (((volatile struct glr *)A4_GL_SCRATCH)->up == A4_GL_MAGIC) A4_PERF[PF_BB2]++;
#ifdef A4_IMM
    a4_gl_flush();
#endif
}

/* Shadows once a pixel without a stencil (the N95 has none, round 10): depth-test them
 * NOTEQUAL and let them write. Their z is 0, window depth 0.5, a value no flags-1
 * triangle has (theirs are 0.5 + ZK/ZW/2 > 0.5) and the clear (0) is not: the first
 * shadow at a pixel passes and stamps 0.5, the next fails. The cost: a later flags-1
 * triangle there passes GEQUAL against 0.5 where the game's buffer still held the
 * road's 1/z (the shadow span does not write it). */
static int shad_zstamp(void)
{
#ifdef A4_SHADZ
    volatile struct glr *s = (volatile struct glr *)A4_GL_SCRATCH;
    return s->sbits == 0 && s->zbits != 0;
#else
    return 0;
#endif
}

/* Undo what entering a kind changed, back to the bring-up state (KIND_TEX). */
static void kind_leave(uint32_t st)
{
    if (st == KIND_FLAT || st == KIND_SHAD) gl_enable(GL_TEXTURE_2D);
    if (st == KIND_FLAT || st == KIND_SHAD || st == KIND_ADD || st == KIND_ADDF) gl_color4x(FX, FX, FX, FX);
    if (st == KIND_ADD || st == KIND_ADDF) { gl_texenvx(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE); gl_enable(GL_ALPHA_TEST); }
    if (st == KIND_ADD || st == KIND_ADDF || st == KIND_SHAD || st == KIND_ALPHA) gl_disable(GL_BLEND);
#ifdef A4_MESH
    if (MESH_ON()) {                                /* mesh_enter's: every class tested, only TEXZ writing */
        if (st != KIND_TEXZ) gl_depthmask(1);
        gl_disable(GL_DEPTH_TEST);
        return;                                     /* no stencil or depth stamp in a mesh frame */
    }
#endif
    if (st == KIND_TEXZ) gl_disable(GL_DEPTH_TEST);
    if (st == KIND_SHAD && ((volatile struct glr *)A4_GL_SCRATCH)->sbits) gl_disable(GL_STENCIL_TEST);
    if (st == KIND_SHAD && shad_zstamp()) { gl_depthfunc(GL_GEQUAL); gl_disable(GL_DEPTH_TEST); }
}

#ifdef A4_MESH
/* A mesh frame: the list's faces are in the depth buffer before the table draws, so every
 * class of the table tests its true depth (tri_conv; a quad sits at window depth 1, as
 * untested), and writes it only where the game's own span does (KIND_TEXZ): painter's
 * among themselves as before, hidden behind a nearer list face, drawn over a farther one. */
static void mesh_enter(uint32_t kind)
{
#ifdef A4_SHADNODEPTH
    /* bench (round 37, the owner: the stock game draws the car's shadow whole): shadows
     * untested in a list frame, painted over whatever is there as the game paints them */
    if (kind == KIND_SHAD) { gl_disable(GL_DEPTH_TEST); gl_depthmask(0); return; }
#endif
    gl_enable(GL_DEPTH_TEST);
    if (kind != KIND_TEXZ) gl_depthmask(0);
}
#endif

/* One run of equal TEXID words: switch GL state if its kind differs from the
 * previous run's (*st), then draw. The state left between runs is the kind's own;
 * batch_draw restores KIND_TEX (the bring-up state) when it is done. */
static void draw_run(uint32_t mat, uint32_t first, uint32_t count, uint32_t *st, const uint16_t *ix)
{
    uint32_t kind = mat >> 28;
    if (kind == KIND_TEXZ && !((volatile struct glr *)A4_GL_SCRATCH)->zbits)
        kind = KIND_TEX;                            /* no depth buffer: draw untested */
#ifdef A4_KINDVIS
    /* diagnosis: flat triangles bright green, additive ones bright cyan, shadows bright
     * red, all opaque and untextured, so a capture shows which class an object is */
    if (kind == KIND_FLAT || kind == KIND_ADD || kind == KIND_ADDF || kind == KIND_SHAD || kind == KIND_ALPHA) {
        kind_leave(*st);
        gl_disable(GL_TEXTURE_2D);
        *st = KIND_FLAT;
        if (kind == KIND_FLAT) gl_color4x(0, FX, 0, FX);
        else if (kind == KIND_ALPHA) gl_color4x(FX, FX, 0, FX);   /* alpha-map class yellow */
        else if (kind == KIND_ADD || kind == KIND_ADDF) gl_color4x(0, FX, FX, FX);
        else gl_color4x(FX, 0, 0, FX);                 /* shadows red */
        if (ix) gl_drawelements(GL_TRIANGLES, (int)(count*3), GL_UNSIGNED_SHORT, ix + first*3);
        else gl_drawarrays(GL_TRIANGLES, (int)(first*3), (int)(count*3));
        return;
    }
#endif
    if (kind != *st) {
        kind_leave(*st);
        /* Each blended class leaves dst alpha (the drawn-mask: 1 where an opaque class
         * drew, 0 elsewhere) as it found it by its own blend, without glColorMask -- a
         * masked channel was the round-11 slowdown on the N95 (MBX.md). */
        if (kind == KIND_FLAT) gl_disable(GL_TEXTURE_2D);
        else if (kind == KIND_TEXZ) gl_enable(GL_DEPTH_TEST);
        else if (kind == KIND_ADD || kind == KIND_ADDF) {
            /* fragment alpha 0 (MODULATE by a colour of alpha 0), so ONE/ONE adds the
             * texel to RGB and nothing to alpha; the alpha test is off for it, and the key
             * texels, 0, add nothing. KIND_ADDF's colour is its run's a/16 (below). */
            gl_disable(GL_ALPHA_TEST);
            gl_texenvx(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
            gl_color4x(FX, FX, FX, 0);
            gl_enable(GL_BLEND); gl_blendfunc(GL_ONE, GL_ONE);
        }
        else if (kind == KIND_ALPHA) {
            /* premultiplied texels (tex_upload_alpha), weighted by dst alpha: T*a + dst*(1-a)
             * where an opaque class drew (alpha 1), the backdrop untouched in alpha (0) for
             * software's own drawing of it; alpha out = da*a + da*(1-a) = da */
            gl_enable(GL_BLEND); gl_blendfunc(GL_DST_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }
#ifdef A4_MESH
        else if (kind == KIND_SHAD && MESH_ON()) {
            /* The shadow's once-a-pixel stamp in dst alpha instead of depth (which now holds
             * the list's faces, and a depth stamp would pass over them): DST_COLOR /
             * ONE_MINUS_DST_ALPHA with src 3/4 gives rgb * (3/4 + 1 - a), alpha * (3/4 + 1 -
             * a) * a... = dst * 3/4 where alpha is 1 (drawn, not yet darkened), dst where it
             * is 3/4 (darkened once: alpha stays 3/4), and alpha 0 (the backdrop) stays 0. */
            gl_disable(GL_TEXTURE_2D); gl_enable(GL_BLEND); gl_blendfunc(GL_DST_COLOR, GL_ONE_MINUS_DST_ALPHA);
            gl_color4x((FX * 12) / 16, (FX * 12) / 16, (FX * 12) / 16, (FX * 12) / 16);
        }
#endif
        else if (kind == KIND_SHAD) {
            /* dst * 12/16 in RGB, alpha * 1 */
            gl_disable(GL_TEXTURE_2D); gl_enable(GL_BLEND); gl_blendfunc(GL_ZERO, GL_SRC_COLOR);
            gl_color4x((FX * 12) / 16, (FX * 12) / 16, (FX * 12) / 16, FX);
            /* once a pixel a frame (the span's stamp map): pass where the stencil is 0,
             * then INCR it (func/op set at bring-up, cleared with the frame); without a
             * stencil, the depth stamp (shad_zstamp) */
            if (((volatile struct glr *)A4_GL_SCRATCH)->sbits) gl_enable(GL_STENCIL_TEST);
            else if (shad_zstamp()) { gl_enable(GL_DEPTH_TEST); gl_depthfunc(GL_NOTEQUAL); }
        }
#ifdef A4_MESH
        if (MESH_ON()) mesh_enter(kind);
#endif
        *st = kind;
    }
    if (kind == KIND_FLAT) {
        /* RGB565 -> GL_FIXED, c*FX/31 (or /63) without a divide; exact at both ends */
        uint32_t r = (mat >> 11) & 31u, g = (mat >> 5) & 63u, b = mat & 31u;
        gl_color4x(r == 31u ? FX : (int)(r * 2114u), g == 63u ? FX : (int)(g * 1040u + (g >> 2)),
                   b == 31u ? FX : (int)(b * 2114u), FX);
    } else if (kind == KIND_ADDF) {
        /* the billboard's mode 3: dst + (sat(dst + T) - dst) * a/16 -- dst + T*a/16 here,
         * the same short of saturation */
        int a = (int)((mat >> 20) & 0xFFu) * (FX / 16);
        gl_color4x(a, a, a, 0);
        gl_bindtex(GL_TEXTURE_2D, mat & 0xFFFFFu);
    } else if (kind != KIND_SHAD) {
        gl_bindtex(GL_TEXTURE_2D, mat & 0x0FFFFFFFu);
    }
    /* an index list (AB_INDEXED): the run's triangles where they sit in the batch, in run
     * order -- the same triangles in the same order as the copy draws */
    if (ix) gl_drawelements(GL_TRIANGLES, (int)(count*3), GL_UNSIGNED_SHORT, ix + first*3);
    else gl_drawarrays(GL_TRIANGLES, (int)(first*3), (int)(count*3));
}

/* Draw the accumulated frame in arrival order, one glDrawArrays per run of
 * consecutive equal TEXID words (same texture or colour, same kind). Identical
 * pixels to the per-triangle path; just fewer GL calls. No depth, no sort. */
static void batch_draw(uint32_t nt)
{
    if (nt == 0) return;
    if (nt > MAXT) nt = MAXT;
    uint32_t *TEXID = (uint32_t *)A4_GL_TEXID;
#ifdef A4_REORDER
    if (((volatile struct glr *)A4_GL_SCRATCH)->fmode & AB_REORDER) {
    uint32_t t0 = a4_ntick();
    struct reo *ro = RO;
    const int16_t *tb = (const int16_t *)A4_GL_TBOX;
#ifdef A4_DEFER
    volatile struct glr *sg = (volatile struct glr *)A4_GL_SCRATCH;
    int defer = (sg->fmode & AB_DEFER) != 0;
    if (defer) {
        /* pass 1: each record's texture (TEXID) and box (TBOX), and the frame's drawn rows */
        int16_t *tbw = (int16_t *)A4_GL_TBOX;
        uint32_t miny = sg->miny, maxy = sg->maxy, nflat = 0, nadd = 0, nshad = 0;
        for (uint32_t t = 0; t < nt; t++) {
            const uint32_t *r = DEFR(t);
            if (r[0] == DEF_QUAD) {
                TEXID[t] = r[1];
                tbox_calc(tbw + t * 4u, (int16_t)(r[2] & 0xFFFFu), (int16_t)(r[2] >> 16), (int16_t)(r[3] & 0xFFFFu), (int16_t)(r[3] >> 16));
                continue;
            }
            uint32_t kind = r[0] & 15u, id; int wsh, hsh;
            if (tri_tex(sg, kind, (int)((r[0] >> 4) & 1u), r[1], (const uint16_t *)r[2], (const uint16_t *)r[3], &id, &wsh, &hsh) == -1) {
                TEXID[t] = DEF_DROP; tbox_calc(tbw + t * 4u, 0x4000, 0x4000, -0x4000, -0x4000);   /* empty: passes everything */
                ((volatile uint32_t *)0x00412D00u)[109]++;                 /* taken, then lost (expect 0) */
                continue;
            }
            TEXID[t] = id;
            int32_t ylo, yhi, xlo, xhi;
            ext3((int16_t)(r[4] & 0xFFFFu), (int16_t)(r[5] & 0xFFFFu), (int16_t)(r[6] & 0xFFFFu), &xlo, &xhi);
            ext3((int16_t)(r[4] >> 16), (int16_t)(r[5] >> 16), (int16_t)(r[6] >> 16), &ylo, &yhi);
            tbox_calc(tbw + t * 4u, xlo, ylo, xhi, yhi);
            int32_t a = ylo < 0 ? 0 : ylo >= VP_H ? VP_H - 1 : ylo, b = yhi < 0 ? 0 : yhi >= VP_H ? VP_H - 1 : yhi;
            if ((uint32_t)a < miny) miny = (uint32_t)a;
            if ((uint32_t)b > maxy) maxy = (uint32_t)b;
            if (kind == KIND_FLAT) nflat++; else if (kind == KIND_ADD) nadd++; else if (kind == KIND_SHAD) nshad++;
        }
        sg->miny = miny; sg->maxy = maxy;
        A4_PERF[PF_FLAT] += nflat; A4_PERF[PF_ADD] += nadd; A4_PERF[PF_SHAD] += nshad;
    }
#endif
    uint32_t nr = 0, runs0 = 1;
    for (uint32_t t = 0; t < nt; t++) {
        uint32_t key = TEXID[t];
        if (t && key != TEXID[t - 1]) runs0++;
        int32_t x0 = tb[t*4], y0 = tb[t*4+1], x1 = tb[t*4+2], y1 = tb[t*4+3];
        int r = (int)nr - 1, found = -1;
        for (uint32_t steps = 0; r >= 0 && steps <= REO_SCAN; r--, steps++) {
            if (ro->key[r] == key) { found = r; break; }
            const int16_t *rb = &ro->box[r * 4];
            if (x1 < rb[0] || rb[2] < x0 || y1 < rb[1] || rb[3] < y0) continue;   /* apart: pass it */
            break;                                  /* shares pixels: t stays after it */
        }
        int16_t *b;
        if (found < 0) {
            found = (int)nr++;
            ro->key[found] = key; ro->head[found] = (uint16_t)t; ro->cnt[found] = 0;
            b = &ro->box[found * 4];
            b[0] = (int16_t)x0; b[1] = (int16_t)y0; b[2] = (int16_t)x1; b[3] = (int16_t)y1;
        } else {
            ro->next[ro->tail[found]] = (uint16_t)t;
            b = &ro->box[found * 4];
            if (x0 < b[0]) b[0] = (int16_t)x0;
            if (y0 < b[1]) b[1] = (int16_t)y0;
            if (x1 > b[2]) b[2] = (int16_t)x1;
            if (y1 > b[3]) b[3] = (int16_t)y1;
        }
        ro->tail[found] = (uint16_t)t; ro->cnt[found]++;
    }
    uint16_t *ix = 0;
#ifdef A4_DEFER
    if (defer) {
        /* pass 3: each run's records converted straight into the arrays the GL reads */
        int32_t *po = ro->pos, *uo = ro->uv;
        for (uint32_t r = 0; r < nr; r++) {
            uint32_t t = ro->head[r];
            if (ro->key[r] == DEF_DROP) { ro->cnt[r] = 0; continue; }   /* drawn as nothing */
            for (uint32_t c = ro->cnt[r]; c; c--) {
                const uint32_t *d = DEFR(t);
                if (d[0] == DEF_QUAD) {
                    for (int k = 0; k < 3; k++) {
                        po[k*4] = (int32_t)d[4 + k*2] * 4096; po[k*4+1] = (int32_t)d[5 + k*2] * 4096;
                        po[k*4+2] = 0; po[k*4+3] = 4096;
                        uo[k*2] = (int32_t)d[10 + k*2]; uo[k*2+1] = (int32_t)d[11 + k*2];
                    }
                } else {
                    uint32_t kind = d[0] & 15u;
                    int wsh = 0, hsh = 0;
                    if (kind != KIND_SHAD && kind != KIND_FLAT) {
                        const uint32_t *ts = (const uint32_t *)d[1];
                        uint32_t tw = ts[1] + 1, th = ts[2] + 1;
                        wsh = tw ? 31 - __builtin_clz(tw) : 0; hsh = th ? 31 - __builtin_clz(th) : 0;
                    }
                    int32_t x0 = (int16_t)(d[4] & 0xFFFFu), y0 = (int16_t)(d[4] >> 16);
                    int32_t x1 = (int16_t)(d[5] & 0xFFFFu), y1 = (int16_t)(d[5] >> 16);
                    int32_t x2 = (int16_t)(d[6] & 0xFFFFu), y2 = (int16_t)(d[6] >> 16);
                    int32_t xlo, xhi, ylo, yhi;
                    ext3(x0, x1, x2, &xlo, &xhi); ext3(y0, y1, y2, &ylo, &yhi);
                    tri_conv(kind, wsh, hsh, x0, y0, x1, y1, x2, y2, (int32_t)d[7], (int32_t)d[8], (int32_t)d[9], d + 10,
                             xlo, xhi, ylo, yhi, po, uo);
                }
#ifdef A4_DEFERCHECK
                {   /* bench: these words against the lean path's at the hook (0x412D00 [110]
                     * triangles differing, [111] checked, [112] the first's word) */
                    volatile uint32_t *ck = (volatile uint32_t *)0x00412D00u;
                    const uint32_t *sh = (const uint32_t *)0x00423000u + t * 24u;
                    int bad = -1;
                    if (!sh[19]) bad = 98;
                    for (int k = 0; k < 12 && bad < 0; k++) if ((uint32_t)po[k] != sh[k]) bad = k;
                    for (int k = 0; k < 6 && bad < 0; k++) if ((uint32_t)uo[k] != sh[12 + k]) bad = 12 + k;
                    if (bad < 0 && TEXID[t] != sh[18]) bad = 18;
                    if (bad < 0 && (tb[t*4] != ((const int16_t *)(sh + 20))[0] || tb[t*4+1] != ((const int16_t *)(sh + 20))[1]
                                    || tb[t*4+2] != ((const int16_t *)(sh + 20))[2] || tb[t*4+3] != ((const int16_t *)(sh + 20))[3])) bad = 20;
                    if (bad >= 0) { if (!ck[110]) ck[112] = (uint32_t)bad; ck[110]++; }
                    ck[111]++;
                }
#endif
                po += 3u * POSC; uo += 3u * UVC;
                t = ro->next[t];
            }
        }
    } else
#endif
    if (((volatile struct glr *)A4_GL_SCRATCH)->fmode & AB_INDEXED) {
        /* the index list in run order, in the copy's place (nothing is copied in this mode):
         * 3 * 512 indices < 2^16 */
        ix = (uint16_t *)ro->pos;
        uint32_t o = 0;
        for (uint32_t r = 0; r < nr; r++) {
            uint32_t t = ro->head[r];
            for (uint32_t c = ro->cnt[r]; c; c--) {
                ix[o] = (uint16_t)(t * 3u); ix[o + 1] = (uint16_t)(t * 3u + 1u); ix[o + 2] = (uint16_t)(t * 3u + 2u);
                o += 3u; t = ro->next[t];
            }
        }
    } else
    {   /* the vertices in run order */
        const int32_t *P = (const int32_t *)A4_GL_POS, *U = (const int32_t *)A4_GL_UV;
        int32_t *po = ro->pos, *uo = ro->uv;
        for (uint32_t r = 0; r < nr; r++) {
            uint32_t t = ro->head[r];
            for (uint32_t c = ro->cnt[r]; c; c--) {
                /* doublewords (every block is 8-aligned: 48 and 24 bytes a triangle from
                 * aligned bases), and the next triangle's source asked for ahead: the
                 * batch was written well before, so its lines are mostly out of the cache */
                const uint64_t *ps = (const uint64_t *)(P + t * (3u * POSC)), *us = (const uint64_t *)(U + t * (3u * UVC));
                uint32_t nx = ro->next[t];             /* garbage on a run's last: PLD never faults */
                __builtin_prefetch(P + nx * (3u * POSC)); __builtin_prefetch(U + nx * (3u * UVC));
                uint64_t *pd = (uint64_t *)po, *ud = (uint64_t *)uo;
                for (uint32_t k = 0; k < 3u * POSC / 2u; k++) pd[k] = ps[k];
                for (uint32_t k = 0; k < 3u * UVC / 2u; k++) ud[k] = us[k];
                po += 3u * POSC; uo += 3u * UVC;
                t = nx;
            }
        }
    }
    A4_PERF[PF_REO] += a4_ntick() - t0;
    A4_PERF[PF_RUNS0] += runs0;
#ifdef A4_REOCHECK
    {   /* bench check of the invariant: every pair the reorder put out of input order has
         * disjoint boxes. 0x412D00 [100] pairs violating it, [101] batches checked,
         * [102] pairs out of order (that the reorder did anything) */
        volatile uint32_t *ck = (volatile uint32_t *)0x00412D00u;
        uint32_t viol = 0, inv = 0, o = 0;
        uint16_t *in = (uint16_t *)A4_GL_READBUF;   /* free between composites */
        for (uint32_t r = 0; r < nr; r++) {
            uint32_t t = ro->head[r];
            for (uint32_t c = ro->cnt[r]; c; c--) { in[o++] = (uint16_t)t; t = ro->next[t]; }
        }
        for (uint32_t i = 0; i < o; i++)
            for (uint32_t j = i + 1; j < o; j++)
                if (in[i] > in[j]) {
                    const int16_t *a = &tb[in[i] * 4], *b = &tb[in[j] * 4];
                    inv++;
                    if (!(a[2] < b[0] || b[2] < a[0] || a[3] < b[1] || b[3] < a[1])) viol++;
                }
        ck[100] += viol; ck[101]++; ck[102] += inv;
        if (o != nt) ck[103]++;                     /* lost or doubled triangles */
    }
#endif
    gl_vertexptr(4, GL_FIXED, 0, ix ? (const void *)A4_GL_POS : (const void *)ro->pos);
    gl_texcoordptr((int)UVC, GL_FIXED, 0, ix ? (const void *)A4_GL_UV : (const void *)ro->uv);
    uint32_t st = KIND_TEX, first = 0;
#ifdef A4_MESH
    if (MESH_ON()) mesh_enter(KIND_TEX);            /* the state the runs start from */
#endif
    for (uint32_t r = 0; r < nr; r++) {
        if (!ro->cnt[r]) continue;                  /* a deferred run whose texture failed */
        draw_run(ro->key[r], first, ro->cnt[r], &st, ix);
        first += ro->cnt[r];
    }
    kind_leave(st);
#if defined(A4_RUNDIAG) || defined(A4_TEL)
    ((volatile uint32_t *)0x00412000u)[4] += nr;     /* draw calls this frame */
#endif
    return;
    }
#endif
#ifdef A4_HPERSP
    gl_vertexptr(4, GL_FIXED, 0, (const void *)A4_GL_POS);
#else
    gl_vertexptr(2, GL_SHORT, 0, (const void *)A4_GL_POS);
#endif
    gl_texcoordptr((int)UVC, GL_FIXED, 0, (const void *)A4_GL_UV);

    uint32_t run_start = 0, st = KIND_TEX;
    uint32_t cur = TEXID[0];
#ifdef A4_MESH
    if (MESH_ON()) mesh_enter(KIND_TEX);            /* the state the runs start from */
#endif
    for (uint32_t t = 1; t < nt; t++) {
        if (TEXID[t] != cur) {
            draw_run(cur, run_start, t - run_start, &st, 0);
            run_start = t;
            cur = TEXID[t];
        }
    }
    draw_run(cur, run_start, nt - run_start, &st, 0);  /* final run */
    kind_leave(st);
#if defined(A4_RUNDIAG) || defined(A4_TEL)
    {   /* draw calls this frame (one per same-texture run); folded at flush */
        uint32_t runs = 1; for (uint32_t t = 1; t < nt; t++) if (TEXID[t] != TEXID[t-1]) runs++;
        ((volatile uint32_t *)0x00412000u)[4] += runs;
        A4_PERF[PF_RUNS0] += runs;                   /* = the calls: nothing grouped */
    }
#endif
}

/* Read back the GPU image of rows [miny, maxy] and write the pixels the GPU drew into the
 * game's surface. Which pixels: with dst alpha (A4_IMM and abits) those an opaque triangle
 * covered (alpha set; the clear leaves 0 and each blended class's blend keeps it), so a
 * blend over the backdrop is left to software's own drawing of it; without, every pixel
 * that is not the magenta sentinel -- nor the sentinel with a glow added to it (R and B
 * saturated, G short of full: additive over the backdrop), so pure pinks are lost there.
 * Read in the implementation's own pair when AB_NATIVE says so (BGRA on the N95). */
void a4_comp_row(uint32_t *d32, const uint32_t *src, const uint32_t *end);   /* a4_px.s */
static void composite_rows(volatile struct glr *s, uint32_t miny, uint32_t maxy)
{
#ifdef A4_DUALCAP
    return;                                         /* the game's own picture stays untouched */
#endif
#if !defined(A4_NO_BLIT) && !defined(A4_WINDOW)
    uint32_t bb = *(volatile uint32_t *)A4_GL_BB;
    if (!bb || miny > maxy) return;
    uint32_t *rb = (uint32_t *)A4_GL_READBUF;
    int sh = STRIP;
#if defined(A4_BIGSTRIP) && defined(A4_REORDER)
    /* round 23: 24-row strips (10 reads a frame, not 15) through the reorder's arrays, free
     * once the frame's batches are drawn (36 KB; a strip is 30 KB) */
    if (s->fmode & AB_BIGSTRIP) { sh = 24; rb = (uint32_t *)RO->pos; }
#endif
    volatile uint16_t *fb = (volatile uint16_t *)bb;
    /* strips covering only the rows drawn. Screen rows are top-down, GL rows bottom-up:
     * screen [miny,maxy] -> GL [H-1-maxy, H-1-miny]. y0 steps on STRIP multiples and VP_H
     * is one, so a strip never over-reads. */
    int gl_lo = VP_H - 1 - (int)maxy; if (gl_lo < 0) gl_lo = 0;
    int gl_hi = VP_H - 1 - (int)miny; if (gl_hi > VP_H - 1) gl_hi = VP_H - 1;
    int nat = (s->fmode & AB_NATIVE) != 0, amask = s->abits != 0;
    int r565 = nat && !amask && s->rdfmt == GL_RGB && s->rdtype == GL_UNSIGNED_SHORT_5_6_5;
    int bgra = nat && s->rdfmt == GL_BGRA && s->rdtype == GL_UNSIGNED_BYTE;
    int rsh = bgra ? 16 : 0, bsh = bgra ? 0 : 16;   /* where R and B sit in a read word */
    /* the strip holding gl_lo (VP_H is a multiple of sh): gl_lo / sh * sh without a divide
     * (the blob has no __aeabi_uidiv; (x * 2731) >> 16 is x / 24 for every x in 0..239) */
    int ystart = sh == 24 ? (int)((((uint32_t)gl_lo * 2731u) >> 16) * 24u) : (gl_lo / STRIP) * STRIP;
    for (int y0 = ystart; y0 <= gl_hi; y0 += sh) {
        uint32_t t0 = a4_ntick();
        if (r565)      gl_readpixels(0, y0, VP_W, sh, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, rb);
        else if (bgra) gl_readpixels(0, y0, VP_W, sh, GL_BGRA, GL_UNSIGNED_BYTE, rb);
        else           gl_readpixels(0, y0, VP_W, sh, GL_RGBA, GL_UNSIGNED_BYTE, rb);
        A4_PERF[PF_READ] += a4_ntick() - t0;
#ifdef A4_ROWSTAT
        {   /* bench (round 50): which rows read back hold a drawn pixel -- census [80] rows read with
             * none, [81] rows read, [82] the sum of each frame's first drawn screen row, [83] of its
             * last, [84] frames (a composite reading from row 0) */
            volatile uint32_t *ck = (volatile uint32_t *)0x00412D00u;
            volatile int32_t *fr = (volatile int32_t *)(ck + 85);   /* [85]/[86] this frame's first and last */
            if (y0 == ystart) { fr[0] = 9999; fr[1] = -1; }
            for (int sy = 0; sy < sh; sy++) {
                int screen_y = VP_H - 1 - (y0 + sy);
                const uint32_t *src = rb + sy * VP_W;
                uint32_t any = 0;
                for (int x = 0; x < VP_W; x++) any |= amask ? src[x] : 0x80000000u;
                ck[81]++;
                if (!(any & 0x80000000u)) { ck[80]++; continue; }
                if (screen_y < fr[0]) fr[0] = screen_y;
                if (screen_y > fr[1]) fr[1] = screen_y;
            }
            if (y0 + sh > gl_hi && fr[1] >= 0) { ck[82] += (uint32_t)fr[0]; ck[83] += (uint32_t)fr[1]; ck[84]++; }
        }
#endif
#ifdef A4_READBACK_ONLY
        continue;                                        /* measurement: readback cost without the blit */
#endif
        if (r565) {
            const uint16_t *rb16 = (const uint16_t *)rb;
            for (int sy = 0; sy < sh; sy++) {
                int screen_y = VP_H - 1 - (y0 + sy);
                if (screen_y < CLIP_TOP) continue;
                const uint16_t *src = rb16 + sy * VP_W;
                volatile uint16_t *dstrow = fb + screen_y * FB_STRIDE;
                for (int x = 0; x < VP_W; x++) {
                    uint32_t px = src[x];
                    if ((px & 0xF81Fu) == 0xF81Fu && (px & 0x07E0u) != 0x07E0u) continue;   /* sentinel (+glow) */
                    dstrow[x] = (uint16_t)px;
                }
            }
            continue;
        }
#ifndef A4_SLOWBLIT
        if (bgra && amask && !((uintptr_t)fb & 3u)) {
            /* the N95's case (round 18): BGRA words, alpha bit 7 the drawn-mask; two pixels
             * a step, one word store when both are drawn (VP_W is even and rows are word
             * aligned). The same pixels as the general loop below: 565 from the top bits,
             * drawn iff alpha >= 0x80. */
            /* round 50: the masks held in registers (the compiler rebuilt all three every pair, short of
             * registers) and the walk by pointer -- 24 instructions a pair to ~17; the same pixels */
#ifdef A4_PX6
            /* round 50: the same pixels in ARMv6 (a4_px.s: PKHBT/PKHTB put two pixels through each
             * mask and shift) -- 22 instructions for four pixels against the loop below's 42 */
            _Static_assert(VP_W % 4 == 0 && FB_STRIDE % 2 == 0, "a4_comp_row takes rows of 4n pixels, word aligned");
            for (int sy = 0; sy < sh; sy++) {
                int screen_y = VP_H - 1 - (y0 + sy);
                if (screen_y < CLIP_TOP) continue;
                const uint32_t *src = rb + sy * VP_W;
                a4_comp_row((uint32_t *)((uint16_t *)fb + screen_y * FB_STRIDE), src, src + VP_W);
#ifdef A4_BLITCHECK_MUTANT
                if ((int32_t)src[0] < 0) ((uint16_t *)fb)[screen_y * FB_STRIDE] ^= 1u;   /* the check's own test */
#endif
            }
            if (0)
#endif
            {
            uint32_t mR = 0xF800u, mG = 0x07E0u, mB = 0x001Fu;
            __asm__("" : "+r"(mR), "+r"(mG), "+r"(mB));
            for (int sy = 0; sy < sh; sy++) {
                int screen_y = VP_H - 1 - (y0 + sy);
                if (screen_y < CLIP_TOP) continue;
                const uint32_t *src = rb + sy * VP_W, *const end = src + VP_W;
                uint32_t *d32 = (uint32_t *)((uint16_t *)fb + screen_y * FB_STRIDE);
                do {
                    const uint32_t a = src[0], b = src[1];
                    src += 2;
                    const uint32_t ca = ((a >> 8) & mR) | ((a >> 5) & mG) | ((a >> 3) & mB);
                    const uint32_t cb = ((b >> 8) & mR) | ((b >> 5) & mG) | ((b >> 3) & mB);
                    if (__builtin_expect((int32_t)(a & b) < 0, 1)) *d32 = ca | (cb << 16);
                    else {
                        if ((int32_t)a < 0) ((uint16_t *)d32)[0] = (uint16_t)ca;
                        if ((int32_t)b < 0) ((uint16_t *)d32)[1] = (uint16_t)cb;
                    }
                    d32++;
                } while (src != end);
#ifdef A4_BLITCHECK_MUTANT
                if ((int32_t)src[-VP_W] < 0) ((uint16_t *)fb)[screen_y * FB_STRIDE] ^= 1u;   /* the check's own test */
#endif
            }
            }
#ifdef A4_BLITCHECK
            /* bench check: the general loop's answer for the same strip, against what the
             * fast loop wrote (0x412D00 [104] pixels differing, [105] strips checked) */
            {
                volatile uint32_t *ck = (volatile uint32_t *)0x00412D00u;
                for (int sy = 0; sy < sh; sy++) {
                    int screen_y = VP_H - 1 - (y0 + sy);
                    if (screen_y < CLIP_TOP) continue;
                    const uint32_t *src = rb + sy * VP_W;
                    volatile uint16_t *dstrow = fb + screen_y * FB_STRIDE;
                    for (int x = 0; x < VP_W; x++) {
                        uint32_t px = src[x];
                        uint32_t r = (px >> rsh) & 0xFF, g = (px >> 8) & 0xFF, b = (px >> bsh) & 0xFF;
                        if ((px >> 24) < 0x80u) continue;
                        if (dstrow[x] != (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))) ck[104]++;
                    }
                }
                ck[105]++;
            }
#endif
            continue;
        }
#endif
        for (int sy = 0; sy < sh; sy++) {
            int screen_y = VP_H - 1 - (y0 + sy);
            if (screen_y < CLIP_TOP) continue;           /* leave the HUD banner to software */
            const uint32_t *src = rb + sy * VP_W;
            volatile uint16_t *dstrow = fb + screen_y * FB_STRIDE;
            for (int x = 0; x < VP_W; x++) {
                uint32_t px = src[x];
                uint32_t r = (px >> rsh) & 0xFF, g = (px >> 8) & 0xFF, b = (px >> bsh) & 0xFF;
                if (amask ? (px >> 24) < 0x80u : (r == 0xFFu && b == 0xFFu && g != 0xFFu)) {
#ifdef A4_SENTINEL_VIS
                    /* diagnosis: paint GPU-didn't-draw (software-kept) pixels bright
                     * green, so the retained software region is visible in the capture. */
                    dstrow[x] = (uint16_t)0x07E0;
#endif
                    continue;                           /* GPU drew nothing (opaque) here */
                }
                dstrow[x] = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
            }
        }
    }
#if defined(A4_STRIPCHECK) && defined(A4_BIGSTRIP)
    if (sh == 24 && bgra && amask) {
        /* bench: the same rows read back again in 16-row strips, against what the 24-row
         * composite wrote (0x412D00 [113] drawn pixels differing, [114] strips) */
        volatile uint32_t *ck = (volatile uint32_t *)0x00412D00u;
        uint32_t *rb16 = (uint32_t *)A4_GL_READBUF;
        int y16 = (gl_lo >> 4) << 4;
        for (; y16 <= gl_hi; y16 += 16) {
            gl_readpixels(0, y16, VP_W, 16, GL_BGRA, GL_UNSIGNED_BYTE, rb16);
            for (int sy = 0; sy < 16; sy++) {
                int screen_y = VP_H - 1 - (y16 + sy);
                if (screen_y < CLIP_TOP) continue;
                const uint32_t *src = rb16 + sy * VP_W;
                volatile uint16_t *dstrow = fb + screen_y * FB_STRIDE;
                for (int x = 0; x < VP_W; x++) {
                    uint32_t px = src[x];
                    if ((px >> 24) < 0x80u) continue;
                    if (dstrow[x] != (uint16_t)(((px >> 8) & 0xF800u) | ((px >> 5) & 0x07E0u) | ((px >> 3) & 0x001Fu))) ck[113]++;
                }
            }
            ck[114]++;
        }
    }
#endif
#else
    (void)s; (void)miny; (void)maxy;
#endif /* !A4_NO_BLIT && !A4_WINDOW */
}

#if defined(A4_PX6) && !defined(A4_PX6_NOCVT)
/* Round 50: the runtime's 565 -> 32-bit convert, done by our ARMv6 rows. ngibackbuffer's present
 * (+34A4) calls [bb+0xC]->vtable[2](conv, dst, src) -- +CB70, ~13.9% of the race's modelled cycles
 * (pct19), 20 instructions a pixel pair. The converter's vtable is the runtime's (read-only code);
 * the object is ours to point at a copy of it whose [2] is cvt_fast, which does what +CB70 does
 * (the same offsets, the same clipped size, the same rows) and hands anything but the plain case
 * back to +CB70 itself. Found from the game's own buffer: the back-buffer object sits right before
 * its pixels (bb = buffer - 0xC0: bb[8] the pixels, bb[9]/bb[10] 320x240, bb[3] the converter,
 * whose [1] is bb again), and +CB70's 112 words hash to CVT_HASH -- every link checked before it
 * is followed, so another runtime build is left alone (CVT[0] says why). */
void a4_cvt565_run(uint32_t *d, const uint32_t *s, const uint32_t *e);   /* a4_px.s */
#define CVT ((volatile uint32_t *)0x00412100u)  /* [0] 0 untried, 1 installed, 2+ refused (the step that failed)
                                                 * [1] the converter, [2] its own vtable, [3] +CB70, [4..10] our
                                                 * vtable: its two words before (offset, typeinfo) and five entries,
                                                 * [11] +CB70's import stub +5EC8: WS32 387, RWindowBase::Size() */
#define CVT_WORDS 112u
#define CVT_HASH 0x31323FE4u
typedef void (*cvt_fn)(uint32_t *self, uint32_t dst, uint32_t src);
typedef void (*cvt_size_fn)(int32_t *size, const void *window);   /* TSize returned through r0, this in r1 */
#define CVT_STUB_OFF (0xCB70u - 0x5EC8u)       /* +CB70's bl to the stub: inside the hashed words */

static void cvt_fast(uint32_t *self, uint32_t dst, uint32_t src)
{
    volatile uint32_t *ck = (volatile uint32_t *)0x00412D00u;   /* [91] fast, [92] handed back, [93]/[94] the check */
    const uint32_t *X = (const uint32_t *)(uintptr_t)self[1];
    const uint32_t *X10 = (const uint32_t *)(uintptr_t)X[0x10 / 4];
    const int32_t y = (int32_t)self[3], x = (int32_t)self[2];
    const uint32_t sstr = X[0x40 / 4], dstr = X10[0xB4 / 4];
    uint32_t sp = src, dp = dst;
    if (y <= 0) sp -= (sstr * (uint32_t)y) << 2; else dp += (dstr * (uint32_t)y) << 2;
    if (x > 0) dp += (uint32_t)x << 2; else sp -= (uint32_t)((x + (int32_t)((uint32_t)x >> 31)) >> 1) << 2;
    int32_t w = (int32_t)X[0x24 / 4], h = (int32_t)X[0x28 / 4];
    /* the window's size, asked of the window server as +CB70 asks it: once for the width and once
     * for the height, and again for whichever is smaller than the buffer's (RWindowBase::Size()) */
    const void *win = (const void *)((uintptr_t)X10 + 0x28u);
    const cvt_size_fn size = (cvt_size_fn)(uintptr_t)CVT[11];
    int32_t sz[2];
    size(sz, win);
    if (sz[0] < w) { size(sz, win); w = sz[0]; }
    size(sz, win);
    if (sz[1] < h) { size(sz, win); h = sz[1]; }
    if (h == 0 || (w == 0 && h > 0)) return;         /* +CB70 writes nothing either */
    int fast = h > 0 && h <= 4096 && w > 0 && w <= 4096 && !(w & 3) && !((sp | dp) & 3u)
        && sstr * 2u >= (uint32_t)w && sstr <= 4096u && dstr >= (uint32_t)w && dstr <= 4096u;
    uint32_t se = 0, de = 0;
    if (fast) {
        se = sp + ((uint32_t)(h - 1) * sstr) * 4u + (uint32_t)w * 2u;
        de = dp + ((uint32_t)(h - 1) * dstr) * 4u + (uint32_t)w * 4u;
        const uint32_t xa = (uint32_t)(uintptr_t)X, xb = (uint32_t)(uintptr_t)X10;
        fast = se > sp && de > dp && (se <= dp || de <= sp)                         /* source and target apart */
            && (xa + 0x44u <= dp || xa >= de) && (xb + 0xB8u <= dp || xb >= de);  /* and what +CB70 re-reads a row */
    }
    if (!fast) { ck[92]++; ((cvt_fn)(uintptr_t)CVT[3])(self, dst, src); return; }
    ck[91]++;
#ifdef A4_CVTCHECK
    /* bench check: +CB70's own answer goes to the screen; each row of ours, made into a spare row,
     * against it ([93] words differing, [94] converts compared) */
    ((cvt_fn)(uintptr_t)CVT[3])(self, dst, src);
    {
        uint32_t *tmp = (uint32_t *)0x00460000u;
        for (int32_t r = 0; r < h; r++) {
            const uint32_t *so = (const uint32_t *)(uintptr_t)(sp + (uint32_t)r * sstr * 4u);
            const uint32_t *dr = (const uint32_t *)(uintptr_t)(dp + (uint32_t)r * dstr * 4u);
            a4_cvt565_run(tmp, so, so + w / 2);
#ifdef A4_CVTCHECK_MUTANT
            if (r == h / 2) tmp[w / 2] ^= 0x80u;           /* the check's own test: one word a convert */
#endif
            for (int32_t k = 0; k < w; k++) if (tmp[k] != dr[k]) ck[93]++;
        }
        ck[94]++;
    }
    return;
#endif
    if (sstr * 2u == (uint32_t)w && dstr == (uint32_t)w) {
        a4_cvt565_run((uint32_t *)(uintptr_t)dp, (const uint32_t *)(uintptr_t)sp, (const uint32_t *)(uintptr_t)se);
        return;                                       /* the rows contiguous: one run */
    }
    for (int32_t r = 0; r < h; r++, sp += sstr * 4u, dp += dstr * 4u)
        a4_cvt565_run((uint32_t *)(uintptr_t)dp, (const uint32_t *)(uintptr_t)sp,
                      (const uint32_t *)(uintptr_t)(sp + (uint32_t)w * 2u));
}

/* At every present: the converter pointed at our vtable, once its object is found and checked. */
static void cvt_install(void)
{
    if (CVT[0] >= 2u) return;                        /* refused: not this runtime */
    const uint32_t buf = *(volatile uint32_t *)A4_GL_BB;
    if (!buf || (buf & 3u) || (buf & 0xFFFu) < 0xC0u) return;   /* bb on buf's own page, or nothing read */
    const uint32_t *bb = (const uint32_t *)(uintptr_t)(buf - 0xC0u);
    if (bb[8] != buf || bb[9] != 320u || bb[10] != 240u) { CVT[0] = 2u; return; }
    uint32_t *conv = (uint32_t *)(uintptr_t)bb[3];
    if (!conv || ((uintptr_t)conv & 3u) || conv[1] != (uint32_t)(uintptr_t)bb) { CVT[0] = 3u; return; }
    volatile uint32_t *our = CVT + 4;
    const uint32_t vt = conv[0];
    if (vt == (uint32_t)(uintptr_t)(our + 2)) return;              /* ours already */
    if (vt != CVT[2]) {                                            /* a vtable not yet checked */
        if (!vt || (vt & 3u)) { CVT[0] = 4u; return; }
        const uint32_t *v = (const uint32_t *)(uintptr_t)vt;
        const uint32_t f = v[2];
        if (!f || (f & 3u)) { CVT[0] = 5u; return; }
        const uint32_t *fw = (const uint32_t *)(uintptr_t)f;
        uint32_t hsh = 0;
        for (uint32_t i = 0; i < CVT_WORDS; i++) hsh = ((hsh << 5) | (hsh >> 27)) ^ fw[i];
        if (hsh != CVT_HASH) { CVT[0] = 6u; return; }
        if (*(const uint32_t *)(uintptr_t)(f - CVT_STUB_OFF) != 0xE51FF004u) { CVT[0] = 7u; return; }   /* ldr pc, [pc, #-4] */
        CVT[11] = f - CVT_STUB_OFF;
        for (int i = 0; i < 7; i++) our[i] = v[i - 2];
        our[4] = (uint32_t)(uintptr_t)cvt_fast;
        CVT[2] = vt; CVT[3] = f;
    }
    CVT[1] = (uint32_t)(uintptr_t)conv;
    conv[0] = (uint32_t)(uintptr_t)(our + 2);
    CVT[0] = 1u;
    ((volatile uint32_t *)0x00412D00u)[90]++;      /* census: installs (a refusal leaves CVT[0] >= 2) */
}
#endif

/* the pipelined composite: the previous frame's image, at this frame's first triangle */
static void composite_prev(volatile struct glr *s) __attribute__((unused));
static void composite_prev(volatile struct glr *s)
{
    composite_rows(s, s->pminy, s->pmaxy);
}

void a4_gl_flush(void)
{
    volatile uint32_t *iat = a4_gl_iat_ptr();
    if (iat[0] < 0x10000u) return;
    volatile struct glr *s = (volatile struct glr *)A4_GL_SCRATCH;
    if (s->up != A4_GL_MAGIC || s->prims == 0 || s->flushed) return;
    s->flushed = 1;
    s->last_prims = s->prims;                            /* frame's prim count (device probe) */
    uint32_t tf0 = a4_ntick();
    A4_PERF[PF_T3D] += tf0 - A4_PERF[PF_MFIRST];

    /* PIPELINED (no A4_IMM): this frame's primitives are drawn now, at the frame's end,
     * and read back at the NEXT frame's first primitive (composite_prev) -- one frame of
     * latency for the GPU world against the software layers. IMMEDIATE (A4_IMM): drawn
     * and composited here, at the frame's first billboard or HUD call (a4_gl_hud) or the
     * present.
     * Draw the rest of THIS frame (the clear was at its first primitive, and any full
     * batch was drawn as it filled). */
    { uint32_t t0 = a4_ntick(); batch_draw(s->bfill); A4_PERF[PF_DRAW] += a4_ntick() - t0; }
    s->bfill = 0;
    if (s->fmode & AB_FLUSH) {
        /* A tile renderer (the MBX) renders nothing until something makes it: without
         * this the frame is only rendered when composite_prev's first glReadPixels asks,
         * and the pipelining buys nothing. */
        uint32_t t0 = a4_ntick(); gl_flush(); A4_PERF[PF_GLFLUSH] += a4_ntick() - t0;
    }
    if (s->dual) { s->dual = 0; A4_PERF[PF_DUAL]++; }
#ifdef A4_IMM
    /* IMMEDIATE: composite this frame's own image now -- at its first billboard blit or
     * HUD painter call (a4_gl_hud), else at the present. No frame of latency between the
     * GPU's objects and what software draws over them (billboards, tags, HUD, the blended
     * classes); and nothing software drew before this point may sit where the GPU drew,
     * or the composite covers it. */
    { uint32_t t0 = a4_ntick(); composite_rows(s, s->miny, s->maxy); A4_PERF[PF_COMP] += a4_ntick() - t0; }
#endif
#if defined(A4_RUNDIAG) || defined(A4_TEL)
    {   /* 0x412000: [0] frames [1] draw calls [2] triangles [3] max calls in a frame
         * [5] calls in the last frame (a4_tel.c writes these into the capture header) */
        volatile uint32_t *rd = (volatile uint32_t *)0x00412000u;
        A4_PERF[PF_CALLS] += rd[4]; A4_PERF[PF_TRIS] += s->prims;
        rd[0]++; rd[1] += rd[4]; rd[2] += s->prims; if (rd[4] > rd[3]) rd[3] = rd[4]; rd[5] = rd[4]; rd[4] = 0;
    }
#endif
    A4_PERF[PF_RENDERS]++;
    { uint32_t e = gl_geterror(); if (s->errc == 0) s->errc = e; }  /* catch a draw GL error */
    { uint32_t tf1 = a4_ntick(); A4_PERF[PF_TFLUSH] += tf1 - tf0; A4_PERF[PF_MFLEND] = tf1; A4_PERF[PF_FLAGS] |= PFF_2D; }

#ifdef A4_ATLAS
    atl_flush_pending(s);                           /* round 25: the atlas copies, after the readback */
#endif
    /* this frame's drawn extent is what composite_prev reads back next frame */
    s->pminy = s->miny; s->pmaxy = s->maxy;
#ifdef A4_SEQDUMP
    seq_frame_done();
#endif
}

/* Every call of the HUD sprite painter (a4_hud_tramp, +2973A) and of the two scaled
 * billboard blits (a4_bb_tramp +3015E, a4_bb2_tramp +3059E). A race frame draws ~1400
 * triangles, then its billboards (glows, the nitro flame, pickups) among its last ~25
 * triangles, then the HUD (round 12's order trace), so under A4_IMM the frame is drawn
 * out and composited at the first of these, before software draws over it. A no-op in a
 * frame with nothing drawn on the GPU or once done. */
void a4_gl_hud(void)
{
#ifdef A4_IMM
    a4_gl_flush();
#endif
}

/* Formerly the 3D->2D boundary (the frame's first translucent fill). A race
 * frame has no such fill, so the boundary is now the frame's first primitive
 * (composite_prev) and the frame end (a4_gl_frame). Kept for patch/fill.c. */
void a4_gl_fill_hook(void *fill_pixels)
{
    (void)fill_pixels;
}

/* GL bring-up state, for the on-screen diagnostic in a measurement build:
 *   0 = libGLES_CM did not link; 1 = linked, bring-up never entered;
 *   2..6 = bring-up reached this EGL step; 7 = up and running. */
int a4_gl_state(void)
{
    volatile uint32_t *iat = a4_gl_iat_ptr();
    if (iat[0] < 0x10000u) return 0;
    volatile struct glr *s = (volatile struct glr *)A4_GL_SCRATCH;
    return 1 + (int)s->bstep;
}

#ifdef A4_DUALCAP
/* Bench instrument (round 10): the GPU's picture of the frame just drawn, read back now
 * (the pbuffer holds it until the next frame's first triangle) as top-down RGB565 rows,
 * the sentinel as 0xF81F, handed to wr() strip by strip -- a4_tel.c appends it to the
 * capture, beside the software picture of the same frame (A4_DUALCAP makes software draw
 * every triangle as well). */
void a4_gl_dumpgpu(void (*wr)(int pos, const void *p, int len), int base)
{
    volatile struct glr *s = (volatile struct glr *)A4_GL_SCRATCH;
    if (s->up != A4_GL_MAGIC) return;
    uint32_t *rb = (uint32_t *)A4_GL_READBUF;   /* the strip buffer is exactly one RGBA strip */
    for (int y0 = 0; y0 < VP_H; y0 += STRIP) {
        gl_readpixels(0, y0, VP_W, STRIP, GL_RGBA, GL_UNSIGNED_BYTE, rb);
        for (int sy = STRIP - 1; sy >= 0; sy--) {     /* GL rows bottom-up -> screen rows */
            const uint32_t *src = rb + sy * VP_W;
            uint16_t *out = (uint16_t *)src;            /* in place: 2 bytes out per 4 read, forward */
            for (int x = 0; x < VP_W; x++) {
                uint32_t px = src[x], r = px & 0xFF, g = (px >> 8) & 0xFF, b = (px >> 16) & 0xFF;
                out[x] = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
#ifdef A4_DUALALPHA
                if ((px >> 24) < 0x80u) out[x] = 0xF81Fu;   /* bench: what the composite would not take */
#endif
            }
            int screen_y = VP_H - 1 - (y0 + sy);
            wr(base + screen_y * VP_W * 2, out, VP_W * 2);
        }
    }
}
#endif

#if defined(A4_MESHDUMP) && defined(A4_MESHCHECK)
/* bench: the frame's list for offline study, after the capture's other blocks: 16 words
 * (n, F, cx, cy, frame, 'MDMP'), then per triangle 64 bytes: x' y' z' u v (int16) x3,
 * texture word, the face's flags now, texture w | h << 16, its texels' address, the
 * texel at the texcoord centroid, bucket, box x0 y0 x1 y1, face address */
void a4_gl_meshdump(void (*wr)(int pos, const void *p, int len), int base)
{
    volatile struct mesh *m = MESH;
    const uint32_t *gctx = (const uint32_t *)m->gctx;
    uint32_t n = m->n, hd[16];
    for (int i = 0; i < 16; i++) hd[i] = 0;
    if (!gctx) n = 0;
    hd[0] = n; hd[1] = m->F; hd[4] = m->frame; hd[5] = 0x504D444Du;
    if (gctx) { hd[2] = (uint32_t)(*(const int32_t *)gctx[0x40110 / 4] >> 1); hd[3] = (uint32_t)(*(const int32_t *)gctx[0x40114 / 4] >> 1); }
    wr(base, hd, 64);
    uint32_t rec[16 * 16];
    for (uint32_t i0 = 0; i0 < n; i0 += 16) {
        uint32_t k = 0;
        for (uint32_t i = i0; i < n && i < i0 + 16u; i++, k++) {
            uint32_t *r = rec + k * 16;
            const int16_t *c = MESH_V + i * 18u;
            int16_t *rc = (int16_t *)r;
            for (int j = 0; j < 3; j++) for (int q = 0; q < 5; q++) rc[j * 5 + q] = c[j * 6 + q];
            rc[15] = 0;
            const uint32_t *ts = (const uint32_t *)(MESH_T[i] & ~3u);
            const uint32_t *face = (const uint32_t *)MESH_D[i];
            r[8] = MESH_T[i]; r[9] = face ? face[1] : 0; r[10] = ts[1] | (ts[2] << 16); r[11] = ts[TS_PIX];
            {   /* the centroid: a third by multiply (no divide in the blob) */
                int32_t u = (int32_t)(((int64_t)((int32_t)c[3] + c[9] + c[15]) * 21846) >> 16) >> 4;
                int32_t v = (int32_t)(((int64_t)((int32_t)c[4] + c[10] + c[16]) * 21846) >> 16) >> 4;
                uint32_t w = ts[1] + 1u, h = ts[2] + 1u;
                u &= (int32_t)(w - 1u); v &= (int32_t)(h - 1u);
                r[12] = ((const uint16_t *)ts[TS_PIX])[(uint32_t)v * w + (uint32_t)u] | ((uint32_t)MESH_Q[i] << 16); }
            const int16_t *b = MESH_B + i * 4u;
            r[13] = (uint16_t)b[0] | ((uint32_t)(uint16_t)b[1] << 16); r[14] = (uint16_t)b[2] | ((uint32_t)(uint16_t)b[3] << 16);
            r[15] = (uint32_t)(uintptr_t)face;
        }
        wr(base + 64 + (int)i0 * 64, rec, (int)k * 64);
    }
}
#endif

/* Bring-up facts for the telemetry header: 0/1/2 the OES_read_format probe (format,
 * type, its error), 3 the pbuffer config, 4 its depth bits. */
uint32_t a4_gl_rdinfo(int k)
{
    volatile struct glr *s = (volatile struct glr *)A4_GL_SCRATCH;
    switch (k) {
    case 0: return s->rdfmt;
    case 1: return s->rdtype;
    case 2: return s->rderr;
    case 3: return s->zcfg;
    case 4: return s->zbits;
    case 6: return s->sbits;
    case 7: return s->abits;
    default: return s->ntex;
    }
}

/* Device probe accessors (read by a4_fps.c's -DA4_GAUGE gauge). */
int a4_gl_prims(void)
{
    volatile uint32_t *iat = a4_gl_iat_ptr();
    if (iat[0] < 0x10000u) return 0;
    volatile struct glr *s = (volatile struct glr *)A4_GL_SCRATCH;
    return (int)s->last_prims;
}
int a4_gl_errc(void)
{
    volatile uint32_t *iat = a4_gl_iat_ptr();
    if (iat[0] < 0x10000u) return 0;
    volatile struct glr *s = (volatile struct glr *)A4_GL_SCRATCH;
    return (int)s->errc;
}
int a4_gl_depthcfg(void)
{
    volatile uint32_t *iat = a4_gl_iat_ptr();
    if (iat[0] < 0x10000u) return 0;
    volatile struct glr *s = (volatile struct glr *)A4_GL_SCRATCH;
    return (int)s->depthcfg;
}
int a4_gl_pixmapcap(void)
{
    volatile uint32_t *iat = a4_gl_iat_ptr();
    if (iat[0] < 0x10000u) return 0;
    volatile struct glr *s = (volatile struct glr *)A4_GL_SCRATCH;
    return (int)s->pixmapcap;
}

/* A4_WINDOW diagnostic: how far window bring-up got. 0 = not entered; 7 = the
 * EGL window surface is up; 0xC0..0xC3 = a bring-up step bailed (cone Static
 * missing / no session found / no group found); see bringup(). */
int a4_gl_winstep(void)
{
    volatile uint32_t *iat = a4_gl_iat_ptr();
    if (iat[0] < 0x10000u) return 0;
    volatile struct glr *s = (volatile struct glr *)A4_GL_SCRATCH;
    if (s->up != A4_GL_MAGIC && s->winstep == 0) return 0;
    return (int)s->winstep;
}

#ifdef A4_WINDOW
/* The fps bar as GL: untextured quads along the top rows of the window, the
 * same scale as the software bar (4 px per fps, red tick at 30, white at 15
 * and 45), because the software buffer is not presented while the GPU is. */
static void bar_quad(int x0, int y0, int x1, int y1, int r, int g, int b)
{
    int16_t q[12] = { (int16_t)x0,(int16_t)y0, (int16_t)x1,(int16_t)y0, (int16_t)x1,(int16_t)y1,
                      (int16_t)x0,(int16_t)y0, (int16_t)x1,(int16_t)y1, (int16_t)x0,(int16_t)y1 };
    gl_color4x(r, g, b, FX);
    gl_vertexptr(2, GL_SHORT, 0, q);
    gl_drawarrays(GL_TRIANGLES, 0, 6);
}
static void bar_draw(uint32_t fps)
{
    if (fps > 60u) fps = 60u;
    gl_disable(GL_TEXTURE_2D);
    bar_quad(0, 0, 240, 8, FX/4, FX/4, FX/4);               /* 0..60 reference, dim */
    bar_quad(0, 0, (int)fps * 4, 8, 0, FX, 0);              /* length = fps, green */
    bar_quad(120, 0, 122, 8, FX, 0, 0);                     /* 30 */
    bar_quad(60, 0, 61, 8, FX, FX, FX);                     /* 15 */
    bar_quad(180, 0, 181, 8, FX, FX, FX);                   /* 45 */
    gl_color4x(FX, FX, FX, FX);
    gl_enable(GL_TEXTURE_2D);
}
#endif

#if defined(A4_ABMARK) && !defined(A4_ABFRAME)
/* The A/B mark: a green square in the top-right corner, written into the game's surface at
 * the present, after the frame's HUD, so the owner can say which half of the A/B a report is
 * about. Rounds 28 and 31 pushed a 4x4 quad at (0,0) with the 3D instead; the HUD, drawn
 * after the composite, covered three of its four columns and the phone showed a strip one
 * pixel wide (the owner, round 31: "around 1x3 pixels on the top left"; the bench's r31cm
 * screenshot, measured: 3 x 11 window pixels = 1 x 4 game pixels left green). Owner: a
 * proper square, 4x4, top right. Round 32, owner: "please make the green box bigger (16x16
 * maybe?)" -- A4_MARK_W 16. */
#ifndef A4_MARK_W
#define A4_MARK_W 16
#endif
static void mark_draw(void)
{
#ifdef A4_ABCYCLE10
    if (!(A4_PERF[PF_MODE] & AB_HSPEC)) return;     /* round 31: the half-step frames on */
#elif defined(A4_MESH)
    if (!MESH_ON()) return;                         /* round 28: a list frame */
#endif
#if !defined(A4_DUALCAP) && !defined(A4_WINDOW)
    uint32_t bb = *(volatile uint32_t *)A4_GL_BB;
    if (!bb) return;
    for (int y = 0; y < A4_MARK_W; y++) {
        volatile uint16_t *row = (volatile uint16_t *)bb + y * FB_STRIDE;
        for (int x = VP_W - A4_MARK_W; x < VP_W; x++) row[x] = (uint16_t)0x07E0;
    }
#endif
}
#endif

#ifdef A4_SLOWREC
/* Round 34 (owner: "the crashes and takedowns are still sluggish"; round 33's speculation is
 * 6 ms at most and the logic small, so the time is in the frame): the race's 16 slowest frames,
 * each split by phase, and the last 8 frames in which list triangles went undrawn (a candidate
 * for the owner's empty frame). At each 3D present, the phase counters' growth since the last
 * one. 0x455800: [0] 'SLW1', [1] frames seen, [2] the last present's tick, [3..15] the counters
 * then; 0x455880: 16 slowest; 0x455C80: 8 lossy (a ring, [3] its next slot). An entry: frame ms,
 * 3D, hook, composite, 2D, present, logic, speculation, the render call, its part before the
 * first triangle, the frame's triangles, list triangles drawn, list triangles lost (texture
 * failures, and a list never drawn by the present), mode (low 16) | never drawn (10 bits) <<
 * 16 | stale lists drawn (6 bits) << 26, frame number, the previous 3D frame's triangles.
 * Round 36 (the owner's empty frame, "almost always going up the hill before the pavement
 * surrounded by cones", not reproduced on the bench, screenshots every 0.25 s through it):
 * the second ring also takes a frame with half the triangles of the 3D frame before it or
 * fewer (from more than 600), [18] counts them. */
#define A4_GL_SLOW 0x00455800u
_Static_assert(A4_GL_SLOW >= 0x00455000u + (16u * 25u + 20u + 1u) * 4u, "the slow frames over the bitmaps");
_Static_assert(A4_GL_SLOW + 0x680u <= 0x00460000u, "the slow frames past 0x460000");
static uint32_t slow_d(volatile uint32_t *was, uint32_t now) { uint32_t d = now >= *was ? now - *was : now; *was = now; return d; }
static void slow_rec(uint32_t had)
{
    volatile uint32_t *S = (volatile uint32_t *)A4_GL_SLOW, *pf = A4_PERF;
    uint32_t now = a4_ntick();
    if (S[0] != 0x31574C53u) { for (uint32_t i = 0; i < 0x680u / 4u; i++) S[i] = 0; S[0] = 0x31574C53u; S[2] = now; return; }
    uint32_t e[16];
    e[0] = now - S[2]; S[2] = now;
    e[1] = slow_d(S + 3, pf[PF_T3D]);   e[2] = slow_d(S + 4, pf[PF_HOOK]);  e[3] = slow_d(S + 5, pf[PF_TFLUSH]);
    e[4] = slow_d(S + 6, pf[PF_T2D]);   e[5] = slow_d(S + 7, pf[PF_TPRES]);
#ifdef A4_STEPTIME
    e[6] = slow_d(S + 8, pf[PF_TLOGIC]); e[7] = slow_d(S + 9, pf[PF_TSPEC]);
    e[8] = slow_d(S + 10, STEPB[28]);   e[9] = slow_d(S + 11, STEPB[29]);
#else
    e[6] = e[7] = e[8] = e[9] = 0;
#endif
    e[10] = had;
#ifdef A4_MESH
    e[11] = slow_d(S + 12, pf[PF_MESH]); e[12] = slow_d(S + 13, pf[PF_MLOST]);
#else
    e[11] = e[12] = 0;
#endif
    e[13] = pf[PF_MODE] & 0xFFFFu;
#ifdef A4_MESH
    {   volatile struct mesh *m = MESH;
        uint32_t nd = (m->frame == ((volatile struct glr *)A4_GL_SCRATCH)->frames && !m->drawn) ? m->n : 0u;
        uint32_t st = slow_d(S + 16, m->stale);
        e[13] |= (nd > 0x3FFu ? 0x3FFu : nd) << 16 | (st > 63u ? 63u : st) << 26;   /* never drawn | stale lists */
        e[12] += nd; S[13] += nd;   /* a4_gl_frame adds nd to PF_MLOST after this: not the next frame's */
    }
#endif
    e[14] = ((volatile struct glr *)A4_GL_SCRATCH)->frames;
    e[15] = S[17];                                  /* round 36: the previous 3D frame's triangles */
    S[1]++;
    if (!had) return;
    uint32_t drop = S[17] > 600u && had * 2u < S[17];   /* round 36: half the triangles of the frame before or fewer */
    S[17] = had;
    volatile uint32_t *R = S + 32;                  /* the 16 slowest: replace the quickest */
    uint32_t lo = 0;
    for (uint32_t i = 1; i < 16u; i++) if (R[i * 16] < R[lo * 16]) lo = i;
    if (e[0] > R[lo * 16]) for (uint32_t k = 0; k < 16u; k++) R[lo * 16 + k] = e[k];
    if (drop) S[18]++;                              /* drop frames (total) */
    if (e[12] || drop) {                            /* list triangles lost, or a sudden drop in triangles */
        volatile uint32_t *L = S + 32 + 256;
        uint32_t j = S[15] & 7u; S[15]++;
        for (uint32_t k = 0; k < 16u; k++) L[j * 16 + k] = e[k];
        ((volatile struct glr *)A4_GL_SCRATCH)->droprec = j + 1u;   /* A4_DROPHOLD adds its verdict */
    }
}
#endif

/* Present detour (once per frame): draw the frame's captured primitives on the
 * GPU, then start the next frame. Returns 1 when the GPU presented the frame
 * itself (A4_WINDOW), so the detour skips the game's own present -- the 565 ->
 * 32-bit convert into the hardware bitmap that would paint over the window. */
#ifdef A4_DROPHOLD
/* round 37: called when a frame is held back -- a fixed block entry for the bench's
 * EKA2L1_PCTRACE_DUMPAT (its address: A4_PRINTSYMS=a4_drop_mark at build time) */
__attribute__((noinline)) void a4_drop_mark(uint32_t had, uint32_t prev)
{
    __asm__ volatile("" : : "r"(had), "r"(prev) : "memory");
}

/* round 37: how long a hold may last, in a4_ntick() ticks (1 ms on the N95): until a
 * render brings a frame of its own, at most this long. The empty render is quick (15-19
 * ms against ~70), so the race loop comes round before the next half-step is due and
 * iterates with no render, and a held present skips the present's pacing: r37i's
 * count-bounded hold (8) ran out within one 56 ms post interval and the frame went out. */
#define DROPHOLD_TICKS 250u
static int hold_live(void)
{
    if (!A4_PERF[PF_HOLDCALL]) return 0;
    if (a4_ntick() - A4_PERF[PF_HOLDT] < DROPHOLD_TICKS) return 1;
    A4_PERF[PF_HOLDCALL] = 0;
    A4_PERF[PF_PREVHAD] = 0;   /* timed out: the hold is over for good (r38m: without this the
                                * next sparse render began a fresh hold, again and again) */
    return 0;
}

/* round 37: a4_fwpost_tramp (a4_tramp.s, at the framework's post +6B2) asks whether a
 * frame is held: 1 skips the framework's post. The bench's trace at a held frame:
 * DoPresent's present skipped, then the framework routine +606 posted the back buffer
 * itself (+6AC/+6B4), and again after the next present that brought no render -- so the
 * held frame reached the screen twice anyway. */
int a4_fw_hold(void)
{
    if (!hold_live()) return 0;
    A4_PERF[PF_FWHELD]++;
    return 1;
}
#endif
#ifdef A4_FRAMEMARK
/* round 37, bench only (needs A4_DROPHOLD): called for every present that brought 3D or
 * goes out, for EKA2L1_PCTRACE_REGS -- r0 = the 3D triangles since the last present, r1 =
 * the 3D frame's before, r2 = the present count, r3 = 1 when the present is not made */
__attribute__((noinline)) void a4_frame_mark(uint32_t had, uint32_t prev, uint32_t frames, uint32_t skipped)
{
    __asm__ volatile("" : : "r"(had), "r"(prev), "r"(frames), "r"(skipped) : "memory");
}
/* round 41, bench only: called just before a present that is made, r0 = the real time
 * (a4_ntick, 1 ms) -- when the player sees the frame, not when its render began */
__attribute__((noinline)) void a4_present_mark(uint32_t t)
{
    __asm__ volatile("" : : "r"(t) : "memory");
}
#endif

int a4_gl_frame(uint32_t fps)
{
    (void)fps;
    volatile uint32_t *iat = a4_gl_iat_ptr();
    if (iat[0] < 0x10000u) return 0;
    volatile struct glr *s = (volatile struct glr *)A4_GL_SCRATCH;
    if (s->up != A4_GL_MAGIC) return 0;
    uint32_t had = s->prims;
#ifdef A4_PRESENTDIAG
    /* choppy/wobble diagnosis: count presents that carried new geometry vs presents
     * with none (a re-present of the same world). depthcfg/pixmapcap are unused in
     * the blit build; read them back via MEMSNAP (g[9]=empty, g[10]=real). */
    if (had) s->pixmapcap++; else s->depthcfg++;
#endif
#ifdef A4_LRCAP
    /* histogram the DoPresent caller (stashed at 0x400840 by a4_fps_tramp) split by
     * whether 3D geometry was drawn this frame (had). Table at 0x400A00:
     * [0]=n, then n entries of (caller, real_count, empty_count). MEMSNAP reads it. */
    {
        uint32_t caller = *(volatile uint32_t *)0x00400840u;
        volatile uint32_t *tab = (volatile uint32_t *)0x00400A00u;
        uint32_t n = tab[0], i;
        for (i = 0; i < n; i++) if (tab[1 + i*3] == caller) break;
        if (i == n && n < 40u) { tab[1 + i*3] = caller; tab[1 + i*3 + 1] = 0; tab[1 + i*3 + 2] = 0; tab[0] = n + 1; }
        if (i < 40u) { if (had) tab[1 + i*3 + 1]++; else tab[1 + i*3 + 2]++; }
    }
#endif
    if (A4_PERF[PF_FLAGS] & PFF_2D) A4_PERF[PF_T2D] += a4_ntick() - A4_PERF[PF_MFLEND];
#if defined(A4_PX6) && !defined(A4_PX6_NOCVT)
    cvt_install();                                      /* the runtime's convert, ours from now on */
#endif
    a4_gl_flush();                                      /* draw this frame on the GPU */
    A4_PERF[PF_FLAGS] &= ~PFF_2D;                       /* a composite inside it has no 2D after */
#if defined(A4_ABMARK) && !defined(A4_ABFRAME)
    if (had) mark_draw();                               /* after the flush's composite and the HUD */
#endif
#ifdef A4_SLOWREC
    slow_rec(had);
#endif
    int presented = 0;
#ifdef A4_WINDOW
    if (s->surf) {
        void *appsess = (void *)s->appsess, *win = (void *)A4_WS_OBJ;
        /* The present hook fires more than once a game frame (the runtime's
         * cadence, ~1.5x in the profile), so a hook with no primitives is not a
         * 2D frame; only a run of them is (a menu). While our window is up the
         * game's present is skipped every time, or a software frame reaches
         * the panel between two of ours. */
        if (had) {                                      /* a 3D frame: the GPU presents */
            s->idle2d = 0;
            if (!s->wvis) { rw_setvisible(win, 1); ws_flush(appsess); s->wvis = 1; }
            bar_draw(fps);
            egl_swap((int)s->disp, (int)s->surf);
        } else if (++s->idle2d > 30u && s->wvis) {       /* 2D for a while (menu): the game presents */
            rw_setvisible(win, 0); ws_flush(appsess); s->wvis = 0;
        }
        presented = (int)s->wvis;
    }
#else
    (void)had;
#endif
#ifdef A4_SKIPREPRES
    /* The race loop (+808) presents three times an iteration: +950 and +97E re-post the
     * previous frame, +9A6 the new one (MBX.md, the lr-capture). Each present call stretches
     * to a fixed period (round 16's timers: the frame pinned at 85.0 ms, the slack inside the
     * present calls), so the two re-posts cap the render rate. Skipped here -- the screen
     * already shows that frame -- and only from those two sites: menus present from
     * elsewhere. DoPresent's caller return is stashed at 0x400840 by a4_fps_tramp. */
    {   extern const int32_t a4_rep1, a4_rep2;
        uint32_t caller = *(volatile uint32_t *)0x00400840u;
        uint32_t r1 = (uint32_t)(uintptr_t)&a4_rep1 + (uint32_t)a4_rep1;
        uint32_t r2 = (uint32_t)(uintptr_t)&a4_rep2 + (uint32_t)a4_rep2;
        if ((A4_PERF[PF_MODE] & AB_SKIPREP) && (caller == r1 || caller == r2)) { presented = 1; A4_PERF[PF_SKIPPED]++; }
#ifdef A4_FRAMEMARK
        uint32_t frame_prev0 = A4_PERF[PF_PREVHAD];
#endif
#ifdef A4_DROPHOLD
        /* round 37: the empty frame (the owner's photo 7; the bench's posts 1205-1206 of
         * glpd): on the LA hill one render in about two laps draws the car, the road and the
         * backdrop and none of the terrain -- 2 to 78 triangles against 1,300 to 1,700 in
         * the frames either side, the game's own render call short (15-19 ms, its part
         * before the first triangle 2-4 ms), with the speculation on and off alike. Its
         * cause is NOT found: the theory is the game's own visibility failing for a camera
         * inside a narrow stretch (under one frame's travel: never two empty renders in a
         * row), which a faster frame rate samples more often; the stock game passed the
         * hill 3 times on the bench without one, which does not exclude it at its rate.
         * Whatever the cause, a real present whose 3D collapsed below a half of the last
         * 3D frame that went out (from over 400), with the camera where it was, is not
         * made: the screen keeps the last frame until a render brings one of its own, at
         * most DROPHOLD_TICKS.
         * The game also presents from the same site with no render since the last present
         * (159 times in r37d's race, a render the half-step skipped), posting the back
         * buffer again -- which still holds the held frame: r37d held it and posted it
         * twice that way (posts 1146-1147). So the presents after a hold that bring no
         * render are held too, from that call site only (a menu presents from elsewhere
         * and is never held), until a render brings a frame of its own -- and with them
         * the framework's own post after each (a4_fw_hold); for at most DROPHOLD_TICKS. */
        if (!presented) {
            uint32_t rendered = s->rendered, camok = s->camok;
            s->rendered = 0; s->camok = 0;
            if (had || rendered) {
                /* a render's present: compared with the last frame that went out (PF_PREVHAD
                 * is not moved by a held one), so a collapse over two renders (r37n: 1,398 ->
                 * 178 -> 68) and a render with no 3D at all (r37m: 0) are held as one; under a
                 * sixth (178 of 1,398 was 0.127; every legitimate cut seen 0.33 or more); and
                 * never longer than DROPHOLD_TICKS from the hold's start, renders included,
                 * so a real cut to a sparse view goes out at most that late */
                uint32_t prev = A4_PERF[PF_PREVHAD], now = a4_ntick();
                uint32_t holding = A4_PERF[PF_HOLDCALL] != 0;
                /* round 38: the ratio alone could not part the empty render from a camera cut
                 * (the phone's last-lap empty render 0.1668, its cuts 0.23 and up; the bench's
                 * crash close-up 148 of 2,250). A cut moves the camera off its place behind
                 * the car; the empty render leaves it there. So: under a quarter, from a race
                 * render whose camera-to-car offset is within 96 units on each axis of the
                 * last frame that went out. */
                uint32_t dev = 0x7FFFu;
                if (rendered && camok) {
                    dev = 0;
                    for (int i = 0; i < 3; i++) { int32_t d = s->camoff[i] - s->goodoff[i]; uint32_t a = (uint32_t)(d < 0 ? -d : d); if (a > dev) dev = a; }
                    if (dev > 0x7FFFu) dev = 0x7FFFu;
                }
                /* round 39: the phone's empty renders drew 146-280 triangles, 0.19-0.30 of the
                 * frame before, over r38's quarter, and Paris's frames before them ~650; the
                 * camera test parts the cuts, so: under a half, from over 400 */
                /* and the last frame shown was the chase camera's: r39m's crash close-up (the
                 * camera ~830 units ahead, looking back) kept its offset within 96 too while its
                 * triangles halved, and was held ~300 ms. Over a whole LA race the chase camera
                 * sat 200-300 units from the car on the ground (4,481 of 4,595 renders) and
                 * 75-175 above it. Another camera view the player picks is not held. */
                /* round 40: the chase camera is the running average of the offset (r39's fixed
                 * 150-350/50-200 came from the bench car and missed the owner's camera) */
                int32_t gd = (int32_t)isqrt64((int64_t)s->goodoff[0] * s->goodoff[0] + (int64_t)s->goodoff[2] * s->goodoff[2]) - s->chaseavg[0] / 16;
                int32_t gy = s->goodoff[1] - s->chaseavg[1] / 16;
                uint32_t chase = s->chaseinit && gd <= 64 && gd >= -64 && gy <= 64 && gy >= -64;
                uint32_t steady = dev <= 96u && chase;
                uint32_t hold = prev > 400u && had * 2u < prev && steady && !(holding && now - A4_PERF[PF_HOLDT] >= DROPHOLD_TICKS);
#ifdef A4_SLOWREC
                if (s->droprec) {   /* the record's drop entry for this frame: lost | camera deviation << 16 | held << 31 */
                    volatile uint32_t *le = (volatile uint32_t *)0x00455800u + 32 + 256 + (s->droprec - 1u) * 16u + 12u;
                    *le = (*le & 0xFFFFu) | dev << 16 | hold << 31;
                }
#endif
                if (hold) {
                    presented = 1; A4_PERF[PF_HELD]++;
                    if (!holding) { A4_PERF[PF_HOLDCALL] = caller; A4_PERF[PF_HOLDT] = now; a4_drop_mark(had, prev); }
                } else {
                    A4_PERF[PF_PREVHAD] = had;
                    A4_PERF[PF_HOLDCALL] = 0;
                    if (rendered && camok) for (int i = 0; i < 3; i++) s->goodoff[i] = s->camoff[i];
                }
            } else if (hold_live()) {
                if (caller == A4_PERF[PF_HOLDCALL]) { presented = 1; A4_PERF[PF_HELD] += 0x10000u; }
                else A4_PERF[PF_HOLDCALL] = 0;
            }
        }
#endif
#ifdef A4_FRAMEREC
        if (!presented) {   /* round 41: the frame record -- when the last render's frame went out */
            volatile uint32_t *F = (volatile uint32_t *)A4_GL_FREC;
            if (F[0] == 0x31435246u && F[1]) {
                volatile uint32_t *e = F + 4u + ((F[1] - 1u) % FREC_N) * 8u;
                if (!e[1]) e[1] = a4_ntick();
            }
        }
#endif
#ifdef A4_FRAMEMARK
        if (!presented) a4_present_mark(a4_ntick());
        if (had || !presented) {
            a4_frame_mark(had, frame_prev0, s->frames, (uint32_t)presented);
            /* and the present count stamped in the bottom row's first 32 pixels (white = 1),
             * so each post in an EKA2L1_POSTDUMP names its present */
            volatile uint16_t *fb = (volatile uint16_t *)*(volatile uint32_t *)A4_GL_BB;
            if (fb) for (uint32_t k = 0; k < 32u; k++) fb[(VP_H - 1u) * FB_STRIDE + k] = ((s->frames >> k) & 1u) ? 0xFFFFu : 0u;
        }
#endif
#ifdef A4_SLOWREC
        {   /* round 35 (the owner's empty frame on the hill; the bench's presented frames show a
             * fresh backdrop over the last frame, the top HUD gone): the game's presents that
             * reach the screen with no render since the last one, and their callers. 0x455800
             * [20] real presents, [21] of them with no render since the last real one, [22]
             * with no 3D, [23] presents skipped, [24] renders, [25] renders skipped (half-step),
             * [26..29] two (caller, count) of the no-render real presents, [30] A4_DROPHOLD's
             * frames held back, [31] its framework posts held (round 37; a third caller before) */
            volatile uint32_t *S = (volatile uint32_t *)0x00455800u;
#ifdef A4_DROPHOLD
            S[30] = A4_PERF[PF_HELD];   /* round 37: frames held back (words 30-31 were a third caller) */
            S[31] = A4_PERF[PF_FWHELD]; /* round 37: framework posts held */
#endif
            if (presented) S[23]++;
            else {
                S[20]++;
                if (!had) S[22]++;
                if (!S[19]) {
                    S[21]++;
                    uint32_t k;
                    for (k = 0; k < 2u; k++) if (S[26 + 2 * k] == caller || !S[26 + 2 * k]) break;
                    if (k < 2u) { S[26 + 2 * k] = caller; S[27 + 2 * k]++; }
                }
                S[19] = 0;
            }
        }
#endif
    }
#endif
    s->droprec = 0;
    s->prims = 0;
    s->flushed = 0;
#ifdef A4_MESH
    if (MESH->frame == s->frames && !MESH->drawn && MESH->n) A4_PERF[PF_MLOST] += MESH->n;  /* taken, never drawn */
    MESH->active = 0;
#endif
    s->frames++;
#ifdef A4_TEL
    a4_tel_tick(had);   /* on-device capture: C:\a4telN.dat; had = 3D prims this present */
#endif
    A4_PERF[PF_MPRES] = a4_ntick(); A4_PERF[PF_FLAGS] |= PFF_PRES;   /* the game's present call next */
    return presented;
}

/* After the game's present call (+1EE, a4_pres_tramp): its time, the runtime's 565 to
 * 32-bit convert and the post, every present (re-presents included). */
void a4_gl_presented(void)
{
    if (A4_PERF[PF_FLAGS] & PFF_PRES) { A4_PERF[PF_TPRES] += a4_ntick() - A4_PERF[PF_MPRES]; A4_PERF[PF_FLAGS] &= ~PFF_PRES; }
    A4_PERF[PF_NPRES]++;
}
