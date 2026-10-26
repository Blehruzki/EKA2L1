# Asphalt 4: what it would take to patch the renderer for speed

The measured cause of the slowness is the software renderer's per-pixel span
fillers (NOTES.md rounds A1/A4, confirmed on hardware). This is the feasibility
of patching them. Three parts: the optimization (the real engineering), the
injection toolchain (built), and the obstacle (the exe's anti-tamper check).

## 1. The optimization -- tractable, and the win is large

The two hot span fillers, measured on the shipped exe:

| filler | instrs/pixel | stack accesses/pixel | share of race frame (A4) |
|---|---|---|---|
| textured + light blend (`+3357a` loop) | **81** | **21** (26%) | ~35% |
| transparent/keyed (`+33b16` loop) | 22 | 3 | ~16% |

The textured filler is the prize. It is register-starved: Thumb-1 gives the
compiler eight usable registers, and this loop's working set is ~12 live values
(texture base, u and v fixed-point accumulators, u and v steps, u and v masks,
the v shift, the light colour, the dest pointer, the span-end pointer, plus a
z/coverage byte pointer it also writes). So **21 of its 81 instructions per
pixel are just moving data to and from the stack** -- pure overhead that exists
only because the code is Thumb.

The fix is to rewrite these loops in **ARM state**, which has 14 general
registers. The whole working set then stays in registers and every one of those
stack accesses disappears; ARM's barrel shifter and conditional execution fold
several more operations. A realistic rewrite:

| filler | Thumb now | ARM rewrite | per-pixel saving |
|---|---|---|---|
| textured | 81 insn, 21 mem | **38 insn, 9 mem (measured, bit-exact)** | ~2.1x fewer insns, mem 21->9 |
| transparent | 22 insn, ~5 mem | **12 insn, ~2 mem, 0 reloads (measured, bit-exact)** | ~1.8x fewer insns, all reloads gone |

The textured figure is now the *measured* result of a built, bit-exact rewrite
(see the update at the bottom), not an estimate. Note the floor is not zero stack
traffic: the loop has 16 values that must stay live across it (6 advancing:
dst, cov, u, v, light, counter; 10 invariant) and the saturating blend needs 4
scratch registers, so with ARM's 14 usable registers exactly 6 invariants must
be reloaded per pixel. The earlier "~30 insn, 0 stack" was optimistic.

Those two are ~51% of a race frame. Halving their cost is roughly **-25% frame
cost** on the bench's instruction count; on real hardware the gain should be
larger than the instruction ratio, because the eliminated traffic is per-pixel
*memory* access on a cacheless-by-comparison ARM11, which is what actually
stalls it. That alone would bring A4 to about Asphalt 3's level or below.

The same treatment applies to the menu's generic sprite painter (`+29736`, 65%
of a menu frame) and the translucent fill (`+2abaa`/`+52f44`) -- the latter
already has a verified ARM rewrite in `patch/fill.c`.

**Correctness is checkable before anything runs.** `tools/equiv.py` runs the
original and the rewritten routine side by side under unicorn on random inputs
and requires byte-identical output (it already passed 1,000 cases for the fill,
and caught a deliberately broken version). So each filler rewrite can be proven
equivalent offline.

**Effort:** moderate and incremental. Each filler is a self-contained loop; the
fill rewrite (done, verified) is the template. Call it a few focused days for the
textured + transparent + sprite fillers, each validated by `equiv.py`.

## 2. The injection toolchain -- built and working

`tools/a4patch.py` already does the mechanics:
- compiles the ARM replacement position-independent (`-fropi`, no relocations),
- appends it to the E32 code section and shifts the later sections,
- writes an entry veneer at the original function (Thumb `bx pc` into ARM, then
  a branch to the new code) so every caller reaches it unchanged,
- fixes the E32 header CRC.

It was used to build a patched exe with the fill replacement; the veneer and the
appended code disassemble correctly. So turning a verified ARM filler into a
patched exe is a solved step.

## 3. The obstacle -- the exe's anti-tamper check

