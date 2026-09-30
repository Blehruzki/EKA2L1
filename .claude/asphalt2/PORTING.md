# Asphalt 2: Urban GT 2 — N-Gage to S60v3 port

Running the N-Gage build of the game on an S60v3 phone, as a loader and an API
shim rather than an emulator: our own 9.x application loads the old EKA1 image
into a code chunk, answers its 462 imports with 9.x equivalents, and bridges
the two object graphs. The other file here, `README.md`, is about a different
job — patching the official S60v3 build — and shares nothing with this.

## The hardware is a Nokia N95

**The phone every hardware round is run on is an N95: Symbian 9.2, S60 3rd
edition Feature Pack 1.** The emulator here runs an RM-409 (Nokia 5320), which
is 9.3 / FP2, because that is the ROM EKA2L1 has. One feature pack apart, and
for the shape of the import sequence it has not mattered -- the phone and the
emulator track each other event for event over fifty-five consecutive events.

Where it can matter is **ordinals**. euser and efsrv export ordinals are not
guaranteed identical across 9.1 to 9.4, and every ordinal in `gate6.s` that was
taken from a `kernelhwsrv` def file rather than from the device is a guess that
happens to hold on FP2. `SetKeyBlockMode` was keyed to the wrong ordinal once
already. When an import behaves strangely on hardware and not in the emulator,
this is the first thing to suspect.

Everything lives in `toolchain/port/`. `gate6.cpp` is the loader and shim,
`gate6.s` its imports, `gen_shim.py` generates the import table into
`gate4_shim.cpp`, `build_gate6.py` builds `gate6.sis`. `readlog.py` reads and
diffs a run's log; `d2.py` disassembles the game image. Use `d2.py` and not the
scratchpad's `dis.py`, which prints every mnemonic one instruction below its
true address and sent round 60's first reading to the wrong call site.

## Read this first: settled, retracted, and the rules

*This file is a working log, appended to as things happen, and a log tells you
what was believed at the time rather than what is true. This section is the
part that is maintained. When something below is overturned, it is recorded
here and the section it overturns is marked.*

### Round 87: the band in full-screen mode is the status pane's window

*E244-E245, and the photograph is what identified it.*

Round 87 confirmed the picker: the cycling works, the choice persists. One
thing wrong -- **full-screen mode still leaves a band across the top of an
N95**, so it is not full screen.

A frame lifted out of the video says what the band is. It is a **smooth
gradient with none of the dither the game's own picture has**, and there is a
hard horizontal edge where the picture starts. That is an Avkon **skin
background**: the status pane's own window, painting over ours. Round 76
established the mechanism -- the port writes the framebuffer directly, so
anything the window server paints afterwards lands on top -- and round 77
established that the `ENoScreenFurniture` construction flag does not stop it.

The layout is not the problem. `screen_fit` puts full screen at 240x320 from
(0,0) and the harness agrees; the pixels are written and then covered.

#### What was tried, and what the bench was good for

Two calls, both resolved at run time through libraries the port already
holds:

| | |
|---|---|
| `CAknAppUiBase::SetFullScreenApp(ETrue)` | avkon 208 |
| `CAknAppUi::StatusPane()` then `CEikStatusPane::MakeVisible(EFalse)` | avkon 2919, eikcoctl 324 |

Together they **killed the app**: 44 records, zero frames, an access
violation at address 0 (E244). Split and bisected: `SetFullScreenApp` alone
runs 1,580 frames and returns; `MakeVisible` is the killer (E245). All three
lookups resolve and `StatusPane()` hands back a live-looking `0x700e68`, so
it is the call itself that will not have it -- most likely that pane belongs
to an app UI Avkon set up more thoroughly than our synthetic one.

**The emulator paints no status pane, so it cannot show whether the band
goes.** What it can show is a call that kills the app, and it did, before a
phone saw it. That is the whole value of running it here first, and it is
worth stating plainly because the run looks like a failure and was not.

#### What ships, and what is deliberately not changed yet

`SetFullScreenApp` only. `MakeVisible` stays in the source, switched off,
with its result written beside it so the next idea starts from a known
position.

The inset is **left at what Avkon reports**, even though a genuinely
full-screen app has nothing to stay clear of. If `SetFullScreenApp` does
remove the band, aspect and fill will waste those rows until a follow-up
drops the inset to zero -- and if it does not, they are exactly as they
shipped in 174. Full-screen mode ignores the inset either way, so **the mode
the round is about is fixed either way**. Dropping the inset now would risk
hiding the top of two working modes on a guess; it costs one round to do it
in the right order.

The theory all this supports: under DSA our writes reach the framebuffer, but
the window server restores what lies outside the app's own region, and
`SetFullScreenApp` is the call that makes that region the whole screen.

### Phase 5: hold C to cycle, and the choice is kept

*E241-E242.*

**The gesture.** Hold C: the first change after about a second, then one every
half second while it is still held. C rather than `*` and one key rather than
a chord, because neither `*` nor Shift exists everywhere -- `*` is a Chr/Fn
symbol on a QWERTY E71, and Shift does not exist at all on a numeric keypad,
where scancodes 0x12 and 0x13 appear zero times in round 86's log.
`EStdKeyBackspace`, **0x01**, is the one key that is a single press on both
layouts and the game never uses it.

**The timing runs off the frame loop, not key auto-repeat.** Repeats do reach
us -- `'5'` shows 772 of them from 38 presses -- but whether a *clear* key
repeats is a per-phone question, and the frame loop is not. The key handler
notes only the down and the up.

**A tap passes through.** The key is swallowed only once a hold has actually
changed something, and then until it is released. Somebody who never holds it
loses nothing, even on a phone whose menus do use C.

Measured with a real held key at the emulator (`holdtest.sh` drives `xdotool`;
EKA2L1 binds host Backspace to `std_key_backspace`, the same 0x01):

    3 mode change(s):
       tick 2305     -> fill
       tick 2339     -> integer   +0.53 s
       tick 2372     -> full      +0.52 s
    1 config write(s), 1 returned KErrNone

A 2.2-second hold gives exactly three changes, which is one at a second and
two more at half-second steps, and the measured gaps are 0.53 and 0.52 s.

**One write, not three.** The first version saved on every step, which put a
file replace, a write and a flush inside the frame loop twice a second for as
long as the key was held -- a stalled frame each time, for nothing. The save
happens once, on release, and the count in the log is how that was checked:
three changes, one write.

**The choice is kept** in `C:\gate6.cfg`, sixteen bytes: magic `G6CF`, a
version, the mode, and an inset override. `C:\` root rather than beside the
save, because it is the one directory guaranteed to exist and be writable
before the game has ever saved -- the installer may have put the game on E:,
and `C:\system\apps\6RBC` does not exist until the first `user.dat` is
written, which is after a race. A file that is missing, short, or carrying an
unknown magic or version is simply not there and the defaults stand.

Round trip on disk after the hold: `magic 0x46433647 ('G6CF'), version 1,
mode 4, inset 48`.

#### No separate key for the inset, and why that promise was dropped

Phase 4 said `#` would nudge the inset if Avkon's answer turned out wrong on
some device. It will not, because `#` has exactly the problem `*` has: on a
QWERTY S60v3 it is a Chr/Fn symbol rather than a key, so it would work on
the numeric-keypad phones and not the others -- half a fix, on the half we
can already test.

It is not needed, because the mode cycle already spans the cases. If Avkon
under-reports the band -- says 48 where a device's is really 56 -- then on a
240x320 panel:

| mode | where the picture starts | covered by a 56-row band? |
|---|---|---|
| 1:1 | y = 80 (48 + half the slack) | no |
| integer | y = 80, same as 1:1 here | no |
| full | y = 0, by design | yes, and that is what it is for |
| aspect | y = 48 | the top 8 rows of 272, 3% |
| fill | y = 48 | the same 8 rows |

So three of the five modes are immune and the other two lose about three per
cent off the top. Adding a key that works on some phones to recover three per
cent on two modes is a worse trade than leaving it out. The log still records
what Avkon said beside what was adopted, so if a device does disagree the
next round says so and the fix is a constant, not a keypress.

**The default is now aspect**, not fill. It is the only mode that holds
176:208 -- within 0.6% on every panel measured -- where fill is +7.4% on an
N95 and +105.5% on a landscape E71. An upgrade never overrides a choice
already written; the default applies only when the file is absent.

### Phase 4: a full-screen mode, and an inset the device chooses

*E240.*

#### Full screen, distortion and all

A fifth mode, and it exists because the complaint it prevents is a fair one:
plenty of people would rather have the whole panel than the right shape, and
being told they cannot is worse than the stretch. It differs from fill in one
respect only -- **fill stops at the top of the status band, full does not** --
so it is 100% of every panel, at (0,0), whatever the inset says.

What it costs, per panel: -11.4% on an N95, +57.6% on an E71, +110.1% on a
landscape 5800, +168.6% on an E90's inner screen. And two panels where it
costs **nothing at all**: 352x416 and 176x208 are both exactly 176:208, so
there full screen is complete *and* undistorted.

#### The inset, asked rather than assumed

`INSET_DEFAULT = 56` is the N95's status pane, measured by dwelling on the
phone in rounds 74 to 78, and it was being applied to every device -- on a
240-tall landscape panel it eats 23% of the screen (E231).

`AknLayoutUtils::LayoutMetricsRect(EMainPane, TRect&)` hands back the area
below the status pane on whatever device is running. A static with a
reference out-parameter, so the call is just (enum, pointer): no `this`, no
struct return. Its top edge is the inset.

It is resolved **at run time** through the avkon `RLibrary` the port already
holds, not as a static import, because this file's opening section warns that
an Avkon ordinal taken from a def file is a guess that happens to hold on
FP2. Three guards, in order: a null lookup falls back; an answer that is not
a plausible main pane -- top edge outside the upper half of the screen, a
bottom above its own top, anything off the panel -- falls back; and what was
resolved, what it answered and what was adopted all go in the log, so a
device that disagrees says so instead of quietly drawing wrongly. The
fallback is 56, which is exactly what shipped, so the worst case is today.


#### 48, not 56 -- and it changes the one confirmed layout

On the RM-409 Avkon answers a main pane of **(0,48) to (240,293)**: a 48-row
status pane and a 27-row softkey pane. The layout becomes **240x272 at (0,48),
85.0% of the screen, +4.3% aspect error**, against 240x264 at (0,56), 82.5%
and +7.4%. Bigger and less distorted.

It is also **the first deliberate change to the layout hardware has
confirmed**, so it is the thing round 87 must actually look at. 56 was
measured on an N95, which is FP1; 48 is what a 5320 reports, which is FP2;
those can legitimately differ and the whole point of asking is that the
number is per device. But if the N95's real band is 56 and Avkon there also
answers 48, the top eight rows of the picture will sit under the band.
**Wasting rows is safe, being covered is visible** -- so this is the one
change in the multi-resolution work that could look worse on the phone that
already worked, and it is called out rather than buried.

If it is wrong there, `#` nudges the inset in phase 5 and the log already
records what Avkon said beside what was adopted, so one round settles it.

#### A brace that was not there

Found while wiring it in:

    if (!c->screenW)
        c->mode = (u32)SCREEN_MODE;
        c->topInset = (u32)INSET_DEFAULT;

The `if` guards the mode and nothing else, so **the inset was reset to 56 on
every call**, not only the first. Harmless while the picker is off and this
runs twice a launch. Not harmless at all once a key can change the inset,
which is the next phase -- the adjustment would have been silently undone the
next time the game asked about the screen, and it would have looked like the
key not working.

### Phase 3: four modes, and an exact one that no longer crops

*E236-E239.*

Four changes to `screen_fit.h`, each one gated by the harness before it was
written:

- **`MAP_MAX` 320 -> 1024.** The old size was never a decision; it happened to
  be enough for a 240x320 phone, and on anything larger an array bound decided
  how big the picture was drawn. Clears all 76 clamp failures.
- **An integer mode.** The largest whole multiple that fits, falling back to
  1x where none does. On a 5800 that is a pixel-exact **352x416 filling 63.6%
  of the screen**; on 240x320 there is no room for 2x and it is 1:1 exactly.
- **Exact modes no longer crop.** `dstH = sh < bh ? sh : bh` truncated
  whenever the usable height was under 208. 1:1 and integer now centre in the
  **whole panel** when that is the only way to show all of the source, letting
  the band overlap the top instead. Overlapping the top of a picture can be
  undone by switching mode; a row that was never drawn cannot.
- **Scaled modes centre vertically** instead of bottom-anchoring. The old
  behaviour was chosen on an N95, where those modes leave no vertical slack at
  all, so it never showed there -- and on a 5800 it dropped the picture 160
  rows for no reason. The harness confirms it is a no-op on every 240x320
  layout.

The gate is `runfit.sh`: it exits non-zero while any of the ~1,500 layouts
fails, and it is clean.

#### Seeing it rather than describing it

`MODE_CYCLE_FRAMES` advances the mode every 150 frames and `DUMP_SCREEN`
writes the **composited framebuffer** -- what the panel shows after the blit,
letterbox and all -- once per mode. Both are test-only and off in anything
shipped. Each dump carries a four-word descriptor (width, height, pitch,
bits) ahead of the pixels, so the renderer takes its geometry from the data:
phase 0's renderer was told its geometry separately, got it wrong by sixteen
columns, and drew a convincing artefact.

| | 240x320 | 320x240 landscape |
|---|---|---|
| fill | 240x264, +7.4% | **320x184, +105.5%** -- the logo squashed flat |
| aspect | 223x264, 76.7% | 155x184, 37.1% |
| 1:1 | 176x208, 47.7% | **176x208**, 47.7% (was 176x184 before phase 3) |
| integer | 176x208 -- no room for 2x | 176x208 -- likewise |

**A finding that bears on the default.** On a landscape panel **1:1 is the
larger picture than aspect** -- 47.7% against 37.1% -- because an exact mode
may sit above the status band to avoid cropping while a scaled one must stay
below it. On 240x320 aspect is the larger. So "aspect by default" is right for
the three phones in hand and is not obviously right everywhere, which is an
argument for choosing the default per panel rather than fixing it.

#### And a harness of my own making

E237 is a spoiled run kept in the record: six frames instead of thousands, no
panic, no fault. I had started it while the previous run's emulator was still
alive -- two instances contended, and then the first one's `emurun.sh` reached
its `pkill -x eka2l1_qt` and killed the second. **`emurun.sh` must not be
invoked while another run is in flight.** An absurdly short result is the
shape that has misled this project before, so it gets checked rather than
read.

### Phase 2: the scaler's sums, on the host, over every panel

*E234-E235. `screen_fit.h` and `fittest.cpp`.*

Phase 1 ended with the emulator able to reach one panel beyond the native one.
Everything else has to be covered by arithmetic -- and arithmetic checked
against a second copy of itself proves nothing, which this project has now
paid for four times. So the sums were **moved**, not copied: `screen_fit.h`
holds them, `gate6.cpp` includes it and adapts its Context to the struct, and
`fittest.cpp` includes the same header and drives it directly. There is one
copy of the arithmetic and the test runs the shipping one.

The harness covers **10 panels x 3 modes x 51 insets, about 1,500 layouts, in
well under a second**, checking seven things per layout:

1. the picture is inside the panel, both axes -- everything else is cosmetic
   next to this, because the blit writes `dstW x dstH` at `(offX, offY)` into
   the real framebuffer;
2. it starts at or below the status band it was told to clear;
3. every `mapX`/`mapY` entry addresses a real source pixel;
4. nothing was written past the entries the caller offered;
5. both maps rise -- a scaler that goes backwards mirrors part of the picture;
6. 1:1 maps pixel for pixel, and shows **all** of the source;
7. aspect keeps the ratio to within a pixel, and fill reaches the edges.

#### Two defects, neither of which shows on any phone we have

**The 320-entry clamp.** `mapX` and `mapY` are `u8[320]` and the picture is
clamped to that, so on a panel wider or taller than 320 the size is decided by
an array bound rather than by the screen. Four distinct cases, 76 in the
sweep. The worst is the 5800/N97: **aspect mode gives a 320x320 square from a
176x208 source.** Widening the maps to 1024 entries clears every one of them
-- checked before the change is written, which is the point of having the
harness first.

**1:1 crops the source.** `dstH = sh < bh ? sh : bh` truncates when the usable
height is under 208, so the bottom of the frame is simply not drawn -- and the
bottom of this game's frame is the HUD. At the real 56-row inset:

| panel | drawn | lost |
|---|---|---|
| E71 landscape 320x240 | 176x184 | 24 rows |
| N91 / 6110 176x220 | 176x164 | 44 rows |
| a panel exactly the source's size | 176x152 | **56 rows, over a quarter** |

Neither defect can appear on the N95, C5-00 or N79: all three are 240x320,
whose 264 usable rows hold 208 comfortably and whose axes are both under 320.
They are latent, and they bite exactly on the panels this work exists to add.
That is the harness earning its place -- both were found without an emulator,
a phone, or a round.

#### What the table says about the modes

Aspect keeps the ratio to between -0.6% and +0.1% on all ten panels, which is
the accumulation slack and invisible. Fill does not: +7.4% on the N95,
**+105.5%** on an E71, **+148.8%** on a landscape 5800 and **+219.4%** on an
E90's inner screen. The default being changed to aspect is settled by this
table rather than by preference.

### Phase 1: the emulator can pose as a landscape panel, and not much else

*E231-E233. `EKA2L1_SCREEN=WxH`, 39 lines in EKA2L1's `window.cpp`.*

The bench has one S60v3 ROM (RM-409, 240x320) and the two N-Gage ROMs cannot
run this port, so without help there is no way to exercise a scaler against a
panel short of buying the phone. The override rewrites every screen mode's
size after `wsini.ini` is parsed, reads its value once from the environment,
and does nothing at all when unset.

**It reaches exactly as far as the ROM's own Avkon layout data.**

| panel | result |
|---|---|
| 240x320 | native, unchanged |
| **320x240** | **runs the full 90 seconds** -- it is the native mode rotated, so Avkon has layouts for it |
| 352x416 | `AVKON` panic **61** during framework construction, before any of our code |
| 360x640 | the same, then `Corrupted graphics command list! Emulation halt.` |

Avkon picks its layout tables by screen size, and a ROM carries them only for
the resolutions its device has. Overriding the width and height does not
conjure matching layout data. The failure is at least honest -- it stops
rather than quietly handing the guest a framebuffer of the wrong size.

So the override is worth keeping for the one case it does cover, and that case
is the valuable one: **320x240 landscape is precisely what hardware cannot
test here**, since the three phones available are all 240x320. Everything
else belongs to the host harness, which is promoted from supplement to the
primary coverage mechanism.

#### What the landscape run said

Fill mode, which build 173 ships as the default:

    panel             320 x 240   (usable 320 x 184 below a 56-row inset)
    picture drawn     320 x 184 at (0, 56)
    aspect            1.7391 vs source 0.8462   (+105.5%)
    screen used       76.7%
    *** at or past the 320-entry limit of mapX/mapY: clamped ***

Three of the four problems the plan predicted, in one run:

- **the aspect blows out by 105.5%** on a landscape panel, which settles the
  default the other way from build 173 and confirms aspect-correct;
- the picture sits **exactly on the 320-entry limit** of `mapX`/`mapY`, so a
  wider panel clamps;
- the **56-row inset**, measured by dwelling on a portrait N95 in rounds 74 to
  78, eats **23%** of a 240-tall screen. It is a per-device constant being
  applied as a universal one, which is what phase 1 of the plan said to
  replace with Avkon's own answer.

`srcOrigin` and `srcPitch` come through at 16 and 176, unchanged.

#### And a third reused note code

`screenfit.py`, written to read that block, got it wrong on first use:
**840 is both `NOTE_SCREEN_DST` and `NOTE_THREAD_ARG`**, and 841 is both
`NOTE_NGAGE` and `NOTE_THREAD_NAME`. Taking "the last record with code 840"
reads a thread argument and reports a top inset of 1 and mode 0 when the
truth is 56 and mode 2. It now matches the exact consecutive run
`screen_layout` writes -- three FIT, one SRC, one DST -- so a reused code
cannot be mistaken for part of it.

It also compared `offY + dstH` against the FIT record's height and reported
the picture hanging off the bottom of a panel it fits exactly. Those two
numbers are measured from different origins: the FIT height is the panel
*minus* the inset, while `offY` starts at the top of the whole screen and
already includes it.

### The renderer, not the port: a strip that looked like wrapping

*Phase 0, after E230. Spotted by the user in a frame I had rendered and
posted, and it would have gone into the whole multi-resolution phase unnoticed.*

A dump render of the title screen showed a narrow vertical strip down the
left, exactly the signature of the horizontal wrap this port spent rounds 74
to 78 on. It is not that, and the measurements say so in four steps:

1. **The picture is 176x208, not the screen.** The content measures 1326x1568
   -- aspect 0.8457 against 176/208 = 0.8462 and 240/320 = 0.75. So it is the
   game's own buffer rendered by `dumppng.py`, not the emulator's display and
   not a phone.
2. **The wrap signature is absent.** Do the first K columns of row y equal the
   last K of row y-1? **0 of 207 rows** for K = 12, 16, 20 and 24.
3. **There is one hard seam and it is at column 15 to 16**, mean neighbouring
   delta **1041**, against 431 and 416 for the strongest seams in the artwork
   itself and 100-260 everywhere else.
4. **Column 16 is `SRC_ORIGIN`.** Re-rendered from source pixel sixteen, the
   1041 seam disappears entirely and the remaining seams shift left by exactly
   sixteen (116 -> 100, 132 -> 116): the same picture, in phase.

`gate6_screen_update` reads from `src + SRC_ORIGIN + y * srcPitch`. The
renderer read from column 0. Sixteen columns of the game's left-hand padding
that the display never shows were being drawn, and the picture was sixteen
columns out of phase. Nothing on any device was affected -- the blit has
always applied the offset -- and `dumppng.py` and `bandpng.py` now default
their origin to 16, with the reason in the docstring.

#### The pattern, now that there are five of them

This is the fifth time in this port that the instrument was the finding, and
the **second inside phase 0 alone**:

| | what it measured wrongly |
|---|---|
| the framedrops | `BOX_EVERY_TRACED = 1`, ~40,000 disk commits a race |
| six rounds of vtable guesses | `MDA_DUMP_VT` prints `rvt[k-2]`, read as `rvt[k]` |
| "no allocation ever failed" | `WRAP_ALLOCATORS` off, so none could be recorded |
| "the reported size changes the layout" | two shots 60 frames apart against a 60-frame blink |
| "the wrap is back" | a renderer starting at column 0 against a blit starting at 16 |
| Asphalt 1's stride is 350 | a vertical-continuity metric reading a two-pixel dither |
| Asphalt 1's picture is misaligned | a scaled screenshot of a window, read three ways, wrong twice |
| round 92's fault is at `RLine::EnumerateCall` | `readbox.py` naming this game's imports out of the other game's table |
| "nothing is freed", read as the policy | box bit 16, which only says the free thunk is installed |

The last two were tools written in this session, each to answer one question,
each inheriting that question's defaults and then being used for another. That
is the specific failure mode, and it is worth stating as a rule: **a tool
written to answer one question carries that question's assumptions, and the
second use is where they bite.** Both were caught by comparing against a
number the port itself already holds -- the blink period against the sampling
interval, `SRC_ORIGIN` against the render origin -- rather than by looking
harder at the picture.

### Ten runs against a binary that was not being built

*E295 to E305, chasing round 90's fix.*

The fix for the duplicate thread name went in, and its probe did not fire.
Then a probe on the line above it did not fire, under three different note
codes, in three different places, while the line below kept logging. That is
impossible in straight-line C, and `load_and_start` is straight-line C from
7098 to 8250 -- one function, no early return, braces counted.

What settled it was not the source but the **record count**: 6,008 for ten
runs in a row while the image grew from 47,640 to 48,048 bytes. A
deterministic run repeats a count; it cannot repeat one across code that
changed. So the image being executed was not the image being built -- and the
emulator's own log had been saying so all along, in a line nobody read:

    gate6.exe (UID3=0xE0001007) runtime code: 0x70000000

Round 90 renamed Asphalt 1's installed files to `gate6a1.*` so it would stop
colliding with Asphalt 2's. Every bench run **before** that rename had copied
Asphalt 1's build onto the emulator's drive as `gate6.exe` and
`gate6_reg.rsc` -- and that registration claimed UID 0xE0001007. After the
rename there were two registrations for one UID, the applist found it twice,
and the stale executable won every launch. The host-side change made in the
same stretch, the duplicate-name refusal, kept working throughout, because
that one is in the emulator and not in the guest.

Three things worth keeping:

- **A build size is a cheap checksum.** Every probe round should have started
  by comparing it. E297's probe was never compiled at all -- the edit and the
  run were one shell command and the sandbox refused it -- and only the
  unchanged size said so.
- **Two names for one number is the same bug as one name for two games.**
  E298's probe went out under `NOTE_THREAD_ARG`, which is 840, and so is
  `NOTE_SCREEN_DST`.
- **Renaming what a bench installs leaves the old name installed.** The
  emulator's drive is not rebuilt between runs, so it accumulates. Nothing
  cleans it, and a stale registration outranks a new one.

### Packaging a second title: loader-only, and two checkers that were wrong

*After E291. Phases 1 to 3 are done for Asphalt 1 in the emulator, so the
next thing it needs is a package.*

`build_release.py` had Asphalt 2 written into it six times over -- the game
tree, the install directory, the caption, the vendor, the install text, and
the rename-and-scramble pair keyed on `6rbc.app`. All six now come out of
`games/<name>/game.h`, and the four-character stem is **parsed from
`GAME_STEM_CHARS`** rather than written down again, because the loader
builds its paths from that same list and two spellings of one name is how a
package installs into a directory the loader never looks in.

`GAME_BUNDLE_DATA` picks the shape. Asphalt 2 keeps its 20.6 MB installer
with the game inside it. Asphalt 1 gets a **31 KB loader-only package**: the
port, its registration and its icon, and nothing else. Its data is copied to
`\system\apps\6r67` by hand, and the loader has always read a hand-copied
dump as happily as an installed one -- it looks for `<stem>.bin` first,
because that is how an installer has to carry an E32 image past the check
that round 82 found, and falls back to the plain `<stem>.app` a card dump
has. Every emulator run in this session has been against hand-placed files,
so that half is already exercised.

Two instruments were wrong, both found by pointing them at the build three
phones already run:

- **`game_setting` read `GAME_CAPTION "Asphalt 2"` as `Asphalt`.** The
  pattern was `(\S+)`, which stops at the first space. Harmless where it had
  been used -- the emulator only needs the UID -- but it would have put
  `Asphalt` on a phone's application list and `Asphalt` in an installer's
  text the moment the release build started reading it. There is now one
  implementation, in `picture.py`, which takes a quoted value whole.
- **`verify_pkg.py` asserted on every package it was given**, 39 descriptions
  against 40 data units for Asphalt 2 and 5 against 6 for Asphalt 1. It
  filtered descriptions to op 1 and then indexed the data units in parallel,
  which pairs every file with the unit belonging to the one before it; the
  install-text entry is op 4 and does carry a unit. A checker that fails on a
  build three phones install is reporting on itself. Fixed, and both packages
  now pass a device-style check: every `SISFileDescription`'s SHA-1 and both
  lengths against the bytes actually shipped.

A note on the install text: it lives in a C header and is read out of it by a
packager that does not run a C compiler, so it carries no backslashes and no
apostrophes. An escape there would reach the phone as an escape.

#### And the two from Asphalt 1, which are the same rule from the other side

*E287-E290, phase 2 of the second title.*

The sixth is the defaults themselves. `dumppng.py`, `bandpng.py` and
`cmpdump.py` all defaulted to pitch 176, height 208 and **origin 16** --
Asphalt 2's numbers, the last of them with a docstring explaining why sixteen
is right. Pointed at a second game those defaults are the fake wrap again, by
construction. There is now a `picture.py` that every one of them asks, that
has **no defaults at all** -- `--game <name>` or all three numbers explicitly
or it refuses -- and that prints a warning on stderr when the numbers in a
game's `game.h` have never been measured. `GAME_PICTURE_MEASURED` is what
says whether they have.

The seventh is the rule's plainest form yet. Asphalt 1's picture was called
misaligned three times off one screenshot -- a horizontal wrap, then two
pictures composited with a live outer copy, then nothing at all -- and the
answer each time came from somewhere other than the screenshot: the zero-run
spacing in the game's own buffer (stride 352, origin 0, no wrap signature),
the composited panel compared pixel for pixel against the source (1.000 over
62,560 pixels, 0 of 14,240 non-black outside it), and finally Asphalt 2 run
as a control, which looks the same and plays on three phones. The HUD around
an inset 3D viewport is how both games draw themselves.

Worth keeping separately: the **first** attempt at measuring the stride was
also wrong. Mean |b[i] - b[i+S]| against candidate strides -- vertical
continuity, which is the textbook method -- put its minimum at 350 bytes and
did not even make 352 a local minimum. Every candidate it liked was 2 mod 4,
because the picture is dithered on a two-pixel period and the metric was
reading the dither. What settled it was a property with no free parameters:
the buffer is `user_allocz`'d, so the ends of the drawn rows are still zero,
and those zero runs recur at exactly the stride.

#### The eighth, and it named a fault after the wrong function

*Round 92.*

`readbox.py` decodes a box's import indices into names through
`readlog.GAME`, which was one hardcoded path: Asphalt 2's `6rbc.app`. Point
it at Asphalt 1's box -- which is what every round of the second title has
done -- and the indices are right, the counts are right, and every **name**
is the other game's. Round 92's phone died with `G6FLT 25100`, import 251,
and the tool printed `RLine::EnumerateCall(int &) const`. This image's 251
is `User::AllocL(int)`, which is a different fault with a different cause;
the analysis happened to go the right way because the index was checked
against the game's own table by hand, and it need not have.

A wrong name is worse than no name, because it reads as a finding. Fixed the
way the geometry was: `picture.image(game)` resolves a title's `.app` from
its `GAME_STEM_CHARS`, `readlog` and `readbox` take `--game <name>`, and
there is no default -- the tool refuses and says which titles it knows.
`build_release.py`'s own copy of the stem parser is gone with it; one
spelling, in `picture.py`.

### The log ended before every failure it was meant to explain

*Rounds 92 and 93, E310, E311.*

The log stopped at a megabyte and dropped everything after it. A megabyte is
about 163 seconds of this game. Round 92 faulted 34,142 frames in; round 93
died on being backgrounded after 29,343. **Neither crash is in its own log.**
Both were read out of the box's thirty-two-event ring, which is enough to
name the last import and nothing else, and the first 163 seconds -- which
nobody needed -- were on disk in full.

The fix is not a bigger file. Two parts of a run are worth keeping and they
are at opposite ends: the boot, which takes thirteen seconds nobody can yet
account for, and the last few seconds before a failure. So the file has a
frozen head of a quarter megabyte, written once, and a ring of 768 KB behind
it, with a two-record header saying where the ring's oldest record is and how
far the head got. E311 reads back as frames 1 to 4,511 in order, ticks
monotonic, with a gap in the middle where the minutes nobody needs used to
be.

Worth saying plainly: this is the third instrument in this port that was
answering a different question from the one being asked, and the one that
cost the most. Two hardware rounds ended with "the log does not go that far".

### Retracted: the stalls were not the leak

*Round 92 claimed it, round 93 refutes it.*

Round 92's boot took 13.16 s, and I attributed it to `GAME_LEAK_ALL` and
`GAME_ALLOC_PAD` -- nothing freed, 512 bytes on every cell, so every
allocation grows a chunk. The arithmetic was real (1,779 `User::AllocL` in
that window, 7.4 ms each, against 0.3 ms later in the same run) and the
conclusion did not follow. Round 93 has the leak off and boots in **13.25 s**.

What went wrong in the reasoning is worth keeping. The 13.16 s was the gap
between two tick readings, and I read it as though the allocations were the
only thing in it. They are only the only thing *the trace can see*: 5,898
traced imports over thirteen seconds is 450 a second, so the game spends
almost all of that time in its own code between imports, and the trace says
nothing about what. A rate computed over a window is not a cost per event
unless the events fill the window.

`LOG_THE_CLOCK` -- a tick every sixteenth traced event -- has been in the
source the whole time, switched off. It is on for build 007.

### The picture is right and the panel disagrees

*Round 93, and the end of guessing about the wrap.*

Build 006 dumped, off the phone, the game's own 176x208 source buffer and the
composited frame buffer at the same two frames. Both are clean. The frame
buffer is 1280 bytes a line, 320 lines, 32 bits a pixel, and every row's
non-zero content lies strictly inside columns 0 to 239; rendered out, it is
the Sound Setup screen, correct and unwrapped. The panel, photographed, shows
that picture with a strip of its right-hand side appearing at the left.

A correct buffer displayed shifted is a scan-out that does not begin where we
think the line begins, and **nothing inside the process can ask the panel
where that is.** HAL is already known to lie about this phone's display: it
reported 16 bits a pixel on a 640-byte line for a 240-pixel screen in round
60, and it reports an offset-to-first-pixel of 0 here that may be no better.

So build 007 stops arguing and adds a knob: `*` and `#` move the picture one
frame-buffer column, `7` and `9` move it eight, `0` resets, the value is
saved per title and written to the log. Not `4` and `6` -- those are
steering, and a diagnostic that eats the controls of a driving game is worse
than the fault it measures.

The knob answers either way. If a setting lines the picture up, the offset is
the answer and it becomes the default. If nothing ever lines it up -- if the
picture slides but never squares -- then the panel's *stride* is wrong and
not its origin, and that is a different fix. Three readings of one photograph
have already been wrong in this port; a person turning a knob until the
picture is straight is a measurement, and a person describing a photograph is
not.

### The game may not draw while the screen is not its own

*Round 93. Both titles, and it had been there since the first one.*

Idle the phone for a moment and the process dies. Background it and the same.
The window server takes direct screen access away whenever something else
needs the screen -- the task list, a call, the screensaver after the
inactivity timeout -- by calling `AbortNow`, and from that moment until
`Restart` the process may not touch the frame buffer.

The game handles this correctly, as far as it goes: the port forwards both
callbacks to its EKA1 observer and always has. But **the game is not the
thing drawing.** `gate6_screen_update` blits into the frame buffer and hands
a region to the driver, and neither ever asked whether the screen was still
ours. On the N-Gage there was nothing to ask: the game owned the screen.

One flag, set in the abort callback and cleared in the restart one, and the
blit and the post both return early while it is set. The game goes on running
-- frames, timers, sound -- and is simply not seen, which is what
backgrounding is supposed to look like.

And the cheaper half of the same fix: `User::ResetInactivityTime()` once a
second from the frame loop, so the screensaver does not start at all. Every
S60 game does this. The game takes its input through the window server like
any other application, so a player holding one direction is, as far as the
system is concerned, idle. The N-Gage version never needed it because an
N-Gage did not put a screensaver over a running game -- which is the same
shape as every other finding in this file: the port has to supply what the
platform used to.

### A diagnostic setting is a thing that ships

*Round 92. The crash was mine, and it had been mine for three builds.*

Asphalt 1's `game.h` went to the phone with `GAME_LEAK_ALL 1` and
`GAME_FREE_BACK_FROM 0x7fffffff`: nothing the game frees is ever handed
back. Both were set during boot debugging -- E274 to tell the game's frees
from the framework's, E271 to stop a vtable pointer written over a freed
cell's link -- and E274's own comment says in as many words that it is not a
shipping setting. It shipped in builds 003, 004 and 005. Round 92 played for
34,142 frames and faulted at the `User::AllocL` that finally had nowhere to
go.

Two things make this worth writing down rather than just fixing.

**The reason for the quarantine had already been superseded and nobody went
back.** E271 held every cell because something was corrupting the free list;
E277 found what (the 9.x `CEikDialog` constructor overrunning a cell sized
for 7.0s) and fixed it with `GAME_ALLOC_PAD`. The stand-in stayed on for six
more emulator runs and three hardware rounds. E309 is the check that should
have been run then: frees back on, quarantine at 4,096 bytes, and **no
corruption** -- 4,108 frames, clean.

**The instrument said what I wanted to hear.** The box prints
`LEAK: nothing is freed` off bit 16 of its wrap word, and I read that in
round 92 as confirmation of the policy. Bit 16 only means the free *thunk*
is installed; it is set whenever `LEAK_EVERYTHING` is, which is always. The
reading was right by luck. There is a `W_LEAKALL` bit now that means the
policy and nothing else, printed on its own line and only when it is on.

And the cost was not only the crash. The tick clock puts the phone's boot at
**13.16 seconds**, and inside it 1,779 `User::AllocL` with almost nothing
else -- **7.4 ms an allocation**, against 0.3 ms for the same call later in
the same run. An allocation that slow is a chunk the kernel has to grow,
which is what never freeing plus 512 bytes a cell guarantees. The tracing
was the first suspect and the numbers acquit it: 531,897 traced events, a
log that stops at its 1 MB cap after 512 block writes, and a box written
once every 1,024 events. The slow thing was the policy, not the instrument.

### Phase 0: the game ignores the size it is told, and a blink nearly said otherwise

*E227-E230. The multi-resolution work opened by re-testing the one place the
record contradicted itself, and the answer is the boring one.*

`PORTING.md` said reporting a different `iScreenSize` gives byte-identical
frames. A comment beside `TELL_GAME_ITS_SIZE` in `gate6.cpp` said the
opposite -- that being told 240x320 makes the game lay its interface out for
240 and lose everything past column 176. Both cannot be true, and it decided
whether multi-resolution had a cheap win in it.

**It does not. The game ignores the size.** Four shots per run at 17-frame
spacing, clustered by exact content: each configuration produces exactly two
distinct pictures and both of them occur, byte for byte, in the other.
`srcPitch` logged 192 in the run told 192x208, so the flag really did take
effect -- the result is not vacuous.

So the clipped HUD is the game's own, nothing reported to it moves it, and
**the scaler is the only place resolution can change.** The stale comment is
corrected in place.

#### How it nearly went the other way

The first pair (E227/E228) compared frames 400 and 460 and found 445 words
differing, all in rows 153-159, columns 54-153 -- one seven-pixel line of the
game's own bitmap font. Rendered, the run told 192x208 reads **`PRESS ANY
KEY`** and the other is bare background. Frames 400 and 460 were identical
*within* each run, which appeared to rule out timing and make the difference
structural.

`PRESS ANY KEY` blinks on a **sixty-frame** cycle. Two shots sixty frames
apart catch the same phase for ever, so each run sampled one phase, and the
two runs -- which do not start in lockstep, 28,782 log records against 37,124
-- caught opposite ones. The "identical within each run" check that seemed to
rule out timing was the alias confirming itself.

Two habits came out of it, both now in the tools:

- `dump_region` takes **four shots at 17-frame spacing**, not two at 60. Any
  spacing that divides a repeating period is a spacing that can only ever see
  one phase of it.
- `phasecmp.py` **clusters shots by exact content** instead of pairing them by
  index or thresholding on brightness. The first version of it did threshold,
  and called all four frames "text on" because the background alone is bright
  enough -- a parameter chosen to separate two known cases, which then had
  nothing to say about a third.

That is the fourth time in this port the instrument has been the finding,
after the 40,000 disk commits a race that caused the framedrops, the
`rvt[k-2]` vtable read that cost six rounds, and `WRAP_ALLOCATORS` being off
so that no allocation failure could ever be recorded. The pattern is specific
enough to name: **every one was a measurement that could only produce one
answer, and none of them looked like it at the time.**

### Which keys the game does not use

*Measured from round 86's log: 7,361 key events over eight minutes of real
play, which is the only honest way to answer this.*

Every `iCode` the game was handed, with counts:

| code | | count |
|---|---|---|
| `0x35` | `'5'` | 772 |
| `0x00` | (key-up) | 587 |
| `0xF807` / `0xF808` | left / right arrow | 395 |
| `0xF845`, `0xF842`, `0xF843` | centre, softkeys | 34 |
| `0xF809` / `0xF80A` | up / down arrow | 16 |
| `0x32`, `0x37` | `'2'`, `'7'` | 34 |
| `0x38`, `0x34` | `'8'`, `'4'` | 1 each |
| `0x2A`, `0x23` | **`*`, `#`** | **0** |

So `*` and `#` are free and the picker belongs on them. Note `'8'` and `'4'`
*do* reach the game -- those were `KEY_MODE` and `KEY_INSET_*` when the picker
was live, which is exactly why rounds 74-78 had to switch it off. The old
picker was stealing game input, and one press in eight minutes is enough to
ruin a race.

The caveat: this is one player's session, and they may simply never have
pressed `*`. It is not proof that the game ignores it, only that the game was
never given it. The picker consumes the key it handles, so the cost of being
wrong is one lost input and the fix is a one-line change.

**A chord is feasible, which the same log settles.** The events arrive as
1,253 `EEventKey` (type 1), 294 `EEventKeyDown` (3) and 293 `EEventKeyUp` (2),
so held state can be tracked rather than inferred -- `'5'` shows 38 downs, 772
repeats and 37 ups, one press still held when the log ends. `EStdKeyBackspace`
is scancode **0x01** (EKA2L1's `keys.h`), which is the C key on a numeric
keypad and the backspace key on a QWERTY S60v3, and it appears **nowhere** in
the log either. So "hold C, press `*`" can be detected, and a bare `*` can be
passed straight through to the game.

What cannot be derived is **keypad rollover**: S60v3 keypads are matrix
scanned and not every pair of keys registers when held together. That is a
property of each phone.

### ...and why the gesture is a long press on C, not a chord

Neither `*` nor Shift is universal across S60v3, in opposite directions:

- **`*` is not one key on QWERTY.** On an E71/E61/E63 the keypad is QWERTY and
  `*` is a symbol reached through Chr/Fn, not a dedicated key.
- **Shift does not exist on a numeric keypad.** The N95, C5-00 and N79 -- the
  three phones this port is tested on, and most of the installed base -- have
  no Shift key at all. Scancodes `0x12` and `0x13` (`EStdKeyLeftShift` /
  `EStdKeyRightShift`) appear **zero** times in round 86's log, and on those
  phones they never can.

So a Shift chord works on QWERTY and is unreachable on everything we can test,
and a `*` chord is the reverse.

**The C key is the only one that is a single physical press on both layouts
with the same scancode** -- `EStdKeyBackspace`, `0x01`, the C key on a numeric
keypad and Backspace on a QWERTY one -- and the game never uses it. Checked
parser-independently: across all 7,361 `NOTE_KEY` records the value `0x01`
occurs 3,093 times and every one is accounted for as either an `EEventKey`
type or a "consumed" response (1,253 + 1,840 = 3,093), leaving none that is a
scancode. `0x2A` and `0x23` occur **not once**.

So the gesture is **hold C for about a second**. One key, no chord, no
rollover, no modifier, and identical on both keypad types. A short tap still
passes through to the game, so nothing is stolen even if some menu wants it.
Holding is measurable two independent ways -- down/up timestamps, or counting
auto-repeats, which reach us (772 repeats from 38 presses of `'5'`).

Note also what the port does **not** capture today: the four `NOTE_KEY`
records per event are `iCode`, `iScanCode`, the event type and the game's own
`TKeyResponse`. **`iModifiers` is not logged**, so nothing in the record can
say whether a modifier was ever held.

### What the game asks about the screen: nothing

Two facts that together decide the whole multi-resolution approach, and both
are checkable rather than argued:

- **The game imports no text rendering at all.** No `DrawText`, no `CFont`, no
  `TextWidth`, no `MeasureText`. Its entire graphics import surface is four
  `CFbsBitmap` calls, three `bitgdi` and two `ws32`. Every glyph and every HUD
  element is its own bitmap, authored for 176x208, drawn straight into the
  framebuffer at coordinates compiled into the binary.
- **`HAL::Get` is imported and never called.** Zero times in eight minutes of
  play in round 86's log. Its only geometry query is `UserSvr::ScreenInfo`,
  whose size it ignores (E133-E137).

So the game draws the same 176x208 picture into whatever buffer it is given,
on any device, and nothing the port reports can move it. **The only place
resolution can be changed is our scaler, between the game's buffer and the
device's framebuffer.** Rendering natively wider would mean patching every
hardcoded layout constant *and* redrawing every asset, and would cost 2.1x the
fill rate at 240x320 on a CPU already at 30 fps.

### What a clean build-172 log looks like, and the three things it cannot say

*Round 86. A third party's eight-minute log, sent with a claim of "still
crashes from out of memory". Worth reading as the reference for a healthy run.*

| | |
|---|---|
| length | 8 min 06 s, 14,547 frames, ~30 fps, 7,361 key events |
| heap peak | **6.80 MB** / 11,999 cells, across 227 samples |
| heap floor | 3.37 MB -- it falls between races |
| cumulative allocation | **97.8 MB**, of which 68.2 MB is zlib's `calloc` at `+0x1166c0` |
| `CMdaAudioOutputStream::Open` | **once** in eight minutes |
| zlib failures / leaves / panics | none |

97.8 MB asked for against a heap that never passes 6.80 MB means roughly
**91 MB was allocated and handed back**. The quarantine is not a one-off
saving made at startup; it is running continuously, all session. And one
`Open` for thirteen track changes rules out the other leak worth suspecting,
an audio stream per track.

The bench agrees closely: **6.67 MB peak here against 6.80 MB on the phone.**
Same game, same memory, two machines.

#### Where the run actually ends

Not at a failure. The log stops **27 records into a music track change that is
byte-identical to the five before it** -- `Stop` -> callback 2 ->
`SetAudioPropertiesL(0x100, 0x2000000)` -> `MaxVolume` 10 -> `SetVolume(10)` ->
`Stop` returned -- immediately before the `RMessage::Complete` that follows it
every other time. The last frame took one tick.

That tail is exact rather than truncated, which is worth stating because it is
not obvious: the MDA proxy calls `log_block` on **every** return, so the
records were flushed at that point on purpose. The death is inside the 255
records after it, which is the second or two between a race ending and the
results screen -- the phase that, twice earlier in the same log, runs
`bg_136x64.mpg`, then writes **`user.dat`**, then opens the next track.

#### The three blind spots

Honest limits on the above, and all three are now closed in build 173:

1. **A null from an allocator was not recorded.** `WRAP_ALLOCATORS` was
   defined as `WATCH_ALLOCATIONS`, which is 0, so the wrapper that catches an
   allocation returning zero was switched off. "No allocation failed" in a
   build-172 log means only that nothing could have said so. This is the same
   shape of mistake as `LEAK_EVERYTHING`: a switch whose setting leaves no
   trace in the output. Now `WRAP_ALLOCATORS = 1`, decoupled.
2. **A spike between samples was invisible.** The heap was read every 64
   frames. Now `User::AllocSize` is read every frame -- it reads two counters
   the allocator already maintains, it does not walk the heap -- and a record
   is written only when the peak beats the last reported one by 64 KB. Eleven
   records for a whole run, at per-frame resolution.
3. **Free *system* RAM was never measured at all.** Our numbers are this
   process's heap. An allocation fails on what the whole phone has left, and
   a heap sitting at 7 MB says nothing about the window server, DevSound, the
   file cache or anything in the background. `HALData::EMemoryRAM` is 15 in
   this numbering (`kernel/hal.def` says so outright), so `EMemoryRAMFree` is
   **16**, and it now goes down beside every heap note. EKA2L1 answers
   `free = total` -- 134,217,728 on the RM-409 -- so the bench can prove the
   call works and nothing else. The phone answers honestly.

The general lesson is the one round 85 already taught, arriving from the other
direction: **an instrument that cannot produce a negative result cannot be
cited as evidence of one.** The heap numbers here are strong because they are
measured; the absence of allocation failures was worth nothing.

### The crashes are a diagnostic switch left on

*E219-E225, confirmed on hardware in round 85: several races on the N95, no
crash. The one finding that explains the crashes, and it is ours.*

**`LEAK_EVERYTHING = 1` in `gate6.cpp` answered `User::Free`, `operator
delete` and `operator delete[]` with a function that does nothing. It has been
on since build 49 and it shipped in every build since, the one on the phone
included.** It was a diagnostic: build 49 was stuck at a wall that looked like
a use-after-free, and freeing nothing is the cleanest way to ask whether it is
one. The run advanced. The switch stayed on. `heapMax` was raised to 64 MB so
the bench would not notice, and it did not.

What it costs, measured rather than assumed:

| | leaking | after |
|---|---|---|
| cells live at the end of a 90 s run | 26,648 | 11,405 |
| bytes held | **51.1 MB** | **6.5 MB** |
| free list / biggest cell | 1,664 / 1,628 | 149,536 / 23,060 |
| frames | ~2,400 | 2,744 |

25,684 allocator calls and 26,648 cells still live is the whole thing said in
one line: the process was freeing nothing. On this bench that is free. A phone
has perhaps twenty megabytes to give, and the first allocation it cannot
satisfy is whichever one happens to come next -- so the crash lands somewhere
different each time, late, and after enough play, which is exactly how it was
reported.

In the failure that was recorded it lands inside zlib. The game's zlib 1.1.3
`uncompress` at `+0xd4f88` allocates a 32 KB window and a state block through
the `calloc` at `+0x1166b0` -- 1,492 calls, 32.1 MB, the single largest
consumer in the run -- and when that fails it returns `Z_MEM_ERROR`. The call
site does this with it:

    33a74  bl    0xd4f88        @ uncompress
    33a78  cmp   r0, #0
    33a7c  mvnne r0, #3         @ ANY nonzero status becomes -4
    33a80  blne  0x118da8       @ User::Leave(-4)

which is why the log could only ever say -4: the game destroys the status
before anything can read it, and -4 is also `KErrNoMemory`, so the one reading
the evidence supported was the one that happened to be right for the wrong
reason.

**The replacement is not "turn it off".** The use-after-free the switch was
hiding may still be there, so the rule is now two rules that do not overlap:

- anything **4 KB or larger goes back at once** -- that is where all the memory
  is, and a container small enough to be read after it was freed is not in
  that band;
- everything smaller goes into a **512-cell quarantine** and is returned when
  that many further frees have pushed it out, so the heap still cannot hand a
  just-freed small object to anybody else.

The ceiling that leaves is 512 x 4 KB, and in practice far less.

**The lesson is about the switch, not the leak.** Nothing in the build said it
was on. It had a comment explaining the experiment, a name that says exactly
what it does, and no expiry -- and the thing it does is invisible on a machine
with memory to spare, which is the only machine it was ever measured on.
`SILENT`, `PAD_THE_ALLOCATIONS`, `PATCH_THE_CHECK` and `PATCH_GATE_TWO` are the
same shape. A diagnostic that changes behaviour and cannot be seen in the
output is a diagnostic that ships.

### How the heap is measured now

`RHeap::Available` is the wrong question and answered 2,320 bytes all through
the leaking run: on a heap that grows on demand the free list is short whatever
is going on. `User::AllocSize` (euser ordinal 664, beside `User::Allocator` at
665, which this port already resolves) answers what is actually held. Four
numbers go down every 64 frames -- cells out, bytes out, free, biggest -- and
all four together, because a heap with 300 KB free in 2 KB pieces fails a 40 KB
request exactly like an empty one.

Beside them: a size histogram over every allocator call, and a table of every
call site that has asked for 8 KB or more with its count and total. That table
is what named `+0x1166b0` as `calloc` in one run after the histogram had only
been able to say "772 allocations between 32 and 64 KB".

And the zlib call sites themselves are hooked -- a `bl` in place of the `bl`,
at both `+0x33a74` and `+0x11562c` -- recording the destination, the room the
caller claims is in it, the source, its length, the stream's first word, and
then the **true** status and the length written back, before the game flattens
it. On the bench every one reads `78 9c`, status 0, and bytes written exactly
equal to the room claimed.

### The display, on phones that are not the N95

*Round 83. Build 170 installs and boots, and two things are wrong: the icon
is an empty box, and the display is broken on the user's C5-00 and N79.
Both were found by another model working from the binary while this session
was out of context, and both readings hold up against the source.*

**`HAL::Get` takes the mode number *in* the same integer it answers in.**
`EDisplayBitsPerPixel`, `EDisplayOffsetBetweenLines` and
`EDisplayOffsetToFirstPixel` are per-mode properties, and every build up to
170 left that integer at zero:

    int bpp = 0, pitch = 0, first = 0, mode = -1;

So all three described **mode 0**, while `EDisplayMode` reported mode **1**
was live -- logged, and never acted on. One line, and it is the bug.

It is not academic. Reading the three phones' logs side by side:

| | HAL bpp | HAL pitch | pixels per line | mode |
|---|---|---|---|---|
| N95, mode 0 | 16 | 640 | 320 | 1 live |
| C5-00, mode 0 | 16 | 4096 | 2048 | 1 live |
| C5-00, mode 1 | 24 | 8192 | 2048 | 1 live |
| N79, mode 0 | 16 | 4096 | 2048 | 1 live |

**A framebuffer line is a fixed number of pixels; the mode only says how
wide a pixel is.** The N95's line is 320 pixels -- its panel is natively
320x240 landscape, which round 72 worked out the hard way from a
photograph. The C5's and the N79's are 2048, an aligned stride. Both of the
C5's modes describe the same 2048-pixel line.

**And the old selection rule asked the wrong question.** `pitch == w * 2 ||
pitch == w * 4` asks whether the line is as wide as the *logical screen*.
On the N95 it is not, so the rule never matched there -- not once -- and
the 1280-byte fallback carried the display for eleven rounds while looking
like a working rule. On a phone with a wider line it rejects the truth and
writes a fifth of each row into the right place and the rest into the next,
which is the streaking the user photographed on the C5.

So: ask the live mode, believe the reported stride when it is sane (at
least a visible row, no more than 65536), and treat **24 bits as 32-bit
storage** -- `EColor16MU` is 24-bit colour held four bytes to a pixel, and
Symbian's own screen driver treats HAL's 24 and 32 as the same thing. The
emulator says 24 for a 960-byte line on a 240 screen, which is four bytes a
pixel exactly, and that is why.

Confirmed on the C5 by the other model's binary patch; ported to source
here and checked to read identically on the bench (E216), where asking the
live mode returns a straight 32 where mode 0 returned 24.

**Confirmed on the N95 too** (round 84), which was the last doubt: its
mode 1 had never been measured, and the worry was that it might report the
same 16 bits on a 640-byte line as its mode 0 does -- which the new rule
would accept, and which round 60 proved wrong. It does not. The
pixels-per-line rule held: whatever mode 1 reports there, it selects the
same 32 bits on a 1280-byte line the old fallback was choosing, and the
display is unchanged.

So the rule is now confirmed on two phones that need different answers --
the N95 at 320 pixels a line and the C5-00 at 2048 -- and no phone still
needs the hardcoded fallback. The N79 has not reported back; the rule
predicts it is the C5's case, since its mode 0 reports the same 16 bits on
a 4096-byte line.

### Why the icon was an empty box

S60v3 draws a **MIF**, not an MBM. Build 170 shipped a correct `.mbm`,
named it in the caption resource, and the phone drew nothing; EKA2L1
rendered it perfectly. That is the third time this project has been caught
by the emulator being more permissive than the device, after the SIS
integrity fields and the executable-outside-`\sys\bin` rule.

`mkmif.py` converts the MBM: each horizontal run of one colour becomes a
rectangle path in an SVG Tiny document, wrapped in a MIF container. Same
geometry, same colours, same 1,936 opaque pixels. Both files ship and the
caption resource names the `.mif` -- two bytes.

**The palette is 0x00BBGGRR.** Reading it as 0xRRGGBB is what made
`aificon.py` draw the icon in blue for a whole round while EKA2L1's own app
list drew it in orange. The table was right; the channel order was not. The
fix in `aificon` then broke `mkmif`, which was swapping a second time --
caught immediately, because `mkmif` is checked byte-for-byte against the
MIF the phone accepted.

### Why the image travels as 6rbc.bin

*Rounds 80 to 82. The first standalone installer was refused by the phone,
and finding out why took six probes and no guessing that survived one.*

**Symbian will not install an E32 executable image anywhere but
`\sys\bin`, and it decides by reading the file rather than its name.**
`6rbc.app` is one -- uid1 0x10000079, `'EPOC'` at 0x10 -- and it is the only
file in the game's tree that is; the `.aif` is a bitmap store and the rest
is data.

Probe 4 is that one file and nothing else, and the phone refuses it. Probe
5 is the same bytes renamed `6rbc.bin`, and the phone refuses that too.
Probe 6 is the same bytes with the first 32 XORed -- the UID triple, the
checksum and the signature -- and the phone takes it. Which is the sensible
design: a rename would have made the rule a formality.

So the installer ships `6rbc.bin`, scrambled, and the loader XORs those 32
bytes back after reading its own copy. `IMAGE_SCRAMBLE` in gate6.cpp and
`SCRAMBLE` in build_release.py are the same constant and have to agree.

**The game opens its own image too**, which is what made this more than a
rename: two opens in the emulator's file log, mode 1 from the loader and
mode 2 from the game's own reader at 0x34a0c. The loader remembers which
name it loaded, and when it loaded a `.bin` it redirects the game's
`...\6rbc.app` to `...\6rbc.bin` on the way into efsrv, beside the drive
translation. A hand-copied N-Gage dump still has the original name and is
untouched.

I had two designs ready for the case where the game minded the scrambled
header -- track the file position and patch reads that touch the first 32
bytes, or write a clean copy out at first launch and redirect there. E214
made both unnecessary in one run: scramble the file, run the game, and it
never notices. **The cheap experiment was worth more than the design.**

**What the failure taught about diagnosis.** The display-text entry was my
leading suspect for a whole round, on the strength of one argument: it is
the last entry in the package, so the installer reaches it at the end, and
the failure was at the end. That is a real argument and it was worth
nothing -- probe 2 carries a text entry and installs. Three of the six
probes existed only to retire hypotheses I had ranked, and the one that
found it was the one I ranked third.

**`sischeck.py`** came out of the same round and closes a gap this project
had written down and not acted on: the emulator verifies none of the
integrity fields a device checks, so a package can be valid here and
refused there. It checks every file's SHA-1 against its actual decompressed
data, every compressed and uncompressed length, and the descriptor layout,
and it is calibrated against a real signed Gameloft S60v3 package that the
phone accepts. Build 167 passed all of it, which is how the package itself
was ruled out early.

### The standalone installer

One SIS, 20.6 MB, 39 files: the port, all 34 of the game's own files, the
icon, and a line of text shown during the install. `build_release.py` makes
it; `buildapp.build` grew three optional arguments for it (`icon`, `extra`,
`install_text`) and the bench build passes none of them, so it is unchanged.

**The icon is the original.** `6rbc.aif` is a direct file store whose UID2
says AIF rather than multi-bitmap, and whose bitmaps are laid out exactly as
an MBM's are -- a 40-byte `SEpocBitmapHeader` and its data, one after
another from offset 20, each header's first word leading to the next. So
`mkmbm.py` copies them byte for byte and writes an MBM container round them:
nothing is decoded, nothing re-encoded, and the icon that reaches the phone
is the one Gameloft drew. The AIF holds two pairs, 44x44 and 42x29; only the
44x44 colour bitmap is shipped.

**Its mask is not**, and the reason is a convention that reversed. The AIF's
mask rows read `00 00 00 00 00 f0 ff ff` -- for a 44-pixel row that is every
visible bit *clear* and the padding set, which in the old AIF world means a
set bit is **transparent** and the icon is a solid square. S60v3 reads an
MBM icon/mask pair the other way round. Copying that mask across would have
asked the shell for an icon that is entirely invisible. `mkmbm` generates a
fully opaque mask instead, which is what the original meant.

**Looking at it mattered.** `aificon.py` renders an AIF's bitmaps to PNG so
an icon can be seen before it ships, and its first answer was a blue smear:
right shape, wrong colours, because the 256-colour table it picked was not
the one the platform uses. The thing that settled it was installing the
package and screenshotting **EKA2L1's own app list**, where the icon comes
out as the orange swoosh it is (`shots/r80-icon-in-applist.png`). The
platform's own renderer is the honest check; mine was the instrument being
wrong again.

**The install-time message** is a `SISFileDescription` with operation
`EOpText` (4) and option `let_continue` (1<<9): the installer displays the
file and puts it nowhere. `sisadd.make_filedesc` takes the operation and
options now, and `mksis.build` reads a `None` target as "show this". The
file is UTF-16LE with a BOM. EKA2L1 processes it (E211) but logs the raw
buffer, so its log line shows the BOM rather than the text -- how Symbian's
own installer renders it is the one thing the bench cannot say.

The package installs to whichever drive the user picks, because every target
is `!:`, and the port then finds the game wherever that was -- see the
section above.

### Which drive the game lives on

*Round 79's other question, answered on the bench: it now installs to phone
memory or a card, and the game cannot tell.*

Three things wanted a drive letter, and only one was ours.

**Ours**: the loader tried `E:\system\apps\6rbc\6rbc.app`, `E:\6rbc.app`
and `C:\6rbc.app` -- never `C:\system\apps\6rbc\`, the layout a C:
install actually uses. It now searches two layouts across C, E, F, D, G and
H, E first because that is where every install so far has put it.

**The game's, one**: it has absolute `E:` paths compiled in -- the prefix
`E:\system\apps\6RBC\Streams\` that it joins the `bgm_*.swav` names
onto, and three full paths beside it. Those cannot be made relative.

**The game's, two**, and the one that would have cost a round: the BiNPDA
loader's read-only-card protection tests the **drive letter**, not the
directory. Its replacements for `RFile::Open`, `Create` and `Replace` are
all `if (name[0] == 'e' || name[0] == 'E') return KErrAccessDenied;`. Move
the game to C: and the protection stops firing, and a game that finds its
own drive writable has good reason to decide it is not on a card.

So the answer is not to teach the port about C:. It is to **never tell the
game about anything but E:**. `CApaApplication::DllName()` answers with the
`E:` form of whichever layout was found; every path the game derives is
therefore an `E:` path; its own compiled-in `E:` paths agree; the protection
fires exactly as it does on a card. The three file calls the protection
replaced then translate `E:` to the real drive on the way into efsrv, after
the refusal has been decided on the name the game asked for.

The save is untouched by all of it, and deliberately. The game asks for
`c:\system\apps\6RBC\user.dat` as an absolute C: path of its own, so it
is neither translated nor refused, and it lands in the same place on every
install -- which is what makes it something a person can back up. On a C:
install the data directory and the save directory are the same folder on
disk, and the two rules still hold apart, because each is decided by what
the game asked for rather than by where the bytes are.

On an E: install `dataDrive` is `'E'` and the translation returns every name
untouched, so the existing setup pays nothing.

`RFs::Delete`, `MkDir`, `MkDirAll` and `RmDir` are not translated. The game
uses them for its C: save directory, which needs no translation, and an
`E:` one would be refused by a real card anyway.

Proved without a phone round: **E209** is the regression on E: (identical
frame profile, identical audio sequence, every name unchanged), and **E210**
moved the whole tree to C:, emptied E:, and read the emulator's own file
server log -- `C:\system\apps\6rbc\6rbc.app` found, every data file on
C:, `C:\system\apps\6RBC\Streams\bgm_moby_lift_me_up.swav` opened from
a path the game spelled with an E, and the card refusal still firing once.

### The save, and what to back up

Hardcoded in the game, absolute, always C:, whatever drive it is installed
to:

- `c:\system\apps\6RBC\user.dat` -- the save
- `c:\system\apps\6RBC\user.bak` -- the game's own backup of it
- `c:\system\apps\GameMgr\6RBC.cfg`, `...\icons\6RBC.mbm` -- N-Gage Game
  Manager, unused on S60v3, but the folder is still created

The game ships a manifest string of its own saying so:
`<File Description="" Path="c:\system\apps\6RBC\*" FileType="UserData"
FreeStatus="Free" CreateStatus="Created" PortableStatus="Portable"/>`.

No package of ours lists anything under that path, so neither installing nor
uninstalling touches a save.

### The instrument outgrew its budget

*Round 79. Sound worked on the phone and the race started dropping frames,
worst on nitro. None of it was the sound.*

`BOX_EVERY_TRACED` had been **1** since round 62, so every traced import was
an `RFile::Write` of 1,264 bytes **and an `RFile::Flush`** -- a commit to
flash. The reasoning for that is still in the source and states its own
premise: *"about two hundred and forty write-and-flush pairs on a run of the
length the phone reaches"*. It was right then. A race with sound makes
**27,378** traced events, because the bridge traces `SendReceive` and
`RMessage::Complete` and those two alone are 24,404 of them. With the log's
blocks of eight and two more flushes per frame, that is about **forty
thousand disk commits in one race, 9.5 in every frame** -- and they bunch
where the sound is busiest, which is why nitro was the worst of it.

Build 166: box every 1024 traced events, log blocks of 256, no flush per
frame. About **two hundred commits a race** -- fewer than round 78's smooth
build, which did two per frame.

The general shape of this, which has now cost rounds twice (the 64-record
plateau of builds 38-43 was the other): **a budget set against one workload
is not a budget.** Every threshold in the instrument -- how often the box
goes down, how deep a block is, what gets traced at all -- was calibrated
against a run that died in the first few hundred imports. The moment the
port started working, the same settings became the slowest thing in the
frame. Re-derive them when the thing being measured changes size.

The clock that replaces them costs nothing: `CLOCK_EVERY_FRAME` writes
`User::TickCount` either side of the game's `RunL`, two records and two
syscalls a frame and no write of its own. `toolchain/port/ticks.py` reads
it: a frame is one tick at 1/64 s, three or more is a drop, and it prints
what was inside the long ones.

What the game itself costs, for comparison, from the same round-79 log:
2,523 `RFile::Read`s from one open file across the race, 1,791 of them
inside a frame, in bursts of up to 63; only 21 `RFile::Open`s in the whole
race, so the sound server streams rather than loading per effect. Whether
any of that is visible with the instrument quiet is what round 80 answers.

### Audio: the chain, end to end

Mapped now, from the music file to the speaker, with the blocker in one
place. Reconnaissance and two probe runs (E179, E180); no fix written yet.

**1. The music exists and is installed.** The image holds the paths
`E:\system\apps\6RBC\Streams\bgm_moby_lift_me_up.swav`, `bgm_win.swav`,
`bgm_loose.swav` and a `sounds\` folder, and the install has
`streams/bgm_*.swav` -- fourteen of them. Also in the image:
`opt_volume`, `SoundServ server`, `SoundServer`, `Sound server panic`, and
`Decoder currently does not parse transport streams`.

**2. The player is `CMdaAudioOutputStream`, and it is import 458.** Two call
sites, `0x1df2c` and `0x1dfc4`, both `add r0, obj, #4` / `mov r1, #0` /
`bl` -- which is `NewL(MMdaAudioOutputStreamCallback&, CMdaServer* = NULL)`
exactly. The result is stored at object+44 and then **called through its
vtable**: at `0x1df08` the game loads `[stream]`, takes slot 2 and calls it
with argument 3. **So `epoc6.def` is wrong about this ordinal for this
build**: it names mediaclientaudiostream ordinal 2
`CMdaAudioOutputStreamPadFunction`, and the call site proves it is `NewL`.
That matters, because the shim believed the def and left it a reporting stub.

**3. The game hosts its own sound server in-process.** `RSessionBase::
CreateSession` is called once per launch, at `0xba540`, and only when
`0xb86ec` -- the connect-or-start-SoundServer site -- returns zero. It does,
and the session is created. Thirty-five `SendReceive` wrappers sit
immediately after it, `0xba5c0` to `0xbad28`: the sound API, one wrapper per
request.

**4. The client speaks constantly, and nothing is listening.**
`RSessionBase::SendReceive` is called **835 times a minute**, from eight of
those wrappers. Our stand-in for it is `LOCAL_NOOP`: it answers `KErrNone`
and drops the message.

*(Corrected from the first reading of this, which said SendReceive was never
called. `TRACE_MILESTONES` traces only the imports listed in `kMilestone`,
and 357 was not among them, so the zero meant "never watched". Anything read
off that list needs the list checked first -- `CServer::*`, `CSession::*`
and import 458 were all in the same position.)*

**5. The server thread builds its server and waits for ever.** Its records
go to RDebug rather than the log, because the box only writes from the main
thread, and the emulator's own output has them: `CTrapCleanup::New`,
`User::AllocL`, `CActiveScheduler` constructor and `Install`,
**`CServer::CServer`**, **`CServer::StartL`**, `RSemaphore::Signal`, then
`CActiveScheduler::Start`. So the thread is alive and sitting in its loop.
But `CServer::StartL` is `LOCAL_NOOP` -- nothing was ever really started --
and there is no session, so no message ever arrives, `ServiceL` never runs,
and `CMdaAudioOutputStream::NewL` is never reached.

**So the silence was one missing piece: the in-process bridge. It is built,
and it works** (E182-E186).

* `CServer::StartL` keeps the server instead of doing nothing.
* `RSessionBase::CreateSession` calls the server's `NewSessionL` -- **vtable
  slot 6**, found by dumping the vtable the game installs at `0x10182b04`
  right after `CServer::CServer` -- and keeps the session in the handle word.
* `RSessionBase::SendReceive` fills in a message and calls the session's
  `ServiceL` -- **vtable slot 5** of `0x10182f44` -- straight, on the
  caller's thread. **The message layout is `iFunction` at 0 and `iArgs[0..3]`
  at 36, 40, 44, 48**, read off the dispatch (`ldr r3, [r1]`, `sub #1`,
  `cmp #34`, a 35-entry jump table matching the 35 client wrappers) and
  confirmed against two handlers and their matching client wrappers. The
  handlers find the live message at **`session+16`**, so the bridge parks it
  there.
* `RMessage::Complete` records the answer in a word of our own at the end of
  the message, which is what `SendReceive` returns.

The first run of it ended in **our own** `G6IMP 464458` panic -- 464x1000 +
458, `CMdaAudioOutputStream::NewL` -- which is the proof: the game had never
reached the audio call in its life. With a logging fake in its place the
whole path runs, no panic, and the bridge carries functions 1, 4, 5, 15
(x48), 19, 25, 26, 27, 28, 32 and 33 across in a minute.

**The chain is complete, and it plays** (E202). What was left was two
tables, and both were off by the same misreading of two ABIs.

**The rule behind both.** GCC98r2 stores the vtable **object's start** in
the object -- offset-to-top, typeinfo, then the entries. EABI stores its
**address point** -- the entries directly. So a slot number counted from
the pointer the object carries means two different things on the two sides.

**The stream: the identity, from slot 3.** The game's layout is two header
words, one destructor entry, then `SetAudioPropertiesL`, `Open`,
`MaxVolume`, `Volume`, `SetVolume`, `SetPriority`, `WriteL`, `Stop`,
`Position`. 9.x's is two destructors (complete and deleting),
`CBase::Extension_`, then the same nine in the same order. Three slots of
head on each side, so **from slot 3 the two are the same table**. The map
is the identity; the game's one destructor entry maps to 9.x's deleting
destructor.

Read out of the binaries rather than guessed. The ROM DLL
(`z/rm-409/sys/bin/mediaclientaudiostream.dll`, code 0x804d3eb8, 17
exports) has `_ZTV21CMdaAudioOutputStream` at ordinal 12 = 0x804d50cc:
zero, the typeinfo, twelve function words, then 0xfffffffc -- the
offset-to-top of the secondary vtable for `MMMFClientUtility`. The
emulator's patch DLL has the same twelve. The game's side comes from its
own dispatch sites: a sweep of the image for vtable calls on the stream
finds slots 3, 4, 5, 7, 8, 9, 10 and nothing else, and each site names
itself -- slot 5 takes no argument and its result feeds slot 7
(`MaxVolume` into `SetVolume`), slot 8 is called with `(100, 0)`
(`SetPriority`), slot 4 is called with the package at `this+0x30` straight
after `NewL` (`Open`).

**The callback: a shift of two.** `MMdaAudioOutputStreamCallback` is a pure
mixin with no destructor, so nothing cancels the game's two header words.
9.x calls entry 0 expecting `MaoscOpenComplete`; the game's entry 0 is
**0xfffffffc**, the offset-to-top of a secondary base -- its callback is a
mixin sub-object four bytes into its sound object. So the callback needs
its own proxy in the other direction, three entries mapping to the game's
2, 3, 4.

**The ordinal is 3, and the reason is worth keeping.** Two DLLs carry this
name. `RLibrary::Load` opens the **ROM's** (UID3 0x10003996, 17 exports,
which is `epoc9.def`'s list); the emulator's patch (UID3 0xEE000001, 55
exports) does not replace it but overwrites individual exports of it, per
`patch/mediaclientaudiostream.dll.map`, whose lines read `<patch export>
<ROM ordinal>`. Its `9 3` is the patch's `NewL` over ROM ordinal 3. Ordinal
9 of the ROM is *typeinfo*, and calling it (E200) landed a run in a string
table.

**The settings package needs no translation.** 9.x's `Open` reads
`[r1,#0x1c]` for the rate and `[r1,#0x20]` for the channels; the game
writes its rate and channels at exactly those offsets of the package it
builds at `this+0x30`. The values are the same enum too: the game's own
Hz-to-enum function answers 0x10, 0x40, 0x100, 0x400, 0x1000, 0x4000,
0x10000 for 8000..48000 Hz and 9.x's `ConvertFreqEnumToNumber` takes
exactly those; the game's channel word is 0x02000000 and 9.x reads that as
mono.

**What E202 recorded.** `Open` returned, **`MaoscOpenComplete(KErrNone)`**,
`SetAudioPropertiesL(16000 Hz, mono)`, `MaxVolume()` = 10, `SetVolume(10)`,
`SetPriority(100, 0)`, three defensive `Stop()`s, and then **71,777
`WriteL` calls each answered by `MaoscBufferCopied(KErrNone, ...)`** over
150 seconds, with the emulator's patch printing `Open complete` and the
game holding `bgm_moby_lift_me_up.swav` open.

**Retracted with it**: the faults at 0x100, 0xFFFFFFFA and 0x64 were never
null fields inside an implementation that had not finished opening. They
were `WriteL` being handed an integer where it wanted a descriptor,
because the map was two slots high. 9.x opening asynchronously is real but
was never the problem -- the open completes, and it says so.

**How the two-slot error survived six runs.** `MDA_DUMP_VT` reads
`rvt[k - 2]` on purpose, to catch the two words behind the pointer, and its
output was then read as if `k` were the slot number. The instrument was
right; counting from it was not. The wider lesson is the one E198 already
pointed at and this session finally took: the answer was in the binaries
the whole time. The patch DLL is compressed with Symbian's own deflate,
which zlib will not inflate, and thirty lines against EKA2L1's own
`flate::inflater` (`src/emu/common/src/flate.cpp`) opened it. Read the
binary before instrumenting the run.

**What the probe added.** Every N-Gage-only import (GAMEUTILS, GAMECOMMS,
ARENAFRAMEWORK, NOKIAFC) is a no-op returning zero. Handing back a non-null
fake object instead changes the game's behaviour markedly: GAMEUTILS ordinal
17 goes from never called to **1,883 calls, one a frame**, and GAMECOMMS
ordinals 2, 20, 27 and 28 begin to be called. The game calls slot 2 of the
fake's vtable on two of those objects, so it treats them as polymorphic.
From the call sites: GAMEUTILS **19, 14, 15, 10 are constructors** (results
stored at object offsets 972, 980, 664, 988), **11 is a predicate** turned
into a boolean, and **17 is a per-frame predicate** on the object 19
returned. Answering that poll with a pointer means "yes" every frame, so the
blanket probe is off again -- the next one needs an answer per import.

### Settled

- **The game runs on the phone.** Round 69: 836 frames, 968 framework calls
  into our slots, 157 key events, no panic, and it reaches its main menu and
  answers the keypad. The SoundServer thread lives and signals its semaphore
  inside the first 100 ms slice. Everything structural is done. What remains
  was that the framebuffer's real format was unknown; rounds 72 and 73
  settled it, and build 142 scales the picture to fill the screen.
- **The framebuffer is 32 bits a pixel on a 1280-byte line** (rounds 72, 73
  and 74, three independent readings). **HAL is wrong about all of it** on
  this phone -- 16 bits, a 640-byte line and `EGray2` -- though 640 is the
  same 320-pixel line at the other depth, so it was right about the pad by
  accident.
- **1280 bytes is a padded stride, not a width** (round 74). 240 pixels at
  four bytes is 960; the hardware keeps a 320-pixel line and shows the first
  240 of it. **The screen is the 240 x 320 that `UserSvr::ScreenInfo`
  reports** -- round 72's reading of it as the rotation of a 320x240 landscape
  panel is retracted, because at 203 pixels wide from column 58 the picture
  ran off the right-hand edge, which needs a visible width under 261. So:
  lay out from the reported size, and use the pitch only to step between rows.
- **Believe HAL's pitch only when it is exactly `w * 2` or `w * 4`.** The
  emulator answers 24 bits on a 960-byte line for a 240-pixel screen, which is
  self-consistent and correct; the N95 answers a line that is a multiple of
  nothing, and there the measured 1280 is used instead. One build then draws
  correctly in both places (E163, E165).
- **The picture is scaled, not centred** (builds 142-143). 176x208 into
  240x320 is width-limited, so it is drawn **240x283 at y=18**. Nearest
  neighbour through a table of source columns and rows built by Bresenham
  accumulation, because there is no `__aeabi_uidiv` in this image and a
  runtime divide will not link. The whole buffer is blanked once after any
  change -- all 320 rows, stopping at the last row's last visible pixel, since
  the pad after the final row need not be allocated. `8` toggles back to the
  centred 176x208.
- **The screen chunk begins with a sixteen-entry word palette, so
  `EDisplayOffsetToFirstPixel` must be applied** (E171, E172). `ScreenInfo`
  hands back the chunk's *base*, not the first pixel. The emulator answers 32
  and needs 32 -- its own source computes the offset as `sizeof(u16) *
  WORD_PALETTE_ENTRIES_COUNT` in `hal.cpp` and adds exactly that to the chunk
  base in `screen.cpp`. The **N95 answers 0 and 0 is right there**. So HAL is
  believed for this one attribute, after a sanity check (a whole number of
  pixels, under 4 KB), where it is not believed about depth, pitch or mode.
  Refusing it cost nine rounds: the picture started eight pixels early and
  every row wrapped, which painting the blit's own outline showed in one run.
- **The port writes the framebuffer directly, underneath the window server,
  so the app must ask for no screen furniture** (round 76). Anything the
  window server paints lands on top of the picture. The game calls
  `BaseConstructL(0)` -- a standard application, status pane at the top,
  button group at the bottom -- and that status pane is the band across the
  top of the phone's screen, visible as itself (close icon, battery) in round
  74's photograph. `ENoScreenFurniture` (0x04) is added to the flags. It is
  not `ENoAppResourceFile` (0x01), which took avkon off the rails in an
  earlier build because avkon does need the resource file.
- **The game's picture starts sixteen pixels into its buffer, and its stride
  is 176** (round 75, E167, E168). Both measured, neither guessed. The stride:
  the phone swept every read stride from 160 to 256 and 176 is the only one
  that does not shear, and scoring two raw frames by vertical continuity picks
  176 at 3.76 against 5.46 for its nearest neighbour. The origin: the sharpest
  column boundary in a raw frame is between 15 and 16 (13.77 against a mean of
  2.76) while the last column of a row runs into the first of the next at
  1.46, *smoother* than a typical adjacent pair -- which is a picture
  displaced sixteen pixels along the buffer, not one overflowing it. **So the
  right-hand side coming back through the left was never an overflow**, and
  the earlier reading of it as one -- "draws up to 192 pixels wide, so sixteen
  columns land on the next row" -- is retracted, along with the conclusion
  that only a patch to the game's layout constants could fix it. Reading from
  pixel sixteen fixes it (E170), and it holds on the title screen and in a
  race alike.
- **`on_main_thread` was wrong on hardware, and it is what killed the
  SoundServer thread.** It identified the main thread by how far the caller's
  stack was from it, allowing a megabyte, on the strength of a comment saying
  "the kernel gives every thread its own chunk and they are megabytes apart".
  That is **EKA2L1's** behaviour. A real EKA2 process packs its thread stacks
  together, so the SoundServer thread's 64 KB stack is kilobytes from the
  main thread's and the test called it the main thread -- which makes every
  guard built on it a no-op for exactly the thread they exist to stop. It
  wrote the main thread's `RFile` through `log_block` and took a bad handle:
  KERN-EXEC 0, named `SoundServer`, for nine rounds (round 68). It now asks
  euser for `RThread::Id()` instead. **This retracts round 63's finding that
  the guard worked**, which was read from a record that was merely lost in an
  eight-record buffer when the process died.
- **Where the SoundServer thread dies**, as of round 67 and now probably
  explained by the entry above. Round 65 flushed
  the box every 100 ms for the whole two seconds that thread was alive and
  caught nothing from it after `CTrapCleanup::New`. A trace thunk records on
  the way *in*, so the death is inside `CTrapCleanup::New()` or the game's
  `operator new(20)` at `0x652f8` -- the only two calls in the gap, and both
  allocate. The game creates the thread with `aHeap = NULL`, which means
  *share the creating thread's heap*; if that null reaches
  `UserHeap::SetupThreadHeap` with a zero heap size, nothing is set up and the
  first allocation reaches for a heap that is not there. KERN-EXEC 0 is a bad
  **handle**, which is what an `RHeap`'s `RChunk` would be. `stack_thunk` now
  substitutes `&User::Allocator()` for the null. **Round 66 tried it and it
  changed nothing** -- the log came back identical record for record -- so
  the thread is *not* dying for want of an allocator and that explanation is
  retired. The substitution stays because it is the right semantics and costs
  nothing. The gap is five calls, all of them now known: `CTrapCleanup::New`,
  then `TTrap::Trap`, `User::AllocL`, `TTrap::UnTrap` and
  `User::LeaveNoMemory` inside the game's `operator new` at `0x652f8`.
- **The main thread no longer hangs, and the hang was never a crash.** It
  used to go into `RSemaphore::Wait` at `0xb8798` waiting for a signal at
  `0xb86ac` that never came, and `gate6 ViewSrv 11` was the view server timing
  out on an application that had stopped answering (round 63).
  `RSemaphore::CreateLocal` answers 0, so the semaphore itself is sound.
  Build 134 waits in 100 ms slices, so the box keeps being flushed while the
  other thread runs, and gives up after two seconds. Round 65: it gave up,
  closed both handles and **finished frame 1** -- the first time on hardware
  with the sound server in the picture.
- **The thread that dies is `SoundServer`** -- named on the phone's own panic
  dialog in round 64, which is the first time any round has had a name rather
  than a guess. It is the thread the game starts at `0xb8660` to run its sound
  server, and it dies somewhere between `CTrapCleanup::New` and the
  `RSemaphore::Signal` five calls later.
- **An instrument must not touch a file from a worker thread.** Build 133 gave
  the worker its own `RFs` and its own file, and killed it at the first
  `RFs::Connect` -- which a trace thunk reaches *before* the import it traces.
  That build is what named `SoundServer`, and it is off. The rule is now
  simply: on a worker, memory only.
- **The KERN-EXEC 0 the phone has raised beside every KERN-EXEC 3 is ours.**
  `box_flush` returned early on a null handle, `box_write` returned early off
  the main thread, and the `RFile::Flush` between them did neither -- so the
  first worker thread to reach a sixteenth traced event called `RFile::Flush`
  on the main thread's handle, which is a bad handle on EKA2. Round 61 put it
  beyond doubt: with the stack clamp in, the SoundServer thread becomes
  creatable, starts, makes its first imports, and the phone dies there with the
  main thread still running -- so the old reading, that the worker was touching
  something an exiting main thread had taken with it, is **retracted**. One
  line fixes it.
- **HAL is finished as a source for the screen's format.** Three attributes,
  three wrong answers on an N95: `EDisplayBitsPerPixel` 16, 
  `EDisplayOffsetBetweenLines` 640 and `EDisplayMode` `EGray2`, for a 240x320
  screen that is 32 bits a pixel on a 960-byte line. Nothing further should be
  asked of it; the pitch-consistency rule is what gets the right answer.
- **EKA2 caps a user thread's stack and EKA1 did not**, and that is what round
  60 stopped on: the game starts its sound server at `0xb86ec` with
  `RThread::Create(..., aStackSize = 100,000, ...)` -- a literal at `0xba370`
  -- and an N95 answers `KErrTooBig`, which the game turns straight into
  `User::Leave(-40)` and `User::Exit`. `stack_thunk` clamps the request to
  64 KB and halves it on a refusal down to 4 KB, so the thread gets the
  largest stack the device will actually give. It copies the caller's stack
  arguments down itself, which is the thing `frame_thunk` got wrong and which
  kept `WRAP_CREATE` off for twenty rounds.
- **EKA2L1 had no such cap**, which is why 142 emulator runs cleared a build
  that could not run. `thread_create` in `src/emu/kernel/src/svc.cpp` now
  refuses a user stack over the EKA2 default ceiling. With the shim's clamp
  turned off the emulator ends the run exactly where the phone does: 1,196
  records aligned from the first frame with **not one code out of place**, and
  every remaining difference a heap address or a handle (E144).
- **HAL cannot be trusted for the screen's format.** It answers
  `EDisplayBitsPerPixel` 24 in the emulator for a four-byte pixel (E138) and,
  on an N95, 16 bits and a 640-byte line for a 240-pixel screen -- **640 is
  not a multiple of 240 at any pixel size**, so the pair was never
  self-consistent and no phone was needed to see it. Written as 2 bytes on a
  640-byte line the splash came out in 88-pixel bands with seams at columns
  16, 104 and 176, which is what 16-bit writes do in a buffer that is 4 bytes
  a pixel on a 960-byte line. An inconsistent pair is now rejected.
  `EDisplayMode` is queried and logged but not yet acted on; in the emulator
  it answers `EColor16MU`, which is the truth the bits-per-pixel attribute
  does not tell, and the next hardware round says whether it tells it there.
- The game's image has no data and no bss; every global lives behind
  `Dll::Tls()`, and that root is built correctly by our loader.
- The reboots -- a month of them -- were **two `RFile`s closed with
  `RHandleBase::Close`**, which closes the file server session out from under
  everything after it. Fixed with `RFile::Close` (efsrv 300). Nothing about
  write volume, write rate or extending files was ever involved.
- `RFile::Open` of `cwivenc.dat` returns `KErrNone`. The phone does not die
  there.
- **Both machines now end at the game's protection check.** `User::Leave(-2)`,
  KErrGeneral, from `0x2b20`: the game's own code, not the framework's. The
  decision is `subs r4, r0, #0 / beq` on what `0xccbb4` answers, and `0xccbb4`
  is a sixty-nine-state obfuscated dispatcher. The answer is not a boolean --
  on the success path it is XOR'd into the next call and stored -- so the
  branch cannot simply be forced.
- **The path through that dispatcher is seven states**, 47 13 36 29 68 59 7,
  and only state 29 decides anything: it calls **`0xe6df8`** and picks the next
  key from whether the result is zero. It was zero, so the machine went to 68
  (`mov r8, #0`) and then 7 (`mov r0, r8`, epilogue). `0xe6df8` returning zero
  is the whole failure.
- **The protection can be made to pass with its own value.** `0xe6df8` returns
  2 when its first argument is null, and state 29 accepts any non-zero answer.
  The patch is at **`0xe6f34`**: `mov r0, #2` in place of the `beq` that returns
  zero, which leaves the loop and the body running and changes only the answer.
  (Taking the early exit at `0xe6e18` instead also reaches state 34, but skips
  the body, so it is the wrong patch even though it looks equivalent.)
  Emulator: state path 47 13 36 29 **34 4** 59 7 instead of ...29 **68**..., and
  179 -> 195 -> **198** traced events. It is a workaround; what the check hashes
  is still unknown.
- **The crack is a forged card identity and a read-only drive.** `DoControl`
  always answers the same twenty bytes -- CID `56785733 10011234 0b70194e
  16000400`, type 0 -- and `RFile::Open` (write mode), `Create` and `Replace`
  all answer **KErrAccessDenied** for any name on E:. The protection's question
  is "am I on a real N-Gage game card?", asked two ways, and this port answers
  both wrongly by construction.
- **Created, resumed, queued ready, never run.** Every link has evidence now:
  create succeeds (eighteen, no failures), the handle is real, `Resume` is
  called on it, the emulator reports `in state 0` -- `create`, the one case that
  calls `schedule()` -- and the breadcrumbs at both entry points are provably
  planted. Not one instruction of either thread executes. The fault is not in
  the shim.
- **RETRACTED: `RThread::Create` never failed.** Seventeen creations and no
  failures once the wrapper came off. `frame_thunk` pushes seven words before
  calling the target, so a six-argument function reads its three stack
  arguments out of our saved registers -- which is where the garbage owner came
  from. **A thunk that pushes before it calls can only wrap a function whose
  arguments all fit in registers.**
- **Two of the three arguments are junk.** The info block is sound (real image
  pc, sensible stack, plausible allocator) while the name descriptor and the
  owner type are garbage -- `owner` can be 0 or 1 and is 75282192. That is
  arguments landing in the wrong places, which is what answering one overload
  where another was asked would do: old euser 291 is
  `(name, fn, stack, heapMin, heapMax, ptr, owner)` against 289's
  `(name, fn, stack, RAllocator*, ptr, owner)`.
- **`RThread::Create` answers KErrGeneral and leaves the handle zero**, and the
  game does not check: it resumes a null handle, gets KErrNone, and waits
  forever. Not memory (fifteen gigabytes free, every stale emulator killed) and
  not flaky (identical twice, to the record index). The old-to-new mapping is
  *correct* -- old euser 289 and new 1158 are the same signature, and the
  heap-min/max overload the shim could have confused it with is 291/1159.
  `thread_create_eka1` in the emulator returns `error_general` in one place
  only: `create_and_add<kernel::thread>` giving back `INVALID_HANDLE`.
- ~~**The game's two threads are created and never entered.**~~ **SOLVED
  (E96, E97).** The crumbs were planted, the handles were real, the resume was
  real and the scheduler really did pick `SoundServer` -- and the entry was
  never reached, because it was never where the thread started.
  `thread::reset_thread_ctx` in the emulator points a newly created thread at
  **the owning process's entry point**, not at the function `RThread::Create`
  was given; a real EXE is linked against `eexe.lib`, whose `_E32Startup`
  dispatches on `r4` (1 = a thread starting, 0 = the process) and only then
  calls `SStdEpocThreadCreateInfo::iFunction`. Our hand-built `_start` ran
  `gate6_main` unconditionally, so every `RThread::Create` **re-ran the whole
  application in the new thread** -- a second CONE startup, a second copy of
  the image, another worker, and so on; the image base climbed `0x47` -> `0x58`
  -> `0x61` -> `0x6e` -> `0x77` -> `0x82` -> `0x95` across seven SoundServers
  in one run. Six instructions of dispatch in `gate6.s` fixed it: both crumbs
  fired, `G6WRK` 970 and 971, and `launches` went from six to one. Nothing was
  ever wrong with `RThread`'s handle layout, with ordinal 289 -> 1158, or with
  the emulator. **The rule this leaves: a hand-built EXE entry point is shared
  between the process and every thread it creates, and must dispatch on `r4`
  before it does anything else.**
- **A worker thread cannot write to the box log.** An `RFs` session belongs to
  the thread that made it, so every event the workers log through `c->fs` is
  dropped on the floor -- and `LOUD_WRITE_ERRORS` turns the `KErrBadHandle`
  into a panic that kills the worker two events in. The guard is in
  `on_main_thread`, at `log_block` and `box_write` rather than only at
  `log_event`, because twenty places flush a block directly; off the main
  thread an event goes to RDebug instead, as `G6t` and `G6:`. The thread is
  told apart by its stack, which is the only thing here that is per-thread
  without asking euser for it.
- **The EKA1 client-server framework has stand-ins, not an implementation.**
  9.x replaced `CServer`, `CSession` and `RMessage` with `CServer2`,
  `CSession2` and `RMessage2` -- different classes, different virtuals, a
  different message object -- so none of the twelve forwards, and every one was
  a stub that panics. The game's SoundServer thread builds a `CServer`, and
  that panic killed the thread; the emulator let the process carry on, a phone
  would not. The stand-ins let it build its object and sit in its own active
  scheduler. Both ends of this server are inside our process, so a real
  in-process bridge is possible -- `CreateSession` finding the server by name,
  `SendReceive` calling its session straight, no kernel IPC -- and that is the
  right answer for audio when audio matters.
- **`gen_shim.py` is not run by the build.** `build_gate6.py` compiles
  `gate4_shim.cpp` but never regenerates it, so a change to the override tables
  looks exactly like a change that did nothing (E114). Regenerate it by hand:
  `python3 gen_shim.py <6rbc.app> gate4_shim.cpp`.
- **Do not patch the protection: let it pass.** The crack never patches the
  check. It forges the memory card's CID through
  `RBusLogicalChannel::DoControl` -- old euser 353, a fixed twenty bytes,
  `KErrNone` -- and lets the real check run; the twenty bytes are exactly the
  `kCardCidBE` this port already answers with. Patching the check instead
  (`PATCH_THE_CHECK`) handed the game a **zeroed sixty-four byte stand-in
  licence**, and everything downstream of it got zeros. Turning the patch off
  took the run from 248 core events to 273 and about 8,800 further import
  calls, with the whole Codewave file sequence appearing in order for the first
  time. `PATCH_THE_CHECK` is 0 and should stay 0.
- **The crack, fully decoded, is already carried across.** Its
  `RLibrary::Lookup` replacement answers exactly four things and forwards
  everything else: `z:\System\Libs\EUser.dll` ordinal 353 -> the forged CID;
  `z:\System\Libs\EFSrv.dll` ordinals 25, 121 and 151 (`RFile::Create`,
  `Open`, `Replace`) -> routines that refuse a write-mode open on `E:` with
  `KErrAccessDenied` and substitute one filename for another. We do the CID and
  the read-only refusal. The substitution is
  `E:\System\Apps\6rbc\6rbc.APP` -> `E:\System\Apps\6rbc\bin\main.dll`,
  which is a packaging artefact of the cracked dump -- there `6RBC.APP` *is*
  the 3964-byte loader and the real image lives in `bin\` -- and means nothing
  here, where `6rbc.app` is the game. What is **not** carried across is the two
  audio compatibility patches, imports 458 and 459, pointing
  `CMdaAudioOutputStreamPadFunction` at `CMdaAudioOutputStream::NewL`; 458 is
  still an unresolved stub. Nothing has called it yet.
- **Our install is not the problem.** Every file we have in common with the
  known-good cracked dump is byte-identical, `6rbc.dat` (12,224,045 bytes) and
  `6rbc.cwa` (38,413) included; we simply have 86 more files than it does.
- **The game runs.** `RunL` enters and returns 4,706 times in a two-minute run
  and `CFbsScreenDevice::Update` is called 4,716 times; the window shows a
  moving picture at 40-42 FPS. 109,542 core events, against 388 before the last
  two fixes, 273 before that and 180 on build 59. Nothing panics, nothing
  leaves, and the run ends only because the harness timeout kills it.
  Two things unblocked it: answering `RSessionBase::CreateSession` with
  `KErrNone` so the sound server connect succeeds, and supplying `memmove`,
  which the game imports from the C runtime and 9.x does not export.
- **RETRACTED (round 75, E167-E170): the clipped right edge is not the
  game's own, and it is not an overflow.** The section below stands as the
  measurement of the stride -- 176, confirmed twice since -- but its reading
  of the artefact was wrong. The game's picture starts **sixteen pixels into
  the buffer**; nothing is written past 176, and the right-hand side that
  appears at the left is the picture's own right edge, displaced. Reading
  from pixel sixteen fixes it. What follows is kept because the reasoning
  that led away from it is worth not repeating: three port-side controls
  were tried and none moved the artefact, and that was read as proof it was
  unreachable, when the untried fourth was where in the buffer to start.

- **The clipped right edge is the game's own, not the port's.** Measured, not
  guessed: the framebuffer is 176x208 with a **176-pixel stride** (the title
  screen renders pixel-perfect from a raw dump at 176 and shears at 192 and
  208), and the game writes **exactly 176x208 and nothing beyond** -- the rest
  of the buffer stays untouched zeros. What it does do is draw up to **192
  pixels wide**, so as many as sixteen columns of a row land on the **next
  row's left edge**: the `0s` at the far left is the tail of `11.90s` from the
  line above. The overflow is concentrated in rows 5 to 31, the car-name banner
  at the top; the stats lines overflow by about one column.
  Three things were tried and none of them moves it, each left in the source
  with its result beside it: reporting `iScreenSize` as 176x208 instead of the
  device's 240x320 (`TELL_GAME_ITS_SIZE`) gives **byte-identical** frames;
  sizing the lent window to 176x208 (`SIZE_THE_WINDOW`) changes nothing; and
  reporting a 192-pixel pitch makes the picture shear, because the game keeps
  writing rows 176 apart. Its stride is hardcoded.
  **Settled (E137): it is the game, and the port cannot change it.** The game
  asks for nothing that could move its layout. Its only geometry query is
  `UserSvr::ScreenInfo`, whose size it ignores, and it imports **no** text
  rendering whatsoever -- no `DrawText`, no `CFont`, no `TextWidth`, no
  `HAL::Get`, no `CCoeControl::Size`. Every glyph is its own bitmap font drawn
  straight into the framebuffer. Layout width and row stride are both hardcoded
  in the binary, so the same binary draws the same picture on any framebuffer,
  N-Gage included. (EKA2L1 does have `NEM-4` and `RH-29` profiles with ROMs and
  the game launches on them, but it dies in the Codewave check at
  `ldr r2, [r1, #0x240]` -- with or without the BiNPDA loader -- so no picture
  can be got that way either.)
  Changing it would mean patching the game's own layout constants, the way the
  protection check is patched. `DUMP_FRAME` drops a raw frame into
  `C:\g6code.bin` for that work if it is ever wanted.
- **Input works.** The game takes keys by overriding **old control slot 1**,
  `CCoeControl::OfferKeyEventL` -- the image names that slot itself, since every
  other control vtable in it carries a base-class veneer there and in the
  `CAknNoteDialog` table that veneer is avkon 1166. Its own control vtable
  overrides 0, 1, 19 and 24: destructor, OfferKeyEventL, FocusChanged, Draw.
  On the 9.x side the wrapper control is already on the framework's control
  stack (`AddToStackL` is diverted to put it there), so the bridge is one slot:
  resolve cone ordinal 26, scan the copied vtable for that address -- rather
  than hardcode an index that would move on another feature pack -- and put a
  thunk there that hands the `TKeyEvent` and `TEventCode` to the game and passes
  its `TKeyResponse` back. `TKeyEvent` is unchanged between EKA1 and 9.x, so it
  goes over as the pointer it is. Driving it with `xdotool` walks the game from
  the attract race into the vehicle selection menu.
- **The picture works.** Three things, all in `gate6.cpp`. The game finds its
  framebuffer through `UserSvr::ScreenInfo` and nothing else -- `CFbsBitmap` and
  `HAL::Get` are never called -- so the wrapper on that import hands it a linear
  buffer of its own and keeps the real address. It writes a linear **176x208 at
  `EColor4K`** (`0000RRRRGGGGBBBB`) whatever size it is told, and the emulator's
  screen is 240x320 `color16ma`, 32bpp on a 960-byte line: 73,216 bytes of the
  game's output covers 76 of those lines, which was the band of streaks.
  Converting 4K to 32bpp at every `CFbsScreenDevice::Update`, centred, gives the
  real picture at 40 FPS. Reading the source as RGB565 instead gives a heavy
  green cast, so the mode matters.
  **Leave the size the game is told alone.** Reporting 176x208 as well as the
  substituted address killed the run at the first `Update` on a null vptr
  (E127); it draws 176x208 regardless, but it reads that size for something
  else. The drawn area is a band
  of about 176x130 across the top, in horizontal magenta/green/grey streaks,
  with the rest white -- a pixel format or stride mismatch between the
  framebuffer the game writes and the screen device that reads it. Screen
  geometry is the other half: the N-Gage is 176x208, the RM-409 is 240x320.
- **The protection is passed and the game reads its assets.** The run now gets
  a 4-byte size header (`0x440` = 1088) and 411 bytes of zlib out of
  `6rbc.dat` at offset 3,406,180, and that blob inflates to exactly 1088
  bytes -- the archive lookup, the seek and the reads are all correct.
  `cwivenc.dat` is the white-box AES blob the protection decrypts with
  (`0x44332211`, version 2, `WBAESDecrypt`) and loads whole without error.
  The remaining `User::Leave(KErrNotFound)` is raised after that, from
  `0xba500` -> `0xb86ec`, which is in the same module as the SoundServer
  entry at `0xb8660`.
- ~~**Where the run stops now: the Codewave archive.**~~ **Superseded: that was
  the licence stand-in (E116), and the archive reads correctly now.** With nothing panicking and
  nothing faulting, the game opens `e:\system\apps\6rbc\6rbc.cwa`, reads two
  2 KB blocks (both `KErrNone`), searches, frees everything and calls
  `User::Leave(-1)` from `0xba354` -- an inlined `LeaveIfError` on what
  `0xba500` answered, which in turn calls `0xb86ec`, inside the same module as
  the SoundServer entry at `0xb8660`. `RSessionBase::CreateSession` is never
  called, so this is not the sound session; it is the archive lookup. The
  `.cwa` is 38 KB of noise with no readable names, and it is **byte-identical
  in the cracked dump** -- as are `nc.dat` and `game.lic`. The crack does not
  touch the data at all; it replaces the app with a loader that rewrites seven
  imports, and the one that matters is **import 326, `RLibrary::Lookup`**,
  which this port already owns. The protection resolves its own functions
  dynamically through that import and the crack answers those lookups itself.
  So the next gate is decoding the crack's routine at `crack+0x380` and
  answering the same lookups from `gate6_library_lookup`.
- **The game checksums its own code from the worker thread, so a planted probe
  changes the answer.** This is now the first thing to suspect whenever an
  instrument and a conclusion disagree. The small worker at `0xcb710` walks
  image addresses -- `0x47ca714`, `0x47ca71c`, `0x47cc864` -- loading
  *instruction words* and running them through the shift-add chains, and the
  value it derives becomes a destination pointer. With probes planted it read
  `0xea030f81`, the branch to our own trampoline, where `add r0, r5, #0x28`
  belongs, and the pointer landed in the middle of the image; the game then
  wrote its data through it, over its own code, and the main thread died calling
  into the wreckage. **With `PLANT_PROBES` and `PLANT_WORKERS` off the fault is
  gone entirely** (E113): no rewrite, no `KERN-EXEC 3`, and SoundServer created
  for the first time. Fifteen runs, E98 to E112, were spent on a fault the
  instrument was causing. Note the limit of the claim: the protection patch at
  `0xe6f30` still modifies code and the run is fine, so it is not that *any*
  change is fatal -- only that the checksummed region includes what we were
  planting in, and which sites those are is not yet known.
- ~~**The worker corrupts one word of the main thread's state, and that is the
  whole of the remaining fault.**~~ **Our own doing (E113); the analysis below
  is still correct as mechanism.** With the worker created and resumed but its
  body not run (`RUN_WORKERS 0` in `gate6.s`, which is the control), the main
  thread reaches 337 core events; with it running, 190 and a `KERN-EXEC 3`. The
  two records are **identical for 806 events** and then differ by exactly one
  word: the fourth word of the object probe 990 latches reads `0x4900000` -- a
  local code chunk the game makes -- in the control, and `0x483c1fc` when the
  worker runs, which is image offset `0x13c1fc`, `mov r3, #1` in the middle of
  a function. It is reached through the vtable slot-3 dispatch at `0xe81d0`,
  which is how the run comes to be executing at `0x13c478`. What writes it is
  the open question.
- **Eleven stale emulators were the `G6MEM`.** `emurun.sh` used `pkill -x`
  without a follow-up `-9`; the emulator does not always go on SIGTERM. Cleared,
  a clean run has no panics at all. The script now force-kills after a second.
- **`frames 1` is one `RunL` that never returns**, not a timer fault. The game
  enters its frame function at record 65 and the remaining 5273 records all
  happen inside it. At the end it resolves `RThread::Create` and
  `RThread::Resume`, and the emulator logs `Thread SoundServer created with
  start pc = 0x90b8660, stack size = 0x186a0`. The main thread goes quiet
  immediately after -- which is what a thread blocked in the kernel looks like
  to a log made of call records.
- **The run ends because the startup finishes.** `TMO=240` gives the same 5337
  records as `TMO=120`, so it is not a cut-off, and of eight launches only one
  panicked -- a `G6MEM` from our own loader failing to allocate the image on a
  relaunch. Seven ran to quiet. The port loads the image, passes the protection,
  loads resources, draws one frame and waits. **`frames 1` is the next thread**:
  no input is bridged and the frame timer may not be re-armed.
- **The protection completes.** The check returns an *object*, not a status:
  state 38 does `ldr r9, [sp,#28]` then `ldr r4, [r9,#20]`, so the forced 2 was
  being dereferenced. Handing it sixty-four zeroed bytes instead (`ldr r2,
  [pc,#4]` / `b 0xe6f58` at `0xe6f30`, literal written by the loader) takes the
  state machine through its full fourteen-state path -- `47 13 36 29 34 38 18 61
  32 40 23 24 59 7` -- **five times**, once per registration. 5337 records,
  **336 traced events** against a previous best of 200, eleven opens with ten
  succeeding, no leave and no exit.
- **A read-only game card gets the port past state 34, and the leave is gone.**
  Two bugs had been hiding it: filenames are type 4 descriptors whose buffer
  begins with its own header, so `name_on_the_card` was reading the length where
  it wanted the drive letter and the rule never ran; and `open_thunk` holds its
  target as a literal, so the dynamic `RFile::Open` bypassed the wrapper
  entirely. With both fixed the refusal fires, the run makes seven opens instead
  of six, the state path goes 47 13 36 29 34 -> **38**, and **no `User::Leave`
  or `User::Exit` appears in the log at all**. 2492 records, one launch, no
  relaunch.
- **"Zero refusals" was a rule that was not running.** E54 to E58 concluded the
  card was answered and insufficient; they were measuring a broken predicate.
- **The card call does happen.** `DoControl` runs twice a launch, op 4 with a
  null buffer then op 6 with a pointer, so the forged CID reaches the game. No
  write to E: is ever attempted before the check, so the read-only rule is not
  even exercised. Both of the crack's answers are supplied and the check still
  fails -- because the crack only had to fix the card, while everything else on
  its machine was real and everything else on ours is a shim.
- **Supplying both is not yet enough.** E54 and E55 deliver the CID and the
  refusals and the check still goes to state 68. Either `gate6_mmc_control` is
  never called -- the lookup is logged, the call is not -- or the digest wants
  more than the card.
- **The crack's key is `0x85bbf5f4`** -- crc32 of `bin\arenaframework.dll`
  (8508 bytes) plus crc32 of its path, of `BiNPDA presents...` and of
  `gt2 loader. (c) 2005 zg.`, summed. XOR it through the table at crack `0xb44`
  and the seven targets are **import-table slots**: 98, 101 (`RFile::Create`),
  109 (`RFile::Open`), 111 (`RFile::Replace`), **326 (`RLibrary::Lookup`)**, and
  458/459, which are an audio-import compatibility fix rather than protection.
  The crack answers the protection's dynamic lookups itself -- through the one
  import `gate6` already owns.
- **The crack is a loader, not a patched binary.** The second dump's
  `bin/main.dll` is byte-identical to our image; its 3964-byte `6RBC.APP` is a
  BiNPDA loader that opens `RDebug` on its own thread and uses
  `RDebug::WriteMemory` to write **seven four-byte patches** into the loaded
  image, each a pointer to one of its own routines. Same technique as `gate6`.
  The patch table is at crack offset `0xb44`; the offsets are XOR'd with a key
  computed at run time from CRC32s, and the key is not recovered yet.
- **There is one dump on this machine and it is not cracked.** All three copies
  of `6rbc.app` are byte-identical, as are every data file beside them; the `e`
  drive is the same dump laid out as an N-Gage card. The protection runs in it.
- **`nc.dat` is the card's CID** -- `bd81cbfb eb08cd1e 6d341c6e e83e5d16`, four
  words, the game's record of the card it was sold on. `gate6_mmc_control` now
  answers it instead of zeros (`ANSWER_THE_CARD`), which is truthful and changes
  nothing: with the check patch off, neither word order gets state 29 past 68.
- **The gates are one gate seen from several places.** Gate one was a decision
  with a non-zero answer the game itself produces, and forcing it cost nothing.
  Gate two (`mov r4, #1` at `0x13f6c8`) is not a decision: it reports whether an
  open succeeded, and forcing the report does not open the file. Past it the run
  reads 2048 bytes on a handle that was never opened, gets **-8 KErrBadHandle**
  and faults. `PATCH_GATE_TWO` is in the source at zero. Forcing reports cannot
  manufacture the entry the parse failed to produce.
- **The wall is Codewave content protection, and it is not a bug.** `cwp.dat`
  begins `CWZ`, `game.lic` reads `Asphalt2 10185-2.0.194-prd-4205THA`, `nc.dat`
  is sixteen bytes of key material, and the two 104-byte records at `0x17e298`
  are its contexts. The map at `obj+40` has no entry for key 1, so the list at
  `0xcc914` stays as it was made -- empty -- and the startup unwinds from there.
  Past this point the choices are patching each gate with the game's own values
  or reverse-engineering the licence format; neither is more emulator work.
- **The empty container is `0xcc7e4`'s return, and predates the protection.**
  `state 34 [sp,#52] <- state 13 <- [sp,#140] <- state 47 <- [sp,#152] <-
  0xccbb4's arg3 <- 0x2998's r3 <- r5 at 0x2e3c <- bl 0xcc7e4`. And `0xcc7e4`
  is the function probes 990 and 991 sit inside, whose `this->[4]` the store at
  `0x1082c0` was poisoning, and which calls `0xe9988`. The poisoned object, the
  un-probeable function and the empty list are one thread, not three. `0x2998`
  is then called five times against that one container.
- **State 34 opens a file whose name is an empty list.** The sixth
  `RFile::Open` of the run gets a four-character name of control bytes and
  answers KErrNotFound. Its source is `[[sp,#52]]`, a pointer whose first word
  is its own address -- this game's empty-list idiom, the same one the cell at
  `0xcc914` uses -- walked by `User::StringLength`. It is empty whether or not
  the check is forced, so **something that should have filled it never did**.
- **The next wall is `0x13f4c4`, and it is ours, not the protection's.** State
  34 calls `0x10a93c`, a factory that news 36 bytes and calls `0x13f4c4` on it;
  that answers false, so the factory deletes and returns zero, and state 4 --
  the same block as 68 -- zeroes the answer again. `0x13f4c8` is the
  nine-character library-name builder that the log has always shown as
  `RLibrary::Load from 0x13f588` / `Lookup from 0x13f5e4` / `Close from
  0x13f610`. A library load that resolves the right ordinals and still reports
  false is something the shim can be wrong about.
- **`0xe6df8` loops once and returns its body's answer.** The list at
  `0x0334ca90` holds 1, the counter goes 0 then 1, and `arg3` is a code address
  so the null-argument exit is not taken either. The zero comes from the single
  call to **`0xe50d8`**, which initialises two identical 104-byte records --
  a tag word twice, twenty bytes, then zeros -- each beside a zeroed 64-byte
  block. Twenty bytes of state and a 64-byte block is a hash, and the game
  imports no crypto library, so it is in the image.
- **The dispatcher's keys decode statically**: `r3 = r0 + 0x1c936f1c`, truncated
  to 32 bits. Every `ldr r0, [pc]` in the state machine names its successor, so
  the graph can be read without running it.
- **A station that changes nothing may simply not have been planted.**
  `crumb_safe` refuses conditional, pc-reading and pc-writing instructions and
  says nothing about it, and a refused plant produces a run bit-identical to the
  one before -- which is also what a station that proves an absence looks like.
- **Furthest reached: 176 traced events on hardware** (build 59), where the
  phone and the emulator agree on 178 of 180 core events and the phone faults
  at the point the emulator calls `User::Leave`. No known divergence between
  the two machines remains. (Earlier marks, kept for the shape of the climb:
  128 from build 44 to 50, 144 at build 51, 136 in builds 44 and 45 where it
  died inside a `User::Free`.)
- **The loader must give back what it takes.** It held `6rbc.app` open
  `EFileShareReadersOnly` for the life of the process, and the game opens that
  same file itself, exclusively, to check its own image. On hardware that open
  answered KErrInUse and the check was skipped; EKA2L1 does not enforce share
  modes, so it never showed. Closing the handle is what took the phone from
  1571 records to 2104.
- The heap is sound, the pointers are sound, **and so is the fatal free**: at
  event 128 `User::CountAllocCells` walks the heap clean at 1209 cells, every
  free matches a live cell, and the fatal one matched a live 27-byte cell with
  its verdict written to disk before the run ended. The fault is inside
  `User::Free`, on a cell that everything we can measure says is fine --
  including its header, which build 47 read: 27 bytes requested, header
  `0x28`, exactly the emulator's shape. Heap chain, pointer, size and header
  all correct, and so is the neighbour build 48 read. **Build 49 then settled
  it: with all three deallocation ordinals answered by a no-op, the run stops
  in exactly the same place.** Nothing was freed -- every freed pointer in the
  run is unique where build 48 reused them -- and the traced event count, the
  last import, the stack high-water, the import count, the free count and the
  verdict count are all identical. `User::Free` is innocent, the cell was never
  damaged, and rounds 44 to 48 are retired. The fault is in the game's own code
  after the 99th `delete` returns, which nothing has looked at because the free
  was standing in front of it.
- **The log goes quiet after that delete whether or not the run survives it.**
  Nothing in the next hundred instructions calls a traced import, and the one
  allocation among them is logged only if it fails. Probes in the emulator show
  the run getting past the delete every time. "It dies in the free" and "it
  stops at the 99th free" were both the end of the recording, not the end of
  the run.
- **The fatal delete is at image offset 0xcc8c0**, named by the `lr` its
  wrapper now records, and round 50's probes show the phone getting past it
  every time.
- **The actual fault is `0x139588`: `ldr r2, [r1, #0x240]`, with `r1` taken
  from `[r6 + 4]`, a field nobody ever wrote.** On the phone it holds
  `0xa6dfb180`, in the emulator `0xeaf88340` -- different nonsense in each,
  which is what an unwritten field looks like. `r1 + 0x240` is the faulting
  address in both.
- **The field is poisoned by one call: `bl 0xe9988` at `0xcc890`,** which is
  handed `&this->[4]` as its fourth argument. Probes on either side show a good
  heap pointer going in and garbage coming out; it returns 1. Everything before
  that call leaves the field intact, the temporary the "fatal" delete frees is
  created and destroyed inside the same sequence, and the object's other seven
  words are unremarkable.
- **`0xe9988` cannot be instrumented from the inside.** One probe at its third
  instruction takes the run from 1240 records to 553, before any marker outside
  it is reached. Flags are preserved by `probe_plant`, so this is the game, not
  the thunk. Every instrument from here watches state through wrappers we
  already own rather than patching a game byte.
- **The poisoning store is `str sl, [ip]` at `0x1082c0`,** through an
  out-parameter held at `[sp, #0xb8]` -- which is what `&this->[4]` became when
  `0xe9988` passed it down. One word, which matches the measurement that only
  word 1 of the object changes. *Named by a run that perturbed; awaiting a
  clean confirmation.*
- **The value is a pointer calculation, not a poison.** `mul` and `mla` over
  runtime values -- `sl = sb*r1 + r8`, then `sl*(r4+1)` scaled -- is
  `base + index * size`, not the image's multiply-by-constant obfuscation. So
  `this->[4]` is meant to hold a computed pointer and one of the inputs is
  wrong. That is also why the value is deterministic per machine and different
  between machines. Confirmed on its own terms in E21: a probe filtered to fire
  only when the store targets the watched field fired exactly once.
- **The computation is fully unwound and every step checks against a register.**
  `array[5]` is a decoy -- it is multiplied by a zero `sb` -- and the stored
  value is `755139455 * r8`, which reproduces the observed `0x1c975dc0` from the
  observed `r8 = 0x42d08240`. **`r8` arrives already wrong**, so the bug is one
  level further up the same function.
- **The value the store clobbers is the correct one.** `[field + 0x240]` --
  the exact word `0x139588` dies reading -- is a good heap pointer at every one
  of the 83 stations before the store. The field held a valid object and the
  call overwrote it.
- **NOPping that one store takes the wall down.** 292 imports against 248, no
  access violation at all, on through two `RLibrary::Load`s never reached
  before, and then an orderly `User::Leave`. A workaround, not an explanation,
  and named as one -- but the first movement here since build 44.
- **The poison lands between a successful allocation and the `delete` called
  from image offset `0x104828`,** inside one of `0xe9988`'s callees. Watched
  from the wrappers, `this->[4]` is intact at every station before that and
  wrong at every station after.
- **`0x139588` is the instruction EKA2L1 cannot run the *original* N-Gage
  binary past either.** That was recorded here long ago and filed as an
  emulator deficiency. It is not one. The original game under the emulator, our
  port under the emulator and our port on an N95 all stop at the same
  instruction for the same reason, so whatever fills `[r6 + 4]` on a real
  N-Gage is happening on none of the three. That is the port's real problem.
- **Build 48's `0x28` header was reuse, not damage.** With the leak on, the
  same free reads `0x20` -- exactly `align8(27 + 4)`. RHeap had handed over a
  whole recycled cell rather than split off a remainder too small to be one.
- **The run is deterministic.** Three runs of build 48 produced byte-identical
  7624-byte logs and identical boxes. A single run is now worth something,
  where under rule 1 it was not.
- **The ring's request column cannot be used to call a header wrong.** RHeap
  hands over a whole free cell rather than split off a remainder too small to
  be a cell, and any deallocation not going through ordinals 315, 408 or 410
  leaves a stale live entry for an address that is later reused. Six of 32
  frees have a header larger than `align8(request + 4)` for those reasons.
- **The emulator called this one right.** It predicted build 49 would not get
  past the wall, and the phone agreed. That is one data point, not a licence to
  trust it -- for the protection paths it is still wrong -- but for questions
  about the shape of the import sequence it has now been right twice.
- **Log volume is not what moves the emulator.** Three extra records per free
  with no dereference leave it ending exactly where it did (1066 -> 1258
  records, the same `0x30002`). When a reading changes, the write is not the
  suspect; what the instrument *reads* is.
- The setup state on the phone matches the emulator exactly: 20612 bytes of
  spare arena, a 3768-byte context, all four optional wraps installed. Nothing
  is being silently skipped there.
- The phone and the emulator run the same import sequence, tracking event for
  event over 55 consecutive events at an offset of 27.
- The game's own protection paths -- `6RBC.off`, `cwdynlog.dll`, GD1DRV, and a
  `TickCount` stopwatch with `Math::Random` mixed into its result -- behave as
  designed. The empty container the emulator dies walking is the *correct*
  state two seconds into a run.
- EKA2L1 cannot run the original N-Gage binary either (KERN-EXEC 3 at
  0x139588). Our port gets *further* under the emulator than the original does.

- **This image has no writable statics.** `flat.ld` folds nothing into
  `.rodata` any more, but `mke32.py` declares data size and bss zero, so a
  non-`const` static lands in the read-only code segment and the first write
  to it faults. Build 52 patched one character of a file name and the phone
  took KERN-EXEC 3 before creating a single file, while the emulator -- which
  maps that memory writable -- ran twelve launches without complaint.
  `buildapp.py` now refuses to build an image that wants one.

- **KERN-EXEC 0 is almost certainly ours.** `box_write` had no handle guard
  where `log_block` has always had one, so a failed `file_replace` of the box
  leaves handle 0 and the next box write is `RFile::Write` on it -- a bad
  handle, which is what KERN-EXEC 0 means. It lands exactly where round 54's
  second launch stops, two records in. Build 55 guards it.

### Two panics, not one

**Every run on the phone raises one KERN-EXEC 0 and one KERN-EXEC 3**, and has
done for many rounds. It was reported up to about round 40, then stopped being
mentioned, and I stopped asking -- so every theory in this file since has been
built around a single failure while the phone was showing two.

KERN-EXEC 3 is an access violation. **KERN-EXEC 0 is a bad handle** -- a
different fault, with a different cause. Both are kernel-side, so neither is
one of our own `G6xxx` panics.

Two panics most likely means two *processes*: the app, and something starting
it again afterwards. The emulator shows exactly that shape -- `User::Leave`,
`User::Exit`, then our loader panicking `G6MEM` while failing to allocate the
image on a relaunch. **Which run the log we read belongs to is not
established.** The log is written from position 0 every time, so a relaunch
overwrites the first run's record with its own, and every log in this file may
be the second run rather than the first. Nothing more should be built on the
logs until that is settled.

### Retracted, and why

| Believed | Why it was wrong |
|---|---|
| A `static` is safe to write to in our loader | It is not. This image has no data section: `flat.ld` folded `.data*` into `.rodata` and `mke32.py` declares data and bss zero, so every static is in read-only memory. Build 52 patched one character of one and the phone took KERN-EXEC 3 before creating a file. The emulator maps it writable and saw nothing. |
| `RThread::Create` answers KErrGeneral (E84-E86b) | **My own instrument.** `frame_thunk` pushes seven words before calling the target, so a six-argument function reads its stack arguments twenty-eight bytes too low. With the wrapper off: seventeen creations, no failures. Three rounds spent on a fault that was not there. |
| Three euser lookups are answered out of the efsrv table (round 57) | **My own error, retracted in round 58.** They were efsrv asks all along: old efsrv 121/136/185 are `RFile::Open`, `RFile::Read` and `RFile::Size`, and they map to new efsrv 93/255/264, which are `RFile::Open`, `RFile::Read` and `RFile::Size`. I named the old ordinals out of the **euser** def file and the new ones out of euser's too, and got three unrelated names. Every mapping in the log is correct. |
| The 35-event gap is a wrong-table bug in `gate6_library_lookup` | It is not a bug in the shim at all. The gap is 26 extra `RFile::Read` calls the emulator makes and the phone does not, from one call site, on one file. |
| The gap is a short read on the phone | Nothing the phone reads is short -- every read fills its buffer exactly and answers KErrNone. The 26 reads are a *different file*, `6rbc.app`, that the phone never opens successfully: the loader is still holding it open readers-only, and the game's own open is exclusive. |
| The 35-event gap is an N95-versus-5320 ordinal difference | The phone's ordinals and mappings are identical to the emulator's. Both machines are handed the wrong functions; only the consequences differ. |
| The app runs once per launch | It runs over and over. Twelve launches in one 45-second emulator session, each identical, each leaving and being restarted. `file_replace` truncates, so each was erasing the last one's log. |
| The port has one failure per run | It has two. One KERN-EXEC 0 and one KERN-EXEC 3, every run, for dozens of rounds -- and the logs may be recording whichever process ran second. |
| The file server was overflowing a 31-byte cell | The allocation ring was searched in slot order and a stale entry won. There was no overflow. |
| The reboot was a write ceiling, then a write rate, then extending writes | Three theories fitted to the shadow of the closed-handle bug. A build writing the log heavily does not reboot once the handle is fixed. |
| Builds 38, 39 and 40 regressed | Each stopped early on a single run. Build 40 later reached 128. Two working instruments were reverted over it. |
| A run is a coin flip | Build 42 stopped at the same point five times out of five. The variance was real earlier and is not the explanation now. |
| 512 bytes meant a disk sector | `LOG_BLOCK` is 8. Sixty-four records is eight of our own blocks. |
| The read came from caller 0xe4774 | It came from 0xed694. The import trace records the call from inside a shared helper. |
| The emulator is a reference | For the protection paths it never was; it cannot run the original game at all. |
| EKA2L1's KERN-EXEC 3 at 0x139588 on the original binary is an emulator bug | It is the same fault our port hits, on real hardware. `[r6+4]` is unwritten in all three cases. What looked like the emulator's limitation was the game's own missing state. |
| The port dies in `User::Free` / at the 99th free | It dies 60-odd instructions later, at `0x139588`. The free was the last thing *logged*, and nothing between it and the fault calls a traced import. |
| Builds 38-43 failed at 64 records | They did not fail there at all. 64 records is eight log blocks; build 44, with the same code, reached 136 events. Four builds were judged on a hidden tail. |
| The allocation ring bounds the heap | It does not. `gate6_result` records every non-zero result, and `RFile::Open` is wrapped too, so a failed open puts `KErrNotFound` in it. A `heapTop` built from it was the whole address space, and the read it guarded walked off the end. |

### Standing rules, each bought with a wasted round

1. **A hardware round is three runs**, compared on the longest. One run is not
   a build's behaviour.
2. **One variable per build.** Build 38 changed four things and cost three
   rounds to unpick.
3. **When every measurement of a suspect comes back clean, remove the suspect
   rather than measure it harder.** Rounds 44 to 48 each measured one more
   property of the fatal cell and each came back clean; round 49 turned the
   free off and answered all five at once. The switch had been in the source
   the whole time.
4. **Suspect the instrument first.** Five of the failures in this file were the
   tool, not the game -- and three of those were read as the game dying when
   the recorder had gone deaf.
5. **A measurement that agrees with the theory gets checked as hard as one that
   does not.**
6. **Never ship from a directory another script built into.** `ref.sh` once
   left a different configuration in `out/` and it went to the phone.
7. **Ordinals come from the device, not from a def file.** The phone is an N95
   (9.2); the emulator ROM is a 5320 (9.3).
8. **The three confirmations go in the reply, every time -- generated, not
   remembered.** They were asked for explicitly, given for rounds 47 to 50,
   and then dropped at round 51 without my noticing: the same shape as every
   other lapse here, a discipline held while it is new and let go once it is
   routine. `toolchain/port/rules.py` prints all three from `ROUNDS.md`, so
   the numbers in them are whatever the file says rather than what I remember;
   `emurun.sh` prints it after every run and `checkrec.py` before every commit.
   Running it also catches a stale "Where we are" -- the first time it ran it
   reported round 50 as the best, two rounds after that stopped being true.
9. **Every run goes in `ROUNDS.md`, on either machine, before the next one is
   started.** This said "hardware round" for fifteen emulator runs, which is
   exactly how long they went unrecorded -- written up in prose here and
   nowhere a repeat could be caught. `toolchain/port/emurun.sh` now appends the
   row itself, with the record count and the fault, and leaves `TODO` in the
   last column; `checkrec.py` refuses while any `TODO` remains. The old
   scratchpad runners forward to it so there is no way to run one unlogged.
10. **A hardware round's row says what it settled, or says that it settled
   nothing.** One row: the single change, how many runs, the result, and what
   it bought. Five of builds 38 to 43 have an empty last column, and saying so
   out loud is the only thing that stopped the sixth.
11. **This section is updated in the same commit as the section that changes
   it.** It went stale one round after it was written, which is how the log got
   into the state that made it necessary. `toolchain/port/checkrec.py` fails
   when a new section is appended without it; run it before committing.
12. **A number the harness produced is not a result.** `emurun.sh` killed the
   emulator at forty-five seconds while the run needed longer, Xvfb died and a
   run was made against no display, and `g6box.dat` reports whichever launch
   wrote last. Three runs were nearly written off as a regression on those.
   Before believing a build got worse, check the display was up, the timeout
   was not hit and the box belongs to the launch being read.
13. **Anything the emulator permits is untested.** EKA2L1 is lax where a phone
   is strict -- share modes, handle validity, read-only memory, integrity
   fields -- so a green emulator run says only that nothing *else* is wrong.
   Every resource the loader takes and does not give back is a candidate, and
   the loader holding `6rbc.app` open cost three months before round 59.

## The two machines, which are not the same machine

**The phone** is a Nokia N95, RM-160, firmware v35.2.001, S60 3rd Edition
**FP1** (Symbian 9.2), 128 MB RAM, hacked — so signing and capabilities are
not a concern.

**The emulator** is EKA2L1 running a Nokia 5320 (RM-409) ROM, which is FP2
(Symbian 9.3). It is not the phone's firmware: EKA2L1 has trouble with any
S60v3 ROM other than the 5320, which is why that one is in use.

Both of the measurements taken from the 5320 ROM that were ever in doubt have
since been checked against the phone's own `BitGdi.dll` and `Ws32.dll`, pulled
off `Z:\sys\bin` with X-plore, and both hold. Ordinals and published vtables
are stable across feature packs; do not spend another round doubting this.

The game's own files must sit together in one directory, the way the card has
them — `E:\System\Apps\6rbc\` with `6rbc.app`, `6rbc.cwa`, `6RBC.dat`,
`cis.dat`, `cwivenc.dat`, `cwp.dat`, `nc.dat`, `nokia_en.rle` and the
`framework`, `plugins`, `streams`, `videos` subdirectories. The game finds
them beside wherever `CApaApplication::DllName()` says it was loaded from, and
a missing one is not something it survives. Nothing under `System\Libs` is
needed: those are EKA1 binaries the phone cannot load and the shim answers
every import into them itself.

## How a round works

Build with `python3 build_gate6.py <outdir>`; install the SIS; **run once**.
The run appends its whole history to `C:\g6box.log`, eight bytes an event, a
block of sixty-four at a time. Read it with `readlog.py`, which names imports
and markers, counts them, and -- the reason it exists -- diffs two logs and
prints where they part company. `emulator-reference.log` beside this file is
the emulator running the same build, for exactly that.

The older two-launch route still works: the second launch reads `C:\g6box.dat`,
panics with `G6BOX <number>` and writes `C:\g6box.txt`. That summary is now a
convenience, written once per block rather than once per event, and the log is
the thing worth reading.

`g6box.txt`:

```
steps   how many imports and markers went past
last    the last one
flags   16 a slot of ours was entered · 32 the frame loop ran · 64 it left · 128 it exited
path    0 = E:\System\Apps\6rbc\ · 1 = E:\ · 2 = C:\
stack   the deepest the stack has been, in bytes
slot    the last vtable slot of ours the framework entered (0x504 = our timer's RunL)
hits    how many times each marker fired, 900 upward
tail    the last sixteen, oldest first
from    where each was called from, as an offset into the loaded image
```

Numbers under 464 in `tail` are import indices — `gen_shim.build()` names
them. 900 and up are breadcrumbs: markers planted in the game's own code,
listed in `kCrumb` in `gate6.cpp`. Current numbering:

```
900 c9d84  901 c9dc0  902 ca470  903 c9e2c  904 c9e38  905 c9e44  906 c9e6c
907 c9fb0  908 ca014  909 ca168  910 ca1d0  911 ca238  912 ca29c  913 ca2c4
914 ca2e0  915 ca2fc  916 ca390  917 ca038  918 ca060  919 ca088  920 ca0b0
921 ca0d8  922 ca100  923 ca128  924 ca150  925 ca1b4  926 c9d20  927 ca170
928 ca178  929 ca180  930 ca1a4  931 ca1ac  932 ca1b0
```

Renumbering happens whenever `kCrumb` changes, so regenerate this list rather
than trusting it. `TRACE_IMPORTS` in `gate6.cpp` turns on an RDebug line per
import, which the emulator's log shows in order and a phone would spend real
time on; leave it at 0 for anything going to hardware.

## What the shim does

The framework owns a 9.x object, the game owns the old one, and only the slots
the game overrides are bridged. Beyond that:

- **`CCoeEnv`** — the game reads the environment's fields at 7.0s offsets and
  also calls cone on it. It gets a *copy* with the old layout, refreshed on
  each `CCoeEnv::Static()`, with the app UI word pointing at the game's own
  object because the game reads its own members off it. Calls go back to the
  real environment.
- **`CDirectScreenAccess`** — `Gc()`, `ScreenDevice()` and `DrawingRegion()`
  are inline in ws32.h, and 9.x moved all three one word later (0x18/0x1c/0x20
  becoming 0x1c/0x20/0x24) because EKA2's `CActive` grew. Shifting the pointer
  is wrong: `iStatus` at 4 and `iActive` at 8 did *not* move, and the game
  reads `iActive` every frame. It gets a shadow in the old layout, refreshed
  from `RunL`; `StartL` and `Cancel` are turned back.
- **`CFbsBitGc`** — the game blits by vtable slot. Old slot 46 is
  `BitBlt(const TPoint&, const CFbsBitmap*)`; on 9.x it is 57. It gets a
  graphics context of ours whose vtable has the old shape, every slot a thunk
  into the real one. `kGcSlot` holds the pairing, read out of both ROMs.
- **The frame loop** is kicked from inside the game's own `FocusChanged` and
  `Draw`, gated on `CCoeControl::IsFocused()` and on an eikcore export the
  N-Gage ROM shows to be `CEikAppUi::IsForeground()`. The first is asked of
  the wrapper; the second is answered yes.
- **The decryptor.** One function, 0xd5094 for 0x1c0 bytes, ships
  XOR-encrypted with 0x56DB7802, and two more regions at 0x10af44 and 0x10b388
  follow at run time. On EKA1 a code segment cannot be written to, so the game
  attaches to itself as a debugger and writes its own plaintext in through
  `RDebug::WriteMemory`, resolving that and four others by ordinal from
  euser. 9.x has no RDebug; our chunk is plain writable memory, so
  `WriteMemory` is a copy, `RThread::Id` need only be consistent with itself,
  and the rest go through the ordinal tables in `gate4_shim.cpp`.
- **Run-time lookups** go through per-library old-to-new ordinal tables, since
  the game keeps euser.dll and efsrv.dll open at once. `RLibrary::Load` is
  watched to know which is which. A lookup is never answered with null,
  because the game calls what it is given without looking.
- **Caches.** Anything written as data and then run as code — the image, the
  stubs, every thunk, the decryptor's output — is followed by
  `User::IMB_Range`. The emulator has no instruction cache and never notices;
  a phone faults at the first instruction.

## Ruled out, with the evidence

Do not re-open these without new evidence. Each cost at least one round.

- **Feature pack difference (FP1 vs FP2).** The phone's own `BitGdi.dll` and
  `Ws32.dll` were measured: `CDirectScreenAccess` offsets identical,
  `CFbsBitGc` vtable identical (73 entries, `BitBlt` at 57).
- **The blit.** A build with `BitBlt` pointed at a no-op rebooted at the same
  import, from the same caller, with the same tail.
- **Stack overflow.** The record carries the high-water mark: 1412 bytes on
  the phone, 2712 in the emulator, against the 64 KB the image asks for.
- **The frame loop failing on a later frame.** `frames` is 1 everywhere. All
  of this is one-time engine setup inside the first `RunL`.
- **The write count is a ceiling, and it was ending every run.** Across every
  configuration -- 5 ms timer plus a flush every sixteen imports, a flush per
  event on the card, a flush per sixty-four, a flush per event on C: -- the
  phone went down after about two thousand `RFile::Write` calls from this
  process, whatever the flushing did and whether the target was the card or
  internal flash. 655 imports plus ~1400 timer ticks; 1923 records; ~2005
  records. That is why the log buffers sixty-four events per write.
- **Writing the record to the memory card was holding the run back.** A
  `CPeriodic` at 5 ms flushed it two hundred times a second, free on the host
  file the emulator writes to and not on a card. Removing the timer alone
  appeared to change nothing, but that run had also been thinned to one flush
  in sixty-four, and an unflushed write dies with the file server, so its
  report was reading up to sixty-three events stale. With the record on C: and
  every write flushed, the game goes from dying two instructions into case 9
  to cycling the state machine, and gets there in two seconds rather than
  eight. Keep the record on C: and keep flushing every write.

Still open: the reboot itself, which survives all of the above.

## Where it stands

**Emulator:** ~15,680 records. Gets through startup, the decryptor and the
engine setup, then faults reading 0x30002 — a string pointer — at game+0xd5abc
(the game's `stricmp`), called from a case in the jump table at 0xd94f0. The
last hundreds of imports are `__udivsi3` from game+0xef1c4.

**Phone:** `CAknAppUi::SetKeyBlockMode` was the fault in the frame-loop kick, twice over.
Diverting it was wrong — it is a 9.x method on the framework's app UI, and the
game's own object is not one — so it is stubbed to a no-op in `gen_shim.py`
(avkon 1529 -- see below) and out of `kDiverts`. With that the phone went from 216 records
to **2863, every one of them identical to the emulator's**, and it is into the
state machine at 0xc9cd4 and cycling. That run ended in a reboot, but at the
196th pass through a loop it had already survived 195 times: the write ceiling
again, not the game. `phone-2026-09-23c.log` beside this file is that run.

Then the same build at the cheap cadence stopped at 192 records — a block
boundary, so somewhere in 192..255 — with KERN-EXEC 3, which is where the runs
before the SetKeyBlockMode fix stopped too. The only difference between it and
the 2863-record run is how often the record is written, so either the fault in
that window moves with the timing (the exact cadence spends milliseconds in the
file server between events, and the window covers the rest of telephony,
`RequestComplete` and the whole frame-loop kick) or 2863 was the lucky one.
`phone-2026-09-23d.log` is that run, and a zoom window over 176..336 is what
goes over it next: dying inside the window names the record, sailing past it
says the timing is what matters.

The zoom window found it. The phone stops at record 221, and 217..220 —
`RequestComplete`, `SetKeyBlockMode`, `KeySounds`, `PushContextL` — match the
emulator exactly. 221 is `CActive::Cancel` from a caller that is not an offset
into the image at all (0x74258070), where the emulator has
`CCoeControl::IsFocused`. The game's own four `bl` sites for that stub are all
at 0x398b8..0x39e04, so nothing in the game called it: something branched into
the stub table and arrived with a stale return address.
`phone-2026-09-23e.log` is that run.

It is a race rather than a code path: the 2863-record run is the same build on
the same phone and went 220 to `IsFocused` like the emulator. The log could not
show what the race was against, because it only ever recorded the game calling
out and never the framework calling in — so `gate6_slot` writes to it now (52
such entries in the emulator run, three inside the frame-loop kick), as do the
load address and, for a `Cancel`, the object it was made on. A `Cancel` on
something that is neither the timer nor the direct screen access object cannot
have come from the game, so it is recorded and refused rather than made, and
the run says what comes next instead of ending there.

The next run stopped in the same place, so it reproduces: `CActive::Cancel`
again, from 0x742a8070 this time, against a load base of 0x4600000 — about
0x788a8070, which is RAM-loaded code and not the image.
`phone-2026-09-23f.log` is that run. With the slot records in, both machines
run an identical cascade of twelve framework calls into our vtables after
`PushContextL` (app 5, appui 19, appui 18, app 12, doc 21, app 6, app 7,
app 5, app 5, doc 20, appui 17, appui 14) and then part: the emulator carries
on to appui slot 3, the phone branches into the stub table. Every slot that
was logged is inside the copied count, so it is the call after the last one.

**It is the heap.** Padding the vtable copies against exactly that — a slot
past the end — broke the emulator instead, deterministically, at the first app
slot of that cascade, three runs out of three; bisecting showed the padded
allocation alone did it and the instrumentation was innocent. Nor is it the
padding as such: `HEAP_NUDGE` makes one allocation at load time and never
touches it, and 384 bytes reproduces the failure exactly while 96 and 2048
leave the run alone. So the port has a fault that depends on where the heap
puts things, and that is what a phone running the same build twice and getting
through only once looks like. `VT_MARGIN` and `HEAP_NUDGE` in `gate6.cpp` are
the bench for it; both are zero in a real build, and the baseline is ~15,800
records and the usual 0x30002.

Worth saying plainly: the emulator can reproduce this class of failure on
demand now, so narrowing it costs nothing at the phone.

**And it was `CAknAppUi::SetKeyBlockMode` after all**, still being called for
real. `BY_ORDINAL` in `gen_shim.py` is keyed on the ordinal in the *N-Gage's*
library; the entry stubbing this one named 2927, which is the 9.x ordinal it
resolves to, so it never matched and the import stayed a direct call. Avkon
therefore wrote CAknAppUiBase's members at its own offsets into the game's app
UI, which is not one of its objects, and what the write landed on depended on
where the heap put things: it was setting our app UI's vtable pointer to 1 — a
TBool at avkon's offset, the vptr at ours — which the framework then dispatched
`ProcessCommandParametersL` through, faulting on 0x4c past 1. Keyed on 1529 it
is a no-op, and every layout that failed now runs the distance, the vtable
padding included.

How it was found, since the method is the transferable part: the fault PC came
out of the emulator's own register dump, `romimg` named the ROM function from
the module base in the emulator's log (`CEikonEnv::ConstructAppFromCommandLineL`
+0x238), `capstone` showed the two instructions (`ldr r0,[r0]` then
`ldr r2,[r0,#0x4c]`) that say a vtable pointer had become 1, and `vptr_check`
— which runs on every import and every call in, and is still there — named the
call it happened during.

**That fix was real and it was not the phone's fault.** The next run stopped in
exactly the same place with exactly the same caller, and the canary never
fired: nothing overwrites the app UI's vtable pointer on the phone. Two
different builds stopping identically also retires the word race — it is
deterministic, and the 2863-record run that got past it is the one that needs
explaining, not this one. `phone-2026-09-23g.log` is that run.

**`gate6_cancel` took the context in the wrong register.** Every one of the
nineteen functions a `ctx_thunk` points at takes the context third, in r2,
because that is where the thunk puts it. This one declared it second. So it
read r1 — and `CActive::Cancel()` takes no arguments, so r1 is whatever the
caller last left there — and dereferenced it immediately. Reaching this
function at all was fatal, and the record shows exactly that: the trace record
for import 279 is written by the thunk on the way in, and the `Cancel on`
record the function itself writes never appears. The emulator never showed it
because nothing in its fifteen thousand records calls `Cancel`, which is also
the honest answer to why a diff against the emulator could not have found this
one.

Still unexplained, and worth keeping in view: the caller. The log records
0x742a8070 against a load base of 0x4600000, so about 0x788a8070, which is
neither the image nor the chunk. The game's own four `bl` sites for that stub
would all record a small offset, so this arrived by an indirect branch with a
stale return address. With the register fixed the handler runs, records what
`Cancel` was called on, and refuses an object that is neither ours — so the
next run says who, and carries on.

**With the register fixed the phone went from 258 records to 6988**, and the
record shows the handler doing its job: `Cancel on 0x603388`, no stray, and the
run carrying on for another six and a half thousand events.
`phone-2026-09-23h.log` is that run. It ended in a reboot, and this one is not
the write ceiling: 58 slot entries and about 325 writes all told, against the
two thousand that ends a run.

**It is not hung, and that reading was wrong.** The emulator runs the same
five-state cycle 636 times and has longer import-free stretches than the phone
does -- 3510, 3043 and 2944 records against the phone's 2944, 2017 and 1266,
two of them exactly equal. The spinning is the game's obfuscated state machine
working, not waiting, and the phone simply stopped part-way through an
ordinary stretch of it. The locals it reads there are never written and its
`r9` is never set, which says the same thing: that arithmetic is obfuscation,
and reading it as a wait loop was reading too much into it.

**The reboot is the instrument.** Breadcrumbs were 6227 of the phone's 6988
records, 89% of them, and they are planted in the hottest code in the run:
every pass costs a call out of the game, a record, and -- inside the zoom
window -- a file write and a flush. All of it happens inside a single `RunL`,
so none of that time is given back to the active scheduler, and roughly ten
seconds of a thread not yielding is what the phone's watchdog resets. That
also matches what the reboots always looked like: seven to ten seconds, no
panic, no leave.

So `PLANT_CRUMBS` is off by default and the zoom window with it. The emulator
reaches the same 0x30002 with 5184 records instead of 15813 and about eighty
writes instead of three hundred, and the game's own loop runs with nothing of
ours in it. The breadcrumbs earned their keep finding the way into the state
machine and can go back on for a question that needs them.

**And it was.** With the breadcrumbs out the reboot stopped: the next run is a
plain KERN-EXEC 3 at 2112 records, `phone-2026-09-23i.log`. The phone's last
sixty records appear in the emulator's run verbatim, so the two are in
lockstep right up to the fault, and it is now inside one of the decrypted
regions -- code that reads as rubbish in the file, so `DUMP_DECRYPTED` writes
all three regions to `C:\g6code.bin` on the bench and they disassemble like
anything else.

What the game is doing there: building a five-character DLL name a character
at a time (`AtC`, `__modsi3`, `Append`), calling a function pointer it
resolved earlier with it, and then scrubbing the name off the stack --

```
0010b1f4  bl   #0x1191e8        @ TDesC16::Ptr()
0010b1f8  ldr  r3, [sp, #0x38]  @ the descriptor's length word
0010b1fc  bic  r3, r3, #0xf0000000
0010b200  lsl  r3, r3, #1       @ length in bytes
0010b210  strb r2, [r0], #1     @ fill it with 0,1,2,... and die here
```

anti-tamper, erasing the name it just used. The phone faults between
`TDesC16::Ptr` at 0x10b1f8 and `RLibrary::Lookup` at 0x10b244. All four
imports on that path map correctly (`Ptr` is euser 1807), and the descriptor
is `sp+0x38`, on the stack, so the write should be in bounds.

A breadcrumb cannot be planted there at load time -- the decryptor writes
straight over it -- so `kLateCrumb` is planted from `gate6_write_memory`
instead, once the region carrying it has arrived, with markers from 940.
Marker 941 sits on the scrub and fires once per byte: the emulator runs the
site twice and writes ten bytes each time. If the phone writes ten and stops,
the pointer is wrong; if it writes many more, the length is.

The late crumbs never fired: the run stopped before reaching them, at 1984
records (`phone-2026-09-23j.log`), earlier than the run before it. Worth
knowing why they are not comparable -- **two builds are not two runs**. The
same phone on two builds parts company at record 1574, in the middle of the
division storm, long before any crumb site. Whatever this game derives from
depends on our build, so only phone-against-emulator on the *same* build says
anything.

On that footing the phone is again in lockstep to the end. It faults in an
earlier loop of the same function, at 0x10b030, on the third of nine
characters:

```
0010aff4  bl  #0x118d78        @ TBuf16<9> at sp+0x58
0010aff8  ldr sl, [pc, #0x33c] @ a pointer the decryptor wrote, at 0x10b33c
0010b024  mov r0, sl ; mov r1, r5
0010b02c  bl  #0x118fc8        @ TDesC16::AtC(i) -- returns a reference
0010b030  ldrb r4, [r0]        @ and the game reads through it
```

The emulator reads all nine, twice. Both source descriptors are plain inline
`EBufC`s of length nine, at image+0x17f0e8 and +0x17f100, well inside a
0x1849bc text section, so the text is there to be read -- which puts the
suspicion on the pointer. It is a literal *inside a decrypted region*, so it
is not in the file to be checked: the decryptor writes it, and whether it
comes out relocated for where the image actually landed is the question.
`NOTE_LITERAL` and `NOTE_TARGET` log both from `gate6_write_memory`. The
emulator says 0x0487f0e8 against a base of 0x4700000 -- correct -- and a
length word of 9. A correct answer on the phone is 0x0477f0e8 against
0x4600000.

**The pointers are right.** The phone says 0x0477f0e8 against a base of
0x4600000, and a length word of 9 -- exactly what it should be, for both
descriptors (`phone-2026-09-23k.log`). So neither the pointer nor the data is
wrong, and reading the third character of nine ought to work.

That run also ended in KERN-EXEC 0 rather than 3, and at 1952 records rather
than 1984 or 2112 -- a third build, a third place, in the same name-building
code. Something the game does depends on our build. The obvious suspect was
the tracer, since it makes every IAT entry point at a thunk of ours and this
code is arithmetic over fetched values: `TRACE_EVERY_IMPORT = 0` leaves the
table pointing straight at what answered it. The emulator faults in exactly
the same place with it off, so the tracer is innocent and that idea is dead.

**What the emulator's own fault is.** It has been stable across every build,
which makes it the better thing to chase, and it is now read: at game+0xd5abc,
the game's `stricmp`, with `ldrb r3,[r5]` and r5 = 0x30002. The caller is case
6 of a second obfuscated state machine at 0xd9430 --

```
000d94e4  mov r0, r8        @ "6RBC.off"
000d94e8  ldr r1, [r5]      @ a name out of a table
000d94ec  bl  #0xd5aa4      @ the game's stricmp
```

-- so the game is searching for a file called `6RBC.off` and one of the
entries it walks has 0x30002 where a name pointer should be. `6RBC.off` is in
neither the game's directory nor inside `6rbc.cwa`, so the search is one that
cannot succeed; what matters is that the walk does not stop when it runs out
of entries. Whatever ends that table is not ending it here.

**Correction, from watching the walk rather than reading it.** It does not run
off the end of the table: `PLANT_WALK` puts a breadcrumb on each of the three
cases that drive the search, carrying r5 with it (`crumb_plant_r5` adds one
`mov r3, r5` to the stub, and the handler logs the register and the four words
it addresses). The search runs **once**. The very first node is already wrong.

```
case 0  start     r5 = 008b8000   @ the container
        its words 008b8000 007039f0 00000000 000f000e
case 6  compare   r5 = 007039f0   @ the first node
        its words 00030002 0000007e 00700be8 40070002
```

So `head = container[+4] = 0x7039f0`, and that node's first word -- where the
search expects a `char *` -- is 0x00030002. Nothing in those four words looks
like a name record. The container is page-aligned and holds its own address at
[+0], which is what a pool or a queue head looks like rather than a heap cell,
so the question is no longer where the walk stopped but where the container
came from and who was supposed to fill it.

Where those addresses live, since it matters: gate6's own `$HEAP` is at
0x700000 with a maximum of 0x4000000, so the container at 0x8b8000 and the
nodes at 0x7039f0 and 0x700be8 are all our own heap. Page-aligned is not
evidence of anything here.

**The name the decrypted code builds is `cwdynlog.dll`**, and the emulator
says so itself: `Try loading cwdynlog.dll to Gate6 failed`. It is built a
character at a time precisely so it is not in the image as text -- searching
for it there finds nothing -- and the file ships nowhere: not in the installed
copy, not inside `6rbc.cwa`, not in the original release. A logging DLL that is
not shipped, so the load is expected to fail, and after it the game looks up an
ordinal and calls whatever comes back, which `gate6_library_lookup` answers
with a no-op rather than null. `6RBC.off` is the same kind of thing: absent
from the original release too, so that search is meant to fail.

Also checked and not the problem: the installed game directory does not match
the original release. The release ships four DLLs -- `bin/ARENAFRAMEWORK.DLL`,
`bin/main.dll`, `Libs/GAMECOMMS.DLL`, `Libs/GAMEUTILS.DLL` -- and the installed
copy has twenty-six from somewhere else and no `bin/` at all. Restoring `bin/`
changes nothing, which figures: `main.dll` is 1,616,972 bytes with a code
section of 0x1850fc, the same image as `6rbc.app`.

**What the container turns out to be.** `result_thunk` wraps a call and
records what came back -- which a trace on the way in cannot do -- and with it
on the allocators the whole history of 0x8b8000 reads out: allocated at 21728
bytes, freed, 448, freed, 1056, freed, then 20 four times over, each freed, and
finally 4 bytes at record 4552 which is still live at the fault. The
allocations bracket `memcpy` calls from 0x1036xx and one of the strings beside
`6RBC.off` is `basic_string`, so that address is a container's buffer being
grown and recycled. The search reads [+0], [+4] and [+0xc] from it, which fits
the 20-byte tenant and not the 4-byte one. So the search is handed a pointer to
a buffer that was freed long before, and following it further means reversing
the game's whole container layer.

Two things worth keeping from building that tool. `r0`-`r3` do not survive a
call, so an argument cannot be held in one across it -- the copy `stmdb` pushed
on the way in is the one to read. And wrapping an import twice makes the
trace's caller column point at our own outer thunk, so `from` is meaningless
for anything `WATCH_ALLOCATIONS` covers.

**Back to the phone's own frontier**, which is 3500 records short of all this.
It dies reading the third of nine characters through `TDesC16::AtC`, and the
emulator reads all nine. `NOTE_TEXT` now logs those characters from our own
read at startup, before the game touches them: the emulator gives 00770066
00690076 002f0072 006f0066 00000070, "fwvir/fop" before descrambling. If the
phone logs all five words the text is readable and the fault is inside `AtC`;
if it stops partway, the memory is.

**It reads all nine**, byte for byte what the emulator reads: 00770066
00690076 002f0072 006f0066 00000070 and 006f004e 00490066 00330067 00320036
00000032, "fwvir/fop" and "NofIg3622" before descrambling
(`phone-2026-09-24a.log`). The memory is readable and the text is right, so
neither is the fault.

**And a methodological correction that cost two readings.** That run stopped at
1962 records, which is exactly a block boundary -- 810 records of notes and
then eighteen full blocks of 64 -- so the last record is the last *flush*, not
the fault, and the sixty-odd events after it were never written. The same is
true of the run before it. Twice now the last record has been read as the
place it died, and twice that was over-reading a cadence. `LOG_ZOOM` exists
for exactly this and was switched off; it is on again over 1850..2100, which
costs about 250 writes and gives the death point exactly.

**The sequence is fixed; where it stops is not.** Counting imports rather than
records, which is the only measure comparable across builds, the phone has run
2051, 1923, 1887, 1887 and now 1853 (`phone-2026-09-24b.log`). Every one of
those is a *prefix* of the next longest -- this run's 1853 imports differ from
the previous run's 1887 at no point at all -- so the game does exactly the same
thing every time and only the stopping point moves. That is worth holding on
to: it rules out a data-dependent divergence and points at something about how
far it gets rather than what it does.

The endpoint also moves with instrumentation in a way that fits. Taking the
breadcrumbs out took the phone from 700 imports to 2051 at a stroke, which is a
speed effect and not a fix; 1850 for the zoom window was set too late and only
three events fell inside it, and that run rebooted rather than panicking.

What the exact records did give: the run ends right after `TDes8::SetLength`
from 0x13f62c, and the next thing the code does is

```
0013f630  ldr r0, [sp, #0x18]      @ the TPtr8's length word
0013f634  bic r0, r0, #0xf0000000  @ its length
0013f638  bl  #0x119768            @ HBufC16::New(that many)
```

An allocation whose size comes straight out of a descriptor. The emulator makes
that call nine times for 27, 31, 28, 27, 26, 31, 26, 31 and 28 characters; a
bad length here would ask the phone for something enormous, which is a better
explanation for a reboot than for a panic. `arg_thunk` records a call's first
argument *before* it is made -- `result_thunk` could not, and a call that never
returns leaves nothing otherwise -- and it is on `HBufC16::New`.

**The lengths are fine** -- 27, 31, 28, 27, the emulator's own first four
(`phone-2026-09-24c.log`) -- so that idea is dead, and the run it came from
stopped at 1785 imports, earlier again.

**It is time, and the instrument was eating it.** Six builds running an
identical sequence, each a prefix of the last, stopping at 2051, 1923, 1887,
1887, 1853 and 1785 imports -- monotonically shorter as more measurement went
in. The reboots are the watchdog: the game never returns to the active
scheduler through any of this, and about ten seconds of that is what the phone
resets. Every record is time the game does not get, and for several rounds the
answer to "why did it stop earlier" was me.

Where the cost is is not the writes -- at 64 events to a block the whole run is
about thirty of them. It is the trace itself, on every import: six registers
saved, three literals loaded, a call out, a record appended, and back, against
a real `TDesC16::AtC` of about ten instructions. `TRACE_SKIPS_HOT` leaves the
eleven hottest imports unwrapped -- `__udivsi3` alone is 57% of all calls, and
the eleven together 88% -- which takes the emulator's run from 5122 records to
773 for the same fault at the same place. What is left still names every file
opened, every library loaded and every frame drawn.

**And it worked.** Measured the only way that compares -- phone against
emulator on the *same* build -- the phone went from 1887 of 4915 imports to
455 of 705: **38% to 65%** (`phone-2026-09-24d.log`). Its last records are the
emulator's 470-475, an alternating pair of allocations, so it stopped mid-loop
again rather than anywhere meaningful.

Checked and cleared on the way: `mke32.py` defaults `heap_max` to 1 MB, which
would have been a fine story -- the S60v3 framework costs far more heap than
the N-Gage's did, and the emulator hands out 64 MB whatever the header says.
But `build_gate6.py` already overrides it to 0x4000000, so both machines have
64 MB and that is not it.

So the instrument comes down again, from pruning to a whitelist:
`TRACE_MILESTONES` wraps only files, libraries, the screen, the frame-loop
kick, and the two the record needs to close itself properly. 197 records in
the emulator against 705 and 4915, the same fault in the same place, and the
block drops to eight events so at most seven are lost when it stops. Twenty-
five times lighter than the trace that was in place two rounds ago.

**The 38% to 65% was not real, and neither was the conclusion drawn from it.**
Those two numbers came from different trace sets, so their denominators were
different -- the exact mistake flagged one paragraph earlier, made immediately.
On a yardstick that does hold, the milestone subsequence extracted from every
log and counted against the emulator's 169, the phone reads:

```
2%  4%  33%  2%  6%  6%  6%  38%   60% 58% 58% 58%  56% 53% 53%   59%
```

Flat at 58-60% since the first build with the breadcrumbs out, through a
twenty-five-fold reduction in instrument. So the trace was never what limited
the run, the reboots aside, and "it is time" was wrong. The stopping point is
fixed.

**Where it is fixed.** The phone's last milestone is the emulator's 123,
`RLibrary::Lookup` from 0x10b0e4, and the two instructions after it are

```
0010b0e4  mov r6, r0      @ what the lookup answered
0010b0f0  bx  r6          @ and straight into it
```

`gate6_library_lookup` hands that address over. It now refuses an `RLibrary`
whose handle is zero rather than asking the kernel about it -- a lookup on an
object that is not open is KERN-EXEC 0, invalid handle, which is the panic the
phone reports and which the emulator allows. The guard never fires here, since
every library the emulator looks up is open, so it is a fix that cannot be
tested on this side. What can be seen either way is the handle and the answer,
and both are now on record for all 66 lookups.

**The handle was fine, and that is what gave it away.** 0x40750035, open, so
the guard never fires and that idea was wrong too (`phone-2026-09-24f.log`).
But the lookups are now on record site by site, and every one of them answers
the same on both machines -- including 0x10b128, where both correctly hand back
our own no-op. The phone also got past the scrub to 0x10b244, further than any
earlier reading had it.

**It is a kernel device driver.** Resolving the two sites the phone stops
between, through euser's exports in the ROM:

```
0x10b0e4  ->  euser 624  User::LoadLogicalDevice(const TDesC16&)
0x10b244  ->  euser 490  RBusLogicalChannel::DoControl(TInt, TAny*)
```

The game loads an LDD -- an N-Gage device driver -- opens a bus logical channel
to it and calls control on it. S60v3 has no such driver, so the load fails, the
channel never opens, and a control call on a channel that is not open is
KERN-EXEC 0: invalid handle, the exact panic, deterministic, and untouched by
anything done to the instrument. That is why the reach has sat at 58-60% since
the breadcrumbs came out.

None of it can be honoured, so `gate6_library_lookup` no longer passes any of
it on: the twenty euser ordinals for logical and physical devices, bus channels
and `RDevice` all answer with the no-op, which returns zero -- KErrNone for the
load and the free, and nothing for the channel. The emulator refuses 624, 490
and 623 twice each, reaches the same 170 milestones and the same fault, so the
game carries on without its driver.

**It worked.** The phone refused 624, 490 and 623, exactly as the emulator
does, and moved for the first time in seven builds: 118 milestones against 102,
**60% to 69%** by count, and positionally it now reaches the emulator's
milestone 141 of 170 -- 83% of the way to the emulator's own frontier
(`phone-2026-09-24g.log`). The plateau was that driver.

It rebooted rather than panicking, and this time the instrument is not a
plausible culprit: 230 records and about thirty writes, against the 6988 and
the thousands that caused the earlier ones.

**Worth being plain about what is and is not ahead.** The only drawing in the
emulator's whole run is at milestone 16 -- `SetClippingRegion`, `SetAutoUpdate`
and one `CFbsScreenDevice::Update` -- which is the black band with pixels
already seen on the phone. Nothing draws again before the emulator faults at
170. So the splash is past the emulator's frontier as well, and the archive
search at game+0xd5abc is now the wall for both machines rather than just this
one.

**Two runs of that build, deliberately identical** (`phone-2026-09-24h.log`):
222 records against 230, 110 milestones against 118, and one a *prefix* of the
other. Same path, different moment, so the reboot is asynchronous -- it is not
in the code the game is running.

Two things are asynchronous here. One is the window server: direct screen
access is a promise to stop drawing when told, and the game holds it while
computing for thousands of operations inside a single `RunL`, never back in
the active scheduler and so never able to hear an abort. `HOLD_THE_SCREEN`
tests that by not taking the screen -- but it cannot be a one-line switch, as
the graphics context only exists once `StartL` has run and the run dies
writing through a null at three milestones. Left on until the stand-in exists.

The other is memory, which varies with whatever else the phone is doing and
would equally explain two runs eight milestones apart. That one is cheap to
settle: `User::Alloc`, `AllocL`, `AllocZL` and `HBufC16::New` return zero on
failure rather than panicking, so `result_thunk` now records **only** the
zeroes -- nothing at all in the ordinary case, the whole answer if it happens.
None in the emulator's 135 allocations.

**Not memory.** 110 milestones again and not one allocation returned zero
(`phone-2026-09-24i.log`). The two runs do differ at record 97, but only in the
caller column, and only because wrapping an import twice makes the trace record
our own outer thunk -- the same trap noted above, and the runs are otherwise
identical.

So the window server is what is left, and it can be tested after all without a
stand-in context: start the access, let `dsa_refresh` take the graphics context
out of it, and then give the screen straight back with `Cancel`. The game is
told it still has it, since it will not move otherwise, and nothing is drawn
between there and the end of the emulator's run so nothing is lost by the lie.
`RELEASE_THE_SCREEN` does that; the emulator reaches the same 170 milestones
and the same fault with it on, which makes it a clean test rather than a
change of behaviour. If the phone stops rebooting, the reboot was a client
holding the screen and not listening; if it does not, the window server is
innocent and something else is interrupting.

**The window server is innocent too.** 115 milestones with the screen given
back, inside the 110-118 band of the runs that held it, and still positionally
the emulator's 141 (`phone-2026-09-24j.log`). Nor is anything leaking: loads
against closes are 15/13 on the phone and 19/17 in the emulator, file sessions
7/4 against 8/5 -- the same two and three outstanding on both. And the blind
spot the milestone trace leaves between 141 and 158 is only 203 calls, almost
all of them the name descrambler again, so nothing exotic is hiding in it.

Which leaves the plainest explanation: the game takes longer than the phone
allows in one `RunL`, and always has. Every earlier reboot was ours -- two
thousand writes, then six thousand breadcrumbs -- and removing those bought
real distance each time, which fits. What is left is the game's own speed on a
332 MHz phone.

`LOG_THE_CLOCK` reads `User::TickCount` every sixteenth milestone, which is ten
readings and 38 ticks across the emulator's whole run. It settles the question
either way: stopping at the same moment each time and a different milestone is
a clock, and stopping at the same milestone after a different time is not. If
it is the clock, the answer is to stop computing inside a `RunL` at all -- to
give the game its own thread, so the active scheduler stays free to service the
framework while it works.


















## Open

- 30 imports still unanswered, mostly the deliberately stubbed N-Gage
  libraries and a CServer/CSession implementation the game carries.
- `OfferKeyEventL` (old control slot 1, 9.x slot 3) is unbridged, so there is
  no input.
- The framework base-class constructors are still `LOCAL_NOOP` approximations.
- `mke32.py` has no `.bss` support, which is why the context is reached
  through a pointer baked into each thunk.

## Stopping to take stock

**The clock says it is not time either.** 63 ticks across the phone's whole run
against the emulator's 38 -- about a second, not the ten a watchdog would want
(`phone-2026-09-24k.log`). That was the last hypothesis standing, and it is
wrong like the others.

What is actually established, as against guessed:

- **Ruled out by measurement**: the write ceiling (30 writes now), instrument
  weight (58-60% held across a 25-fold reduction), memory (no allocation
  returns zero), resource leaks (loads/closes and file sessions match the
  emulator exactly), the window server (releasing the screen changes nothing),
  elapsed time (one second), and a data-dependent divergence (every phone run
  is a prefix of the emulator's sequence).
- **Fixed, and each one real**: the GCC98/EABI vtable and ABI work, the
  decryptor, the cache flushes, `SetKeyBlockMode` on the right ordinal,
  `gate6_cancel`'s register, and the driver refusal -- which moved the phone
  off a plateau it had sat on for seven builds.
- **Still unexplained**: the reboot, at 107-118 milestones, five runs running.

**A reboot is not a user-side fault.** Symbian's own documentation is plain
about it: a user thread that touches bad memory gets KERN-EXEC 3, and the OS
reboots when the faulting thread is a *kernel* one. So whatever is happening is
kernel-side, which a user process can reach in very few ways -- essentially
through a device driver, or by taking a system server down with it.

**And the game drives a kernel device.** The LDD it loads is named `GD1DRV`,
and `gd1drv.ldd` (uid2 0x100000af, a kernel LDD) sits in `system/libs` of both
N-Gage ROMs beside `gd1eng.dll`, and in no S60v3 ROM. The game opens a bus
logical channel to it and issues `DoControl`. Refusing those calls stopped the
KERN-EXEC 0, correctly -- but it leaves the game running on whatever it makes of
a driver that answered zero to everything, which is not the same as working.

## What this needs, to be worked on without the phone

**The N-Gage is already here.** EKA2L1 has NEM-4 and RH-29 installed with their
ROMs, and the original unmodified game runs on them: `--device NEM-4 --run
0x101fd42d` reaches the same files in the same order and loads GD1DRV.LDD
before it stops. That is the reference this work has never had -- the game
behaving *correctly*, to compare the port against, instead of inferring correct
behaviour from a 5320 running the port.

**EKA2L1 is this repository.** Every divergence so far has been the emulator
being lenient where hardware is strict: invalid handles tolerated, DSA rules
unenforced, an absent LDD shrugged off. Those are all things that can be made
strict here, and a stricter emulator reproduces the phone's failures locally
instead of one per hardware round.

**GD1DRV can be emulated.** EKA2L1 has an `ldd::factory` framework and no
GD1DRV in it -- `suitable_ldd_instantiate_func` finds nothing, which is why
even the native run cannot use the driver. Writing that factory would let the
native game get past it and show what the driver is actually for, which is the
one thing needed to decide what the shim should answer instead of zero.

## What GD1DRV is

EKA2L1 answers this itself: `ldd/src/collection.cpp` maps the name `gd1drv` to
`mmcif_factory`, the **MMC interface**. Its channel implements two EKA1
controls, `select_card = 4` and `card_info = 6`, and the second fills a
twenty-byte `{ TUint32 cid[4]; TUint32 type; }` with the memory card's CID and
a type of 0, ROM.

The game's own code, in the decrypted region, is exactly that:

```
0010b248  add r5, sp, #0x4c   @ the channel
0010b250  mov r1, #4          @ select_card
0010b25c  bx  r6
0010b26c  bl  #0x118f38       @ zero twenty bytes at sp+0x24
0010b270  mov r3, #4
0010b274  str r3, [r4, #0x10] @ type = 4, unknown
0010b27c  mov r1, #6          @ card_info
0010b288  bx  r6
0010b294  ldrb r3, [r1, r3]   @ then eight bytes out of the CID, 14 down to 7
```

So the game asks the card who it is and reads eight bytes of the answer. This
is the copy protection: an N-Gage game shipped on a card, checking the card.
Answering nothing leaves the type at 4 -- the game is told its card is not a
game card. `gate6_mmc_control` now answers `card_info` the way EKA2L1's own
channel would, a zero CID and ROM, so the game is told what the emulator would
tell it rather than what an absent card would. The emulator's run is unchanged
at 170 milestones, so this is not the wall, but it removes a wrong answer.

**The native reference is real but limited.** The original game does run on
NEM-4 (`--run 0x101fd42d`) and does load GD1DRV.LDD -- but EKA2L1's N-Gage
support takes it down at 0x9EBD3A00 well before our port gets on the S60v3
side, so it cannot serve as a full trace to diff against. It is still worth
having: it showed that the native game reads `E:\game.id` from the card root,
which our port never opens.

## Where it actually stops

Reading the tail properly rather than the milestone count: the phone gets
**past** the whole driver interaction -- select, card info, free, close -- and
then opens a file at 0x10a314 and dies on the `RFile::Read` at 0x10a814.

```
0010a7f8  mov r0, sp          @ a TPtr8 on the stack
0010a804  bl  #0x1195b8       @ TPtr8::TPtr8(buffer, length)
0010a810  bl  #0x11a418       @ RFile::Read(that)
```

Which is worth pausing on, because `RFile::Read` is the file server writing
into *our* address space across an IPC boundary. A descriptor that points
somewhere it should not is no longer a fault in this process; it is a server
writing where it was told to. That is one of the few things a user process can
do that ends kernel-side, which is what a reboot means.

`WATCH_THE_READS` records the descriptor before each read -- its type and
length word, its maximum, and its buffer. The emulator's are all unremarkable:
type 2, lengths matching maxima, buffers on the stack at 0x40xxxx or in the
heap at 0x8bxxxx. Anything on the phone pointing into the game's chunk at
0x46xxxxx, or anywhere that is not stack or heap, is the answer.

## A buffer overflow that was mine, not the game's

*What this section said before was wrong, and the way it was wrong is worth
keeping.* Recording each read buffer against the cell it was allocated in, in
the emulator, gave this:

```
max 0x8      buf 0040f880   (stack)
max 0x80     buf 0040f888   (stack)
max 0x2823   buf 008c3e38   cell 008c3e38 size 10275
max 0x5      buf 0040f7c8   (stack)
max 0x54e0   buf 008b2a78   cell 008b2030 size 73216
max 0x2807c  buf 008bd448   cell 008bd448 size 31        <-- 163964 asked for
max 0x2807c  buf 008b9b70   cell 008b9b70 size 163964
max 0x2807c  buf 008b9b70   cell 008b9b70 size 163964
```

and the sixth line was written up here as the file server being handed a
thirty-one byte cell and asked for a hundred and sixty kilobytes -- a
server-side overflow, which is the one kind of thing a user process can do
that ends kernel-side, which is what a reboot means. It fit so well that it
went in as a finding.

It is an artefact of the instrument. `allocPtr`/`allocLen` is a ring of the
last thirty-two allocations and it is never told about frees, so an address
that has been handed out twice appears in it twice. The search ran the ring in
slot order and stopped at the first entry containing the buffer, which is
whichever of the two happens to sit at the lower index -- here the stale
thirty-one byte one. Searching newest-first instead, the same run reports:

```
max 0x54e0   buf 008b2a78   cell 008b2a78 size 21728
max 0x2807c  buf 008bd448   cell 008bd448 size 163964
max 0x2807c  buf 008b9b70   cell 008b9b70 size 163964
```

Every read sits in a cell exactly its own size. There is no overflow, and there
never was one.

### Probes, and what they cost to have built

What settled it is a new instrument. A *probe* is a breadcrumb that also
reports two of the game's own registers, at one exact instruction:
`probe_plant` takes the address as given and refuses the site if what stands
there cannot be moved, where `crumb_plant` hunts forward for an instruction it
can move and so answers a few instructions late. Two registers, a marker, and
the site are enough to ask "what was in here, here".

Three of them took the overflow apart in two runs.

The first five went around 0xe4774, which this section had named as the guilty
caller. They reported the table at r6 = 0x8b1c28, index 0, the slot at +0x240
written with 0x8b2a78, and the same slot read back -- correct, and with a
length of 0x54e0 rather than 0x2807c. That path was never the one. The import
trace records the call to `RFile::Read` from *inside* the helper at 0x10a814,
which is the same address whichever caller asked, and the attribution to
0xe4774 was a guess dressed as a reading.

So the next probe went on the helper's own first instruction, reporting `lr`:

```
lr 047e3774  buf 0040f7c8   ->  caller 0x0e3770
lr 047e4778  buf 008b2a78   ->  caller 0x0e4774
lr 047ed698  buf 008bd448   ->  caller 0x0ed694
```

(the image loads at 0x4700000). The read this section was about comes from
**0xed694** -- the caller it had cleared as correct -- and a third probe, on
that caller's own allocation, closes it:

```
0x000ed678  r0 = 008bd448   r4 = 0002807c
```

The game asked for 163,964 bytes and got a cell of 163,964 bytes, and read
163,964 bytes into it. There is nothing wrong with the read.

### What is left of it

- The reboot has no explanation again. This was the leading one for four days.
- 0xd5a08 is not `User::Alloc` but a one-instruction veneer into an import
  stub at 0x119608 (`ldr ip,[pc,#4]; ldr ip,[ip]; bx ip`), one of a table of
  them at 0x1195f0 and up reading an IAT at 0x10184xxx. Allocation is in
  `kHot` and untraced, which is why no import record appears between the
  `RFile::Open` and the read.
- The read helper at 0x10a7dc is fully read:
  `read(void *buf, TInt len, RFile *f)` builds a `TPtr8(buf, len)` on its own
  stack, calls `RFile::Read`, and returns the length read or -1. The length is
  the caller's own and is never derived from the buffer.
- `CLAMP_THE_READS` clamped reads that were not too long. That it starved the
  game (43 milestones against 170) was the instrument breaking the run, not
  evidence about the buffer.

**Twice now the instrument has been the bug** -- the breadcrumbs that rebooted
the phone, and now this. The pattern is the same both times: the tool was
written in the same hour as the theory it went on to confirm, and nothing was
pointed at the tool until the theory ran out of places to go. A measurement
that agrees with the theory has to be checked as hard as one that does not,
and the cheapest check is a second instrument that does not share the first
one's assumptions. Here that was three probes and two emulator runs, and it
could have been run on day one.

## What the next hardware round is for

Four reboots in a row and the one thing they do not say is *where*. The three
newest phone logs end at 224, 226 and 265 records, and with eight events to a
block the last record on disk is up to seven events before the one that killed
the phone. That ambiguity is not academic: it is the same gap that let 0xe4774
be named as the caller doing the damage when it was 0xed694.

So this build is for location, and carries three changes, all of them cheap:

- **`LOG_ZOOM = 65`.** From the sixty-fifth traced event on, every record is
  written and flushed as it happens. About eighty extra writes on a run of the
  length the phone manages; the build that took the phone down by write volume
  alone did around two thousand. *If this one reboots noticeably earlier than
  the last three, the instrument is implicated again and the window closes.*
- **`RFile::Open` names its file.** `arg_thunk` now keeps the third argument
  as well as the first two, so the name descriptor can be read: the last
  twenty-four characters, which is the filename and enough of the path to
  place it. The emulator's six opens read
  `nokia_en.rle`, `6RBC.dat`, `cis.dat`, `cwivenc.dat` ×3. A record that says
  "a read" becomes one that says where in its own loading sequence the game
  had got to.
- **Probes on the read helper.** Two records per read, naming the caller.

What the phone and the emulator do is otherwise the same shape. Comparing the
import histograms of the phone's last run against the emulator's, the phone is
a strict prefix -- fewer of everything, nothing it does that the emulator does
not, including the one pass through direct screen access. It is not taking a
different path. It stops.

And it stops fast: the tick records put the whole run at **64 ticks, one
second**, from the first milestone to the last. Whatever kills it is not a
watchdog and not a slow leak.

### Checked while waiting: the game's globals are not missing

The image declares `dataSize = 0` and `bssSize = 0`: the game has no writable
static data at all, so every global it has lives behind `Dll::Tls()`. That is
the GCC98r2 pattern for a polymorphic DLL, and it is the sort of thing a loader
that never runs a DLL attach would silently lose -- which would explain the
container full of stale pointers that the emulator dies on.

It is not lost. Reading the image out:

```
000b8f24  operator new(8)               @ the TLS root
000b8e48  [r5+4] = 0x10182f38           @ its table
          [r5+0] = operator new(0x28)   @ ten pointers, zeroed
          Dll::SetTls(r5)
000b8f44  set(a, b): Dll::Tls()->[0][b] = a
000b8fa4  b 0xc8b2c -> UserSvr::DllTls(0x10000000)   @ the handle is the
                                                       image's own code base
```

and three probes say it all happened:

```
0x000b8f54  r0 = 0089aac8   -> 0089aaf0 04882f38 00000000 00000000
```

`0x89aaf0` is the ten-pointer array and `0x4882f38` is `0x10182f38` correctly
rebased into our chunk. The root is built, the handle survives our relocation,
and 9.x answers it.

*What nearly went in here as a finding* is that `UserSvr::DllSetTls` never
appears in the trace while `DllTls` appears six times -- read as "the creator
never runs". It never appears because `TRACE_MILESTONES` traces a whitelist
and 304 is not on it. Three probes and one emulator run, no hardware, and the
theory was dead before it was written down. That is the intended cost of one
now.

## The instrument goes nearly silent

> *Superseded in its premise: write volume was never what took the phone down.*

The zoomed build (`g6box-28`) did what it was for and cost what it was warned
it might. It located the death precisely for the first time -- and it died
sooner than the three before it.

```
                 records  traced events  ticks  est. writes
g6box-30             226           115      -           86
g6box-24             224           107      -           86
g6box-18             265           106     64           91
g6box-28 (zoom)      295            86     27          129
```

Records went up only because the build writes more per event; the yardstick is
traced events, and it fell. So did the clock.

**Where it dies.** With every record flushed as it happened, the last one on
disk is the last thing that happened, and it is the `RLibrary::Lookup` from
0x13f5e4 -- the second pass through that site, having succeeded on the first
fourteen records earlier. Our own `gate6_library_lookup` logged nothing at all
after it, so the phone went down inside the handler, before it had read the
library's handle. The site is:

```
0013f5d8  ldr ip, [r4]      @ a function pointer out of a table
0013f5e0  bx ip             @ -> RLibrary::Lookup        <- last record
0013f5e4  mov r6, r0        @ whatever it answered
0013f5f0  bx r6             @ ... is called, unconditionally
```

**What that is worth against what it cost.** Two of the four things this port
has spent hardware rounds on turned out to be the instrument. The run that
recorded most also died soonest. The write budget has never been measured
against reach, only guessed at -- so rather than guess again at the right
weight, `SILENT` takes it to nearly nothing:

- `LOG_BLOCK` 1024 and no zoom: the log never fills, so it never writes.
- No probes, no read watching, no allocation watching, no clock.
- The box, which was rewritten on every one of the fifty-eight framework calls
  into our vtable slots -- half the whole budget, and not one of those writes
  survives a reboot -- now goes down once every thirty-two traced events. Four
  writes on a run of the length the phone manages, carrying the count, the last
  import and the ring of the last thirty-two events.

Three or four writes against a hundred and twenty-nine. **Nothing the shim
does changes; only what it says about it.** The emulator reaches the same
0x30002 with the same trace, and writes no log at all.

The question is the one thirty rounds have not asked: does the phone still go
down when almost nothing is being written? Either answer is worth the round.
If it still reboots, the instrument is finally exonerated and every future
build can afford to talk. If it does not, the log has been the bug all along,
and the next instrument is a memory-only ring read out at the end.

## The reboots were ours

The silent build did not reboot the phone. It panicked KERN-EXEC 3 -- an
ordinary unhandled exception in the game's own thread -- and got further than
any run before it.

```
                          traced events   how it ended    writes
g6box-18 .. g6box-30        106 .. 118    reboot            86-91
g6box-28 (every record)            86     reboot              129
g6box-32 (silent)               128+      KERN-EXEC 3         3-5
```

Thirty rounds. **The instrument was the reboot**, all of it, and the write
volume was the variable the whole time -- which is why the reboot moved around
with each build and never matched anything the game was doing. It is the third
time the tool has been the bug and by far the most expensive: two of the three
theories this file records at length were autopsies on a corpse we made.

What the box says, with the game's own panic instead of a dead phone:

```
  128 traced events at the last write, so 128..159 in all
  last import 283  RLibrary::Close
  reached a slot of ours, THE FRAME LOOP RAN
  stack high-water 1932 bytes
```

`THE FRAME LOOP RAN` has never been set on hardware before. And the phone's
last sixteen events are the emulator's, instruction for instruction, offset by
twenty-seven:

```
  phone 117..127   283@13f610 332@13f63c 326@13f68c 326@10abf8 325@10b08c
                   326@10b0e4 326@10b128 326@10b244 326@10b2d4 326@10b308 283@10b37c
  emu   144..154   283@13f610 332@13f63c 326@13f68c 326@10abf8 325@10b08c
                   326@10b0e4 326@10b128 326@10b244 326@10b2d4 326@10b308 283@10b37c
```

The phone died between events 128 and 159, which maps onto the emulator's
155-186; the emulator's own 0x30002 falls between 160 and 191. **Those windows
overlap**, so the phone and the emulator may now be failing at the same place
-- which would make the rest of this local.

### The instrument that should have been there all along

A log that appends pays a write per block and loses whatever has not been
flushed. A box is a fixed record rewritten in place: one `file_write_at`
carries the whole of it however big it is. So the ring went from sixteen
events to sixty-four -- same single write -- and it goes down every sixteen
traced events, which is about ten writes a run against the eighty-six the
rebooting builds were doing. The last box therefore always holds every event
since the one before it, and forty-eight more for context.

`gate6_fault` now writes the box before it panics, so if the exception handler
ever does run the record is exact rather than up to fifteen events short.
It did not run this time: KERN-EXEC 3 is what the kernel raises when nothing
handled the exception, and our own panic category would have shown instead.
`User::SetExceptionHandler` is not taking on 9.x, which is its own small
problem and worth one look later.

**The rule this earns:** the instrument's cost is a measurement, not a guess.
Every future build states its write budget, and no build goes to hardware
spending more than the last one that survived.

## The emulator's own fault, read out

With the phone and the emulator failing in the same window, the 0x30002 fault
is worth the probes. Six of them, one run:

```
0x000cc914  r5 = 008b80e0   str r5, [r5]     <- User::Alloc(4), then self
0x000cc92c  r5 = 008b80e0   ldr r2, [r5]
0x000ccb68  r5 = 008b80e0   ldr r0, [r5]     -> the container, = the cell
0x000d9428  r0 = 008b80e0   r1 = 0483fce0    <- find(container, "6RBC.off")
0x000d9484  r6 = 008b80e0                    <- head = [container + 4]
0x000d94e4  r5 = 00703abc                    <- and stricmp on its [0]
```

and the machine at 0xd9428, once its jump table is unpicked, is nothing
exotic:

```
find(container, name):
    for (n = container->[4]; n; n = n->[0xc])
        if (!stricmp(name, n->[0])) return n;
    return 0;
```

So the game asks for **four bytes**, writes the cell's own address into it, and
later reads `[cell + 4]` as the head of a list. `User::AllocLen` says a
four-byte request gets a **thirty-six** byte cell here, so that read is inside
the cell -- it is not out of bounds, it is uninitialised. It holds 0x703abc,
whose `[0]` is 0x00030002, and stricmp walks into it.

The container's words are the giveaway:

```
0x8b80e0:  008b80e0  00703abc  00000000  000f000e
           00000000  00000000  008b2dec  008b2df8
```

Word 0 is what 0xcc914 wrote. The rest is not noise -- two heap pointers, a
pair of counters -- it is a **live-looking object the game allocated earlier,
freed, and is still reading**. Our heap handed that address back out for the
four-byte cell and word 0 went over the top of it.

### Three fixes tried, none of them a fix

| | traced events | stricmp reached | ends |
|---|---|---|---|
| as it is | 179 | yes | fault 0x30002 |
| zero the cell's slack | 43 | **no** | clean `User::Leave` |
| pad every allocation by 16 and zero | 160 | no | fault elsewhere |
| free nothing at all | 160 | no | fault elsewhere |

Zeroing works exactly as intended -- the list reads empty and the walk stops
before a single node -- and the game then gives up at a *third* of the
distance. It was reading that memory on purpose. Padding moves every cell in
the heap and fails earlier somewhere else, which is the trap `VT_MARGIN` set
two months ago. Leaking gets no further either.

**So the walk is a symptom.** The container is meant to hold a list of names
and it holds a freed object; the question is what was supposed to fill it, not
how to survive its being empty. That is the same shape as `6RBC.off` and
`cwdynlog.dll`: things the game looks for that are not there.

`User::AllocLen` is imported now and `gate6_alloc` stays, switched off, with
all three experiments behind their own constants -- they cost nothing off and
each one is a question that will be asked again.

*Two process notes.* `REPORT_LAST_BOX` -- the startup panic that reported the
previous run's box -- truncates the log and kills the run before the game
starts, which cost four confused iterations here before it was spotted. It was
the only way to read a record off a phone that had just rebooted; the box
survives on its own now, so it is off. And a build with no writable globals
cannot hold a `static Context *` for a one-off measurement: it fails at the
link, which is the design working.

## What the container is waiting for, and why the emulator is not a reference

> *Refined: our port gets further under the emulator than the original N-Gage binary does.*

Following the empty container back a layer at a time:

```
0xcc90c   User::Alloc(4) -> the slot;  [slot] = slot          "empty"
0xcc928   0xe9808(table = owner + 0x28, 1, 0)
0xcc940   r0 = 0xe98c4(table, 1, *slot, 0, key = 0)
          ... twenty instructions of shift-and-add ...
0xcc984   [slot] = that                                        install
0xccb68   find(*slot, "6RBC.off")
```

The twenty instructions between are a multiply by 3467093631 followed by a
multiply by 4016970111, and those two multiply to **1** mod 2^32. The whole
chain is the identity: `[slot] = 0xe98c4(...)`, obfuscated.

And 0xe98c4 is not a container lookup at all. It walks a table of 24-byte
entries for one whose `[+4]` matches the key, and for each match computes

```
overrun = entry[+0xc] - 1.5 * entry[+0x10]      clamped at zero
```

`0xd59e0`, which fills `entry[+8]`, resolves to **`User::TickCount`**;
`0xd59e4`, one veneer along, is `Math::Random`. The entry the probes caught
reads

```
[+4] = 0 (the key, matched)   [+8] = 0x8e -> 0x90   (ticks, rising)
[+0xc] = 0x30 -> 0x2e         [+0x10] = [+0x14] = 0xf00
```

0xf00 is 3840 ticks: **sixty seconds**. So this is a stopwatch, not a heap,
and the result it hands back is

```
return sl( fallback | overrun | (count << (Math::Random() % 16)) )
```

-- the fallback with junk OR'd into it if anything has overrun, which is
anti-tamper machinery of the same family as the `6RBC.off` and `cwdynlog.dll`
searches and the GD1DRV card check. Two seconds into a run nothing has
overrun, so it returns the fallback unchanged, and `[slot] == slot` is the
**correct** result. The empty container is not a symptom of anything. The game
then walks it anyway.

### The emulator is not ground truth for these paths

Which raises the obvious question: what does the real game do here? EKA2L1 has
both N-Gage ROMs installed and the original is sitting on `e.ngage`, so it can
be asked directly --

```
eka2l1_qt --device RH-29 --run 0x101fd42d
```

-- and **the unmodified game, on its own platform, dies the same way**:
KERN-EXEC 3, after opening `cwp.dat`, `nc.dat` and `cwivenc.dat` in the same
order our port opens them. The fault is at 0x139588:

```
0010a9f0  mov r0, #0x24        @ thirty-six bytes
0010a9f4  bl  operator new
0010aa00  blne #0x139568       @ construct(it, r4)
00139588  ldr r2, [r1, #0x240] @ <- faults; r1 is not an object
```

`[r1 + 0x240]` -- the same +0x240 table this session opened with at 0xe4724.

So EKA2L1 cannot run this game on the N-Gage either, and **the emulator has
never been a reference for the protection paths**, only for the shape of the
import sequence. Two conclusions follow. The 0x30002 fault may be an artefact
of whatever EKA2L1 is not giving the protection rather than a defect in the
port. And the phone, which is the only real platform in this loop, is the only
thing that can say which.

## A twelve-bit offset, and what build 34 is for

`arg_thunk` kept the third argument of a wrapped call by emitting

```
str r2, [r12, #offsetof(Context, argR2)]
```

and that instruction encodes the offset in **twelve bits**. When `SILENT` set
`LOG_BLOCK` to 1024 the log buffer in front of the field grew to eight
kilobytes, `argR2` moved to offset 9404, and the assembler-by-hand truncated it
to 1212 -- so the write went 8192 bytes short, landing inside the log buffer,
where in a silent build nothing ever reads it. No damage: builds 32 and 33 are
unaffected, and the only casualty was a field being read back as zero. But it
is the fourth self-inflicted instrument bug in this file and the first one that
could have corrupted state rather than only lying.

The thunk carries the field's own address as a literal now, which cannot be
truncated, and the silent build's buffer is a quarter of what it was. None of
the other thunks index the context; they all use literals already.

**Build 34** is still silent, and carries:

- The box every **eight** traced events rather than sixteen: about twenty
  writes on a run of the length the phone manages, against the eighty-six the
  rebooting builds were doing. It pins where it stopped to within eight events.
- **The last file opened**, in the box. It rides the write the box was making
  anyway, and it turns "it stopped at a read" into "it stopped on this file".
  The emulator's says `...s\6rbc\cwivenc.d`.
- **What `User::SetExceptionHandler` returned.** KERN-EXEC 3 is the kernel's
  panic for an exception nothing handled, so our handler is not running and our
  own category never gets its chance. The emulator answers KErrNone; if the
  phone answers anything else, that is why, and a working handler would give
  the faulting address outright.
- A log again, by accident and worth keeping: a 256-record buffer fills once in
  a run of this length, so one extra write buys the first 256 records while the
  box holds the tail.

## It is the log file, not the number of writes

> *Superseded: the log was never the cause. See "The reboots were the handle".*

Build 34 rebooted the phone at eighty-eight traced events. It was supposed to
be the cheap build.

```
build  writes  what it wrote     traced events  outcome
18/24/30  86-91  log + box           106-118     reboot
28       129     log, every record        86     reboot
32         4     box only               128+     KERN-EXEC 3
33        10     box only               128+     KERN-EXEC 3
34        12     box + one log flush   88-95     reboot
```

The count does not order these: eighty-six writes got further than twelve. But
**every build that wrote the log rebooted, and the two that wrote only the box
did not.** Six runs, no exceptions.

The difference between the two files is not how often they are written but
*how*. The box is one fixed record rewritten at position zero -- it extends the
file exactly once, when it is created, and never again. The log appends: every
block goes to a new offset and the file grows. On a phone that means the file
server updating the FAT on the internal drive, over and over, from inside a
startup sequence that never yields. The box does none of that.

So the rule is not a budget any more, it is a shape: **nothing this build
writes may extend a file.** Build 35 writes only the box, ten times, and the
history that the log was there to provide comes from the ring instead -- which
is free, because one write carries it whatever its depth. It is a hundred and
twenty-eight events deep now, which covers the whole of a run the phone gets
through.

Build 34 did buy two things. `User::SetExceptionHandler` returns **KErrNone on
the phone**, so the handler is installed and the exception still is not reaching
it -- the reason KERN-EXEC 3 shows instead of our own category is something
else, and worth one look later. And the last file opened is
`...s\6rbc\cwivenc.d`, the same as the emulator's.

## KERN-EXEC 0: an RFile closed as a plain handle

A three-run test settled this in one go. Delete the box and run: reboot. Leave
the box in place and run: **KERN-EXEC 0**, four to six times over, every time.

The only code that behaves differently when `g6box.dat` exists is the startup
read of the previous run's box, and in it:

```c
file_open(ctx->boxFile, ctx->boxFs, &name, 1);
...
rhandle_close(ctx->boxFile);        // RHandleBase::Close
```

On 9.x an `RFile` is an `RSubSessionBase`, whose first member is the
`RSessionBase` it belongs to. So the first word of an `RFile` is **the file
server session's handle**, and closing an RFile as a plain handle closes the
session out from under everything that follows -- which is invalid-handle,
KERN-EXEC 0, on the next file operation. The emulator allows it and says
nothing, exactly as `CLAUDE.md` says it would.

There were two of these. The other, `rhandle_close(probe)`, runs on *every*
startup, on the sibling-`.cwa` probe, and is the same mistake. It now uses
`RFile::Close` (efsrv ordinal 300, imported for this).

The startup read is gone entirely rather than fixed. It existed to get a record
off a phone that had just rebooted, which the box now does on its own; it wrote
a second file that grows, which the previous section says nothing may do; and
it carried this bug. `g6box.txt` goes with it.

**What this does not explain is the reboot.** When the box is absent the read
never runs, and that is the run that rebooted. Two separate faults were being
read as one, and every second run has been polluted by this since the box was
introduced.

*Still open, noted rather than changed:* the game closes its own files through
import 99, which `gen_shim` names `RFsBase::Close` and which we answer with
`RHandleBase::Close`. It is called on `object + 4` at 0x10a7c8 -- the same
address the read helper treats as the `RFile` -- so it is very likely the same
mistake in the game's own path. The other caller, 0x34ac4, closes `r4 + 0xa0`
and then touches `r4 + 0xa8`, and which of those is the RFile is not clear from
the code. The current mapping reaches a hundred and twenty-eight events, so it
stays until there is a reason beyond suspicion.

## The reboots were the handle, and the log was never the problem

Build 36 went out with the wrong binary. `ref.sh` built the reference with
`LOG_ANYWAY` on, restored the *source* afterwards and left that build sitting in
`out/`, which is where the `.sis` was copied from. So the phone got a build
writing the log at a block of eight -- the shape that had rebooted it every
time for a month.

It did not reboot. Three runs, no reboot, and further than any silent build:

```
run 1 (deleted first)   KERN-EXEC 3   131 traced events
run 2 (deleted first)   KERN-EXEC 3   131 traced events
run 3 (files left)      KERN-EXEC 3, and sometimes 0 alongside it
```

So the previous section is wrong. It is not that the log extends a file and the
box does not. **It was the two RFiles closed as plain handles**, which closed
the file server session; everything written afterwards went through a dead one,
and the more a build wrote the worse that got. The correlation with the log was
real and the cause was not. Fix the close and the log is free.

That is the fifth instrument bug and the first that was hiding a real one --
every reboot for a month was this, and each of the theories built on top of it
(a write ceiling, a write rate, extending writes) was fitted to its shadow.

*The process failure is its own lesson.* A script that builds one configuration
and ships another is a trap that goes off silently, and this one went off in the
user's hand. `ref.sh` builds and leaves the shipped configuration now; there is
only one binary.

### Where it stops

The run ends one event short of the emulator's, and the last records are ours,
from immediately before the call:

```
-- about to call import 109       RFile::Open
--   asked for  007c506c          the RFile
--   asked for  007c5068          the RFs, four bytes below it
--     text     ...ystem\apps\6rbc\cwivenc.d
                                  <- and nothing further
```

The name is read correctly by our own code, so the descriptor is sound. The
open of `cwivenc.dat` either never returns or the fault is on the instruction
after it. The emulator, at the same event, opens it and gets KErrNone.

The user also reports **a black bar with pixels in it, in every run**, and one
to two seconds before the panic. Something is being drawn.

Build 37 keeps the log, and puts a result thunk on `RFile::Open` -- wrapped
*inside* the trace thunk, so the trace still records the game's own return
address rather than ours, which is the alignment against the emulator for the
one import being asked about. Whatever it returns, or the absence of any record
at all, answers this.

## RFile::Open succeeds; the fault is in the free after it

Build 37 answered its question in one record:

```
import 109  RFile::Open  from 10a314
--   by import  6d
-- returned     0                 <- KErrNone
```

The open succeeds. The game's next two instructions are

```
0010a314  mov r0, r4        @ keep the result
0010a318  mov r0, r5
0010a31c  bl  #0x118e68     @ __builtin_delete -- the name buffer
```

and that is where the phone stops. `__builtin_delete` maps to
`scppnwdl::_ZdlPv`, which is correct, and the buffer came from `HBufC16::New`
on the same heap, so the free itself is right. **A free only faults on a heap
that is already wrong**, which means the damage was done earlier and this is
merely where it surfaces -- and the emulator, whose heap is a flat region that
forgives almost anything, sails through.

So build 38 matches every free against the allocations still outstanding. The
ring is 256 deep and tracks `__builtin_new`, the three `User::Alloc` variants
and `HBufC16::New`; every free marks its cell spent, from the first one, so a
double free cannot read as an ordinary match. Records are rationed -- plain
matches only over the stretch the run dies in -- but a **double free** or a
**stray** (a pointer never handed out) is reported wherever it happens.

The emulator's baseline: sixty-nine frees, every one matched to a live cell,
no doubles and no strays. If the phone shows either, that is the corruption,
and it will name the pointer.

*Also worth recording*: four panics in that single run, two KERN-EXEC 0 and two
KERN-EXEC 3. One thread cannot panic four times, so more than the game's thread
is going down -- which fits a heap the file server is also writing into.

## Build 38 regressed, and was reverted rather than explained

> *Superseded: build 38 was not a regression; that was one run. See "Three runs of one build".*

Build 38 -- the free-matching one -- took the phone from 132 traced events to
**nine**, dying in the framework's own startup with the box never written past
arming. The emulator ran it to the usual 170 and the usual fault, so there is
nothing local to bisect against.

It changed four things at once: a 256-deep allocation ring in place of a
32-deep one, `arg_thunk` on three free imports, `__builtin_new` added to the
result-wrapped allocators, and the free bookkeeping itself. Any of them could
be it, and finding out costs a hardware round per guess.

So it is reverted to build 37 whole. **A change that breaks something and
cannot be bisected locally is not worth keeping while it is unexplained**, and
four changes in one build is how a round gets wasted -- the same lesson as
build 34, which broke the write budget, and build 36, which shipped the wrong
binary. One variable.

### A search in time instead

The question build 38 was asking was *which* free. The better question is
*when* the heap went bad, because that brackets the write that did it without
needing to identify the victim.

`User::CountAllocCells` walks the whole heap cell by cell. On an intact heap it
returns a count; on a broken one it walks into the damage. Build 39 calls it
every four traced events and keeps the last event at which it came back in the
box, with the cell count. However the run ends, the box then says when the heap
was last whole -- and the emulator, for comparison, walks clean the whole way:

```
heap last walked clean at event 160, 803 cells
```

That is one import and one call every four events against build 37, which is
the smallest delta that can answer anything.

## Build 39's instrument worked and could not say so

> *Partly superseded: the walk never ran at all -- the run never reached event sixteen.*

Build 37, reinstalled unchanged, reached 132 traced events again with a single
KERN-EXEC 3. So the phone had not changed and builds 38 and 39 really did
regress -- and build 39 differs from 37 by one import and one call, which is as
clean a one-variable result as this project has had.

But "regressed" is the wrong word for what build 39 probably did.
`User::CountAllocCells` walks the heap and faults on a broken one; the run died
at traced event ten, and the box only wrote every sixteen, so **the walk's
verdict never reached the disk**. An instrument that detects the thing it was
built for and then dies before it can report reads exactly like a regression.

Build 40 fixes the reporting rather than the walk. The box goes down *before*
each walk carrying "begun at event N", and again after it carrying "came back
at event N". A walk that faults leaves the two disagreeing; one that returns
leaves them equal. The emulator now reads

```
heap walk: begun at event 160, last came back at event 160, 803 cells
```

with no false positive -- the first attempt flushed only before the walk, and
the emulator's own unrelated fault then left the two fields apart and the flag
lit for the wrong reason.

The interval is sixteen rather than four, so eight walks on a run of this
length is sixteen box writes against the forty-odd the log already does. That
also disambiguates the two readings of build 39: if the run reaches 132 again,
walking every four events was itself the perturbation; if it stops early with
the two fields disagreeing, the heap is broken by then and we have the bracket.

*The ordinal question stays open.* `user_countalloccells` was taken from a
`kernelhwsrv` def file, not from the 5320's own `euser.dll`, and euser ordinals
are not guaranteed identical across 9.1 to 9.4. It works against the RM-409 ROM
in the emulator, which is the same firmware family, so it is probably right --
but "probably" is how `SetKeyBlockMode` got keyed to the wrong ordinal months
ago, and the device's export table is sitting in `z/rm-409/sys/bin/euser.dll`
if this needs settling.

## An assumption that was never tested: that a run is repeatable

> *Partly superseded: the variance is real but does not explain builds 38-43, which fail deterministically.*

Build 40's box says the heap walk **never ran** -- it fires every sixteen
traced events and the run reached eleven. So neither the call nor the import
can be what stopped it, and the same is true of build 39.

Lined up against build 37's run, build 40 is **identical for sixty records**
and then takes a different turn:

```
 59  -- entered appui slot 4      | -- entered appui slot 4
 60  -- entered control slot 3    | -- entered appui slot 8        <<<
 61  -- entered appui slot 7      | -- entered control slot 26
 62  -- entered appui slot 4      | import 305  UserSvr::DllTls
 63  -- entered control slot 29   | import 279  CActive::Cancel
 64  -- entered control slot 41   | end
```

The framework calls a different slot, the game cancels an active object, and
the run ends. Slot order is the one thing in this record already known to vary
-- `readlog --no-slots` exists because two feature packs do not agree on it --
so this may be no difference at all.

Which exposes the assumption underneath five builds of reasoning: **that one
run of a build is that build's behaviour.** Builds 38, 39 and 40 each stopped
at nine to eleven events and each was read as a regression caused by whatever
it had changed. Build 37 reached 131 and 132 twice. Three against two is not
enough to tell a real regression from a coin landing the same way three times,
and every conclusion drawn from those three builds rests on it.

So the next round is not a new build. It is **build 40 again, three times**,
with nothing changed. If it reaches 132 even once, the last three builds were
never regressions and the heap walk is still an open instrument. If it stops at
eleven every time, the difference is real and worth bisecting properly.

It costs no install and it tests something that should have been tested before
the first "regression" was declared.

## Three runs of one build: 128, early, early

```
build 40, run 1   128 traced events, heap walked clean, 1209 cells
build 40, run 2   stopped before the first box write
build 40, run 3   stopped before the first box write
```

Same binary, three runs, one of them as far as build 37 ever got. So:

**Builds 38, 39 and 40 were never regressions.** Each stopped early once, each
was read as broken by whatever it had changed, and two working instruments were
reverted on the strength of a coin landing the same way three times. The
reasoning in the three sections above is wrong wherever it treats a single run
as a build's behaviour.

**The heap is intact.** At traced event 128 -- four events before the free that
faults -- `CountAllocCells` walks the whole heap and counts 1209 cells. The
chain is sound. So the free is not faulting because the heap is structurally
broken, and the theory the last two builds were built on is dead.

That is worth more than it cost. It narrows the free to the **pointer**: one
that was never handed out, or one already freed. Which is precisely what build
38's free matching was built to find, and build 38 works. Both instruments ship
together now.

### The rule this earns

**A hardware round is three runs, not one.** Every count in this file taken
from a single run is an upper bound on nothing: the same build reaches 128 or
stops at eleven depending on something none of these instruments see. Compare
the longest of three, and never call a build a regression on one run again.

Some of what is written above will have to be re-read with that in mind -- the
write-budget table in particular, whose rows are single runs.

## Sixty-four records, five times: the log was dying, not the game

> *Superseded: sixty-four records is eight of our own log blocks, not a sector.*

Build 41, three runs, all three stopping at nine to eleven traced events. And
every short run in this record -- builds 38, 39, 40 and all three of 41 --
stopped at **exactly sixty-four records**.

Sixty-four records is 512 bytes. One sector. A number that round is not a game
crashing; it is a file that stopped growing.

`log_block` never looked at what `file_write_at` returned, and it advances
`logPos` whether or not the write landed. So a file server that starts refusing
leaves the log at 512 bytes, every later write goes to a higher offset and also
fails, and the run carries on with no record at all. The box stops at the same
moment for the same reason -- which is exactly why those runs read as "ended at
event nine" and were written up three times as regressions.

**So the short runs were never short.** They are runs where the instrument went
deaf early, and the game very likely carried on to the same place it always
does. The KERN-EXEC 0 the user reports on those runs is the natural end of that
story: writes on a session that is no longer good.

That also retires the "build 41 is 0 for 3" reading from the section above, and
the dose-response it seemed to show across builds 37, 40 and 41. There is no
gradient; there is a coin, and what it decides is whether the *log* survives,
not how far the game gets.

Build 42 stops discarding the error. The first failed write panics with its own
category -- `G6WR` for the log, `G6BW` for the box -- carrying the file
server's error code as the reason. The run is already over as far as the record
goes; this way the phone puts the reason on its own screen, and a number that
the user can read off is worth more than a file that is not there.

## No write error, so the bisect is real

Five runs of build 42: sixty-four records and nine traced events, every one.
And **no `G6WR` or `G6BW` panic** -- so `file_write_at` is not failing, the
512-byte theory is wrong, and the run genuinely ends there.

That also kills the coin. Five identical runs is not a coin; builds 38 to 42
stop here reliably, and build 37 does not, twice. The one long run of build 40
is the outlier. So there is a real difference between 37 and everything after
it, and it has to be bisected rather than reasoned about -- three sections of
this file are what reasoning about it produced.

```
build 37   28600 bytes of code, 145 relocations, 37 imports   reaches 132
build 42   25560 bytes of code, 158 relocations, 38 imports   stops at 9
```

The code got *smaller* while gaining features, which is odd enough to check:
the diff is 113 insertions against 9 deletions, all of them intended, and
`gate4_shim.cpp` is untouched since long before either. Nothing was lost in the
merge that built 41. Left as a compiler artefact, noted in case it turns out
not to be.

**Build 43 is build 37 plus the free matching, and nothing else.** The heap
walk is gone and so is `user_countalloccells` -- the one thing builds 39
through 42 all carried and 37 did not, and the one whose ordinal was taken from
a def file rather than from the N95. Build 38 failed without it, but that was a
single run, and single runs are what this whole detour was made of.

If 43 runs the distance, the import was the difference and the free matching is
finally in hand. If it stops at nine, the free matching is the difference and
the bisect continues into it. Either way the next answer is one variable wide.

## The sector was a block boundary of our own

Build 43 -- build 37 plus the free matching, with the heap walk and its import
removed -- stops at sixty-four records too, both runs. So the import was not
it, and the free matching is where the bisect goes next.

But first, two things that were wrong.

**Sixty-four records is eight blocks of eight.** `LOG_BLOCK` is 8; the log
flushes on block boundaries; so a file of exactly 512 bytes means the run died
somewhere in records 64 to 71 with the last flush at 64. It is a boundary of
our own making, and reading it as a disk sector produced a whole section above
about extending writes and the file server. The `G6WR` panic that never fired
had already said as much.

**The 3040-byte code shrink is not damage.** `gate6_arg` went from 0x808 to
0x49c while *gaining* code, and so did every other function that touches the
context -- `gate6_library_lookup`, `gate6_result`, `gate6_write_memory`,
`gate6_cancel`. All of them shrinking together is the compiler re-optimising
around a different `Context` layout, not code going missing. The remaining
hand-assembled offsets were checked: `arg_thunk` was the only one that indexed
the context with a twelve-bit immediate, and it uses a literal now.

### What build 44 carries

Both build 43 runs are identical through record 62 and then differ by one slot
from build 37 -- `appui slot 8` where 37 has `control slot 29` -- and die
within the next eight records, which the block hides.

- **Every record written on its own for the first 768 bytes.** Ninety-six
  records at exact resolution, then back to blocks. The true last record goes
  on disk instead of the last multiple of eight.
- **The setup's own state, in a box write taken before the game runs.** Spare
  arena remaining, `sizeof(Context)`, and a bitmask of which optional wraps
  installed. Every failing run still produces a box, so this is the one record
  guaranteed to come back -- and a thunk silently skipped for want of arena
  would look exactly like the game dying. The emulator reads 20612 bytes spare,
  a 3768-byte context, and all four wraps installed.

## The furthest run yet, and it dies inside a free

Build 44: **136 traced events**, four past the previous best and well past the
64 that four builds in a row had been read as. So builds 38 to 43 were not
failing where they appeared to -- the log's block boundary was hiding the tail,
exactly as the section above worked out.

The free matching ran, and it clears the pointer:

```
31 frees, 31 matched a live cell, 0 double frees, 0 strays
```

The setup state matches the emulator exactly -- 20612 bytes of spare arena,
a 3768-byte context, all four optional wraps installed -- so nothing is being
silently skipped on the phone that is present here.

And the last three records are:

```
731  -- about to call import 0x13b      User::Free
732  --   asked for  007b7cd8
     (nothing further)
```

**It dies inside a free of 0x7b7cd8**, and whether the ring knew that pointer
is the one thing not on disk, because the exact-logging window was placed over
the first 96 records -- where the failure was wrongly believed to be.

So build 45 writes the pointer and flushes it *before* anything is done with
it. Thirty-one frees in a run can each afford a write of their own, and this is
the one place the record has to survive the thing it is recording.

Two facts now stand together and constrain what is left: at event 128 the heap
walks clean with 1209 cells, and every free up to the fatal one matches a live
cell. The heap is sound and the pointers are sound -- so either the fatal
pointer is the first bad one, or `User::Free` is faulting for a reason that is
not the cell it was given.

## Build 48: the neighbouring cell, and a bound that was not a bound

Every property of the fatal free that belongs to the cell itself now measures
correct -- the allocated chain walks clean, the ring recognises the pointer,
the requested size is 27 bytes and the header in front of it reads `0x28`. What
that leaves is the part of a free that is about somebody else's memory.
`RHeap::Free` coalesces: it reads the header of the cell *after* the one being
freed to decide whether the two can merge. A damaged neighbour would give
exactly the run we have -- a clean walk, a good pointer, a good header, and a
fault inside `User::Free`.

So build 48 reads the neighbour. The cell's own header gives its length, so
the next header sits at `payload - 4 + length`, and the record carries that
address, the word at it, and what the allocation ring makes of it.

### The first attempt walked off the end of the heap

The read was first guarded by a `heapTop` -- the highest address any recorded
allocation had reached, `result + arg` maintained in `gate6_result`. It did not
work. The emulator's fault moved from `0x30002` (its normal ending, 1066
records) to `0x9B0000` at 768 records: an unmapped high address, which is what
an instrument reading past the end of the thing it is measuring looks like.

The reason is worth keeping, because the ring is used for more than this.
**`gate6_result` records every non-zero result into the allocation ring, and
the ring is wrapped around `RFile::Open` as well as the five allocators.** A
failed open returns `KErrNotFound`, so `allocPtr` gets `0xFFFFFFFF` and
`heapTop` gets `0xFFFFFFFF + arg`. One failed open -- and the game's file-exists
switch performs several -- and the bound is the whole address space. The bound
was computed from a ring that does not only contain allocations.

### What is safe, and how it was checked

The neighbour is dereferenced only when the ring vouches for it the same way it
vouched for the cell being freed: some entry's payload has to start one word
past the candidate header. Nothing else is touched. A neighbour the ring does
not know is reported as an address and left alone, which is itself an answer --
it means a free cell, or one older than 256 allocations.

Two emulator runs separate the instrument from the effect:

| | records | fault |
|---|---|---|
| read disabled | 1066 | `0x30002` |
| three extra records per free, **no dereference** | 1258 | `0x30002` |
| ring-vouched dereference | 1236 | `0x30002` |

The middle row is the one that matters. Extra log volume does not move the
emulator; only the dereference did. That is the check rule 4 asks for -- the
`0x9B0000` reading was not assumed to be the off-heap read just because the
theory said so.

The records it produces here look like this:

```
free                     from 8b89e0
  matched a live cell of from 24
  cell header word       from 28
  cell header word       from 0
    next cell            from 8b8a04      <- the neighbour's header address
    next cell            from 28          <- the word in it
    next cell            from ffffffff    <- SPENT: the ring freed it already
```

The write count is unchanged: the new records go inside the block the verdict
already flushed, and `log_block` is called exactly as often as in build 47.

## Build 49: stop measuring the cell and turn the frees off

Rounds 44 to 48 each closed one description of the damage and found none.
Chain, pointer, size, header, neighbour: every measurable property of the fatal
free is correct, and the neighbour corroborates the header from a record that
does not pass through our arithmetic. There is nothing left to measure about
that cell, and a sixth round describing it harder is the mistake this file
already has five entries for.

So build 49 intervenes instead. The three deallocation ordinals -- 315
(`User::Free`), 408 (`operator delete`) and 410 (`operator delete[]`) -- are
answered by a function that does nothing. `LEAK_EVERYTHING` has been in the
source since the use-after-free theory and has never been shipped.

It splits the question either way:

- the run **advances** -- the fault is in the free, and the port has moved past
  a wall it has been at since build 44;
- the run **dies in the same place with nothing freed** -- then the fault was
  never in `User::Free`. The free is merely the last thing we write before it,
  and everything between that record and the next traced import has been
  wearing the blame.

### The switch had to move before the instrument

`LEAK_EVERYTHING` was implemented late in the setup, after the free watcher and
the trace loop had already wrapped those IAT entries -- so turning it on
*overwrote* both wrappers and turned off every record of a free at the same
time. That is two variables in one build, and the round would not have been
comparable with 48. It is installed before the watcher now: the no-op is what
gets wrapped, the same records come out naming the same pointers, and the only
difference on the wire is that nothing is freed. A `LEAK` flag in the box says
so out loud.

### What the emulator says first

| | imports reached | ending |
|---|---|---|
| build 48 | 289 | `0x30002`, at a free of `8b8a58` |
| build 49 | 275 | `0xEAF88580`, at a free of `b32248` |

The leak is demonstrably in effect: in build 48 the emulator freed `8b89e0`
ten times over and `8b8a58` eight times, because the heap kept handing the
same addresses back. In build 49 **every freed pointer in the run is unique**,
which is only true if nothing is being returned to the heap.

And it does not get past the wall. The emulator still ends at a free -- of a
27-byte cell with a `0x28` header, the same signature as the phone's -- with no
free having actually happened. That is a prediction for the hardware round
rather than an answer, because the emulator is not a reference and dies of a
different fault. But it is the way to bet.

## Round 49: the free was never the wall

Three runs, 7592-byte logs to the byte, and the box carries `LEAK: nothing is
freed`. Every one of the 99 freed pointers in the run is unique -- build 48
freed `7b7c10` three times over, because the heap kept handing it back -- so
nothing went to the heap, and the run stops in exactly the same place:

| | build 48 | build 49 |
|---|---|---|
| traced events at the last box write | 128 | 128 |
| last import in the box | `RLibrary::Close` | `RLibrary::Close` |
| stack high-water | 1932 | 1932 |
| imports in the log | 248 | 248 |
| frees / verdicts | 99 / 32 | 99 / 32 |
| stops at | the 99th free | the 99th free |

`User::Free` never ran, and the wall did not move. It is the last record before
the fault because it is the last thing we write, not because it is what faults.
Everything between that record and the next traced import -- the game's own
code, after `delete` returns -- is what five rounds of heap forensics were
standing in front of.

One number falls out of it. The fatal cell's header reads `0x20` here,
`align8(27 + 4)`, where build 48 read `0x28` on the same free. That was reuse,
not damage: RHeap hands over a whole recycled cell rather than split off a
remainder too small to be a cell. The build-48 entry warned the request column
could not tell those apart; the leak removes reuse and the header snaps to the
arithmetic.

### What it costs to have taken five rounds over this

Rounds 44 to 48 were each a sound measurement, and each one came back clean.
The rule they were missing is not about heaps: **when every measurement of a
suspect comes back clean, the cheapest next move is to remove the suspect, not
to measure it more precisely.** `LEAK_EVERYTHING` had been sitting in the
source, unshipped, the whole time.

## Build 50: the log going quiet is not the game dying

Round 49's `lr` record names the fatal `delete`: image offset **0xcc8c0**,
returning to 0xcc8c4. The code there:

```
000cc8b8  cmp    r7, #0
000cc8bc  movne  r0, r5
000cc8c0  blne   #0xd5a0c        <- the delete, 99th of the run
000cc8c4  mov    r7, r8
000cc8c8..e0     (a constant-multiply chain on r4)
000cc8e4  cmp    r4, #0
000cc8e8  beq    #0xcca88
000cc8ec  ldr    r0, [r6, #4]
000cc8f0  mov    r1, r7
000cc8f4  mov    r2, #1
000cc8f8  bl     #0x10a9e0
...
000cc908  mov    r0, #4
000cc90c  bl     #0xd5a08        <- User::Alloc(4)
000cc914  str    r5, [r5]
```

**Not one instruction in that stretch calls a traced import.** The allocation at
0xcc90c is wrapped, but `gate6_result` logs allocations only when they *fail*.
So the log goes quiet after the delete whether the run survives the next
hundred instructions or not, and "it dies in the free" -- and then "it stops at
the 99th free" -- were both reading the end of the recording as the end of the
run.

The emulator proves it outright. Six probes planted along that stretch, one
run:

```
marker 990 at 0x000cc8c4   r6 = 8b2710     straight after the delete
marker 991 at 0x000cc8ec   r6 = 8b2710     the first load after it
marker 992 at 0x0010a9e4   r0 = eaf88340   entered the call it makes
```

The run gets past the delete every time. What it does not get past is what
`[r6 + 4]` hands it: **0xeaf88340**, which is an ARM branch word, not an
object. 0x10a9e0 allocates 0x24 bytes and calls 0x139568 with that value, and
0x139568 dereferences it -- the emulator's fault address, `0xEAF88580`, is that
garbage plus 0x240.

Two things follow. The near one: `[r6 + 4]` is uninitialised or stale, which is
the same shape as the container at 0xcc914 that this file already describes --
a field holding what was in the memory before, read as a pointer. The far one:
**0x139568 is where EKA2L1 cannot run the original N-Gage binary either**
(KERN-EXEC 3 at 0x139588). Past this point the emulator may be measuring
itself.

So build 50 ships the probes. It is build 49 plus six markers, and it answers
the question the log cannot: how far past the delete does the *phone* get. In
the emulator they fire three times in a run and cost ten records each, so the
write budget is unchanged in kind.

The constant-multiply chains on r4 either side of the delete are obfuscation,
not arithmetic: the first multiplies by 3467093631 and the second by
4017970111, which are inverses mod 2^32. r4 comes out of the pair unchanged.

## Round 50: the fault, at last, and it is an old acquaintance

Three runs, identical. Markers 990, 991 and 992 fire; 993 does not.

```
marker 990 at 0x000cc8c4   r6 = 7d68e8   r4 = cea7a67f
marker 991 at 0x000cc8ec   r6 = 7d68e8   r4 = 1
marker 992 at 0x0010a9e4   r0 = a6dfb180
```

`r4` is `0xcea7a67f` before the second obfuscation multiply and `1` after it,
which is the pair of inverse constants doing nothing, as expected. `r6` is
stable. What is not stable, and not a pointer, is what `[r6 + 4]` hands over.

`0x10a9e0` takes it, allocates 0x24 bytes and calls `0x139568` with it:

```
00139568  push  {r4-r8, sb, sl, lr}
0013956c  sub   sp, sp, #0x1c
00139570  mov   r6, r0
00139574  ldr   r3, [pc, #0x118]
00139578  str   r3, [r6]              <- a vtable: this is a constructor
0013957c  mov   r4, #0
00139580  str   r4, [r6, #8]
00139584  str   r4, [r6, #0xc]
00139588  ldr   r2, [r1, #0x240]      <- and here it dies
```

`r1 = 0xa6dfb180`, so it reads `0xa6dfb3c0`. In the emulator `r1` is
`0xeaf88340` and the fault address is `0xEAF88580` -- the number the emulator
has been printing for weeks, which is that same instruction, arrived at the
same way.

The object at `r6`:

```
[r6+00] 913458     [r6+10] 0
[r6+04] a6dfb180   <- the only nonsense in it
[r6+08] 7d76d0     [r6+18] 0
[r6+0c] 4800000    [r6+1c] 0
```

Three plausible heap pointers, one large mapped address, four zeros, and one
field holding different garbage on the phone than in the emulator. That is what
a field nobody wrote looks like.

### What this costs the emulator as an alibi

`0x139588` is where **EKA2L1 cannot run the original N-Gage binary** -- KERN-EXEC
3, written into this file long ago and filed under "the emulator is not a
reference". It was not the emulator. Our port reaches the same instruction with
the same kind of value in the same register, on a real N95.

Three runs of three different things fail identically:

| | |
|---|---|
| the original binary, under EKA2L1 | KERN-EXEC 3 at `0x139588` |
| our port, under EKA2L1 | fault at `0xEAF88580` = `r1 + 0x240` |
| our port, on an N95 | fault at `r1 + 0x240`, `r1 = 0xa6dfb180` |

The original game runs on a real N-Gage. So something that fills `[r6 + 4]` on
that device fills it on none of these three, and that is the port's actual
problem. It has been sitting in this file, mislabelled, since before the month
of reboots.

**Next: who is `r6`, and what is supposed to write its fifth word.** That is a
static question about the image, so it costs no hardware round.

## Where `this->[4]` goes bad: one call, and no hardware round spent

Round 50 put the fault on a pointer read out of `this->[4]`. Three probes
across the function that owns it, in the emulator, narrow the damage to a
single instruction:

```
000cc864  add r0, r5, #0x28        marker: this->[4] = 8d8ee8   a heap pointer
000cc86c  bl  #0xe97cc
000cc870  bl  #0xd5fbc
000cc874  add r3, r5, #4           marker: this->[4] = 8d8ee8   still fine
000cc884  ldr r0, [r4, #4]
000cc888  mov r1, r5
000cc88c  mov r2, #1
000cc890  bl  #0xe9988             <- r3 is &this->[4], its fourth argument
000cc894  mov r4, r0               marker: this->[4] = eaf88340  garbage
```

**`0xe9988` is handed `&this->[4]` as an out-parameter, runs for three hundred
records, returns 1, and leaves nonsense in it.** The field is a good heap
pointer on either side of `0xe97cc` and `0xd5fbc` and only changes across that
one call.

Two details fall out of the same run. `0xd5fbc` returns `0xb32248` -- which is
the pointer the "fatal" delete at 0xcc8c0 frees, so that temporary is created
and destroyed inside this sequence and was never anything to do with the fault.
And `r6`'s other seven words are unremarkable on both machines: three heap
pointers, one large mapped address, four zeros. Only the fifth word is wrong.

`0xe9988` sits a hundred and ninety-six bytes after `0xe98c4`, the `TickCount`
stopwatch with `Math::Random` mixed into its result that this file records as
one of the game's protection paths. That is a neighbourhood, not evidence, and
the next step is inside `0xe9988` -- which is more emulator bisection, not a
round on the phone.

**No build shipped for this.** The emulator and the phone have now agreed for
three rounds running, the question is where inside one function a value goes
bad, and that is answerable for free.

## Instrumenting a function that will not be instrumented

The next question was where inside `0xe9988` the field goes bad, and the
obvious way to ask it -- probes along the function -- does not work.

**One probe at its third instruction takes the run from 1240 records to 553**,
and the run dies before even the marker at `0xcc864` outside it is reached. Ten
probes did the same. The site is `str r0, [sp, #0x30]`, an ordinary
instruction; `probe_plant` saves and restores CPSR around its call, so this is
not a clobbered flag. A single patched word in that function is enough to end
the run, which for a game carrying a decryptor, a `TickCount` stopwatch and
three other protection paths is a finding rather than an obstacle -- and a
standing constraint on every instrument from here.

### The watch: a station that patches nothing

So the field is watched instead of the function. The first probe latches the
object's address, and from then on **every wrapper we already own re-reads
`this->[4]` and writes one record**: `gate6_trace` on each traced import,
`gate6_result` on each allocation, `gate6_arg` on each free. No byte of the
game is touched to get one.

It does not perturb the run: 1292 records against 1240, the same
`0xEAF88580`.

What it shows:

```
1236  >> watched field   8d8ee8
1237  import 283  RLibrary::Close(void)   from 135850
1238  >> watched field   8d8ee8
1239  about to call import 198  (delete b32220, called from 0x13589c)
...
1249  >> watched field   8d8ee8      <- after an allocation returned
1250  >> watched field   eaf88340    <- before the next delete
1251  about to call import 13b  (delete c4fe50, called from 0x104828)
```

The field survives everything up to and including a successful allocation, and
is poisoned before the `delete` at image offset `0x104828`. That call sits in
one of `0xe9988`'s callees, in the game's obfuscated dispatch style -- constant
multiply chains, `b #0x1037ac`, and operand words inlined in the instruction
stream.

### Two negatives worth keeping

**The poisoned value is not a transform of the good one.** Solving
`0x8d8ee8 * C == 0xeaf88340 (mod 2^32)` gives `C = 0x17052F88`, nothing
resembling the obfuscation constants. The two machines do not even agree on
the shape: the value's 2-adic valuation is 6 in the emulator and 7 on the
phone, so it is not one function of one differently-based pointer.

**It is not `Math::Random` reaching the field directly, either.** The value is
deterministic per machine -- the same `0xEAF88580` from the emulator every run,
the same `0xa6dfb180` on three phone runs.

The phone's *pre*-call value is still unknown: round 50's probe set did not
include `0xcc864`. The current build does, so the next hardware round yields it
without being spent on it.

## The store that poisons the field, and what it is computing

Four emulator runs, E17 to E20, from "somewhere inside `0xe9988`" to one
instruction.

**E17 -- it is a deliberate store, not an overrun.** The watch was widened from
one word to four. Across the flip, word 1 changes and words 0, 2 and 3 do not:

```
before: 83dc3e00  8d8ee8    8b29d8  4900000
after : 83dc3e00  eaf88340  8b29d8  4900000
```

A copy that ran long, or an overrun from the cell in front, takes neighbours
with it. This takes exactly one word, which also clears our own shim of having
scribbled on it.

**E18 -- the other function in the window does tolerate a probe.** `0xe9988`
dies on a single patched word; `0x103774`, which is running when the field goes
bad, does not. That was the fork in the road, and it went the good way.

**E19 -- and it is not either of that function's two array stores.** The only
non-frame stores in it are the same instruction twice,
`str sl, [r5, ip, lsl #2]`. Both were probed; all fifty firings fill two
five-element arrays at `0x40f640` and `0x8d9144`, and not one of them addresses
`this+4`. The flip is bracketed between them, and the stretch between is
dispatcher jumps -- one call out, no stores.

**E20 -- so instrument the obfuscation's own choke point.** The function
dispatches through a jump table at `0x1037ac`
(`cmp r3, #0x16; ldrls pc, [pc, r3, lsl #2]`), reached by `b` from everywhere,
with the block's key loaded inline into `r0` just before the jump. A probe
there is a station at every block boundary without knowing the path -- and made
*quiet*, reporting only when the watched word has changed, it is silent until
the one boundary that matters. It fired **once**, with `r0 = 0xe649867c`.

That key appears twice in the image. One of the two blocks ending in it is
this:

```
00108294  mul  sb, sl, sb
00108298  mov  sl, r1
0010829c  mla  sl, sb, sl, r8        sl = sb*r1 + r8
001082a0  mla  r3, sl, r4, sl        r3 = sl*(r4 + 1)
001082a4  add  r3, r3, r3, lsl #2
001082a8  add  r3, sl, r3, lsl #10
001082ac  add  r3, sl, r3, lsl #3
001082b0  rsb  r3, sl, r3, lsl #4
001082b4  add  r3, r3, r3, lsl #3
001082b8  rsb  sl, sl, r3, lsl #7
001082bc  ldr  ip, [sp, #0xb8]       <- the out-pointer, off the frame
001082c0  str  sl, [ip]              <- the store
```

`ldr ip, [sp, #0xb8]; str sl, [ip]` is a store **through an out-parameter held
in a stack slot** -- which is what `&this->[4]` became after `0xe9988` passed it
down. One word, matching E17 exactly.

### What the value is, and why that matters

The arithmetic is not the obfuscation. Obfuscation in this image is
multiply-by-constant, built from shift-adds with an inverse chain later;
`mul sb, sl, sb` and `mla sl, sb, sl, r8` multiply two **runtime** values.
`mla r3, sl, r4, sl` is `sl * (r4 + 1)`, and the shift-add tail scales it
again.

That is the shape of `base + index * size`, not of a hash. So `this->[4]` is
meant to receive a **computed pointer into an array**, and it is coming out
wild because one of `sb`, `r1`, `r8` or `r4` is wrong. The field is not being
deliberately poisoned by anti-tamper; it is a pointer calculation with a bad
input.

Which also finally explains the shape of the value: deterministic per machine,
different between machines, never appearing anywhere earlier in the log, and
not a constant multiple of anything -- exactly what `a*b + c` over
machine-specific addresses produces.

### The caveat on E20, stated plainly

**That run perturbed.** The emulator's fault moved from `0xEAF88580` to
`0x1C976000`, and the field's garbage changed with it -- consistently, since
`0x1c975dc0 + 0x240` is the new fault, so the mechanism held. But a probe that
moves the outcome is a probe whose evidence has to be confirmed, and rule 5
says a measurement that agrees with the theory gets checked as hard as one that
does not. **The next run re-confirms `0x1082c0` without the dispatcher probe**,
by planting a single quiet probe there instead.

## The value, computed: every step now checked against a register

E20's site needed confirming, and E21 to E23 did that and then followed the
arithmetic up.

**E21 -- the store confirms itself.** A single probe on `0x1082c0`, filtered to
fire only when the register it reports *is* the watched field's address, with
no probe on the dispatcher. It fired once:

```
marker 992 at 0x001082c0   ip = 8b2764   sl = 1c975dc0
```

`this` is `0x8b2760` that run, so `ip` is `this+4` exactly, and the fault
address is `0x1c975dc0 + 0x240`. That is the instruction naming its own target,
which is a stronger statement than "the word changed near here".

**E22 -- `array[5]` is a decoy.** The block runs exactly once in the whole run,
so there was nothing to filter after all. At `0x108298`, `r1` holds `array[5]`
= `0x782efefd` and `sb` is **zero**, so `mla sl, sb, sl, r8` reduces to
`sl = r8`. The load two instructions earlier -- `mov r7, #5;
ldr r1, [r1, r7, lsl #2]` -- is multiplied away. Obfuscation, not content.

**E23 -- and the rest is arithmetic that checks out.** `r8 = 0x42d08240`, which
is exactly the candidate the tail inversion predicted for `r4 = 0`, and

```
755139455 * 0x42d08240 = 0x1c975dc0     (observed)
```

So the chain of custody is complete and every step is confirmed against an
observed register:

```
r6 = fb0896a0 , sb -> r8 = 42d08240 -> * 755139455 -> 1c975dc0
   -> str sl, [ip]  at 0x1082c0,  ip = this+4
   -> ldr r0, [r6, #4]  at 0xcc8ec
   -> ldr r2, [r1, #0x240]  at 0x139588  -> fault at 1c976000
```

**`r8` arrives already wrong.** De-obfuscating `r6` and `r8` with this block's
own constant (`483517375`, the identity chain with `r4 = 0`) gives `0x3AAC9160`
and `0x346B0DC0` -- not pointers either, so they came through a different
chain, or with a different `r4`. Where `r6` and the first `sb` come from is the
next question, and it is one level further up the same function.

### A caveat that has to be repeated

The fault has read `0x1C976000` since E20 rather than `0xEAF88580`. That is not
the dispatcher probe -- E21 removed it and the value stayed. It is the
`lastWatch` field added to `Context`, which moved every field after it and with
them the heap layout. **The mechanism is unchanged** (the fault is still the
stored value plus `0x240`, and the store is still `0x1082c0`), and the value
being layout-dependent is itself consistent with a pointer computed from
addresses. But the number is not comparable with anything before E20, and
nothing should be inferred from the change in it.

## The pre-call value was the answer all along

**E24 asked the one question that mattered.** `0x139588` dies on
`ldr r2, [r1, #0x240]`. So: read `[field + 0x240]` at every station, guarded so
the poisoned value is never dereferenced. Before the store, at all 83 stations:

```
>> watched field          8d8f38
>> would read at +0x240   8d9c60
```

A good heap pointer, stable throughout. **The field already held a valid
object, with exactly the word `0x139588` wants, and `0xe9988` overwrote it.**

**E25 acted on that.** The block containing the store runs exactly once in a
run (E22), so one word of the game's code -- `str sl, [ip]` at `0x1082c0` --
was replaced with `mov r0, r0`. The field keeps what it had.

```
                 imports   ends
before            248      fault at the stored value + 0x240
store NOPped      292      no access violation at all
```

The run goes on past everything it has ever reached: `RLibrary::Load` at
`0x13964c` and again at `0x13f588` -- sites never seen before -- `Lookup`,
`HBufC16::New`, a second pass of the `0xe98c4` stopwatch, more deletes. Then
`User::Leave` from `0x2b20`, and the app exits. The `G6MEM` panic after it
carries `1616972`, which is the byte size of `6rbc.app`: that is our *loader*
failing to allocate the image on a relaunch, not the game.

So the failure mode has changed completely, from a wild pointer dereference to
**the game's own error path**, taken in an orderly way. That is the first
movement at this wall since build 44.

### What this is and is not

It **is** a workaround. It does not explain why the game computes a wild
pointer at `0x1082c0`, only that the value it clobbers was the correct one. The
honest reading is that `0xe9988` is meant to *find* something and hand it back,
finds nothing, and writes a computed miss where the caller expected the thing
it already had.

It **is** narrow: one word, in a block measured to execute once. It is not a
blanket patch of the game's code, and the region tolerates it where `0xe9988`
tolerates nothing.

And it **is** the first candidate fix this project has had rather than another
measurement. The next question is what `User::Leave` is complaining about --
but that is a question at a place the port has never stood before.

## Every library the game loads, and the one that does not

E26 wrapped `RLibrary::Load` on both sides: `arg_thunk` widened to keep `r1`
(the file name, the one register it used to throw away) and a `result_thunk`
for the return code.

| called from | name | result |
|---|---|---|
| `d5058`, `d589c` x3, `d516c`, `d54b8`, `13f588` x5, `10b08c` x2 | `euser.dll` | `KErrNone` |
| `13964c` x5 | `efsrv.dll` | `KErrNone` |
| `e815c` | `c:\system\cwdynlog.dll` | **`KErrNotFound`** |

So the game resolves euser and efsrv **by name at runtime** rather than through
its import table -- which is worth knowing on its own, because those lookups go
to the *phone's* ordinals, not to ours, and this file's standing worry about
ordinals across feature packs applies to every one of them.

The two Loads it reaches only after the wall came down, `0x13964c` and
`0x13f588`, both succeed. The only failure in the run is `cwdynlog.dll`, which
this file already records as a protection path and which is *supposed* to be
missing. **So a `Lookup` on an unopened handle is not where the KERN-EXEC 0
comes from**, and that theory is closed before it cost a round.

Widening `arg_thunk` moved every literal offset in it by four bytes. That exact
arithmetic went wrong once before -- the store landed in the log buffer instead
of the field, and cost a round -- so each offset is now spelled out beside the
instruction that uses it, and `ARG_WORDS` went from 16 to 20.

The emulator run now ends with **no fault at all**.

## The app is not run once. It is run over and over.

The phone raises two panics per run. Chasing that turned up something larger.

`file_replace` truncates, so every launch of the app was erasing the previous
launch's log and writing its own from position 0. Nothing recorded which launch
a log belonged to. So build 52 stamps it: the box carries a **launch counter**,
read out of the previous box before `file_replace` destroys it, and the log
goes to `C:\g6box<N>.log` -- one digit, patched at startup, so no launch can
overwrite another.

One 45-second emulator session:

```
g6box1.log .. g6box9.log     16776 bytes each, byte-identical
box: LAUNCH 12
     179 traced events, last import User::Leave
     flags: THE FRAME LOOP RAN, User::Leave, User::Exit
```

**Twelve launches.** Each one identical, each reaching 179 traced events --
further than any number in this file -- leaving cleanly and exiting, and being
started again. (The digit saturates at 9, so launches 10 and up share
`g6box9.log`; the counter in the box is the true one.)

So every log this project has ever read was whichever launch happened to run
last, and every count in this file was measured on an unknown member of a
series. That did not invalidate the findings -- the launches are identical --
but it was not known to be true, and it is exactly the kind of thing that has
cost this project rounds before.

Reading the previous box was done here once and removed, because it closed an
`RFile` with `RHandleBase::Close` and took the file server session down with
it -- the bug behind a month of reboots. This one closes with `file_close`,
efsrv 300, and touches the handle no other way.

### What it changes about the phone

The phone's "one KERN-EXEC 0 and one KERN-EXEC 3 per run" is now two
observations about a *series* of launches, not two failures in one. The first
launch's own record has never been seen. Build 52 is the first build that can
show it.

## This image has no writable statics, and now it cannot have any

Build 52 produced **no files at all** and panicked KERN-EXEC 3 on the phone.
That was mine, and the cause is worth more than the bug.

To give each launch its own log, build 52 patched one character of the file
name in place:

```c
static u16 kLogPath[] = {'C',':','\\','g','6','b','o','x','0','.','l','o','g'};
kLogPath[LOG_DIGIT] = '0' + launchNo;
```

**There is nowhere for that array to live.** `flat.ld` folded `.data*` into
`.rodata`, and `mke32.py` declares data size and bss both zero -- so every
static in this image is placed in the read-only code segment. The write faults
on the first launch, before a single file is created, which is exactly what the
phone showed.

The emulator ran twelve launches without a murmur, because it maps that memory
writable. This is the clearest case yet of the rule this file already carries:
**the emulator is not a reference.** It is also a case the emulator could never
have caught, however many runs it got.

### The guard

Rather than remember it, `flat.ld` now gives writable data a section of its own
instead of folding it away, and `buildapp.py` refuses to build if anything
lands there:

```
buildapp: 8 bytes of writable statics.
  This image has no data section -- they would land in the
  read-only code segment and fault on the first write.
  Make them const, or build the value on the stack.
```

Tested by reintroducing a writable static, which the build then refused, and
removing it again, which the build then accepted.

The fix itself is that the digit is patched into a **copy on the stack**. The
template stays `const`, and the descriptor points at the local for the one call
that uses it.

Build 53 is build 52 with that change and the guard. In the emulator it is
identical to 52 -- twelve launches, 2097 records, 179 traced events -- so
nothing about its behaviour moved.

## Build 54: back to one variable

Build 53 rebooted, and produced one launch where the emulator produces twelve.
A reboot has not happened since build 36 fixed the closed file-server handle.

Looking at what shipped, the fault is procedural before it is technical.
**Build 53 carried three changes against the last build known to survive.**
Build 51 ran three times on the phone without incident; between it and 53 the
loader gained the launch counter's `RFile` open/read/close, the
`RLibrary::Load` wrap, and a widened `arg_thunk` -- three commits, shipped as
one build. Rule 2 exists for exactly this, and I did not apply it.

So build 54 is build 51 plus **one** thing:

```
$ git diff <build 51> -- gate6.cpp
+ BOX_LAUNCH, BOX_TICK                 two box slots
+ launchNo                             one context word
- kLogPath[] = "C:\g6box.log"
+ kLogPath[] = "C:\g6box0.log"         one digit, patched into a stack copy
```

The `RLibrary::Load` wrap and the widened `arg_thunk` are reverted. So is the
part of the launch counter that **read the previous box**: open, read and close
an `RFile` at startup is the exact shape of the bug behind a month of reboots,
and although this one used `file_close` and may well be innocent, a suspect
that can be deleted instead of measured should be deleted. That is rule 3, and
it is the rule that broke the last wall.

The digit comes from `User::TickCount` instead. Nothing is opened and nothing
is read; two launches are milliseconds apart at worst and a tick is about
1/64 s, so they land on different digits. A collision costs one overwritten log
and nothing else. The tick goes in the box so the launches can be ordered
afterwards.

In the emulator: nine log files on digits 1 to 9, **1832 records each -- which
is exactly E25's count**, the build-51-era run. That is the check that build 54
really is build 51 plus the name and nothing else. 179 traced events, no fault,
and 265 records of write budget handed back.

## KERN-EXEC 0 is ours, and it has been all along

Round 54 showed each run is two launches: a full one of 1563 records, and one
that writes two records and dies without ever writing a box. That second launch
is where the KERN-EXEC 0 has been hiding for dozens of rounds -- it was never in
a log because every log it wrote was truncated by the next launch.

`log_block` has always guarded its handle:

```c
if (!c->logFill || !c->logFile[0])
    return;
```

**`box_write` never did.** If `file_replace` of the box fails, `boxFile` stays
zero, and the next box write is `RFile::Write` **on handle 0** -- which is
exactly KERN-EXEC 0, a bad handle. The guarded `box_flush` at startup is
skipped on a failed replace, so the first unguarded write is the one the app
framework triggers, right where the stub launch stops: two records in.

Build 55 gives `box_write` the guard `log_block` has had all along, zeroes the
handle on a failed replace so the guard holds, and says out loud what the
replace returned. It also writes the tick into the *log*, because the launch
that dies before writing a box carried no tick and round 54 therefore could not
say which of the pair ran first.

In the emulator: 1834 records against E29's 1832 -- the two new records and
nothing else. The emulator's box replace always succeeds, so **the guard itself
can only be tested on the phone.**

If the KERN-EXEC 0 goes away, it was ours, and the remaining failure is the
single KERN-EXEC 3 at the end of the full launch. If it does not, the stub dies
somewhere else in the loader and its log will now carry the tick to place it.

## Round 55: the ordering, and a half-applied fix

Two results, one of them settling a question that had been open since round 54.

**The full launch runs first.** Each run's stub now carries its own tick, and
against the full launch's box tick:

| run | full launch | stub | gap |
|---|---|---|---|
| 1 | 78064 | 78177 | +113 |
| 2 | 80520 | 80631 | +111 |
| 3 | 82699 | 82809 | +110 |

About 1.7 seconds, three times out of three. So the launch this project has
been measuring for fifty rounds **is** the first one, and the stub is what
happens after it panics. That worry can be closed.

**And the stub's box `file_replace` returns -6, `KErrArgument`** -- the same
value every run. Not `KErrInUse`, not `KErrAccessDenied`. Unexplained.

### The fix was half-applied

Build 55 put the handle guard inside `box_write`. `box_flush` calls
`box_write` **and then `file_flush` on the same handle**, and I left the second
one alone:

```c
static void box_flush(Context *c)
{
    box_write(c);            // guarded in build 55
    file_flush(c->boxFile);  // not guarded -- RFile::Flush on handle 0
}
```

So the stub went from two records to four and died in the same place. That is
what a half-applied fix looks like, and the shape of it is familiar: the guard
was added where the bug was *described*, not everywhere the handle is used.
Build 56 guards `box_flush` itself, which covers both calls.

`log_block` was checked at the same time and has always guarded correctly. The
only other unguarded writes are in the dump path, which is off.

## The 35-event gap: the phone gives up at a library lookup

Diffing the phone's first launch against the emulator's, by import sequence
rather than by address, puts the divergence on one instruction.

Both machines do exactly this:

```
CCoeEnv::Static
RLibrary::Load    from 13f588      (euser.dll)
RLibrary::Lookup  from 13f5e4      answered
RLibrary::Close   from 13f610
HBufC16::New      from 1909e4
RLibrary::Lookup  from 13f68c      answered
delete
```

and then:

| | next |
|---|---|
| emulator | `RLibrary::Lookup from 10abf8`, **twenty-six times** |
| phone | `RLibrary::Close from 135850` -- it gives up |

The game calls the function it has just looked up and branches on what comes
back. On the phone that branch goes the other way.

### What the lookups are

`gate6_lookup` has had the ordinal and its 9.x mapping in scope since it was
written and has never logged either -- a failed mapping went only to RDebug,
which the phone does not show. One `log_event` fixes that, and the emulator
answers immediately:

| caller | old ordinal | mapped to 9.x |
|---|---|---|
| `13f5e4` | 1114 | 595 |
| **`13f68c`** | **121** | **93** |
| `10abf8` (the burst, x26) | 136 | 255 |
| `10b128` | 355 | **0 -- nothing** |

So the branch turns on **old ordinal 121, answered by 9.x ordinal 93 of
euser.dll**, and the burst the phone never enters is old 136 resolved
twenty-six times.

### Why this is the ordinal warning coming due

This file has carried the same caution since it was written: the 9.x ordinals
in the mapping came from `kernelhwsrv` def files rather than from the device,
and they are a guess that happens to hold on FP2. The emulator is a 5320 (9.3);
the phone is an N95 (9.2).

**If euser's ordinal 93 is a different function on 9.2 than on 9.3, the game is
handed the wrong function, calls it, and branches on its answer** -- which is
precisely the shape of what the logs show. Nothing crashes; the game simply
decides not to proceed.

The addresses the lookups answer with cannot settle it, because an address on
one ROM means nothing against an address on another. **What settles it is the
N95's own euser.dll export table.** That is a one-off file pull, and the same
thing was done once before for `BitGdi.dll` and `Ws32.dll` when two
measurements were in doubt.

## The wrong table: euser ordinals answered out of efsrv

Round 57 put the phone's lookup ordinals beside the emulator's and they are
**identical** -- same old ordinals, same 9.x mappings, in the same order. So the
mapping table is not where the two machines differ, and the N95-versus-5320
ordinal theory is wrong.

What the logged ordinals *do* show is that the mappings themselves are nonsense,
on both machines:

| caller | old ordinal | name | mapped to | name |
|---|---|---|---|---|
| `13f5e4` | 1114 | `User::StringLength(const unsigned char*)` | 595 | `User::StringLength(const unsigned char*)` |
| `13f68c` | 121 | `CObjectIx::At(const CObject*) const` | 93 | **`TBufCBase8::TBufCBase8(TDesC8 const&, int)`** |
| `10abf8` **x26** | 136 | `RPointerArrayBase::BinarySearchUnsigned(unsigned, int&)` | 255 | **`CArrayFixBase::CArrayFixBase(...)`** |
| `10ac6c` | 185 | `TDes16::Collate()` | 264 | **`CArrayFixFlat<int>::CArrayFixFlat(int)`** |
| `10b2d4` | 172 | `RHandleBase::Close()` | 120 | `RHandleBase::Close()` |

Three of the five are unrelated functions. And they are not missing from 9.x:
`RPointerArrayBase::BinarySearchUnsigned` is euser ordinal **1588**,
`TDes16::Collate()` is **978**, `CObjectIx::At(const CObject*)` is **1866**.

### It is not the table

`kShimEuser[ordinal - 1]` gives the right answer for every one of them, in the
generated table and in the copy checked into `gate4_shim.cpp` alike. The table
is correct.

**The answers came from the wrong table.** `kShimEfsrv[120]` is 93,
`kShimEfsrv[135]` is 255, `kShimEfsrv[184]` is 264 -- all three, exactly.

`gate6_library_lookup` picks the table like this:

```c
const u32 kind = (lib == c->dynLib[LIB_EUSER]) ? LIB_EUSER
               : (lib == c->dynLib[LIB_EFSRV]) ? LIB_EFSRV : LIB_OTHER;
```

It identifies a library by comparing an `RLibrary` **pointer** against one
remembered from an earlier load. The game loads euser twelve times and efsrv
five times in a run, closing them in between, so those objects are created and
destroyed repeatedly and their addresses are reused. When a fresh euser
`RLibrary` lands on the address a closed efsrv one used to occupy, this
comparison says efsrv, and every euser ordinal the game asks for is translated
through the efsrv table.

### What it costs

The game asks for a binary search twenty-six times and is handed an array
constructor. It asks for `CObjectIx::At` and is handed a descriptor
constructor. It calls them, gets nonsense, and decides what to do next on the
strength of it -- on the emulator it carries on into the burst, on the phone it
closes the library and gives up. **That is the whole 35-event gap**, and neither
machine was ever going to work; the emulator only looked like it did.

This also retires the theory the previous section built: the ordinals are not a
9.2-versus-9.3 problem, and the N95's `euser.dll` is no longer needed to settle
it.

**Identity by stale pointer** is the flaw, and it is the same shape as two
earlier bugs in this file -- the allocation ring matching a stale entry, and the
`RFile` closed as a plain handle. A library has to be identified by something
that survives being closed and reopened.


## Retraction: the lookup mappings were right, and the gap is a short read

Round 57 read the logged lookup ordinals and concluded that three euser asks
were being answered out of the efsrv table. **That conclusion is wrong**, and
this section withdraws it.

The mistake was mine and it was in the naming, not in the shim. I took the old
ordinals 121, 136 and 185 and looked them up in the **euser** 7.0 def, which
answers `CObjectIx::At`, `RPointerArrayBase::BinarySearchUnsigned` and
`TDes16::Collate`; then I took the mapped values 93, 255 and 264 and looked
*those* up in euser as well. Two wrong dictionaries, one after the other.

Read out of efsrv, which is the library the game actually asked, every one of
them is exact:

| call site | old efsrv | name | new efsrv | name |
|---|---|---|---|---|
| `13f68c` | 121 | `RFile::Open(RFs&, const TDesC16&, TUint)` | 93 | `RFile::Open(RFs&, const TDesC16&, unsigned int)` |
| `10abf8` | 136 | `RFile::Read(TDes8&) const` | 255 | `RFile::Read(TDes8&) const` |
| `10ac6c` | 185 | `RFile::Size(TInt&) const` | 264 | `RFile::Size(int&) const` |

and the euser asks in the same run are exact too: old 1114 -> new 595 is
`User::StringLength` on both sides, 355 is `RBusLogicalChannel::DoCreate`
(dropped in 9.x, which is why it maps to 0 and takes the driver refusal), 172
is `RHandleBase::Close`. **`gate6_library_lookup` is picking the right table.**

### What the 35-event gap actually is

Filtering the notes out of both logs leaves 180 core events in the emulator and
151 on the phone, and aligning them leaves exactly one substantive difference:

```
delete  emu[73:100]      27 x  326 RLibrary::Lookup  from 0x10abf8
```

Twenty-seven lookups at **one call site**, and that site resolves
`RFile::Read`. The emulator calls it 29 times, the phone 3. Every other lookup
site is called the same number of times on both machines, `RFile::Size` once
each, the opens and closes identical, and the run is byte-for-byte identical
across all three phone launches.

So the game opens a file and reads it in a loop, re-resolving `RFile::Read`
each time round. On the emulator the loop runs 26 times on one handle
(`0x405b002b`) in a tight burst. On the phone the same loop stops after its
first read.

**The gap is a short read, not a wrong function.** Nothing in the shim
translates it; the game asked for `RFile::Read`, got `RFile::Read`, called it,
and the answer on the phone ended the loop. What the logs cannot yet say is
*why*: whether the open failed, the file is absent or empty where the phone
looks for it, or the descriptor handed to `Read` has no room in it. None of
those three has ever been recorded, because a dynamically resolved call is
handed straight to the game and never passes through the result thunks.

### What this costs, and what it does not

The round 57 row in `ROUNDS.md` is corrected, not deleted. The one thing round
57 did establish stands: **the phone's ordinals and mappings are identical to
the emulator's**, so the N95-versus-5320 ordinal theory is dead either way.

The lesson is narrower than the last one and worth writing down plainly: when a
mapping looks absurd, check which library it came out of before deciding the
code is broken. Two of the three "unrelated functions" were unrelated only
because I read them in the wrong book.

## The loader was holding the game's own image open

Round 58 put a wrapper in front of `RFile::Read` and `RFile::Size` and the
answer came back in four numbers. The phone: `Size` = 125, one read into a
125-byte buffer, 125 bytes returned, KErrNone. The emulator: twenty-six reads
into a **65536**-byte buffer, 65536 bytes each, before `Size` is ever called,
and only then the same 125-byte read.

So nothing the phone reads is short. The twenty-six reads are a *different
file*, opened earlier, that the phone does not read at all.

Round 59's `RFile::Open` wrapper named it. The five files the game opens:

| file | size | reads |
|---|---|---|
| `E:\system\apps\6rbc\6rbc.app` | 1.6 MB | **26 x 64 KiB** |
| `E:\system\apps\6rbc\cwp.dat` | 125 | 1 |
| `E:\system\apps\6rbc\nc.dat` | 16 | 1, twice |
| `E:\system\apps\6rbc\6rbc.cwa` | 38 KB | -- |

The burst is the game reading **its own image**. That is a protection check --
the thing a copy of the game does to itself before it will run.

And the loader has that file open. `gate6_load` opens
`E:\system\apps\6rbc\6rbc.app` with mode 1, `EFileRead | EFileShareReadersOnly`,
reads the image into the chunk, and **never closes it**. It is held for the life
of the process.

A file already open readers-only cannot be opened *exclusively*, and exclusive
is the default share mode. On hardware the game's own open of `6rbc.app`
answers **KErrInUse** and the whole check is skipped. EKA2L1's file server does
not enforce share modes, so the emulator opened it happily and read it
twenty-six times, and the difference never showed in three months of logs.

**The fix is one line**: close the handle once the image has been read. Nothing
reads through it afterwards. E35 confirms it costs the emulator nothing --
2132 records, 29 reads, five opens, byte-for-byte E34 -- which is exactly what a
change that only matters on hardware should look like.

### What this says about the method

This is the second time a difference between the two machines has been *ours*
rather than the game's, and both were invisible from the emulator alone: the
emulator is lax where a phone is strict, so anything the emulator permits is
untested. Round 58's instrument -- standing in front of a dynamically resolved
call, which no round had ever done -- is what turned a 27-event hole in a diff
into a named file and a share mode.

## The gap is closed

Build 59 closed the loader's handle on `6rbc.app` and the phone answered
exactly as predicted:

| | build 58 | build 59 |
|---|---|---|
| `RFile::Open` on `6rbc.app` | (never logged; skipped) | **0** |
| reads | 3 | **29** |
| records | 1571 | **2086, 2104** |
| traced events | 144 (build 51's best) | **176** |

All five opens answer KErrNone, no allocation ever fails, and the game runs its
own self-check -- twenty-six 64 KiB reads of its own image -- on hardware for
the first time.

Aligning the phone's core events against the emulator's for the same build:

```
emu 180 core events | phone 178
replace emu[139] phone[139]   probe address   0x3874a38 vs 0x7d6908
replace emu[164] phone[164]   probe value
replace emu[166:168] phone[166:168]   the same two probe addresses
delete  emu[178:180]          324 User::Leave, 308 User::Exit
```

**Two machines, 178 events, no divergence.** The three "replace" rows are heap
addresses inside probes and were never going to match. The only real difference
left is the tail: the emulator gives up through `User::Leave` and `User::Exit`,
and the phone faults at the same point instead.

So the port no longer has a hardware-specific failure in front of it. It has
the failure the emulator has always had, which is the one thing about this
project that has ever been easy to work on: it reproduces locally, every run,
without asking anyone to install anything.

### What is next

`User::Leave(...)` from `0x2b20` is now the whole question. Something in the
startup sequence decides it cannot continue and leaves; the framework catches
it and the process exits. Nothing in the log says what the leave code is or
what decided. That is the next instrument, and it can be built and tested
entirely in the emulator.

## What the game leaves with, and who decides

`User::Leave` is the last thing either machine reaches, and the log had only
ever recorded that it happened and where from. An `arg_thunk` on import 324
writes down r0 and the return address, and a leave does not come back, so that
is the only chance to see it.

**`User::Leave(-2)` -- KErrGeneral -- from `0x2b20`.**

That is not a leave the framework raised. It is the game's own code, and the
function it ends is short enough to read whole:

```
2998  push {r4, r5, lr}
299c  sub  sp, sp, #32
29a0  mov  r5, r0                @ the object to fill in
29a4  ldr  r0/lr/r4/r12, [pc]    @ six literals from the pool at 0x2aa0
...   str  r12, [sp] / [sp,#4] / [sp,#8]
29c8  eor  r0, r1, r0            @ the caller's own r1, r2, r3, each
29d0  eor  r2, r2, lr            @ XOR'd with one of those literals
29d4  eor  r3, r3, r4
29d8  bl   0xccbb4
29dc  subs r4, r0, #0
29e0  beq  2b18                  @ <-- taken
...
2b18  mvn  r0, #1                @ -2
2b1c  bl   User::Leave
```

So: build seven XOR-obfuscated arguments, call `0xccbb4`, and if it answers
**zero**, give up with KErrGeneral. On the success path `r4` -- the same
answer -- is XOR'd into the next call's r3 and the result stored at `[r5]`, so
it is not a boolean. It is a value the rest of the startup needs. **Forcing the
branch would hand the next call a zero it has never seen.**

### `0xccbb4` is a state machine

```
ccbb4  push {r4-r10, lr}
ccbb8  sub  sp, sp, #212
...
ccbf0  add  r3, r0, #478150656   @ the key in r0, unfolded in four adds
ccbfc  add  r3, r3, #28
ccc00  cmp  r3, #68
ccc04  ldrls pc, [pc, r3, lsl #2]
ccc08  <69 entries, 0x100ccd24 .. 0x100cd8f0>
```

Sixty-nine states, each a short block ending in `b` back to the dispatcher with
the next key in r0, and the arithmetic between them done in the shift-add
multiply chains this image uses everywhere. The first state reads `[sp,#128]`,
dereferences it, compares it with 2 and picks one of two keys -- so the state
graph is data-dependent from the first hop.

This is the game's protection, and it is reached on both machines now. Reading
it statically is a project; **the state trace is not**. A probe on `0xccc00`
logging r3 on every pass gives the exact path through the sixty-nine states and
the state that decides on zero, and it can be built and read entirely in the
emulator.

`DUMP_DECRYPTED` was turned on for one run to check whether any of this lives
only in memory. It does not -- three regions, 448 + 1056 + 1092 bytes, none of
them near `0xccbb4`. The whole check is plaintext in the file. The flag is off
again.

## Seven states, and the one call that answers zero

A station on the protection's dispatcher gives the path through it directly,
and it is far shorter than sixty-nine states suggested:

```
47 -> 13 -> 36 -> 29 -> 68 -> 59 -> 7
```

Seven blocks. The table at `0xccc08` turns each index into its address, and the
three that matter read plainly:

```
cdc64   state 29
cdc74     ldr  r0, [r12, #4]          @ r12 = [sp,#56]
cdc84     mov  r1, #100
cdc88     mov  r2, r8
cdc8c     ldr  r3, [sp, #140]
cdc90     bl   0xe6df8                @ <- the check
cdc94     mov  r4, r0
cdca4     cmp  r4, #0
cdca8     movne r0, <key A>           @ nonzero -> state 34
cdcac     moveq r0, <key B>           @ zero    -> state 68
cdcb0     b    dispatcher

cf488   state 68
cf488     mov  r8, #0                 @ the answer becomes zero

cd0f4   state 7
cd0f4     mov  r0, r8
cd0f8     b    0xcf498                @ add sp, #212 / pop / bx lr
```

So `0xccbb4` is not itself the check. It is a wrapper whose one decision is
**`0xe6df8`**, and `0xe6df8` returned zero.

### The keys decode

The dispatcher builds its index as `r3 = r0 + 0x1c936f1c`, truncated to 32
bits -- one literal load and four adds, which is the same unfolding this image
does to every constant. The two keys in state 29 are `0xe36c9106` and
`0xe36c9128`, and adding `0x1c936f1c` gives **34** and **68** exactly. Every
`ldr r0, [pc]` in the state machine can be read the same way, so the whole
sixty-nine-state graph is recoverable statically now without running anything.

### What is next

`0xe6df8(r0 = [[sp+56]+4], r1 = 100, r2 = 0, r3 = <literal>, +2 stack words)`.
`r1 = 100` is a round number -- a count, a size or a limit. A station on
`0xcdc94` would log the result, but the result is already known; what is wanted
is inside `0xe6df8`, and it can be read the same way this was.

### A method note

The first attempt planted nothing at all: I put the station on `0xccc00`,
which is the `ldrls pc, [pc, r3, lsl #2]`, because I had read my own
disassembler's output one line out of step. `crumb_safe` refused it -- correctly,
a conditional load into pc is neither unconditional nor safe to re-execute
elsewhere -- and the run came back bit-identical to the one before, which is
what a refused plant looks like. **A station that changes nothing has either
proved something or not been planted, and those two look the same.** The fix
was one instruction earlier, on the `cmp`, where restoring the flags before
re-executing it keeps the load that follows honest.

## Inside the check: a loop of one, and two hash contexts

`0xe6df8` is an ordinary function, and short enough to read whole:

```
e6df8  push {r4-r10, lr}
e6e00  subs r10, r3, #0 / movne r10, #1     @ r10 = (arg3 != 0)
e6e18  subs r5, r0, #0
e6e1c  moveq r2, #2
e6e20  beq  e6f58                           @ arg0 == 0 -> return 2
e6e24  mov  r1, #0                          @ the counter
e6e8c  ldr  r2, [r5]                        @ <- loop top: the bound
e6ecc  cmp  r1, r2
e6ed0  bge  e6f30                           @ counter >= bound -> out
e6ee8  bl   0xe50d8                         @ the body
e6f2c  b    e6e8c
e6f30  cmp  r10, #0
e6f34  beq  e6f54                           @ arg3 was null -> return 0
e6f38  <identity chain on r0>               @ otherwise return r0
```

Every one of those "chains" is the identity -- fifteen shift-adds whose net
multiplier is 1, which is what this image does to every value it touches.

A station on the loop top says: **`r5 = 0x0334ca90`, `[r5] = 1`, counter 0 then
1.** The list has one entry and the body runs once. So the zero is not an empty
list, and `r10` is not null either -- `arg3` is `0x100ccb38`, a code address.
The function returns **whatever the single call to `0xe50d8` left in r0**, and
that was zero.

### `0xe50d8` initialises two hash contexts

```
e50d8  push {r4-r10, lr}
e50dc  sub  sp, sp, #608
e50f0  add  r0, sp, #504 / ldr r1, =0x1017e298 / mov r2, #104 / bl memcpy
e5100  add  r0, sp, #296 / ldr r1, =0x1017e300 / mov r2, #104 / bl memcpy
e5110  add  r1, sp, #544 ; zero 64 bytes
e5140  add  r1, sp, #336 ; zero 64 bytes
```

Two 104-byte records, adjacent in the image, **byte-for-byte identical**:

```
17e298: ba243c3f ba243c3f 0efdae7e 7e7195f6 d0dfea81 616c0309 c4d0618f
        then 76 zero bytes
17e300: the same again
```

A tag word twice, then **twenty bytes**, then zeros -- and each record is paired
with a 64-byte block that is zeroed. Twenty bytes of state and a 64-byte block
is the shape of a hash, and the game imports no crypto library (apparc, avkon,
bitgdi, cone, dfpaeabi, drtaeabi, efsrv, eikcoctl, eikcore, eikdlg, esock,
estlib, etel, euser, fbscli, hal, scppnwdl, ws32) so whatever it is, it is in
the image. The twenty bytes are not SHA-1's standard IV, so it is a variant or
the words are stored transformed.

**This is the protection, and it is computing something twice and comparing.**
What goes into it is the open question, and there are two candidates already on
the table: the 1.6 MB of `6rbc.app` the game reads before any of this, and the
memory card's CID -- which the port answers with zeros, because there is no
N-Gage game card and `gate6_mmc_control` copies what EKA2L1's own mmcif channel
would say.

### What is next

The same method again, one level down: find `0xe50d8`'s exits, station the one
that decides, and see which of the two digests it is unhappy with. Nothing here
needs hardware.

## Forcing the check's own success value, and what is behind it

The check answers zero and the game leaves. Forcing the *branch* would hand the
next call a zero it has never seen -- but the check has a legal success value of
its own:

```
e6e18  subs r5, r0, #0
e6e1c  moveq r2, #2
e6e20  beq  e6f58        @ -> return 2
```

and state 29 treats **any** non-zero answer as success. So `2` is a value the
game's own code produces and its own code accepts. Turning `subs r5, r0, #0`
into `subs r5, r0, r0` takes that exit every time. Nothing is invented; the
check returns the game's own number by the game's own path.

**It works.** The state path changes from

```
47 13 36 29 68 59 7        @ 68 is `mov r8, #0`
```

to

```
47 13 36 29 34 4 59 7
```

and traced events go **179 to 195**. State 29 took its success key.

### And the wall moves one step

State 34 fails the same shape:

```
cde4c  mov  r0, #24
cde50  bl   0xd5a20            @ 24 bytes
cde6c  ldr  r0, [r12, #4]
cde74  mov  r1, r3 ; mov r2, #0
cde7c  bl   0x10a93c
cde80  mov  r8, r0
cde88  str  r8, [r9, #12]
cde94  cmp  r8, #0
cde98  moveq r0, <key>          @ -> state 4
```

and **state 4 is the same block as state 68** -- `mov r8, #0`. Two table entries,
one failure block.

`0x10a93c` is a factory: `new (36)`, construct it with the first argument, then
call one of two methods depending on a byte flag, and delete and answer zero if
that method reports false. With the flag zero it calls `0x13f4c4`, and that is
**our** territory rather than the protection's: `0x13f4c8` builds a
nine-character library name a byte at a time, loads it, resolves ordinals and
uses them -- it is the site the log has been showing all along as
`RLibrary::Load from 0x13f588`, `Lookup from 0x13f5e4`, `Close from 0x13f610`.

So the chain now reads:

```
0x2998 -> 0xccbb4 state 29 -> 0xe6df8 -> 0xe50d8 -> 0xe7b84     (forced)
0x2998 -> 0xccbb4 state 34 -> 0x10a93c -> 0x13f4c4              (open)
```

The second one is worth more than the first, because a library load that
resolves the right ordinals and still reports false is something the shim can
plausibly be wrong about -- unlike a digest over a game card that is not there.

### The honest caveat

`PATCH_THE_CHECK` is a workaround. What the protection hashes is still unknown,
and the answer may be that it cannot be satisfied without the N-Gage game card
it was written to look for. The patch is in the record as a patch.

## The name it opens is an empty list

The forced check gets to state 34, and state 34 fails on a sixth `RFile::Open`
that no earlier run ever reached. Logging the raw descriptor says why:

| open | header | text | err |
|---|---|---|---|
| `6rbc.app` | `4000001c 0000001c ptr` | the path | 0 |
| `cwp.dat` | `4000001b 0000001c ptr` | the path | 0 |
| `nc.dat` x2 | `4000001a 0000001c ptr` | the path | 0 |
| `6rbc.cwa` | `4000001c 0000001c ptr` | the path | 0 |
| **the sixth** | **`40000004 00000010 ptr`** | **`0x0008 0x000a`** | **-1** |

Type nibble 4 in every one, and the buffer at `ptr` begins with its own header
-- which is why the first decode of these names came out a word early. The
sixth has four characters and they are control bytes.

A station on state 34's input says where they come from:

```
r3 = 0x04160a08     [r3] = 0x04160a08
```

**The word at the pointer is the pointer.** That is this game's way of saying a
list is empty, and it is already on file: the cell at `0xcc914` does exactly the
same thing. State 34 hands that pointer on as a C string, `User::StringLength`
walks four bytes of it before hitting the zero, and those four bytes become the
filename.

### And it is not the patch's doing

The first patch took the check's early exit, which skips the whole body -- and
the body is the obvious candidate for whatever fills that list. So the patch
moved: `0xe6f34` is the `beq` that returns zero when the fourth argument is
null, and replacing it with `mov r0, #2` leaves the loop and the body intact and
changes only the answer, to the same 2 the function already returns elsewhere.
The chains from there to the return are the identity, so 2 is what the caller
sees.

With the body running -- station 997 fires twice, one iteration and the exit,
so `0xe50d8` did execute -- **state 34 still reads a list holding its own
address**. Traced events 195 -> 198, and no exception handler fired at all.

So the empty list is not a consequence of forcing the check. Something that
should have put an entry in it never did, and that is a much better kind of bug
to have: a list this port failed to populate is the shim's business, where a
digest over a game card that is not there is not.

### What is next

Find who writes to that list. The pointer is `[[sp,#52]]` in the dispatcher's
frame, so an earlier state -- 47, 13 or 36 -- put it there, and one of them, or
something they call, is meant to add to it. Stations on those three states'
inputs will say which.

## The empty container comes from the function this project has been watching all along

Chasing the empty list outwards, one dereference at a time:

```
state 34   ldr r9, [sp,#52] ; ldr r3, [r9] ; ldr r3, [r3]   @ the "string"
sp+52      written in state 13, from [sp,#140]
sp+140     written in state 47, from [sp,#152]
sp+152     written in 0xccbb4's prologue, from r3           @ arg3
arg3       0x2998 passes its own r3, XOR'd with a literal
0x2998     called from 0x2e54 and 0x2e7c with r3 = r5
r5         `bl 0xcc7e4 ; subs r5, r0, #0` at 0x2e38
```

So the container is **`0xcc7e4`'s return value**, and it is empty before the
protection is ever entered. Forcing the check did not empty it and could not
have.

And `0xcc7e4` is not a new address. It is the function probes 990 and 991 sit
inside -- `add r0, r5, #40` at `0xcc864` and `mov r4, r0` at `0xcc894` -- the one
this project has been watching for months because its `this->[4]` was being
poisoned by the store at `0x1082c0`. It is also the caller of `0xe9988`, the
function that refused to carry a probe at all:

```
cc864  add  r0, r5, #40 ; mov r1, #1
cc86c  bl   0xe97cc
cc870  bl   0xd5fbc
cc878  subs r7, r0, #0 ; movne r7, #1
cc884  ldr  r0, [r4, #4]
cc888  mov  r1, r5 ; mov r2, #1
cc890  bl   0xe9988
cc894  mov  r4, r0
```

The call site at `0x2e38` is followed by `subs r5, r0, #0 / beq`, and then
`0x2998` is called **five times** -- once at `0x2e54` and four more round the
loop at `0x2e7c`, `cmp r4, #3 / ble`. Five registrations against one container.
The container is empty on the first one.

### Why this matters

Two threads that have been separate for months are the same thread. The object
whose fourth word was being poisoned, the function that would not take a probe,
and the empty list the protection trips over are all one object made by one
function. Whatever `0xcc7e4` failed to do, it is upstream of everything since.

### What is next

`0xcc7e4` calls `0xe97cc`, `0xd5fbc` and `0xe9988` before it returns. One of
them fills the container. `0xe9988` is the one to be careful with -- a single
probe at its third instruction once took a run from 1240 records to 553 -- but
`0xcc7e4` itself already carries two stations without trouble, and the two calls
before `0xe9988` have never been looked at.

## The wall has a name: Codewave, and the map has no key 1

Two stations inside `0xcc7e4` settle what happens around the empty list:

```
cc8e4  cmp r4, #0     -> r4 = 1        @ so the skip is not taken
cc908  mov r0, #4 ; bl <alloc>
cc914  str r5, [r5]                    @ the list is created empty, deliberately
cc928  bl 0xe9808 (obj+40, 1, 0)
cc92c  ldr r2, [r5]
cc940  bl 0xe98c4 (obj+40, 1, r2, 0)
cc944  mov r4, r0     -> r0 = r5       @ it answers the head: key 1 is not there
```

So `0xcc914` -- the four-byte cell that writes its own address, the landmark this
file has had since the `User::Free` era -- **is** the list state 34 reads, and it
is created empty by design. The container it is looked up in is at `obj+40`, it
is keyed by an integer, and `0xe98c4` answers "not found" for key 1.

### What the container is for

The files the game opens name it. `cwp.dat` is 125 bytes and begins **`CWZ`**;
`nc.dat` is sixteen bytes of binary; `game.lic` is thirty-four bytes of ASCII:

```
Asphalt2 10185-2.0.194-prd-4205THA
```

and `version.txt` says `2.0.194`. Those, with `cwivenc.dat` and `cis.dat`
alongside them, are the **Codewave** content-protection set that N-Gage titles
of this era shipped with. The two 104-byte records at `0x17e298` and `0x17e300`
-- tag `0xba243c3f` twice, then twenty bytes -- are its contexts, and `0xe6df8`,
`0xe50d8` and `0xe7b84` are its machinery.

So the remaining wall is not a bug. It is the game's licence check: it parses
its protection blob, looks for entry 1, finds nothing, and the whole startup
unwinds from there through `User::Leave(-2)`.

### What this means for the port, said plainly

Everything between the loader and this point is now working on hardware, and the
two machines agree to the event. What is left is DRM, and there are only two
honest ways past it:

1. **Patch each gate as it comes.** `0xe6f34` already does this for the first
   one, with the game's own return value, and it bought sixteen events. There
   will be more gates; each is a few hours and none of them is understanding.
2. **Understand the Codewave format** well enough to produce an entry the game
   accepts -- parse `cwp.dat`, work out what keys it derives and from what, and
   see whether the port can supply them. The card CID the port answers with
   zeros is a candidate input, and if it is a real input then no amount of
   correctness makes this pass on a phone with no N-Gage game card in it.

Neither is emulator work in the sense the last thirty rounds were. This is the
point to say so rather than keep drilling one call deeper each round.

## Route one, and where it stops

Gate two is one word: `0x13f4c8` ends

```
13f6b4  bx    r6          @ RFile::Open through our shim
13f6c4  cmp   r4, #0
13f6c8  movne r4, #0
13f6cc  moveq r4, #1
```

and replacing `movne r4, #0` with `mov r4, #1` makes it always report success.
Five of its six calls open a real path and already answered 1, so only the
sixth -- the one handed the empty list as a name -- changes.

**It works, and it fails immediately.** The state trace stops at `47 13 36 29
34`: no return to the dispatcher, no `User::Leave`, no `User::Exit`. The run
goes somewhere it has never been and the last thing in the log is

```
Lookup old 136 -> new 255      @ RFile::Read
Read  into 2048 bytes  ->  -8  @ KErrBadHandle
<fault>
```

a read on the handle that was never opened. The patch's own comment predicted
this before the run, which is the only good thing about it.

### What route one actually buys

Gate one was a decision: a function that answers zero where the game accepts
anything non-zero, and there was a non-zero the game itself produces. Sixteen
events, no side effects, and the build is still the best there has been.

Gate two is not a decision. `0x13f4c8` reports whether an open succeeded, and
forcing the report does not open the file. Behind it is an `RFile` that does not
exist, and behind *that* is a name that only exists if the licence parse
produced an entry for key 1 -- which is the thing gate one was checking.

So the gates are not a sequence of independent booleans. **They are one gate
seen from several places**: the parse produced nothing, and everything after it
is reading from nothing. Forcing the reports keeps the run alive a few hundred
instructions longer and then it dies on the emptiness itself.

`PATCH_GATE_TWO` is left in the source at zero, with the finding attached, so
the next round starts from this rather than rediscovering it. E47 confirms the
tree is back to the best run there has been.

### What would actually move this

Making the parse produce an entry, which is route two: read `cwp.dat`'s `CWZ`
format, find what it keys entries by and what it digests, and see whether the
port can supply it. Everything needed for that is on this machine -- the 125
bytes of `cwp.dat`, the 16 of `nc.dat`, the licence string, the two 104-byte
contexts at `0x17e298` and the whole of `0xe50d8` -- and none of it needs the
phone.

## The dump on this machine is not cracked, and `nc.dat` is the card

Asked whether the dump already in hand is a cracked one, the answer is no, and
it is not a judgement call:

```
e.ngage/system/apps/6rbc/6rbc.app   1616972 bytes
e/6rbc.app                          1616972 bytes
e/system/apps/6rbc/6rbc.app         1616972 bytes
```

**All three are byte-identical**, and so are `cwp.dat`, `nc.dat`, `cis.dat`,
`cwivenc.dat`, `6rbc.cwa`, `game.lic` and `6rbc.dat` across every tree. There is
one dump here in three places. The `e` drive is that dump laid out as an N-Gage
card -- it carries `game.id` (`N-Gage`), `nokia.dat`, `version.dat` and
`ngagegamestarter.txt` (`Path: \system\apps\6RBC\6RBC.app`) -- which is why it
looked like a second one. The later timestamp on `e/6rbc.app` is a copy, not an
edit.

And the protection is demonstrably live in it: it runs, it computes, and it
answers zero. A cracked image would not reach `User::Leave(-2)`.

### `nc.dat` is the card's CID

Sixteen bytes, four words, sitting next to the game:

```
bd81cbfb eb08cd1e 6d341c6e e83e5d16
```

That is the length and shape of an MMC CID, and an earlier session evidently
reached the same conclusion -- `/tmp/card_bd81cbfb-eb08cd1e-6d341c6e-e83e5d16/`
is a whole card tree named after it. So the game carries the identity of the
card it was sold on, and `gate6_mmc_control` has been answering **zeros**,
which is what EKA2L1's own mmcif channel says.

Answering `nc.dat` instead is three lines and an obvious shot. It is not the
answer:

| run | CID answered | check patch | state path |
|---|---|---|---|
| E48 | `nc.dat`, big-endian | on | 47 13 36 29 **34 4** 59 7 |
| E49 | `nc.dat`, big-endian | **off** | 47 13 36 29 **68** 59 7 |
| E50 | `nc.dat`, little-endian | **off** | 47 13 36 29 **68** 59 7 |
| E51 | `nc.dat`, big-endian | on | 47 13 36 29 **34 4** 59 7 |

Neither word order makes the real check pass, and the driver log shows ordinal
490 -- the card-info call -- being answered on every run, so the CID genuinely
reaches the game. `ANSWER_THE_CARD` is kept on anyway: it costs nothing and it
is the truthful answer where zeros were a guess.

So the CID is either not an input to the digest, or not the only one.

## The second dump is a crack *loader*, and it works exactly like ours

`Asphalt_Urban_GT_2.zip` is not a cracked binary. Its game code is
`System/Apps/6RBC/bin/main.dll`, 1616972 bytes, and it is **byte-identical** to
the `6rbc.app` this project has been running all along; so is every data file
beside it -- `cwp.dat`, `nc.dat`, `cis.dat`, `cwivenc.dat`, `6rbc.cwa`,
`game.lic`, `6RBC.dat`, `version.txt`, `nokia_EN.RLE`, the resources.

The difference is one file: **`6RBC.APP`, 3964 bytes**, where our tree has the
1.6 MB image under that name. Its strings say what it is:

```
E:\System\Apps\6rbc\6rbc.APP
E:\System\Apps\6rbc\bin\main.dll
z:\System\Libs\EUser.dll
z:\System\Libs\EFSrv.dll
e:\system\apps\6rbc\bin\
main.dll
BiNPDA presents...
e:\system\apps\6rbc\bin\arenaframework.dll
```

and its imports say how it works: `RLibrary::Load`, `RLibrary::Lookup`,
`RLibrary::EntryPoint`, `RThread::Id`, **`RDebug::Open`** and
**`RDebug::WriteMemory`** -- the same pair this port already shims, because the
game's own decryptor uses them.

**It is the same technique as gate6.** Load the image, open a debug channel to
your own thread, and write bytes into the loaded code.

### The patch loop, in full

```
05cc  bl 0x480 ; mov r9, r0           @ r9 = a key, computed at run time
05f8  ldr r12, [pc,#204] -> 0x10000b44 @ the patch table
05fc  ldm/stm x6                       @ copied onto the stack at sp+16
0628  add r0, sp, #8 ; bl RThread::Id  @ r10 = our own thread id
0640  bl RDebug::Open(16, 16, 16, 0x10000)
064c  mov r0, r4 ; bl RLibrary::EntryPoint ; mov r8, r0   @ main.dll's base
      loop r5 = 0 .. 6:
0674    bl TPtrC8::TPtrC8(&rec[r5].word, 4)
0678    ldr r1, [r6, r5*8]             @ rec.offset, obfuscated
067c    eor r1, r9, r1                 @ ^ the key
0684    add r1, r8, r1                 @ base + offset
0690    bl RDebug::WriteMemory(r10, r1, that TPtrC8, 4)
069c    cmp r5, #6 ; bls
```

**Seven four-byte patches**, and the table at code offset `0xb44` is seven
records of `{offset ^ key, value}`:

| # | offset ^ key | value | what it points at in the crack |
|---|---|---|---|
| 0 | `85a3a51c` | crack+`0x18c` | `bx lr` -- do nothing |
| 1 | `85a3a510` | crack+`0x190` | `b` to an import stub |
| 2 | `85a3beb0` | crack+`0x194` | `b` to an import stub |
| 3 | `85a3bb20` | crack+`0x380` | `push {r4,r5,r6,lr} / sub sp,#1040` |
| 4 | `85a3be84` | crack+`0x198` | `push {r4-r7,lr}` |
| 5 | `85a3be8c` | crack+`0x244` | `push {r4-r7,lr}` |
| 6 | `85a3bea4` | crack+`0x2bc` | `push {r4-r7,lr}` |

So the crack replaces **seven function pointers** in the loaded image with its
own routines -- one of which simply returns. That is the whole crack, and it is
about seven hundred bytes of ARM code.

### The key is not recovered yet

The offsets are XOR'd with a value computed at run time by `0x480`, which opens
files, takes `crc32` from `ezlib` over UTF-16 descriptors (`bic #0xf0000000`
then `lsl #1` is a descriptor's byte length) and sums the results. Tried and
failed: CRC32 of every string in the crack, singly and in pairs and triples.
Brute force over every 4-aligned offset in the image, filtered to keys where all
seven targets currently hold pointers (1356 candidates), then to pointers at
function starts (45), then to functions that call any known protection address
(0). The targets are not in the protection's own jump table either.

### Two ways to finish this

1. **Let the crack compute its own key.** Everything it needs is something this
   port already provides: `RDebug::Open` answers, `gate6_write_memory` performs
   the write and `DUMP_DECRYPTED` records it. Hosting a 3964-byte EKA1 image
   whose imports our shim already resolves is a smaller job than the loader we
   already have, and it ends with the seven patches written down.
2. **Run the cracked dump under EKA2L1 here** and capture the writes from the
   emulator side. It is reported to work on the Android build, and this is the
   same emulator.

Either ends with seven `{offset, word}` pairs and about seven hundred bytes of
replacement code to carry into `gate6`, which is a form this project already has
machinery for.

## The crack's key, and the seven imports it replaces

The key is recovered. The function at crack offset `0x480` opens
`e:\system\apps\6rbc\bin\arenaframework.dll`, reads 8508 bytes of it -- which is
exactly that file's size in the cracked dump -- and sums four CRC32s:

```
crc32(arenaframework.dll, 8508)
+ crc32(u"e:\system\apps\6rbc\bin\arenaframework.dll")
+ crc32(u"BiNPDA presents...")
+ crc32(b"gt2 loader. (c) 2005 zg.")
= 0x85bbf5f4
```

XOR that through the seven records at `0xb44` and every offset lands, 4-aligned,
inside the loaded image's **import address table** -- `[text_size, code_size)`,
which is `[1591740, 1593596)` and which `gate6` already calls `iat[]`. The words
those offsets currently hold are the ordinals the loader has yet to resolve,
which is how the decode checks out: 318, 25, 121, 151, 672, 2, 1.

| import | was | replaced by |
|---|---|---|
| 98 | ordinal 318 | `bx lr` -- returns at once |
| 101 | `RFile::Create` | a crack routine |
| 109 | `RFile::Open` | a crack routine |
| 111 | `RFile::Replace` | a crack routine |
| **326** | **`RLibrary::Lookup(int) const`** | a crack routine, 1040 bytes of stack |
| 458 | `CMdaAudioOutputStreamPadFunction` | `b` to `CMdaAudioOutputStream::NewL` |
| 459 | ordinal 1 | `b` to an import stub |

Two of those are not protection at all: the N-Gage build imports an audio
padding function and the crack points it at the real `NewL`. A compatibility
fix, and one this port will want anyway.

The other five are the crack, and **import 326 is the one that matters**. The
protection resolves its own functions dynamically through `RLibrary::Lookup` --
which is the whole reason this project ever wrote `gate6_library_lookup` -- and
the crack answers those lookups itself, standing in front of `RFile::Open`,
`Create` and `Replace` at the same time.

So the thing we would have to build is the thing we already have. `gate6` owns
import 326 and already wraps `RFile::Open`. What is left is four routines of ARM
code, kept in `.claude/asphalt2/crack/` with the decode, to read and carry
across.

Route 2 -- running the cracked dump under EKA2L1 to watch the writes -- is no
longer needed: the writes are known without running anything.

## What the crack actually does, in four routines

All four are read now, and together they are short enough to state in full.

**`RLibrary::Lookup` (crack `0x380`)** -- the protection resolves its own
functions dynamically, so the crack answers those lookups itself:

```
name = library.FileName()
if name ~= u"z:\System\Libs\EUser.dll"  and ordinal == 353:  -> crack 0x334
if name ~= u"z:\System\Libs\EFSrv.dll":
      ordinal 25  -> crack 0x2bc      (RFile::Create)
      ordinal 121 -> crack 0x198      (RFile::Open)
      ordinal 151 -> crack 0x244      (RFile::Replace)
otherwise the real RLibrary::Lookup
```

Old euser **353 is `RBusLogicalChannel::DoControl(int, void*)`** -- the driver
call this port already answers.

**`DoControl` (crack `0x334`)**:

```
memcpy(scratch, crack+0xa70, 20)
if (out) memcpy(out, scratch, 20)
return 0
```

Twenty bytes, whatever was asked, and they are four CID words and a type word:
**`56785733 10011234 0b70194e 16000400`, type 0**. Not `nc.dat`, not a card
that ever existed -- a forged identity the protection accepts.

**`RFile::Open` (crack `0x198`)**:

```
if (name[0] == 'e' or 'E') and (mode & EFileWrite):  return -21   KErrAccessDenied
if name ~= u"E:\System\Apps\6rbc\6rbc.APP":  name = u"E:\System\Apps\6rbc\bin\main.dll"
the real Open
```

**`RFile::Create` (`0x2bc`) and `Replace` (`0x244`)**: the same first line,
unconditionally -- any Create or Replace on E: answers KErrAccessDenied.

So the crack is: **a forged card identity, and a drive that refuses to be
written to.** The redirect is only bookkeeping for the split layout, and the
other two patches are an audio-import compatibility fix.

That reframes the whole thing. The protection's question is *"am I on a real
N-Gage game card?"*, and it asks it two ways -- the card's CID, and whether its
own drive is read only. This port answers both wrongly by construction: EKA2L1's
E: is writable and so is a memory card.

### Both answers supplied, and the check still fails

| run | what changed | result |
|---|---|---|
| E52/E53 | the forged CID, check patch off | dies at `RFile::Open` after 160 events, no dispatcher states at all, emulator segfault -- the card path entered for the first time |
| E54 | plus the read-only rules on Open/Create/Replace, static and dynamic | the early death is gone, 2259 records, but **zero refusals fire** and the state path is still 47 13 36 29 **68** 59 7 |
| E55 | plus answering every `DoControl`, not only `MMC_CARD_INFO` | no change |

Two honest readings, and the next round has to tell them apart:

1. `gate6_mmc_control` is never actually *called*. The lookup of 9.x ordinal 490
   is logged as refused, so the address is handed over, but nothing records the
   call itself.
2. The CID is delivered and the digest wants something else as well.

A station or a log line inside `gate6_mmc_control` separates them in one run,
and that is the next thing.

## The card is delivered, and it is not enough

A `ctx3_thunk` on `DoControl` -- r0 to r2 are the arguments, r3 is free -- finally
records whether the call happens. It does, twice a launch, as a pair:

```
DoControl(op = 4, out = 0)          @ select the card
DoControl(op = 6, out = <pointer>)  @ and ask for its info
```

So the original `MMC_CARD_INFO` gate was right all along, the twenty bytes go
into the game's own buffer, and the forged identity the crack uses is genuinely
delivered. E57 then closed a real gap -- the *dynamic* `RFile::Open` had been
skipping the read-only rule that the static import got -- and changed nothing.

**No refusal ever fires.** The game does not attempt a write to E: anywhere
before the check. So of the two questions the crack answers, only one is even
being asked on this path, and it is being answered correctly, and the check
still goes to state 68.

### What that means, honestly

The crack had to fix exactly two things because everything else on its machine
was real. It ran on an N-Gage, under EKA1, with a genuine euser and efsrv and a
genuine file server, and the only lies it needed to tell were about the card.

This port's machine is a shim. Hundreds of answers in it are approximations --
framework base classes are `LOCAL_NOOP`, the screen is the wrong size, the
foreground observer is a stand-in, `RFs` is the phone's. The protection digests
something, and the something is not only the card. Supplying the crack's two
answers was worth doing and is kept, but it was never going to be sufficient on
its own, and there is no short list of further answers to copy: the crack does
not have them, because it never needed them.

So **the verdict override at `0xe6f34` is the route**, and the value of all this
is that we now know precisely what is being overridden: a test for a physical
N-Gage game card, which an N95 with no such card in it cannot pass honestly, by
any amount of correctness in the shim.

E58 keeps the card answers and puts the override back: **200 traced events**, a
new emulator best, and the same state 34 failure to work on next.

## Supplying the answer works, and the leave is gone

The question was whether the port could extract what the protection wants and
supply it directly rather than hope a phone answers correctly. It can, and it
did -- but not before a bug of my own came out.

### The decode that had been silently disabling the card rule

Every filename this game opens is a type 4 descriptor whose `ptr` addresses a
buffer that **begins with its own header**: two shorts of length before the
characters. This file has tripped over that three times now. `name_on_the_card`
was reading the length word where it wanted the drive letter, so it never saw
an `E`, so **the read-only rule had never once applied** -- which is exactly why
E54 to E58 reported "zero refusals" and concluded the card was answered and not
enough. The conclusion was drawn from a rule that was not running.

`name_text` now does the skip in one place, and the substitution experiment that
found it (E60-E68, which broke the run at 35 events because it fired on perfectly
good names) is off again, unneeded.

### The thunk that held its target as a literal

The second one: `open_thunk` keeps its target as a literal word, so pointing a
context field at the card wrapper redirected nothing. The dynamic `RFile::Open`
went straight to efsrv. Building the logging thunk **around** the card wrapper
is the fix.

### What happened when both were right

| | before | after |
|---|---|---|
| records | 2392 | **2492** |
| card refusals | 0 | **1** (`-21`) |
| opens | 6, the last failing | **7, and the last two succeed** |
| state path | 47 13 36 29 34 **4** | 47 13 36 29 34 -> **38** |
| ending | `User::Leave(-2)` / `User::Exit` | **neither appears in the log** |
| launches | 6-8, relaunching | **1** |

**State 34 passes.** The wall that has ended every run since the protection was
first reached is down, and the game no longer gives up: there is no `User::Leave`
anywhere in the log. It runs on past the frame loop and faults, which is an
ordinary bug and the kind this project knows how to work.

The read-only game card was the answer after all. It just needed the rule to
actually run.

### What is still true

E72: with the verdict override off, the real check still does not pass, and the
run dies early the way E52 did. So the card rules change what happens *after*
the check, not the check itself, and `PATCH_THE_CHECK` stays. What it overrides
is still exactly what it was: a test for a physical N-Gage game card.

## The check returns an object, and the protection completes

State 38 settled what `0xe6f34`'s `mov r0, #2` had been getting wrong:

```
cdf80  ldr r9, [sp, #28]      @ the check's answer, stored by state 29
cdf84  ldr r4, [r9, #20]      @ a read at 2 + 20
```

The check returns an **object**. The 2 it produces on its null-argument exit is
legal only to a caller that never dereferences it, and state 29's caller is not
that caller. Forcing 2 took the run past state 34 and straight into a read at
address 0x16 -- the fault that ended E69 to E73.

So hand it an object. Sixty-four zeroed bytes from the heap, three words of
patch because the address is only known at run time:

```
e6f30  ldr r2, [pc, #4]       @ the literal at e6f3c
e6f34  b   e6f58              @ into the identity chain that returns r2
e6f3c  <the buffer>           @ written by the loader
```

`[+20]` then reads 0, a length of nothing, and the call state 38 makes with it
asks for nothing.

### What that did

| | E73 | E74 |
|---|---|---|
| records | 2492 | **5337** |
| traced events | 176..207 | **336** |
| state path | `47 13 36 29 34 38` then a fault | **`47 13 36 29 34 38 18 61 32 40 23 24 59 7`** |
| times round | once | **five**, once per call of the loop at `0x2e7c` |
| opens | 7 | **11, ten succeeding** |
| `User::Leave` | absent | absent |

**The protection runs to completion.** Fourteen states, five registrations, and
the machine reaches state 7 -- its ordinary return -- every time. The previous
best this project had ever recorded was 200 traced events; this is 336.

### The lesson, which is the same one twice

Three times now the thing standing in the way has been an answer of mine that
was *plausible* rather than right, and each time the evidence for it was an
absence: no refusal fired, no record appeared, a small integer looked like a
status code. An absence is the weakest evidence there is, and this file should
treat one as a question rather than a result.

## The variance was the harness, and the result holds

Three re-runs of build 76 read 22, 48 and then a partial run, against E74's 336
traced events, and for a while it looked as though E74 had been a fluke. It had
not. `emurun.sh` killed the emulator after forty-five seconds, and the run now
takes longer than that because **it gets further**: given `TMO=120`, four
launches in one session reach 5337, 5337, 5335 and 5335 records. The short reads
were launches caught partway.

`emurun.sh` now defaults to 120 seconds. Two smaller things this exposed and
that are worth writing down:

- **Xvfb had died**, and the script's `pgrep || start` did not notice it in time,
  so one run was made against no display at all. A run against a dead display is
  not a short run, it is no run.
- **The box is unreliable when a build relaunches.** `g6box.dat` is written by
  whichever launch wrote last, so with several launches in a session it can
  report 0 events while a 5337-record log sits beside it. The largest
  `g6box[0-9].log` is the measure; the box is only good when `launches` is 1.

Three runs were nearly written off on harness artefacts. The rows are kept as
E75 to E77 rather than deleted, because "the build regressed" has been wrong in
this file before -- four times, by the Retracted table's count -- and every one
of those was read off a number the harness produced rather than the port.

### Where the run ends now

246 import events, the protection's fourteen states five times over, then a
loop of `RLibrary::Load` / `Lookup` / `Close` with `HBufC16::New` and
`Math::Random` between -- the game loading resources, which is what it should be
doing -- and it stops inside a `__builtin_delete` of a 180-byte cell.

## It stops because it has finished, not because it broke

Two questions were open about the 5337-record run: whether that number is an
ending or the emulator's timeout, and what kills it.

**It is an ending.** With `TMO=240`, twice the time, the largest log is the same
42696 bytes. Nothing is being cut off.

**Nothing kills it.** Eight launches in that session produced exactly **one**
panic -- a `G6MEM`, which is our own loader's, raised when `user_alloc(1616972)`
failed to get room for the image on a relaunch. The other seven neither
panicked, nor left, nor exited. They ran their startup and went quiet.

So the state of the port is now: it loads the image, satisfies the protection,
loads resources, draws a frame, and waits. That is the first time in this file's
history that a run has ended without giving up or faulting.

### What it is waiting for

`frames 1`. The frame loop ran **once**. A game that was running would go round
it, so the port is not idling in a render loop -- it did one frame and stopped.
The standing gaps say what is most likely missing, and they have been on the
list since the beginning:

- **`OfferKeyEventL` is unbridged**, so there is no input at all. A startup that
  ends at a menu waiting for a key would look exactly like this.
- The frame loop is driven by a timer that is really ours, and `CActive::Cancel`
  goes through `gate6_cancel`; if the timer is not being re-armed, one frame is
  all there would ever be.
- Screen geometry is still 176x208 against the device's 240x320.

None of those is protection and none of them needs hardware to work on.

### The one panic, which is ours and not new

`G6MEM 1616972` is `PANIC(CAT_MEM, size)` in `gate6_load`: 1.6 MB for the image,
refused. With `LEAK_EVERYTHING` on and the run now going much further than it
used to, each launch takes far more heap than it did and never gives any back,
and a relaunch on top of processes that are still alive eventually cannot get
its image in. It is a consequence of running further, not a regression, and it
only hits relaunches.

## One RunL, and it never comes back

`frames 1` looked like a timer that was not being re-armed. It is not.

A log either side of `old_call(c->oldTimer, OLD_RUNL)` says the game's frame
function is entered **once**, at record 65, and **never returns**. The other
5273 records -- the whole protection sequence, the five registrations, the
resource-loading loop, every file open -- happen inside that single call. The
game has never reached its second frame because it has not finished its first.

So the timer bridge is fine and always was. The question is what the first frame
is waiting for.

### It is spinning up its sound server

The dynamic lookups name it. Of the thirteen ordinals this run resolves, two are
called exactly once each at the end:

```
old  289 -> new 1158   RThread::Create(TDesC16 const&, int (*)(void*), ...)
old  954 -> new 1795   RThread::Resume() const
```

and the emulator's own log agrees:

```
Thread SoundServer created with start pc = 0x90b8660, stack size = 0x186a0
Thread 872578056      created with start pc = 0x9bcb710, stack size = 0x2000
```

The game creates a **SoundServer** thread with a 100 KB stack, resumes it, and
the main thread goes quiet immediately afterwards. Nothing traced happens again,
which is what waiting looks like: a main thread blocked in the kernel makes no
calls at all.

That is a completely different kind of problem from everything before it. It is
not protection, it is not a shim answer being wrong -- **it is the game booting
its audio subsystem**, and the port getting far enough to do that is the point.

### What to look at next

Whether the SoundServer thread runs, and what the main thread is blocked on. The
second is the harder one: a thread waiting in the kernel is invisible to a log
made of call records, so it needs a different instrument -- a marker the
*worker* writes, or the emulator's own view of the thread's state.

## The threads are created and never entered

Two questions were left: does the SoundServer thread run, and what is the main
thread blocked on. The first is answered, and answering it took two attempts
because the first instrument was silent in exactly the way the bug is.

**A breadcrumb at each worker's entry point logged nothing** -- and could not
have. An `RFs` session belongs to the thread that made it, so a write from one
of the game's threads through the main thread's handle answers an error and
leaves no record. "No record" and "never ran" look identical, which is the
mistake this file has now made three times.

So the crumbs **panic** instead. A panic needs no handle and the emulator prints
the category. And a third crumb went in at `0xcc7e4`, the object maker, which
the log has running on every launch, as a control:

```
panicked with category: G6WRK and exit code: 972      <- the control, 0xcc7e4
(nothing for 970, 0xb8660)                            <- SoundServer
(nothing for 971, 0xcb710)
```

**The mechanism works and the workers do not run.** The game creates two
threads -- the emulator names one `SoundServer`, both start at offsets inside
the loaded image, so they are the game's own code with our shim under them --
resumes them, and neither ever reaches its first instruction.

That is now the thing to explain. `RThread::Create` and `RThread::Resume` are
both resolved through our own lookup, old 289 -> new 1158 and old 954 -> new
1795, and an `RThread` is a handle whose layout the shim has never had to think
about. A `Resume` on the wrong handle would look exactly like this.

## Eleven emulators

`emurun.sh` ended with `pkill -x eka2l1_qt`, and the emulator does not always go
on SIGTERM. **Eleven of them were still alive**, the oldest forty minutes old,
each holding its memory and three or four per cent of a core. That is where the
`G6MEM` panics came from: our loader asking for 1.6 MB in a machine that had
eleven dead emulators in it. With them cleared, a clean run has **no panics at
all**.

The script now sends SIGKILL a second later. `-x` stays: `pkill -f` would match
the shell that started it.

That is the third harness artefact in two sessions -- after the forty-five
second timeout and the dead Xvfb -- and all three produced numbers that looked
like the port getting worse.

## `RThread::Create` answers KErrGeneral

Wrapping both thread calls where our own lookup answers them gives the whole
thing in five records:

```
867  -2          @ RThread::Create answered KErrGeneral
868   0          @ and the handle it left in the object
869  0x1b44458   @ RThread::Resume, called on that object
868   0          @ whose handle is still zero
867   0          @ and Resume answered KErrNone
```

**The threads are not failing to start. They are failing to be created**, and
the game does not look at the error -- it resumes a null handle, gets
KErrNone for its trouble, and carries on into the wait that never ends.

Not memory: repeated with fifteen gigabytes free and every stale emulator
killed. Not flaky: identical both times, down to the record index.

### The mapping is right, which rules out the obvious

Old euser 289 and new euser 1158 are the *same* signature --
`RThread::Create(const TDesC16&, int (*)(void*), int, RAllocator*, void*,
TOwnerType)`. The old library has a second overload at 291 taking heap min and
max as ints, and 9.x has the matching one at 1159, so the shim could have paired
them wrongly and did not. That was the likely explanation and it is wrong.

### What the emulator's own log points at

The line the emulator prints on a successful creation --

```
Thread SoundServer created with start pc = 0x..., stack size = 0x186a0
```

comes from **`thread_create_eka1`**, the EKA1 executor path in
`src/emu/kernel/src/svc.cpp`. That function returns `error_general` in exactly
one place: when `create_and_add<kernel::thread>` gives back
`INVALID_HANDLE`, before the log line is printed. Which matches what we see --
`-2` and no log line for the launch being watched, while other launches in the
same session print eleven successful creations.

So the next question is why that allocation fails for this call and not the
others, and it is a question about the emulator's own code, in a repository we
have open.

## Making the emulator say why

`thread_create` in `src/emu/kernel/src/svc.cpp` returns `error_general` when
`create_and_add<kernel::thread>` gives back `INVALID_HANDLE`, and said nothing
about it. Four lines of `LOG_ERROR` there -- an improvement to the emulator in
its own right, since a silent failure is the hardest kind -- and it answers at
once:

```
Thread -590328542 NOT created: pc = 0x47cb710, user stack = 0x2000,
    heap 0..0, allocator = 0x8b2ad8, ptr = 0x40f868,
    owner = 75282192, total size = 64
```

Two things fall out.

**The failing thread is the second one.** `pc = 0x?cb710` with an 8 KB stack is
the unnamed thread, not `SoundServer` -- which has a 100 KB stack and is created
successfully in the same run. So the sound server is fine and something else is
not.

**The name and the owner are garbage.** `owner` is an `epoc::owner_type`: it can
be 0 or 1, and it is 75282192. The name prints as a negative number, which is
what `to_std_string` makes of a descriptor that is not one. Meanwhile the `info`
block beside them is perfectly sound -- a real image offset for the pc, a
sensible stack, a plausible allocator pointer.

So of the three arguments the kernel is handed, the third is right and the first
two are wrong. That is the signature of **arguments landing in the wrong
places**, and the shape that produces it is a call made with one overload's
argument list and received with another's: old euser 291 takes
`(name, fn, stack, heapMin, heapMax, ptr, owner)` where 289 takes
`(name, fn, stack, RAllocator*, ptr, owner)`, and a 289 answered where 291 was
asked would slide every stack argument along by one.

Our lookup log says the call came through as old 289 -> new 1158, which is the
`RAllocator*` pair and correct. The next instrument has to show what the game
actually pushed: the saved r1 and the three stack words at the call.

### A note on the boundary

This is a change to the emulator's own source rather than to
`.claude/asphalt2/`. It earns its place there -- a kernel call that fails
silently is worth a log line whatever is being run -- but it is the first time
this work has touched the project proper, and it is committed on its own so it
can be dropped without taking anything else with it.

## The kernel's owner argument is the thread function

Two more instruments, and between them they put the fault in a very narrow
place.

**The game's call is impeccable.** Logging the whole frame it pushes:

```
r0  this   = 0x3dc8a60
r1  name   = a type-3 descriptor, length 11, max 64      -- well formed
r2  fn     = 0x98cb710                                   -- a real image offset
r3  stack  = 0x2000
[sp+0]     = 0            -- aHeap  = NULL
[sp+4]     = 0x3c9f550    -- aPtr
[sp+8]     = 0            -- aType  = EOwnerThread
```

That is ordinal 289's signature filled in correctly, argument for argument.
Nothing is wrong on our side of the call.

**And the kernel receives the function pointer as the owner type.**

```
Thread ... NOT created: pc = 0x47cb710, ..., owner = 75282192, ...
                             ^^^^^^^^^                ^^^^^^^^
                             0x47cb710         75282192 == 0x47cb710
```

The same word in both places. `epoc::owner_type` can be 0 or 1; it is holding
the address of the thread's entry point. So between the game's correct call and
the kernel's handler, **an argument has moved**, and it is not the info block --
that arrives sound, `total_size` 64, every field where it should be.

Two candidates remain, and they are distinguishable:

1. **9.x euser reads the game's arguments one slot off.** The game is GCC98r2
   and euser is EABI, and while the two agree on scalars, they have not been
   checked against each other for this call.
2. **EKA2L1's `thread_create` bridge has the wrong parameter list for this
   ROM's exec.** It is registered at `0x68` in two tables and `0x67` in a third,
   and its second parameter may simply not be the owner.

The second can be settled by printing the raw registers at the bridge and
comparing them with the frame above, which is the next thing.

### Side note: a null check the bridge did not have

`thread_create` did `thread_name_des.get(pr)->to_std_string(pr)` with no null
check, and an anonymous thread is legal. It is hardened now. It was not this
bug -- the pointer is `0x40f74c`, not null -- but it would have been someone's.

## Retraction: the thread-create failure was my own thunk

`RThread::Create` does not answer KErrGeneral. It works, and it always did.

Taking the wrapper off it gives **seventeen thread creations and no failures**.
Every part of the story E84 to E86b told -- the `-2`, the handle left at zero,
the garbage owner, the garbage name -- was produced by the instrument that was
watching.

### Why

`frame_thunk` does this:

```
stmdb sp!, {r0-r4, r12, lr}     @ seven words
ldr   r12, =target ; blx r12
```

Seven words is twenty-eight bytes, and the target reads its **stack** arguments
relative to the stack pointer it is entered with. `RThread::Create` takes six
arguments: four in registers and `aHeap`, `aPtr`, `aType` on the stack. euser
read those three out of our saved registers, passed the thread function where
the owner type belonged, and the kernel refused the create.

The same objection applies to `open_thunk`, `self_thunk` and `result_thunk`.
They are all safe where they are used today -- `RFile::Open`, `Create` and
`Replace` take four arguments, all in registers -- but the rule has to be
written down, because nothing in the code says so:

> **A thunk that pushes before it calls can only wrap a function whose
> arguments all fit in registers.** Four or fewer, and nothing variadic. For
> anything wider the wrapper has to replicate the caller's stack frame or stay
> out of the way.

### What survives

- **E80 stands.** `RunL` is entered once and never returns. No thread wrapper
  existed when that was measured.
- **E82 stands.** The workers are created and never entered, and it had a
  control -- a third crumb at `0xcc7e4` that fires.
- **E86's frame dump stands** as evidence in its own right: the game's call
  really is impeccable, `aHeap` null, `aPtr` set, `aType` zero.
- The two emulator patches stand on their own merits: a kernel call that failed
  silently now says why, and a null name descriptor is no longer dereferenced.

So the question is back where E82 left it: the threads are created, and they
never run. Three rounds were spent on a fault that was not there.

### How it should have been caught

The clue was in the first measurement and I read past it. E84 reported that
`RThread::Create` failed **in the same runs where the emulator logged
`Thread SoundServer created`** -- two creates, one working and one not, through
the same euser. The one that worked was the static import; the one that failed
was the only one with a wrapper on it. That is not a subtle tell.

## Created, resumed, queued, and never run

With the artefact out of the way, the worker question was asked again properly,
and every link in the chain now has its own evidence:

| | |
|---|---|
| `RThread::Create` | succeeds -- eighteen creations, no failures |
| the handle it writes | `0x5e005a`, a real one |
| `RThread::Resume` | called on that handle |
| the emulator's view | `thread_resume SoundServer (handle 0x20001c) in state 0` |
| state 0 | `create`, the one case that calls `schedule()` |
| the breadcrumb at the entry | `planted: [970, 971]`, refused: none |
| the breadcrumb firing | **never** |

So the threads are created, resumed, queued ready with their entry points
instrumented, and **not one instruction of either is executed**.

### Where that puts the fault

Not in the shim. Every argument the game passes is right, the handle is real,
and the emulator agrees it has queued the thread. `next_ready_thread` picks the
highest-priority ready queue, and `thread_create` hands every thread
`priority_normal` regardless of what the caller asked -- so the worker sits in
the same queue as the main thread and waits for a switch.

The switch never comes, and the main thread makes no traced call after the
resume. Two readings fit:

1. **The main thread is spinning** in game code, waiting for a flag the worker
   would set. No syscall, no timeslice, no switch -- and on a real phone the
   kernel would have preempted it. The emulator's CPU sat at 23-35% rather than
   pegged, which argues against this, though the Qt side is in that figure too.
2. **The main thread is blocked** and the scheduler still is not picking the
   worker.

They are distinguishable from inside the emulator: log `reschedule()` and what
it picks. That is the next thing, and it is a question about EKA2L1 rather than
about the port -- which is the second time this week that the answer has been on
that side of the line.

### Two notes from the instrument

`crumb_plant` refused silently, which is how a round was lost believing the
workers never ran when the crumb might simply not have been there. It reports
now, either way.

And four note codes were added on top of ones already in use -- 870 and 871 are
`NOTE_RESULT` and `NOTE_RESULT_OF` -- which would have made the decoder lie.
Caught before the run. 800 to 849 is empty and the new ones live there.

## Is a generic loader viable? The ordinal directory, measured

The port redials: the game asks for an old ordinal, the shim answers with a
9.x one. For one game that map is 462 entries and every one was looked at.
A **generic loader** -- one binary that runs any N-Gage title, with the game's
files supplied separately -- needs the map to cover the whole platform, and
nobody is going to look at fifty thousand entries. So the question is what
the error rate of a generated map would be, and whether the errors land in
functions games actually call.

`toolchain/port/ordcheck.py` measures both halves. It is desk work: no
device, no emulator run.

### The new side: 96.7% of the platform is in the safe direction

`gen_shim.py` resolves 9.x ordinals out of the Symbian source release. The
phone runs its own ROM, not the release, and the two disagree about export
counts almost everywhere -- euser by 317, avkon by 159, efsrv by 70.

That sounds fatal and is not, because **Symbian froze its .def files**: a
later release appends exports and never renumbers, which is the whole reason
a 9.1 binary runs on a 9.3 phone. So the sign is what matters, and over the
153 libraries present in both the RM-409 ROM and the release:

| | libraries | |
|---|---|---|
| release == ROM | 79 (51.6%) | identical numbering |
| release > ROM | 69 (45.1%) | release is newer -- safe under append-only |
| release < ROM | 5 (3.3%) | release is **older** than the phone |

Every library the port binds to is in the safe direction -- bitgdi +19,
cone +11, efsrv +70, euser +317, ezlib +14, gdi +45 -- and the five going
the other way are `bsulinifile`, `epbusm`, `dfprvct2_1`, `drtrvct2_1` and
`responsemsg`: driver and runtime odds and ends that no game imports.

The architecture already handles the residue. An ordinal the release knows
and the phone does not is simply absent, and imports resolve through
`RLibrary::Lookup` at run time rather than a static import section, so it
comes back **null and gets reported** instead of failing the load. That was
decided for a different reason in build 4 and it is what makes the loader
safe to attempt.

### The old side: 100% of what a game actually imports

EKA2L1's `epoc6.def` lists 555 libraries' exports in ordinal order. The only
authoritative old-side list is `7.0-euseru.def`, so euser is the one library
that can be scored:

* **86.7%** of all 1,646 positions name the same function outright;
* **89.2%** once methods reparented in 9.x are counted as hits
  (`RHeap::AllocL` became `RAllocator::AllocL` at the same ordinal);
* and of the remainder, inspection shows most are still renames of the same
  code -- `memclr`/`Mem::FillZ`, `User::Heap`/`User::Allocator`,
  `RSessionBase::Share`/`DoShare`, `TLex8::Val`/`BoundedVal`;
* **96.3% of the 164 euser ordinals Asphalt 2 actually imports** -- 6 wrong.

### Retracted: "100% of the 25 ordinals a real game imports"

The first run of this scored the directory against
`crack/binpda_6rbc.app` and reported **100% of 25 euser ordinals**. That
file is **not the game**. It is the BiNPDA crack *loader*: 3,964 bytes,
eight DLLs, a separate program that loads `bin\main.dll` and rewrites seven
of its imports -- described two directories away in `crack/README.md`, which
I had read. The game's own `6rbc.app` is 21 DLLs and 462 imports, 164 of
them euser, and it lived in a scratchpad that has since been cleared.

So the headline was a real measurement of the wrong file. `ngage-imports.txt`
keeps the true list and `ordcheck.py` now reads it by default.

There was a second signal I walked past: `epocdb.py`'s own docstring says
"155 of the 164 ordinals the N-Gage game actually imports" -- 94.5%, and
flatly inconsistent with 100% of 25. **A number that disagrees with what is
already written down is the cheapest error check there is, and I did not
make it.**

### What the six are

    3  User::AllocZL(int)          epoc6: CBase::newL(unsigned int)
  411  memclr                      epoc6: Mem::FillZ(void *, int)
  528  User::Allocator(void)       epoc6: User::Heap(void)
  871  User::ReAllocL(void *, int) epoc6: User::ReAllocL(void *, int)
 1496  TInt64::operator=(int)      epoc6: TInt64::operator=(int)
 1582  User::AllocZ(int)           epoc6: CBase::operator new(unsigned int)

Four are the same function under two names: 411 and 528 are renames the
`epocdb` docstring already called out, and 871 and 1496 are the comparison
failing on the release's digit suffix and on operator mangling rather than
on the data. Two -- 3 and 1582 -- pair `User::AllocZ{,L}` against `CBase`'s
allocating `new`, which are near-certainly aliases at one address, and
**that is a guess, not a measurement.**

So: 96.3% strict, and somewhere between that and 100% in practice, with two
unproven. The strict figure is the one to quote. The errors are in the tail
-- `TBusLocalDrive::Lock`, `RNotifier::LoadNotifiers` -- not in the
allocator, the descriptors or the file server.

### Verdict

### Six games, and the limit is closed

Five more N-Gage titles were read: **One**, **Colin McRae Rally 2005**,
**Asphalt Urban GT** (the first one), **Ashen** and **Call of Duty**, all
retail card dumps. Every one is a plain EKA1/GCC98r2 E32 image --
**uncompressed, unencrypted, imports readable straight off the original**,
with no cracking involved. Colin McRae keeps its engine in a second
executable, `6r66.nax`, which reads the same way.

Over the union of all six games' euser imports:

> **368 distinct ordinals, 5 disagreements -- 98.6%.**

The five are `User::AllocZL`, `memclr`, `User::Allocator`,
`User::ReAllocL` and `User::AllocZ`. Three are the same function under two
names (`memclr`/`Mem::FillZ` and `User::Allocator`/`User::Heap` are renames
`epocdb.py` already documents; `User1::ReAlloc1L` is the release's own digit
suffix). The other two pair `User::AllocZ{,L}` against `CBase`'s allocating
`new` -- near-certainly aliases at one address, and still unproven.

Getting there needed one more fix to the comparison, worth separating from
tuning: the first union run scored **19** wrong and **15 of them were
operators** -- `__as`, `__pl`, `__eq`, `__lt`, `__apl` and friends, every
one the identical function on both sides. GCC98r2's operator codes are a
closed, documented set, so decoding them is reading the format rather than
fitting the answer. 94.8% -> 98.6%.

### What the five games are shaped like

| game | imports | display path |
|---|---|---|
| One | 532 | **identical to Asphalt 2** |
| Asphalt Urban GT | 391 | **identical to Asphalt 2** |
| Ashen | 354 | CFbsBitGc through a context |
| Call of Duty | 427 | CFbsBitGc through a context |
| Colin McRae 2005 | 583 + 53 | window server only, engine in a `.nax` |

**One and Asphalt Urban GT import the same three bitgdi entries as Asphalt
2** -- `SetAutoUpdate(TInt)`, `SetClippingRegion`, `Update(const TRegion &)`
-- and the same two ws32 ordinals, **348 and 350**, which are this port's
imports 460 and 461: `CDirectScreenAccess::NewL` and `StartL`. The same
architecture, down to the ordinal. Everything rounds 74 to 89 bought --
the framebuffer conversion, the scaler, the picture modes, and the posted
region behind the band -- applies to them unchanged. **The band bug would
have hit both of them, identically.**

Ashen and Call of Duty take a graphics context from `CFbsDevice` and never
call `SetAutoUpdate` or `Update`, so they do not take the screen the same
way: a different display path, and real work. Colin McRae imports no bitgdi
at all.

Four of the five need `gamecomms` and `nokiafc`, which the shim already
stubs, and four of five use the MDA audio stream, which the port already
bridges.

**So the next game to port is One or Asphalt Urban GT**, and neither should
need much beyond the loader that exists.

### Verdict

**Go.** euser is still one library -- it is the only one with an
authoritative old-side list -- but the sample within it is now six games and
368 ordinals rather than one game and 164.

### Two instrument bugs, caught here rather than later

Both by reading the sample rather than the percentage, which is now the
standing rule on this project.

* The first cut compared `ASin__4MathRdRCd` against
  `Math::ASin(double &, double const &)` as **text** and scored 3%. Same
  function, two spellings.
* The second demangled, but `gnuv2.demangle` does not handle `G`, GCC98r2's
  marker for a class argument passed by value, so `User::After` still read
  as a disagreement. Class and method now come straight off the mangled
  symbol.

Seventh and eighth time the instrument was the finding -- and the
wrong-file retraction above is the ninth, caught only because the question
"do the games need cracking?" sent me back to look at what the input
actually was.
