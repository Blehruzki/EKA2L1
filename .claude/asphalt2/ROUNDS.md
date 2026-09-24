# Hardware rounds

One row per build that went to the phone. The point of this file is so that a
test is never asked for twice: before proposing a run, check whether it is
already here.

**How to use it against me.** If I ask for a hardware run, ask which row it is
not a repeat of. If I state a finding, ask which row established it. If a row's
"settled" column is empty, that round bought nothing and I should say so rather
than let it blur into the next one.

**Both machines are in here.** The emulator table below came late: fifteen
emulator runs happened while only the hardware rounds were being written down,
and they existed only as prose in `PORTING.md`, where a repeat could not be
caught. `emurun.sh` writes each run's row itself now and `checkrec.py` will not
let a stub row survive a commit, so "I forgot to log it" is not available.

The phone is a **Nokia N95** (Symbian 9.2, S60 3rd FP1). The emulator runs an
RM-409 (5320, 9.3 / FP2). Counts are *traced events* unless stated.

## Emulator runs

Hardware rounds are not the only tests. Most of the work happens here, and the
same rule applies: a run that is not written down gets repeated. `emurun.sh`
appends a row automatically with the record count and the fault, leaving
`TODO` in the last column; `checkrec.py` refuses a commit while any `TODO` is
still there, so the row has to be finished before the next thing is done.

Rows before the script existed were reconstructed from the session that ran
them, and say so.