This is the gate, and it is the one unsolved part. **Any modification to the
exe makes it fail to run** -- not just a code patch, but a single changed byte
of a dead string (round A2-A3). The game checks its own executable: a patched
build died with a divide-by-zero in game code (`+2D4AA`), a one-byte change died
differently (via `+4B8E0`), while the untouched exe runs for minutes every time.
The checking code is deliberately obfuscated (control-flow flattened, the
routine that names `ASPHALT4_NGI.EXE` at `+4BE26`), and the `.EXEv` file shipped
beside the exe is involved but is not a plain CRC/MD5/SHA table of the file
(`tools/exev.py` found no such digest).

This is ordinary anti-tamper of the kind a performance mod has to clear, not a
licensing or online check (the game is already unlocked and runs offline). What
getting past it requires, in principle: fully characterise what the check reads
and computes and where it keeps its reference value, then either regenerate that
reference for the patched file or neutralise the check. That is a reverse-
engineering task of unknown size until the check is fully traced. 

## Bottom line

- **The speed win is real and sized:** the textured filler is 81 instrs/pixel
  with a quarter wasted on stack spills; an ARM/register rewrite roughly halves
  the renderer and should feel better still on hardware. Correctness is provable
  offline before shipping.
- **The tooling to build and inject it exists** (`a4patch.py`, `equiv.py`,
  `fill.c` as the worked example).
- **The blocker is the anti-tamper check**, which no patched exe survives. That
  is the thing that would have to be solved first.

## Update: inner-loop rewrite is harder than the fill (verification rig built)

Started the textured-filler rewrite (user request). Built a loop-level
verification rig (`renderer/span_harness.py`): it runs the *original* inner loop
(Thumb `+3357A..+3361C`) under unicorn with controlled inputs and compares to a
reimplementation -- the same bit-exact-or-nothing check used for the fill, but at
loop granularity, needing no running exe.

**Result: the first-pass reimplementation is not bit-exact** -- only a minority
of pixels match (`renderer/span_model_wip.py`). The loop decodes structurally
(affine texel lookup + a per-pixel light gradient added with a saturating RGB565
blend, then clears a coverage byte), but the exact blend is not yet reproduced:
a faithful-looking transcription of the saturating-add ops still diverges, so an
op or a register-dataflow detail is still misread. This is unlike the rectangle
fill, which was simple enough to get right first try and verify at 1,000 cases.

So the honest status of the optimization: the *target* is certain (81 instr/px,
21 stack spills -> an ARM/register rewrite is a clear ~2.5x win) and the rig to
prove a rewrite correct offline exists and works -- but producing the bit-exact
rewrite needs more careful single-step reverse engineering of the blend than has
been done, and even once correct it still cannot be confirmed end-to-end on the
device because of the anti-tamper check. The rig is the reusable part; the
bit-exact model is unfinished.

## Update: inner loop now reverse-engineered and verified bit-exact

Single-stepped the original loop under unicorn (`renderer/span_trace.py`) to find
the divergence. Root cause: the Thumb disassembler was mis-rendering the operand
order of some two-register ALU ops (showing e.g. `ands r1,r2` where the hardware
does `ands r2,r1`), so the reimplementation of the saturating-add used a constant
`0x800` for the green-channel carry where the original uses the *computed* carry.
The trace gave the true data-flow; with the fix the model is **bit-exact: 2000/
2000 randomized spans match the original** (`renderer/span_model.py`, checked by
`renderer/span_harness.py`).

