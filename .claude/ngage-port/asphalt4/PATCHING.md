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
engineering task of unknown size until the check is fully traced, and it is where
this investigation stops -- the renderer work (1) and the injection (2) are
ready, but none of it runs on the real exe until the check is dealt with, and I
have not implemented that.

## Bottom line

- **The speed win is real and sized:** the textured filler is 81 instrs/pixel
  with a quarter wasted on stack spills; an ARM/register rewrite roughly halves
  the renderer and should feel better still on hardware. Correctness is provable
  offline before shipping.
- **The tooling to build and inject it exists** (`a4patch.py`, `equiv.py`,
  `fill.c` as the worked example).
- **The blocker is the anti-tamper check**, which no patched exe survives. That
  is the thing that would have to be solved first, and it is the one part not
  done here.

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
