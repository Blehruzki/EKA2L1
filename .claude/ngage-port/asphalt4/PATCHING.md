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

| filler | Thumb now | ARM rewrite (est.) | per-pixel saving |
|---|---|---|---|
| textured | 81 insn, 21 mem | ~30 insn, 0 stack mem | ~2.5x fewer insns, all per-pixel stack traffic gone |
| transparent | 22 insn, 3 mem | ~13 insn, 0 stack mem | ~1.7x |

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