| # | The one change | records | ends | What it settled |
|---|---|---|---|---|
| E1 | baseline, neighbour read off *(reconstructed)* | 1066 | `0x30002` | The reference point for rounds 48-50 |
| E2 | 3 extra records per free, **no dereference** | 1258 | `0x30002` | **Log volume does not perturb the emulator.** The earlier `0x9B0000` really was the read |
| E3 | ring-vouched neighbour dereference | 1236 | `0x30002` | Safe. Shipped as build 48 |
| E4 | `LEAK_EVERYTHING` | 1188 | `0xEAF88580` | Leak verified in effect: every freed pointer unique. Does not pass the wall |
| E5 | + `W_LEAK` box flag | 1188 | `0xEAF88580` | The flag is not behavioural. Shipped as build 49 |
| E6 | `arg_thunk` records `lr` | 1188 | `0xEAF88580` | **The fatal delete is at image offset `0xcc8c0`** |
| E7 | 4 probes after the delete | 1208 | `0xEAF88580` | 990 and 991 fire: the run gets past the delete |
| E8 | 6 probes | 1210 | `0xEAF88580` | 992 fires, 993 does not. Shipped as build 50 |
| E9 | probes at `0xcc864` and `0xcc894` | 1230 | `0xEAF88580` | `this->[4]` good at entry, garbage after |
| E10 | + probe at `0xcc874` | 1240 | `0xEAF88580` | **`bl 0xe9988` is the one call that poisons it** |
| E11 | 10 probes *inside* `0xe9988` | 595 | – | Run dies before any marker. The function will not take a patch |
| E12 | + inner probes silent until the watch latches | 553 | – | Not the logging. The planting itself |
| E13 | a single inner probe at `0xe9990` | 553 | `0x8D8EE…` | Confirms it: one patched word in that function ends the run |
| E14 | `watch_note` in `gate6_result` / `gate6_arg` | 1268 | `0xEAF88580` | No perturbation, and the poison lands in a window between an allocation and the `delete` at `0x104828` |
| E15 | + `watch_note` in `gate6_trace` | 1292 | `0xEAF88580` | Same window with three times the stations. Current state |
| E16 | self-test of `emurun.sh` (no source change) | 1292 | `0xEAF88580` | The runner logs itself, and `checkrec.py` refuses the stub. Also an independent repeat of E15, to the record |
| E17 | watch four words of the object, not just the poisoned one | 1538 | `0xEAF88580` | Only word 1 of the object changes. Words 0, 2 and 3 are untouched, so this is a **deliberate single-word store**, not an overrun -- which rules out a stray write from our own shim |
| E18 | probe at 0x104824, in the other function of the window (+ 4-word watch in the probe) | 1550 | `0xEAF88580` | **0x103774 tolerates a probe** where 0xe9988 does not, so the window can be bisected. Field good at the last wrapper, bad at 0x104824 |
| E19 | probe both indexed array stores, reporting r5 and ip | 2250 | `0xEAF88580` | Not those two. All 50 firings fill two 5-element arrays at 0x40f640 and 0x8d9144; neither ever addresses the field. But the flip is bracketed between them |
| E20 | quiet probe on the 0x1037ac dispatcher, reporting the block key when the field changes | 1556 | `0x1C976000` | **The store is `str sl, [ip]` at 0x1082c0**, named by the dispatch key `0xe649867c` the quiet probe reported. **Perturbed**: the fault moved to `0x1C976000`, so the site needs confirming on a clean run |
| E21 | single target-filtered probe on the store at 0x1082c0, no dispatcher probe | 1558 | `0x1C976000` | **Confirms E20 on its own terms.** A target-filtered probe on `0x1082c0` fired exactly once, with `ip = this+4` and `sl = 0x1c975dc0` -- the store names its own target, no dispatcher probe involved |
| E22 | loud probe at 0x108298 on the array[5] load feeding the store | 1564 | `0x1C976000` | The block runs **once**. `sb = 0` at the `mla`, so `array[5]` (`0x782efefd`) is multiplied away and the stored value is a pure function of `r8` |
| E23 | probe r8 and r6 at 0x108290, and r4 at 0x1082a0 | 1570 | `0x1C976000` | `r8 = 0x42d08240`, matching the inversion's `r4 = 0` candidate exactly. `755139455 * r8 = 0x1c975dc0`, the observed value -- **the whole chain is now verified arithmetic**, and `r8` arrives already wrong |
| E24 | read [field + 0x240] at every station, the word 0x139588 dies on | 1653 | `0x1C976000` | **The pre-call value is the right one.** `[field + 0x240]` -- the exact word `0x139588` dies reading -- is a good heap pointer at all 83 stations before the store. The field held a valid object and the call overwrote it |
| E25 | NOP the store at 0x1082c0 -- keep the pre-call value in this->[4] | 1832 | `--` | **The wall comes down.** NOP the store and there is no access violation at all: 292 imports against 248, on through two more `RLibrary::Load`s, and the game then *leaves* cleanly. A workaround, not an explanation -- but the first thing to move this since build 44 |

<!-- EMURUN -->

## Builds 1-30 (before the record was kept this way)

Not itemised, and that is itself a finding. These rounds were spent on reboots
that turned out to be **ours** -- two `RFile`s closed with
`RHandleBase::Close`, which closes the file server session. Every theory built
on top of that (a write ceiling, a write rate, extending writes) was fitted to
its shadow. Roughly thirty rounds, and the honest summary is that they taught
us about the instrument rather than the game. Detail is in `PORTING.md` under
"The reboots were the handle".

## Builds 31 onward

