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
