# Asphalt 4 on the N95: the current picture

The one page to read first. MBX.md is the full record (every round, every run, in order); this file is what
is true now -- how a frame is made, where the phone's time goes, and the plan. Keep it current: when a step
lands, change the numbers and the plan here in the same commit as its MBX.md entry.

Owner's goals: 30 fps on the Nokia N95 8GB, real frames only, physics as stock, fixes that hold everywhere.
Owner's rule for this stretch (after phone round r87a): **do the whole reverse engineering on the bench, take
the gains, then one phone round** -- no build per finding.

## How a frame is made (C89, round 88)

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
  where no GPU class drew. Since C89 the sky is the GPU's too (the routine +35FDA's stores recorded and drawn as
  textured quads at the first triangle, pixel-exact; the stores still run, since the player's car paint reads the
  screen's top rows as its reflection table), so copies within the sky's rows are skipped; Dubai's sun
  (+3059E) keeps the sky in software for 128 frames after it is drawn.
- The scene's last call, +37FA4, tests the sun's visibility on the screen's pixels and draws its lens flare: since
  C89 the GPU's frame is composited before it (A4_FLAREFLUSH), as stock's order.

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

## The phone with r90b (Paris two races, New York, LA; MBX.md "Phone round r90b")

| map | renders under 60 ms | 75 ms or more | boosted | its own cost |
|---|---|---|---|---|
| Paris | 67% (r87a 47%) | 3.7% (9.9%) | 36% | the paint re-bakes: ~30 ms in 2% of renders |
| New York | 52% | 1.2% | 23% | the snow: ~5 ms every render (the 2D phase) |
| LA | 72% | 0.2% | 39% | none |

An unboosted render ~51-54 ms (~19 fps), a boosted one ~60 (+8-9 ms: our blur).

Paris in detail:

- Renders under 60 ms 67% (r87a 47%), 75 ms or more 3.7% (9.9%); Montmartre (segments 30-99) 49-67 ms a render
  (r87a 60-68), its 75 ms-plus renders about halved. The game's own part of the 3D pass 6-8 ms (r87a up to 27).