| # | The one change | Runs | Result | What it settled |
|---|---|---|---|---|
| 31 | `LOG_ZOOM`, filenames on `RFile::Open` | 1 | reboot, 86 events | Located the death precisely for the first time; cost reach |
| 32 | Silent: ~4 writes, box only | 1 | **KERN-EXEC 3**, 128+ | No reboot. First evidence the instrument was the reboot |
| 33 | Box ring 16 -> 64 | 1 | KERN-EXEC 3, 128+ | Phone and emulator match event for event, offset 27 |
| 34 | Box every 8 events, name + exc result | 1 | reboot, 88 | Broke my own write budget. `SetExceptionHandler` returns KErrNone |
| 35 | Ring 128, back to 10 writes | – | superseded | – |
| 36 | **`RFile::Close`** for both bad closes | 3 | KERN-EXEC 3 | Fixed KERN-EXEC 0. The deterministic file-exists switch |
| 37 | `RFile::Open` result thunk | 2+1 | KERN-EXEC 3, **132** | `RFile::Open` returns KErrNone. Reference build, reran later to prove the phone had not changed |
| 38 | Free matching (4 changes at once) | 1 | "64 records" | Nothing. Judged a regression on one run |
| 39 | Heap walk + `CountAllocCells` import | 1 | "64 records" | Nothing |
| 40 | Heap walk able to report | 3 | 128 / short / short | Heap walks clean, **1209 cells** at event 128 |
| 41 | 38 + 40 together | 3 | "64 records" | Nothing |
| 42 | Panic on a failed write | 5 | "64 records", no `G6WR` | Writes are **not** failing. Killed the 512-byte theory |
| 43 | 37 + free matching only | 2 | "64 records" | The import was not the difference |
| 44 | Exact logging over the opening | 1 | **136**, dies in `User::Free(0x7b7cd8)` | 31 frees, all matched. Furthest yet. "64 records" was eight of our own log blocks all along |
| 45 | Flush each freed pointer before the free | 1 | 136, dies freeing `0x7b89a8` | The fatal pointer is now named. Its ring verdict is still missing: the verdict record is written but not flushed |

## What builds 38-43 actually cost

Six rounds, and the table above shows why: builds 38, 39, 41, 42 and 43 all
report "64 records", and **none of them were failing there**. Build 44, with
the same code, reached 136. Sixty-four records is eight `LOG_BLOCK` flushes of
eight, and the tail was sitting unflushed in the buffer the whole time.

Five of those six rounds settled nothing. That is the largest single waste in
this project after the reboots, and it came from reading an instrument's blind
spot as the game's behaviour -- the same mistake, for the fifth time.
| 46 | Flush the ring's verdict, not just the pointer | 1 | 136, dies freeing `0x7b8e20` | **The fatal free is legitimate.** Its pointer matched a live 27-byte cell -- no double, no stray. The verdict was the last record written, so the fault is in `User::Free` itself |
| 47 | Log the cell's header words before each free | 1 | 136, dies freeing `0x7b8e20` | **The header is healthy.** 27 bytes requested, header reads `0x28` -- exactly the emulator's pattern. The cell itself is not damaged |
| 48 | Read the *neighbouring* cell's header, ring-vouched | 3 | 128 events, all three identical, dies freeing `0x7b89a8` | **The header is right, and the neighbour corroborates it.** The ring independently holds an allocation at `next + 4`, so the cell really does end where its header says. Nothing about the free is corrupt |
| 49 | **Free nothing.** All three deallocation ordinals answered by a no-op | 3 | 128 events, identical, stops at the same 99th free | **`User::Free` is not the wall.** Nothing was freed -- every freed pointer in the run is unique where build 48 reused them -- and the run stops in exactly the same place. Retires rounds 44-48 |
| 50 | Six probes along the stretch after the fatal delete | 3 | identical; 990, 991, 992 reached, 993 not | **The delete was never it.** The run gets past it every time and dies at `0x139588`, `ldr r2, [r1, #0x240]`, with `r1` = `[r6+4]` = garbage. The same instruction EKA2L1 cannot run the *original* N-Gage binary past |
| 51 | **NOP the store at 0x1082c0** (+ the watch instrumentation) | pending | – | – |

## Where we are

**Furthest: 136 traced events** (builds 44 and 45, and build 48's box puts all
three of its runs in the same 128-159 window). The run gets through the whole
resource-loading sequence, opens `cwivenc.dat` successfully, and dies inside a
`User::Free`. Build 48 is where it stopped being a lottery: three runs,
byte-identical logs.

**Best round so far: 50.** It reached the actual fault -- `0x139588`,
`ldr r2, [r1, #0x240]`, with a `r1` nobody ever wrote -- and in doing so joined
our port's failure to the one EKA2L1 has always had with the original binary.
Everything before it was looking at the wrong instruction.