So the textured filler is now fully understood and reproducible, which is the
prerequisite for the ARM/register rewrite. Remaining: write the rewrite (keep the
~12-value working set in ARM's registers, drop all 21 per-pixel spills) and verify
it against the original with the same rig; then the transparent filler and the
menu sprite painter. The device-test wall (anti-tamper) is unchanged and still
gates any on-hardware confirmation.

## Update: textured-filler ARM rewrite written and verified bit-exact

Both forms of the rewrite are in `renderer/`, each **bit-exact over 2000
randomized spans** against the original loop (run through `verify_rewrite.py`,
which drives the shipped Thumb loop and the rewrite on identical inputs under
unicorn and requires byte-identical output):

- `a4_span_textured.c` -- the C rewrite. clang `-O2` for ARM: **43 insn/pixel,
  13 mem/pixel** (10 invariant reloads + texel load + 2 stores). clang even
  reloads K1/K2 from the literal pool each pixel.
- `a4_span_textured.s` -- hand-tuned ARM hitting the register floor: **38
  insn/pixel, 9 mem/pixel** (6 invariant reloads + texel + 2 stores). Keeps the
  6 advancing values plus the 4 hottest invariants (K1, K2, tex, umask) resident
  and reloads only the 6 cold invariants (vmask, vshift, ustep, vstep, thresh,
  lightmask) straight from the incoming stack args -- no spill stores.

Measured against the shipped Thumb loop (81 insn, 21 mem/pixel): the hand-tuned
version is **2.1x fewer instructions and 2.3x less per-pixel memory traffic**.
That traffic cut is the part that should matter most on an ARM11.

Why not zero stack traffic (the register-pressure floor): 16 values live across
the whole loop and the blend peaks at 4 scratch registers; 16 + 4 > 14, so at
least 6 invariants must come from memory each pixel. (umask could be derived
from vshift as `(1<<vshift)-1` to free one slot, but only if the game always
ships that texture layout -- not assumed here, to keep the rewrite a faithful
drop-in for arbitrary args.)

Next fillers (transparent `+33b16`, sprite `+29736`) would follow the same
write-then-`verify_rewrite.py` path.

## Update: transparent-filler ARM rewrite written and verified bit-exact

`renderer/a4_span_transparent.c` rewrites the keyed/transparent copy loop
(`+33B16`): affine texel lookup, colour-key test (a zero texel is transparent
and leaves the destination pixel untouched), store if non-zero. No light blend,
so unlike the textured filler the whole working set -- 4 advancing values (dst,
u, v, counter) + 6 invariants (ustep, vstep, umask, vmask, vshift, tex) + ~2
scratch -- fits inside ARM's 14 registers. Decode confirmed first try (the loop
has no tricky saturating-add) and the rewrite is **bit-exact over 2000 randomized
spans** (`renderer/transp_harness.py`, `transp_model.py`, `transp_verify.py`;
the inputs include the destination's prior contents so the key-skip is checked).

Here clang `-O2` for ARM is already optimal -- **12 insn/pixel, ~2 mem/pixel, 0
invariant reloads** (all 6 invariants stay resident; the only loop memory is the
texel load and the conditional `strhne` store). So no hand-written asm is needed
for this one. Against the shipped Thumb loop (22 insn, ~5 mem/pixel including 3
reloads): ~1.8x fewer instructions and all per-pixel stack traffic gone.

Remaining filler: the menu sprite painter (`+29736`). The two race-frame
fillers (textured + transparent, ~51% of a race frame) are now both done.

## Update: menu sprite painter -- the win is an algorithmic hoist, not a Thumb->ARM rewrite

The painter at `+29736` is not a tight span like the other two. It is a full
palettized sprite blitter (~500 instructions): clip setup, X/Y flip, 4-bit and
8-bit indexed source, an optional alpha plane, and a 5-way blend-mode switch
(0 opaque/colour-keyed, 1 luminance, 2 per-channel RGB via external `blx`
calls, 3 invert, 4 constant-fill), wrapped in a nested X/Y loop. So "rewrite
the inner loop in ARM" is the wrong frame for it.

The real waste is in the per-pixel core (`+298D8..+2991E`): for every pixel it
walks the palette struct and converts a palette entry (3 colour bytes) to RGB565
-- bank ptr -> palette ptr -> colorkey -> colour-table ptr -> 3 byte loads ->
shifts, ~24 instructions and **7 memory loads per pixel**. That result depends
only on the pixel's palette index and the sprite's palette, both constant for
the whole blit.

Verified that the conversion is a pure function of (palette, index):
`renderer/palconv_harness.py` runs the shipped block under unicorn against
`palconv_model.py` and they match **3000/3000** over random palettes, indices
and colorkeys. The RGB565 packing is
`((B>>3)<<11) | ((G>>2)<<5) | (R>>3)` from `colortab[i*3..i*3+2]`, and the
pixel is transparent iff `palette.colorkey == index`.

So the optimization is to hoist that conversion into a per-blit lookup table
(`renderer/a4_sprite_lut.c`, `a4_build_lut()` -- <=256 entries, built once;
its output matches the model over 2000 random palettes x 256 entries). The
common menu path (8-bit source, no flip, mode 0 / no alpha) then reduces to
`a4_paint_row_keyed()`: index -> lut -> keyed store, which clang compiles to an
**8-instruction inner loop with zero per-pixel palette work** (ldrb, cmp,
conditional lut load + store, advance) -- down from ~24 insn and 7 loads for the
conversion alone. This is the inner-loop shape of the hot path, not a drop-in
for the whole multi-mode function (the LUT hoist applies to every mode that
reads palette colours, but only this path is a self-contained leaf; the RGB and
luminance modes also call external routines).

All three fillers that dominate their frames are now accounted for: textured and
transparent with verified bit-exact ARM rewrites, and the sprite painter with a
verified-pure conversion and the LUT-hoist it unlocks.

## Update: a patched "general-v1" build measured on the bench -- the real win is ~6%, not ~25%

The owner supplied a patched build ("general-v1") that runs modified code: the
textured filler and the alpha rectangle fill are enabled, the transparent filler
is **not** (left experimental). It runs on the capped dyncom bench and was
profiled head-to-head against the stock exe, same attract-demo race, same
FRAMESTAT and PCPROF windows. (How the build gets past the game's integrity
check is the owner's own work and is deliberately not recorded here; this entry
is only the performance measurement.)

**Whole-frame cost (FRAMESTAT, demo race, instructions per frame):**

| build | mean | median | p90 |
|---|---|---|---|
| stock | 8.20 M | 7.60 M | 11.21 M |
| general-v1 | 7.73 M | 7.45 M | 10.40 M |

So the patch buys **about 6% off the frame on the bench** -- not the ~25% this
file previously predicted. That matches the owner's phone report (general-v1
felt no different in ordinary racing; only the spinning-camera finish helped).