- Per render, light renders: 3D 16.1 (our hook 10.4), composite 10.1 (read 7.5: ~5.5 of it the GPU's tile render),
  2D 4.4, yield 2.2, the rest 14.4 = logic 2.7 + walk 1.4 + cars 0.3 + shadow insert 1.4 + +31A26/50 1.5 + **the
  runtime's post ~2.8 (our convert ~2.5)** + ~4 unnamed.
- **Boosts: ~8.4 ms a boosted render** (our blur; the game's was ~17) -- 36% of the owner's renders boosted.
- **Hitches: ~30 ms re-bakes of the car paint texture** (its table is the screen's top rows), 2% of renders.

## The plan (the RE program, bench only, then one phone round)

Ordered by size and certainty. Each step: read it from the binary, make it exact, check it on the bench (a
check build comparing against the game's own result where possible), log it, update this table.

1. **Nitro's radial blur** (+1054C, MBX.md round 87): today the game spends ~17 ms a boosted render on it and the
   composite paints over it, so it is never seen. Our own exact routine (same order, same arithmetic) run after
   the composite at +12A12: the blur visible again, ~3-4 ms instead of 17. First, a bench that boosts at speed.
2. **The whole frame by cycles** (done, MBX.md round 88 step 2): the blended classes' and additive billboards'
   software copies are the largest block of the game's own work in the table; within the sky's rows the GPU's sky
   (round 54's A4_GPUSKY, exact) makes them unnecessary. Found on the way: +37FA4, the scene's last call, draws the
   sun's lens flare into the screen before the composite (now after it: A4_FLAREFLUSH).
3. **Our GL submission** (done, read): the phone's own FRC4 (r87a, 1,865 renders) fits ~47 us a draw + ~0.9 us a
   corner, but the cars' flush (1.7 ms for a few thousand corners) bounds the driver's per-corner cost well below
   that -- the corners carry our own per-triangle work. Indexed draws (meshes share 1.8x / 3.2x corners) would save
   little: dropped. The shadow (~290 faces a render) looked like 4.9 ms of draws + 1.7 of insert on the phone, but
   FRC4's "shadow draws" timer also holds the table's batch flush that falls inside a shadow bucket (56 of the
   batch's 80 draws a scene): the shadow's own CPU is ~1-2 ms there. Its dedicated exact path (A4_SHADFAST) trims part
   of it; the rest is memory (each face's list record a cache miss; the N95 has no L2).
4. **The game's walk of the table** (answered by step 2): its cost was the blended copies, not the walk (0.7 ms).
5. **The composite and present** (read): the read-back already uses the phone's native BGRA; its wait is the MBX's
   tile render; the present's convert is hand-tuned (~2.4 ms on the phone); the yield is FRC4's "pres". No cheap cut.
6. **The alpha-map objects** (answered by step 2): their ~330 software copies a render in Montmartre lay within the
   sky's rows -- under the GPU sky they are GPU-only (bc88d: 0.27 copies a composite left, ~0% over holes).

## Progress

| step | state | gain (bench) | notes |
|---|---|---|---|
| r86: objects' sort and box, cars before the transform | on the phone (r86c) | ~3 ms in heavy LA | |
| r87: meshes of up to 20 texture groups on the GPU | on the phone (r87a) | phone: Montmartre ~72 -> ~66 ms | |
| 1. nitro blur (A4_NITROBLUR, C88) | done, bench | ~8.5 ms a boosted render (15.2 -> 6.7 on the model); exact, 0 px of 351 blurs | the blur visible again |
| 2. the GPU's sky (A4_GPUSKY) + the flare composited first (A4_FLAREFLUSH), C89 | done, bench | night Paris ~4-5 ms a render (segments 100-119: 44.5 -> 40.4, 46.2 -> 41.5) | exact: 0 px differing on all six tracks; the sky routine's stores kept (the car paint's reflection reads them); the flare now over the 3D, as stock |
| 3. indexed draws | dropped | | the driver's per-corner cost is small on the phone |
| 4/6. the alpha copies left below the sky | dropped | | ~none left under the GPU sky, Montmartre included |
| 3'. the shadow's per-face path (A4_SHADFAST), C90 | done, bench | ~0.2 ms a render on the model | exact: 190,026 faces, 0 records differing; the "6.6 ms" was mostly the table's batch flush timed inside it |
| r89a (C89) | battery passed, not sent | | superseded by r90a |
| r90a (C90 = C89 + A4_SHADFAST) | battery passed, not sent | Paris 400 s by segment vs r87a's run: 3-10 ms a render lower through Montmartre and after | the kept version without probes |
| **r90b (C91 = C90 + A4_RESTTIME)** | **sent** (round 88) | as r90a | the phone round reads the post, the convert, the shadow's nested flush |
| wheels (A4_CARANIM), for the next candidate | done, bench | none (a correctness fix) | the owner: "check and make sure the cars wheels are actually spinning" -- they were not: the GPU car's bake froze the player's tyres (+1B084's two frames), steer and bounce; exact now, 0 of 4,229 takes differing from the game's mesh |
| the reflection (A4_M1LIVE) | done, bench | Paris's ~30 ms re-bake hitches gone (0 re-bakes); costs ~1.65 ms a refresh on the model, paced to 0.75 ms a render | the stock arithmetic on the paint's reflective texels (7,618 in 4 rectangles) from the frame just composited, one frame late; the phone's upload cost is the unknown (TIM2 [40..48]) |
| the painter's rows (A4_PAINTFWD) | done, bench | the 2D phase -0.5 to -0.7 ms on every map (model) | exact: 174,914 calls against the game's painter, 0 px |
| New York's snow (A4_SNOWFAST) | done, bench | New York's 2D phase -0.9 to -1.1 ms with the above (model) | exact: 193 renders against the game's +286A4, 0 flake bytes, 0 px; 63% of flake pieces skipped (clipped) |
| the blur two pixels at once (A4_BLURFAST2) | dropped | none on the model | exact, but the blur is memory-bound |
| **r91a (C92 = C91 + A4_CARANIM, A4_M1LIVE, A4_PAINTFWD, A4_SNOWFAST)** | **sent** (round 88; sha256 a9006828e0088011..., 1,015,124 bytes) | Instant Play 2,446 frames in 300 s (r90b's battery 2,379); renders about even, New York ~0.5 ms better; the reflection ~0.4 ms a render on the model | the next phone round: the tyres, the reflection's cost and look, New York's 2D phase |
| phone round r91a (LA, Mini Cooper) | logged | LA unboosted 49.7 ms a render (r90b 53.1), the 2D phase 2.39 ms (3.6); but 75 ms or more 4.4% (0.2%) | the tyres spin; the reflection's refresh cost the phone 18.2 ms of uploads (the MBX ~2.3 ms a call), so it stepped every 8th render -- the owner's "every half a second" -- and each step was a slow render; the opponents' wheels are stock (the game animates the player's only) |
| the reflection on the GPU (A4_M1SKY) | done, bench | no texture changed during a race; one-time at a race's start: a point table (36 ms on the model) and the half sky (~48 ms of uploads on the phone, by r91a's price) | the player's car's 56 reflecting faces drawn again over a half-value sky at each corner's screen point (a least-squares plane of the game's map, 2.79 px a texel off it); mapped every frame on all six maps (Dubai's from the sky routine's own copies); off after 8 frames with no sky |
| **r91b (C93 = C91 + A4_CARANIM, A4_M1SKY, A4_PAINTFWD, A4_SNOWFAST)** | **sent** (round 88, after r91a's round; battery bat93) | six maps 5,682 frames, Paris 400 s 3,675 (pa91b 3,667); Instant Play 2,312 / 2,415 in two runs of different scene weight (m91b 2,446) | sha256 539751517cf14cb7..., 1,021,832 bytes; the next phone round: the reflection's look and its pass cost, the one-time start (TIM2 [49]), no 75 ms steps |
| phone round r91b (LA) | logged | renders as r91a (75 ms or more 4.0%: the race's start and boosted heavy scenes, none the reflection); the pass ~0.5 ms a scene | "glides through the window smoothly", but reflects one texture -- the panorama -- not the scenery; an upload into a fresh texture is cheap on the phone (the half sky 256 KB in 11 ms), rewriting a drawn one is what costs 2.3 ms a call |
| the reflection from the frame (A4_M1SKY, second form) | done, bench | ~1.5 ms a render on the model (one capture) | each composite's top rows into a fresh 256x64 texture (glDeleteTextures imported, ord 47), sampled by the pass at each corner's screen point: stock's source, one render late; a white/black mutant flips the car's window every post |
| **r91c (C93, the second form)** | **sent** (round 88, after r91b's round; battery bat93c) | six maps 5,639 frames, Instant Play 2,422, Paris 3,486 | sha256 eb1526dbf07bc0a0..., 1,020,896 bytes; the next phone round: the scenery in the reflection, the capture's cost (TIM2 [46]), the names recycled (TIM2 [45]) |
| phone round r91c (LA, Paris, New York) | logged | the capture 1.10-1.17 ms a render on the phone; Paris marks unboosted 52-61 ms, New York 58.5 (2D 6.1, r90b 7-12), 75 ms or more 5.4% there (r90b 1.2%, another route); texture names recycled (ordinal 47 is glDeleteTextures, settled) | the reflection "shows accordingly and smoothly" -- r91c is the build to keep for it; new: Montmartre's bicycles black |
| black props (Montmartre's bicycles) | done, bench | none (nothing changed) | **the game's own asset**: tracks\track02\2149_bicycle.rle is a one-colour black silhouette as shipped (palette 0-254 black, 255 the key; inflated from tracksc.bar by zlib, identical to what the game loads); the stock game draws it black; of 1,087 textures only it and Cars\black.RLE are all black; Monaco's tyres a real dark texture |
| the 30 fps ceilings (bench, r91c, Paris 400 s, by segment) | done, bench | upper bounds: no GPU wait -3.9 ms, no draw/bind cost -2.2 to -6.1, no copy/convert -4.8 to -9.0, all three -9.5 to -13 | the bench at its ceiling ~26-31 ms; on the phone, with the caveats (MBX.md: the model overstates the copy/convert ~3x, a third of triangles cannot share an atlas, hiding the GPU wait puts the 3D a frame late), a realistic ~7-12 ms: Montmartre ~20-23 fps, elsewhere ~24-26; **30 fps also needs the game's own per-frame work cut** (~19 ms on the phone) |
| the front windshield in reverse (A4_CARNEARBOX) | done, bench | none (the reverse view now the GPU's, cheaper) | the owner: no reflection on the front windshield reversing. The GPU cars' near-plane test used the bounding sphere; at the car's front the camera is close enough for it to reach the plane, the game drew the car from the paint baked once, and the live reflection never ran. The test now on the mesh's box (every vertex inside): the windshield reflects as stock's (srv) |
| **r91d (C94 = C93 + A4_CARNEARBOX)** | **sent** (round 88, after r91c's round; battery bat94) | six maps 5,645 frames, Instant Play 2,386, Paris 3,587, reversing on the exact binary: the windshield reflecting; garage posts identical to r91c's | sha256 d8d6da2eea769e66..., 1,021,108 bytes; the next phone round: the front windshield reversing |
| the ceilings' firm-up (bench) | done | noise ~1-2 ms a segment; the GPU wait not priceable on the bench (the host's own GL surfaces once the modelled wait is gone) -- 5.5-6.5 ms by the phone's own measure; one texture sheet: 135.5 -> 76.1 draws a frame at most (66% of textured draws inside one copy) | revised: ~8-12 ms realistic, Montmartre ~21-23 fps, lighter ~24-26; 30 fps needs the game's own work cut |
| phone round r91d (the Mini Cooper reversing) | logged | the owner's videos, every frame: stock's windshield dark glass with the scenery moving in it, r91d's flat light blue; r91d took no capture in reverse (573 scenes, 1 capture) | **not fixed for every car**: the Mini's front really is nearer the camera than the GPU's near plane, so the game draws that car itself, its paint a bake of a table with no picture |
| the reflection on the car the game draws (A4_M1HOOK) | done, bench | reverse on one route (autopilot A/B): 30.0 -> 30.9 ms a render on the bench -- the capture the chase view already pays (on the phone 1.07-1.18 ms), the paint's re-bakes gone; forward unchanged; the Ducati (no texel to reflect) now skips the capture, pass and point table it paid since r91b | the game's own paint triangles matched to their faces (~145 a render, 0 unmatched), drawn as var 3, the reflecting ones (~68) drawn again over the capture at the face's points; the Mini's windshield and roof windows as stock's (h95 / s95, rv95); the Veyron unchanged (rvv95) |
| **r91e (C95 = C94 + A4_M1HOOK)** | candidate (round 89; battery bat95) | six maps 5,641 frames, Instant Play 2,314, Paris 3,560, the Mini and the Veyron reversing and the Ducati on the exact binary, the boot fault only; garage posts identical to r91d's | sha256 992b6b369dc1c7b9..., 1,026,300 bytes; waits on the owner's OK to send |

## Versions kept (the owner, round 88: "keep track of this version and the redesign one ... I don't want to lose this one")

The incremental line ends here as **r90**; any redesign is a separate line and must never cost it.

| version | flags | what | sha256 (unbaked) | bytes |
|---|---|---|---|---|
| **r90b** (sent in round 88) | flags/C91.flags | C90 + A4_RESTTIME (phone timers for the post, the convert, the shadow's nested flushes) | 93391d95d55aa6e2e34e5481203b7a75ca8f13e6f0b30b53c1cd0149180bbb2a | 1,006,472 |
| r90a | flags/C90.flags | the same without those timers | 6ebb8ba608ae307b06ae7f46de64422ea28b7cdb54fba480c5efd70d70bf8f36 | 1,004,860 |
| r91a (sent in round 88, after r90b) | flags/C92.flags | C91 + A4_CARANIM, A4_M1LIVE, A4_PAINTFWD, A4_SNOWFAST | a9006828e00880117e141a95acb0e6eb7938c09de5fca9718ed16cb79a3a861a | 1,015,124 |

- **r91a's tree**: commit `548bc6c` (built there); r90a, r90b and r91a all rebuild to the bytes above from the r91b
  work's tree too (checked after each change of round 88's reflection work, MBX.md).

- **Tree**: commit `7fe92d3b42dfa3b8b859aca8611057dcc35d5992` on the fork's `ngage-port` (both build from it; the branch
  is never rewritten). `git checkout 7fe92d3` gives the exact sources.
- **Recipe** (from `.claude/ngage-port/asphalt4`): `A4_THIRDSTEP=1 A4_SCRATCH_SIZE=0x1C3000 A4_EXTRA_CFLAGS="$(cat
  flags/C91.flags)" python3 tools/injector.py <baseline> a4_r90b.exe --gl --capture` (C90.flags for r90a). Toolchain:
  clang/ld.lld/llvm-objcopy as the injector calls them; the emulator for the bench from this repository.
- **Inputs not in git** (the owner's, from the handoff zip): the baseline `asphalt4_ngi.exe` sha256
  4baef63ac70f7a46c08689dc614d0ce3b752298d495241aca01145d111b2aa06 (765,940 bytes; the injector refuses the pristine
  image); the guard DLL and make_loader_hashes.py -- the owner bakes every phone build.
- **The guarantee**: every redesign change goes behind its own flags; after each redesign commit C90 and C91 are
  rebuilt and must give the bytes above (sha256), and that check is logged in MBX.md. If a redesign ever has to
  touch shared code in a way that changes them, it goes on a branch of its own instead (the owner's OK first).
- The record of how r90 was reached: MBX.md rounds 84-88; this file's tables above.

## Where things are

- Code: renderer/ (a4_gl.c the renderer and its instruments, a4_eng.h the engine, a4_tel.c the telemetry,
  eng/ the lifted functions and their edits), tools/injector.py (the build), flags/CNN.flags (each round's set,
  README there), tools/frec3.py (reads the captures: modes phase, steps, obj, gob, timers, ...).
- The record: MBX.md (rounds, runs, results), ENGINE.md (the engine read from the binary), EMU_PATCHES.md at the
  repository root (every emulator switch the bench uses). Older overviews (NOTES.md, PATCHING.md, FINISH.md) are
  history from rounds A1-A5.
- Rules: ../CLAUDE.md (the four rules; print them in every reply that reports a result or asks for a phone round).