**Runner-up: 49.** It answered a question five rounds had only
narrowed, and answered it in the direction that retires them: with every
deallocation turned into a no-op, the run stops in exactly the same place. The
fault is not in `User::Free`, was never in the cell, and the whole
"which property of this cell is damaged" programme is closed. It is also the
cheapest round in the file -- one switch, already in the source.

**Runner-up: 48**, which made the run deterministic (three byte-identical logs)
and established the last of the measurements 49 then made moot.

**Before those: 44.** It was the first to show that "64 records" -- which
five earlier rounds had been scored on -- was our own log block hiding the
tail, and it reached 136 with the free matching working. Everything since is
refinement of what it exposed.

Before those, **36**, the `RFile::Close` fix: it ended a month of reboots and
is the single change that made hardware rounds informative again.

### What is known at the point of failure

| | |
|---|---|
| Heap at event 128 | walks clean, 1209 cells |
| The fatal free | a live 27-byte cell the ring recognised |
| Frees before the fatal one | 31, all matched a live cell, no doubles, no strays |
| `RFile::Open` | returns KErrNone |
| Setup state | identical to the emulator |
| The fatal cell's header | correct: `0x20` for a 27-byte request once reuse is off |
| Fatal call | the 99th `delete` of the run. The pointer moves with the heap layout; the call does not |
| **`User::Free`** | **innocent. Build 49 turned every deallocation into a no-op and the run stopped in the same place** |
| Determinism | three byte-identical logs in each of rounds 48 and 49 |

So the heap is sound, the pointers are sound, the open succeeds -- **and the
fatal free is of a live, known, 27-byte cell**. Round 46 closed the last gap:
the verdict record was the final thing written before the run ended, so
everything up to and including our own handler completed and the fault is
inside `User::Free`.

Builds 47 and 48 read the header and then the neighbour's header; both were
right. Build 49 then removed the free entirely and the run stopped in the same
place, which makes all of that moot: **the cell was never the problem.** What
is left is the game's own code after the 99th `delete` returns, which nothing
has looked at because the free was standing in front of it.

### What build 48 found

Three runs, and for the first time they are **identical**: 7624-byte logs to
the byte, 953 records, 128 traced events at the last box write, the same last
import (`RLibrary::Close`), the same 1932-byte stack high-water. Two died
freeing `0x7b89a8`, the third `0x7b8e20` -- the same free, one heap layout
apart. The run is deterministic now; three-run rounds are cheap confirmation
rather than a lottery.

Thirty-two frees carried a full verdict. The fatal one:

```
free 7b89a8  matched a live cell of 1b  header 28
  next cell 7b89cc   header 20   ring says live, 18 bytes
```

**The header is right.** `0x7b89a8 - 4 + 0x28` is `0x7b89cc`, and the ring
holds an allocation whose payload is `0x7b89d0` -- one word past it. That
record came from `User::Alloc` returning that address, not from our
arithmetic, so it is independent corroboration that the cell really does end
where its header says. The neighbour's own header, `0x20`, is the right size
for the 24-byte cell the ring says is there.

So the coalescing theory does not survive its own test. Heap chain, pointer,
size, header, and now the neighbour: every one of them measures correct, and
`User::Free` still faults.

### The one thing that distinguishes the fatal free

Of the 27 frees whose neighbour the ring recognised, 26 coalesce into a cell
the ring has already seen freed. The fatal one is **the only one whose
neighbour is still live**. That is not in itself wrong -- reading a live
neighbour's header is what a free does -- but it is the only measured property
that singles this free out.

### A column that looked like a finding and is not