**Why the estimate was wrong, from the v1 profile (PCPROF, same window):**
- The textured rewrite *is* executing: 12.56% of the frame now runs in the
  appended ARM loop, confirmed by its address being in the appended region.
- But the stock "textured filler" at `+333F4` was 34.81% of the frame, and only
  ~18.5 points of that is the inner pixel loop. The other ~16.3 points are
  per-span setup/outer-loop code the inner-loop rewrite never touches -- and it
  is still there in v1 (16.33%).
- So the rewrite only ever attacked ~18.5% of the frame and cut that by ~1.5-1.7x
  (clang's 43-insn loop, not the hand-tuned 38) -> ~6% whole-frame.

**The ceiling of loop-rewriting, corrected.** The old "halve the renderer,
~25%" was wrong because it treated all 51% (textured + transparent) as
inner-loop-rewritable when most is setup and only one filler was enabled.
Enabling the transparent rewrite adds its share (its inner loop, ~1.8x over its
~16% region), and hand-tuning both to the register floor adds a little more --
realistically **~10-13% off the frame**, still short of the ~33% by which A4
exceeds A3 (round A4). The large remaining levers are the per-span *setup* code
(16% of the frame, unexamined), the `+2E1C8` function (6.5%, unexamined), and
reducing the *amount* of work drawn (spans, overdraw, draw distance, geometry),
which is the asset route in the `.bar` archives.

## Update: the inner-loop injection contract, and why pixel-equivalence is not enough

To finish the fill work (enable the transparent filler + use the hand-tuned
textured asm) the rewrites must be injected as the shipped functions' *inner
loops*, and a drop-in needs more than matching the span's pixels: it must leave
the registers the surrounding function reads after the loop. Both contracts are
now pinned and verified (`renderer/exit_verify.py`, A4_CODE pointing at the
extracted code):

- **Textured `+333F4`.** The function loops per span (`+3352C`..`+3361C`) and
  the inner pixel loop is `+3357A`..`+3361C`. The loop advances a per-pixel LCG
  dither `light = light*K1 + K2` (K1=0x0019660D, K2=0x3C6EF35F); its final value
  lives in **r7**, is carried to the next span, and is saved to `[ctx+0x48]` for
  the next polygon. So the drop-in's exit contract is **r7 = final light,
  r4 = dst + 2n, r0 = cov + n**; r5/r6 (per-span base coords) are preserved and
  ip/lr are recomputed by the per-span setup so they don't matter. `a4_span_textured`
  now returns the final light (pixel writes unchanged); `exit_verify.py` checks
  **pixels + exit light + pointer advance, 2000/2000**. A shim that got the exit
  light subtly wrong would look right on a static frame and corrupt the dither
  across spans on hardware -- the bug the bench never enters.
- **Transparent `+33964`.** This is a multi-mode polygon rasterizer (blend-mode
  switch on `[ctx+0x64]`); the hot path's inner loop `+33B16`..`+33B40` is the
  last thing the function does -- it exits the loop and immediately returns
  (`add sp,#0x34; pop`). Nothing is carried or saved, so the exit contract is
  just the pixel writes, already verified 2000/2000 (`transp_verify.py`).

**Handoff:** the verified rewrites (`renderer/a4_span_textured.{c,s}` returning
final light, `renderer/a4_span_transparent.c`) plus these contracts are enough
to inject both with the existing v1 inner-loop injector. The emulator-side
verification of the resulting build (bench head-to-head, dither continuity) is
the measurement step.

## Update: inner-loop injector built and the injected image verified bit-exact

`tools/injector.py` builds the general performance edition from the stock exe:
it compiles the trampolines (`renderer/a4_tramp.s`) + the two span rewrites +
the alpha fill into one `-fropi` blob appended past the code section, plants a
Thumb->ARM veneer at each inner-loop entry (`+3357A` textured, `+33B16`
transparent) and at the fill entry (`+2ABAA`), patches each trampoline's
return-displacement placeholder to branch back into the Thumb function
(`+3361C`, `+33B42`), shifts the later sections and recomputes the E32 CRC. No
absolute address is written, so the relocation sections are untouched; code
grows ~944 bytes.

Each trampoline was proven a correct mid-function drop-in in isolation
(`renderer/tramp_verify.py`, `renderer/transp_tramp_verify.py`: 2000/2000 each,
checking pixels + exit light + end pointers + preserved r5/r6 + coverage), and
then the **produced image** was exercised end-to-end
(`renderer/inject_check.py`): entering the planted Thumb veneers and running
through the appended trampolines and rewrites reproduces the original loops over
**1500/1500** random spans each, for both fillers, including the landing back in
the Thumb function. So the injected renderer is bit-exact with the stock one;
the only behavioural change is speed.

This produces an exe only; it still needs the owner's integrity step to run on
hardware, and because the code layout differs from v1 (textured hand-asm +
transparent now enabled, vs v1's textured-clang only) the owner re-validates the
guard and regenerates the loader hashes for this exact image. Bench measurement
of the baked build is the next step.

## Update: the general edition boots and the whole-frame bench can't resolve it

The owner baked the general edition and it boots and renders on the capped
dyncom bench with no fault. Head-to-head on the attract demo:

| build | race mean | median | p10-p90 |
|---|---|---|---|
| stock | 8.20 M | 7.60 M | 5.6-11.2 M |
| v1 (textured-clang only) | 7.73 M | 7.45 M | 5.3-10.4 M |
| general (textured-asm + transparent) | 8.01 M | 7.82 M | 5.5-10.7 M |

These are **indistinguishable**: the attract demo renders different scenes each
run and that scene-to-scene variation (the 5-11 M spread) dwarfs the ~6% we are
trying to resolve, so the whole-frame number across independent demo runs is
noise. The scene-independent facts stand -- textured 81->38 insn/px, transparent
22->12, both realized and verified in-image -- but a trustworthy whole-frame
bench delta needs an identical workload (a deterministic demo replay) or the
phone. The phone, especially the slower GPU-less S60v3 targets, is the judge.

## Update: #2, the fixed-point vector leaves (vec_add, vec_mulsub)

Two hot pure leaves in the Lib3D math library, rewritten in hand-tuned ARM
(`renderer/a4_vec.s`) and injected as whole-function replacements (plain entry
veneers -- no trampoline, since they return normally):

- `+51D96` `a4_vec_add(a, b)`      : `a[i] += b[i]`, i=0..3
- `+51DB8` `a4_vec_mulsub(a, s, b)`: `a[i] -= s*b[i]`, i=0..3

The shipped Thumb versions do 8 scattered `ldr`/`str` per call; forced
`ldmia/stmia` collapses those into 3 bursts, giving **24 instructions vs 36 and
8->3 memory transfers per call** -- a win both on the bench (fewer instructions)
and, more importantly, in memory pattern on the ARM11. Bit-exact over 4000
random cases (`renderer/a4_vec_verify.py`) and verified again in the produced
image (8000/8000). Combined these are ~3% of the race frame, so the realized
saving is small (~1-1.5% of frame on the bench); they are free and fidelity-
preserving. The larger math functions (`+51E44`, `+51F08`, `+51E94`) reference
global tables through pc-relative literals (absolute addresses), so rewriting
them needs relocation handling and is left for now.

`tools/injector.py` now plants all five hooks (fill + two vec leaves + the two
filler inner loops) into one general edition.

## Update: on-device measurement tool (frame-time log + on-screen fps gauge)

Added so the general edition's effect can be read on real hardware, where the
bench can't resolve it. Three new detours in the injector (all BLX-insertion,
4 bytes, re-executing their displaced instructions):

- **fps tick** at `+0x1E6` (just before the per-frame present call, so the
  overlay lands on the finished frame and is carried by the present copy). It
  reads `User::TickCount()` -- which the game imports (euser ord 674, IAT
  `+0x50CE8`), resolved pc-relatively so it is load-address independent -- and
  calls `a4_fps_tick` (`renderer/a4_fps.c`).
- **back-buffer cache** at the textured filler's `+0x33412`, where `r2 = ctx[0x4c]`
  is the surface the 3D renders into; stashed so the overlay can draw portably.

State (counters + a 128-entry ring of per-frame tick deltas) lives in a fixed
scratch carved from the image's `.bss`, which the injector grows (`bss_size`
bumped); the data section loads at its linked base so the absolute scratch
address is valid at runtime. A bench `MEMSNAP` of the scratch reads the exact
frames/deltas -- the objective record.

