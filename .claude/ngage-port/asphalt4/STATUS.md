# Asphalt 4 on the N95: the current picture

The one page to read first. MBX.md is the full record (every round, every run, in order); this file is what
is true now -- how a frame is made, where the phone's time goes, and the plan. Keep it current: when a step
lands, change the numbers and the plan here in the same commit as its MBX.md entry.

Owner's goals: 30 fps on the Nokia N95 8GB, real frames only, physics as stock, fixes that hold everywhere.
Owner's rule for this stretch (after phone round r87a): **do the whole reverse engineering on the bench, take
the gains, then one phone round** -- no build per finding.

## How a frame is made (C87, round 87)

The game is the N-Gage 2.0 binary, unchanged on disk except what tools/injector.py adds: a blob of our code
(renderer/*.c, *.s, the lifted engine renderer/eng/lifted.c) and hooks into the game's own functions.

- **Logic** -- the 3x engine (a4_eng.h, ENGINE.md): the game's step run at three substeps a render, so motion
  is the game's own at the phone's frame rate.
- **The scene** (+12992 -> +12952 -> +D3BA, lifted as engi_0D3BA):
  - the scenery walk (+CE8C per object): a static object whose textures are plain is **taken** (a4_obj_take):
    its mesh baked once into the object store (GOBJ, up to 20 texture groups since r87), drawn on the GPU with
    the camera's matrix -- the game's per-vertex transform and per-face work skipped. Anything else (alpha-map
    textures, too near, moved, out of range) goes the game's way;
  - the cars (+34AEA -> +349F8): the same for car meshes (GCAR), taken before the transform when certainly
    visible (r86b);
  - the game's own per-face path for what is left: +349F8's transform, cull and insert (+349B0) -> our list
    (a4_mesh_insert, mesh_take), or the game's ordering table (OT) for classes the list does not take;
  - the shadow's faces (+2732E) batched (A4_SHADBATCH);
  - **the table's draw** (+31CC6): the OT walked in order; every triangle reaches our hook (a4_gl_draw_prim ->
    draw_prim_take), which batches it for the GPU. At the frame's first triangle the GPU objects, cars and the
    list are drawn (gobj_flush, gcar_flush, mesh_draw).
- **The composite** (A4_IMM, a4_gl_flush): at the first billboard or HUD call the GPU's frame is drawn out,
  read back in strips (glReadPixels; the first read waits for the MBX's tile render) and written into the game's
  565 screen wherever the GPU drew.
- **2D and the present**: the game's HUD and sprites in software over it, then its present (the runtime's
  convert and post).
- Blended classes (alpha-map, additive) are drawn twice: on the GPU and by the game's span, whose copy shows only
  where no GPU class drew (the software sky).

## Where the phone's time goes (r87a, Paris, FRC4 -- every render split, ms)

| block | light segments | Montmartre (30-70) | nitro boost |
|---|---|---|---|
| **a render** | **51-54** | **61-68** | **+17** |
| before the 3D: logic (game steps) | 3.1-3.4 | 3.5-4.1 | |
| before the 3D: scenery walk | 0.9-1.2 | 3.3-4.4 | |
| before the 3D: shadow insert, +31A26/50, cars | 3.5 | 3.5 | |
| before the 3D: not yet attributed | ~8 | ~8-11 | |
| table's draw: our hook (GL submission) | 12-14 | 15-19 | |
| table's draw: the game's own walk | ~6 | ~9 | |
| after the table (the nitro blur, +1054C) | 0 | 0 | **17** |
| composite (reads 7 of it) | 10 | 11 | |
| 2D + present | 6 | 6 | |

GL a render: ~155-200 draws, 135-150 binds, ~85 matrix, ~170 other calls; 9-13k corners.

Corrections this table carries: the r86c reading "the game's side is +20 ms in Montmartre" was mostly nitro
(the owner marks where it drops, and it drops in boosts); without boosts Montmartre's extra is ~14 ms, spread
over the walk, our hook and the game's walk of the table. The bench "not reproducing the phone" was largely the
bench never boosting at speed (its car reached 327 km/h, the phone's boosts run at 360-534).

## The plan (the RE program, bench only, then one phone round)

Ordered by size and certainty. Each step: read it from the binary, make it exact, check it on the bench (a
check build comparing against the game's own result where possible), log it, update this table.

1. **Nitro's radial blur** (+1054C, MBX.md round 87): today the game spends ~17 ms a boosted render on it and the
   composite paints over it, so it is never seen. Our own exact routine (same order, same arithmetic) run after
   the composite at +12A12: the blur visible again, ~3-4 ms instead of 17. First, a bench that boosts at speed.
2. **The ~8 ms before the 3D that no timer names** -- profile it on a bench that now enters the phone's states.
3. **Our GL submission** (12-19 ms, ~550 calls a render): redundant state, binds and draws a texture; the
   shadow's and the batch's draws.
4. **The game's walk of the table** (6-9 ms): its per-triangle dispatch around our hook.
5. **The composite** (10-11 ms every render): the read-back and merge -- the largest fixed cost; round 55's
   direct-present design is the way to remove it.
6. **The alpha-map objects** (15-20 a render in Montmartre, the scenery still on the game's path): taking them
   needs their blend in the right order.

## Progress

| step | state | gain (bench) | notes |
|---|---|---|---|
| r86: objects' sort and box, cars before the transform | on the phone (r86c) | ~3 ms in heavy LA | |
| r87: meshes of up to 20 texture groups on the GPU | on the phone (r87a) | phone: Montmartre ~72 -> ~66 ms | |
| 1. nitro blur (A4_NITROBLUR, C88) | done, bench | ~8.5 ms a boosted render (15.2 -> 6.7 on the model); exact, 0 px of 351 blurs | the blur visible again |
| 2. the unattributed ~8 ms before the 3D | next | | |

## Where things are

- Code: renderer/ (a4_gl.c the renderer and its instruments, a4_eng.h the engine, a4_tel.c the telemetry,
  eng/ the lifted functions and their edits), tools/injector.py (the build), flags/CNN.flags (each round's set,
  README there), tools/frec3.py (reads the captures: modes phase, steps, obj, gob, timers, ...).
- The record: MBX.md (rounds, runs, results), ENGINE.md (the engine read from the binary), EMU_PATCHES.md at the
  repository root (every emulator switch the bench uses). Older overviews (NOTES.md, PATCHING.md, FINISH.md) are
  history from rounds A1-A5.
- Rules: ../CLAUDE.md (the four rules; print them in every reply that reports a result or asks for a phone round).