Six of the 32 frees have a header larger than `align8(request + 4)`, the
fatal one among them (27 bytes in a 40-byte cell). That looked like damage for
about ten minutes. It is not usable: the request column is the *ring's* record,
and two ordinary things break it -- RHeap hands over a whole free cell rather
than splitting off a remainder too small to be a cell, and any deallocation
that does not go through ordinals 315, 408 or 410 leaves a stale live entry in
the ring for an address that was reused. Twenty-six of thirty-two fit the rule
exactly, including every large allocation. **The request column cannot be used
to call a header wrong.**

### What build 49 found

The leak was installed -- the box says `LEAK: nothing is freed`, and every one
of the 99 freed pointers in the run is unique, where build 48 freed `7b7c10`
three times and the emulator freed one address ten times. Nothing went back to
the heap.

And the run stops in exactly the same place.

| | build 48 | build 49 |
|---|---|---|
| traced events at the last box write | 128 | 128 |
| last import in the box | `RLibrary::Close` | `RLibrary::Close` |
| stack high-water | 1932 | 1932 |
| imports in the log | 248 | 248 |
| frees | 99 | 99 |
| verdicts | 32 | 32 |
| stops at | the 99th free | the 99th free |

**So `User::Free` is not the wall.** It never ran. The free is simply the last
thing written before the fault, and the ground between that record and the next
traced import -- the game's own code, after the `delete` returns -- is what has
been wearing the blame since build 44.

Five rounds of "which property of this cell is damaged" are retired by one
round that did not measure the cell at all.

### A number that was wrong, and is now explained

With nothing reused, the fatal cell's header reads `0x20` -- exactly
`align8(0x1b + 4)`, the size its 27-byte request calls for. Build 48 read
`0x28` on the same free. That was not damage: it was RHeap handing over a whole
recycled cell rather than splitting off a remainder too small to be one, which
is what the build-48 entry above already warned the request column could not
distinguish. The leak removes reuse, and the header snaps to the arithmetic.

### What build 50 found

Three runs, identical again. Markers 990 (`0xcc8c4`), 991 (`0xcc8ec`) and 992
(`0x10a9e4`) all fire; 993 does not.

```
marker 990 at 0x000cc8c4   r6 = 7d68e8   r4 = cea7a67f
marker 991 at 0x000cc8ec   r6 = 7d68e8   r4 = 1
marker 992 at 0x0010a9e4   r0 = a6dfb180
```

**The run gets past the fatal delete every time.** What it does not get past is
`bl 0x139568`, whose second instruction to touch its argument is:

```
00139588  ldr r2, [r1, #0x240]
```

with `r1 = 0xa6dfb180`. `0xa6dfb180 + 0x240` is the address the phone faults
on, and in the emulator the same instruction with `r1 = 0xeaf88340` gives
`0xEAF88580`, which is the fault address the emulator has been reporting all
along.

The eight words the probe dumps at `r6` are the object the bad pointer comes
out of:

```
[r6+00] 913458     [r6+10] 0
[r6+04] a6dfb180   <- the one that kills it
[r6+08] 7d76d0     [r6+18] 0
[r6+0c] 4800000    [r6+1c] 0
```

Three of those are plausible heap pointers and one is a large mapped address.
Only `[r6+4]` is nonsense, and it is different nonsense on the phone
(`a6dfb180`) from the emulator (`eaf88340`) -- the signature of a field nobody
wrote, read out of whatever the allocator happened to hand over.

### The finding that reframes the whole project

`0x139588` is **the instruction EKA2L1 cannot run the original N-Gage binary
past** -- KERN-EXEC 3, recorded in `PORTING.md` long before any of this, and
filed as an emulator deficiency. It is not one. Our port on real hardware dies
at the same instruction with the same kind of garbage in the same register.

So three separate runs -- the original game under the emulator, our port under
the emulator, our port on an N95 -- all stop at `ldr r2, [r1, #0x240]` because
`r1` was never initialised. The original game runs on a real N-Gage, so
something that fills `[r6+4]` on that device is not happening on any of the
three. That, not the heap, is the port's actual problem, and it has been
visible since before the reboot months.