The game's imports are minimal (no efsrv, no TLS/malloc), so the requested
`C:\gate6-a4fps.log` file -- reachable only through Open C `libc`, whose ordinals
aren't identified -- was left as best-effort and is **not** implemented; the ring
(bench) and the on-screen gauge (phone) cover the need.

**On-screen gauge.** The back buffer has an unusual stride (720 uint16, found by
a vertical-line calibration) and the display path turns 2D writes into stripes
(a bench artifact, and the phone's path may differ again), so a 2D digit readout
can't be drawn reliably. A *contiguous* run of pixels maps to a contiguous screen
region on any stride/scale, so the gauge is a contiguous bar: a dim full-scale
reference with a bright run on top whose length is the per-window frame count.
Its bright fraction tracks fps, which is what lets a **stock-vs-patched run be
compared by eye** (longer green = faster). Verified on the bench: the hooks stay
bit-exact (1500/1500), the game boots, the ring fills with per-frame deltas, and
the bar renders as a clean solid band over the race (no stripes). The new
`EKA2L1_PCMEM` probe (`EMU_PATCHES.md`) was used to find the render context's
surface base.

**True fps + reference ticks.** The gauge needed a scale, so the tick also reads
`TTime::HomeTime` (euser ord 859, IAT `+0x50CF0`, also imported) for real
microseconds and closes the window at one real second -- making the per-window
frame count **true fps**, on bench and phone alike (the trampoline now resolves
both euser imports pc-relatively and passes them to `a4_fps_tick`, which calls
them). Reference ticks are painted into the bar: **red at 30 fps**, white at 15
and 45. Green reaching past the red tick means above 30. Bench reads ~33 fps for
the capped demo; on a phone at ~12 fps the green falls well short of the red 30
line. Both euser calls preserve r4-r11, and the detour saves r0-r3/r12 so the
present call's arguments survive (a dropped r2 there was an early USER-87 crash).

## Update: stock baseline build (gauge only) + the heavy-vs-light fps pattern confirmed

The gauge lets a patched build be read by eye, but there was no way to read the
**stock** build the same way (unpatched, it draws no bar). So `injector.py` got a
`--measure-only` mode: it wires in only the measurement detours (fps tick `+0x1E6`,
back-buffer cache `+0x33412`) and the grown `.bss`, and **skips** the renderer
rewrites (`FILL_HOOKS` + `LOOP_HOOKS`). The result, `a4_stock_gauge.exe`, is the
**stock renderer with the same gauge, same scale, same 30-fps red tick** -- the
visual baseline to hold the optimized build against.

Proven correct offline, not on the bench, because the bench can't boot a modified
exe: `restore_orig.sh` put the **stock** factory DLL back, and the stock module
guard crashes any tampered image (KERN-EXEC 3, "access violation reading 0x8",
during boot -- the guard's own trap, reproduced identically by v5 and by
`a4_stock_gauge`; the plain stock exe boots fine). Running a gauge build on the
bench needs the owner's patched factory DLL + a regenerated hash, which is the
owner's integrity bake. Instead, `a4_stock_gauge.exe` was diffed against
phone-confirmed `v5`: **identical everywhere except** the five renderer sites
(`+0x2ABAA, +0x3357A, +0x33B16, +0x51D96, +0x51DB8`, left at stock bytes) and two
dead trampoline-return placeholders (`a4_tex_ret`, `a4_transp_ret`, never reached
with the loops stock). The fps/cache detours and the blob are byte-identical to
v5. So it is a strict subset of v5's edits; since v5 boots and gauges on the phone,
this does too. (Caught in review: the old `asphalt4_onebyte.exe` source had one
corrupted byte -- 'S'->'T' in the dead `FPS:` debug string at `+0x3C82`, NOTES:89;
rebuilt from the true stock image so the subset relation is exact.)

**The heavy-vs-light fps swing (user: "<30 in crowded streets, >30 in the LA
tunnel, ~27 avg").** Confirmed on the bench by measuring the *stock* exe (no guard
issue) with `EKA2L1_FRAMESTAT=1` on dyncom: instructions per frame **is** the fps
signal, since fps = rate / (instr per frame). Over 1,340 demo-race frames:

| percentile | instr/frame | relative speed |
|---|---|---|
| p5  (sparse) | 8.24 M | fastest |
| p50 (median) | 10.6 M | |
| p95 (crowded)| 12.5 M | slowest |

A **~1.5x spread** between the sparsest and busiest frames (and down to ~3 M on
empty transition frames). It is scene-driven -- the cost is the per-pixel span
fillers, so it rises with the textured/transparent pixels on screen: many cars +
traffic fill far more span than an empty tunnel. Scaled to the owner's reported
27-fps average (their phone ~2.6x the N95-by-eye 115 instr/us estimate), the band
runs **~36 fps on the sparse frames down to ~24 fps on the crowded ones, ~68% of
frames under 30** -- straddling 30 exactly where reported: crowded streets below,
empty tunnel above. The absolute numbers scale with the phone; the ratio does not.
Not driven to the literal LA tunnel on the bench, but the demo's own scene-cost
spread reproduces the mechanism and the straddle.

`a4_stock_gauge.exe` (sha256 5e6df31d…) ships to the owner alongside v5: bake its
hash (`make_loader_hashes.py <exe> <patched factory DLL> <outdir>`) with the same
patched DLL, deploy both, and the two gauges read side by side.

## Update: baked stock-gauge booted on the bench -- gauge counts presents, not posts

The owner baked `a4_stock_gauge.exe` (sha256 5e6df31d…, loader SHA-1 29d9f768…)
with the patched factory DLL + regenerated 20-byte hash records and returned the
four-file set. Installed on the bench (E:\sys\bin + C:\sys\hash), **it boots clean
-- the guard accepts it**, no KERN-EXEC 3, gauge active. First gauge build booted
on the bench since the guard went version-specific.

Run on dyncom at `EKA2L1_CPUCAP=115` (phone-like), gauge `disp` read live via
`MEMSNAP` of the scratch, `FRAMESTAT=1` alongside:

- **Gauge `disp` across the demo: 22, 24, 27, 28, 27 fps** -- mid-20s, swinging
  with scene.
- **FRAMESTAT: ~1,323 direct-screen posts vs ~3,963 gauge frames = ~3:1.** The
  gauge's `+0x1E6` detour counts the game's *present-loop* calls, which fire ~3x
  per EKA2L1 direct-screen post. So `disp` is the present rate, **not** the panel
  post rate; on the bench the true post rate at this cap is ~8.6/s (host us/post
  p50 ≈ 117 ms). The present count is the same guest code on bench and phone, so
  the owner's by-eye phone reading and the bench `disp` are the same quantity --
  which is why both land near 27 -- but neither is asserted to equal the panel's
  refresh. Worth a note beside the gauge: it reads the game's present cadence.
- **CPU work per frame: p5 5.5 M → p50 10.9 M → p95 13.0 M instr, a ~2.4x swing.**
  This is the phone-independent scene signal. A tunnel-sparse frame (toward 5.5 M)
  renders ~2.4x faster than a crowded one (toward 13 M) -- the mechanism that puts
  the tunnel above 30 and crowded streets below, as the owner reported.

(No usable bench screenshot: `import -window root` of the Xvfb root captured an
empty 278-byte image -- the GL-composited Qt window isn't on the root. The `disp`
MEMSNAP is the objective record, and the owner has already read the gauge on the
phone.) Bench restored to stock (stock exe+DLL, aside hashes, dynarmic).

## Update: v5 vs stock head-to-head on the bench (both baked, both boot)

v5 baked the same way (`make_loader_hashes.py` on `a4_general_v5.exe` + the
unchanged patched factory DLL; exe SHA-1 1c7e2523…) and **boots clean on the
bench** too. Both gauge builds now run under the guard. Same config as the stock
run: dyncom, `CPUCAP=115`, gauge `disp` live, `FRAMESTAT=1`.

The demo is non-deterministic (different scenes each run), so the comparison is
distribution-to-distribution over the gameplay frames (>3 M instr), not frame to
frame. **Instructions per frame:**

| percentile | stock | v5 | v5 reduction |
|---|---|---|---|
| p25 | 9.66 M | 8.77 M | -9.2% |
| p50 (median) | 10.86 M | 9.14 M | **-15.8%** |
| p75 | 11.21 M | 9.54 M | -14.9% |
| p90 | 12.28 M | 10.79 M | -12.2% |
| p95 (heavy) | 12.96 M | 11.55 M | -10.8% |
| mean | 11.14 M | 9.84 M | **-11.7%** |

A consistent **~12% off the frame, 10-16% across the whole working range**, and
the win holds in the heavy tail (p75-p95) where the per-pixel span fillers
dominate -- which is exactly where the owner reports it on device: the
race-finish spinning-camera (a heavy full-screen effect) runs smooth on v5 and
choppy on stock, and the frame rate is "slightly better on all scenarios." This
is larger than the ~6% the earlier v1 note measured, because v5 carries more
hooks than v1 (transparent inner loop + the two vec-math leaves on top of fill +
textured), and because FRAMESTAT counts the whole frame -- the render-only
reduction is larger than the 12% whole-frame figure. Gauge `disp` corroborates:
v5 peaked 30-33 where stock peaked 28, both ~27 median.

(The p5 bucket read v5 *higher* -- 7.74 M vs 5.51 M -- which is scene-mix noise,
not a regression: the sparsest 5% of frames barely touch the fillers, and the two
non-deterministic runs sampled different near-empty frames there. The rewrites
cannot make a frame with no spans slower than the Thumb original; the veneer
overhead is a few instructions, not 2 M.)

Bench restored to stock. The owner confirmed the gain on the N95-3 independently.
