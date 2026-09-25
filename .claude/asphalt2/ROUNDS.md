# Hardware rounds

One row per build that went to the phone. The point of this file is so that a
test is never asked for twice: before proposing a run, check whether it is
already here.

**How to use it against me.** If I ask for a hardware run, ask which row it is
not a repeat of. If I state a finding, ask which row established it. If a row's
"settled" column is empty, that round bought nothing and I should say so rather
than let it blur into the next one.

**The three confirmations are generated, not remembered.**
`toolchain/port/rules.py` prints them from this file -- what is logged, how
many rows a new test has to be checked against, and how far we are with which
round was best. They were given for rounds 47 to 50 and then dropped at round
51 without my noticing. If a reply of mine reports a result or asks for a run
and does not carry them, that is the lapse, not an oversight in the format.

**Both machines are in here.** The emulator table below came late: fifteen
emulator runs happened while only the hardware rounds were being written down,
and they existed only as prose in `PORTING.md`, where a repeat could not be
caught. `emurun.sh` writes each run's row itself now and `checkrec.py` will not
let a stub row survive a commit, so "I forgot to log it" is not available.

The phone is a **Nokia N95** (Symbian 9.2, S60 3rd FP1). The emulator runs an
RM-409 (5320, 9.3 / FP2). Counts are *traced events* unless stated.

**The phone shows two panics per run, and has for many rounds:** one
**KERN-EXEC 0** and one **KERN-EXEC 3**. That was mentioned in rounds up to
about 40 and then stopped being mentioned, and I stopped asking -- so it went
unrecorded through every round since, while every theory in this file was built
around a single failure. KERN-EXEC 3 is an access violation; **KERN-EXEC 0 is a
bad handle**, which is a different fault with a different cause. Both are
kernel-side, so neither is one of our own `G6xxx` panics.

Two panics most likely means two *processes*: the app, and something starting
it again afterwards. The emulator shows exactly that shape -- the run ends with
`User::Leave`, `User::Exit`, and then our loader panics `G6MEM` failing to
allocate the image on a relaunch. **Which of the two the log we read belongs to
is not established**, and it needs to be before the next theory is built on it.

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
| E26 | wrap RLibrary::Load -- its name from r1, and its return code | 2097 | `--` | Every `RLibrary::Load` in the run resolves: `euser.dll` x12, `efsrv.dll` x5, all `KErrNone`. The only failure is `c:\system\cwdynlog.dll` -> `KErrNotFound`, which is the known protection path. **The bad-handle theory does not hold here.** The emulator run now ends with no fault at all |
| E27 | launch counter in the box, and a per-launch log name | 2097 | `--` | **Twelve launches in one 45-second session**, all byte-identical, each reaching 179 traced events and ending `User::Leave` / `User::Exit`. The app is in a relaunch loop, and every log in this project was whichever launch happened to be last |
| E28 | build 53 -- launch name built on the stack, writable-statics guard in the build | 2097 | `--` | Identical to build 52 in the emulator -- 12 launches, 2097 records, 179 events -- so the fix is behaviour-neutral there. The guard was tested by reintroducing a writable static: the build refuses it |
| E29 | build 54 -- per-launch log name from the clock, no file read at startup | 1832 | `--` | Launches land on digits 1-9, 1832 records each -- **exactly E25's count**, which confirms build 54 is build 51 plus the one change and nothing else. 179 traced events, no fault, and 265 records of write budget given back |
| E30 | build 55 -- guard the box write, log the tick and the box replace result | 1834 | `--` | 1834 records -- E29's 1832 plus the two new ones -- so the guard costs nothing. Record 2 is the tick, record 3 the box replace's result (`0` here). The emulator's replace always succeeds, so the guard itself can only be tested on the phone |
| E31 | build 56 -- guard file_flush as well as box_write | 1834 | `--` | 1834 records, unchanged from E30 -- the guard costs nothing and the emulator's box replace never fails, so again only the phone can test it |
| E32 | log the ordinal and its mapping in gate6_lookup | 1884 | `--` | The branch the phone does not take is a lookup of **old ordinal 121, mapped to 9.x ordinal 93**; the burst it skips is old 136 -> 9.x 255, twenty-six times. One ordinal in the run maps to nothing at all: old 355 |
| E33 | build 58 -- stand in front of `RFile::Read` and `RFile::Size` and log what they answer | 2060 | `0x45933C0` | The instrument works and costs no behaviour: 29 reads, exactly as E32. Every read takes a **TPtr8 with a 65536-byte maximum** (`0x20000000`, `0x10000`), answers **KErrNone**, and comes back with the length word at `0x20010000` -- a full 64 KiB, twenty-six times, then `0x2000007d` (125 bytes) at the end of the file, and two later reads of a 16-byte buffer. `RFile::Size` is called once. So the emulator reads about 1.7 MB in full chunks and stops when the file runs out. **This is the control the phone run needs**: the same four numbers from hardware say whether the short read is a failed call, an empty file, or a descriptor with no room in it |

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
| 51 | **NOP the store at 0x1082c0** (+ the watch instrumentation) | 3 | 144 traced events, 262 imports, 1563 records | **The wall is down on hardware.** 128 -> 144 events, 248 -> 262 imports, and the phone follows the emulator's new sequence import for import. First advance since build 44 |
| 52 | Launch counter in the box, one log file per launch | – | **nothing produced, KERN-EXEC 3** | Mine. I made `kLogPath` non-`const` to patch a digit into it, and **our image has no writable data section** -- `flat.ld` folds `.data*` into `.rodata` and `mke32.py` declares data and bss zero. The write faults before any file is created. The emulator maps that memory writable, so it ran 12 launches happily |
| 53 | Build 52 with the name built on the stack, plus a build-time guard | 1 | **reboot**, one launch only (`g6box1.log` + `.dat`) | The write does not fault any more -- the files exist. But a **reboot**, which has not happened since build 36, and only one launch where the emulator does twelve. **I broke rule 2**: 53 carries three changes against the last build known to survive (51) -- the launch counter's `RFile` open/read/close, the `RLibrary::Load` wrap, and a widened `arg_thunk` |
| 54 | Build 51 + per-launch log name from the clock, one variable | 3 | **no reboot**; every run is exactly 2 launches: one of 1563 records, one of **2** | **The reboot was one of the three things build 53 carried** -- it is gone with them reverted. And the second launch is visible for the first time: it writes `image loaded at` and `chunk ends at`, then dies. It never writes a box |
| 55 | **Guard the box write**, and log the tick + the box replace result | 3 | still KERN-EXEC 0 and 3; stubs now 4 records instead of 2 | **The ordering is settled: the full launch is FIRST**, the stub is the relaunch ~110 ticks (1.7 s) later, all three runs. So every measurement in this file was the first launch. And the stub's box `file_replace` returns **-6, KErrArgument**, every time. The guard was incomplete: `box_flush` still calls `file_flush` on the same handle |
| 56 | Guard `file_flush` too -- the other use of the same handle | 3 | KERN-EXEC 0 gone in 2 of 3 runs; **CONE 2** new in all three; relaunch goes from 4 records to **148** | **The guard worked.** The relaunch no longer dies on our bad handle -- it runs into the framework and fails honestly on `RFile::Open` = **-14, KErrInUse**, because the panicked first process still holds the game's data file. The relaunch is a *consequence*, not a second bug |
| 57 | Log the lookup ordinal and its 9.x mapping | 3 | same ordinals and mappings as the emulator, exactly; all three runs byte-identical in structure | **The ordinal theory is dead** -- the phone maps exactly as the emulator does. My first reading of this row ("three euser asks answered out of efsrv") was **wrong and is retracted**: they were efsrv asks, and 121/136/185 -> 93/255/264 is `RFile::Open`/`Read`/`Size` -> `RFile::Open`/`Read`/`Size`, correct on both sides. I had named them out of the euser def. **The real gap: 26 extra `RFile::Read` calls the emulator makes at `0x10abf8` and the phone does not** -- a read loop that stops after one iteration on hardware |

## Where we are

**Furthest: 179 traced events** in the emulator (build 52), **144 on the
phone** (build 51). Both are past the wall that held from build 44 to build 50
at 128. The run now gets through the whole resource-loading sequence, past the
store that was poisoning `this->[4]`, through two `RLibrary::Load`s it had
never reached, and out via an orderly `User::Leave` / `User::Exit`.

**Best round so far: 51.** It is the one that moved the port rather than
describing it: NOP one word of the game's code and the phone goes 128 -> 144
traced events, 248 -> 262 imports, following the emulator's new sequence import
for import. First advance on hardware since build 44, and the first candidate
fix this project has produced instead of another measurement.

**Runner-up: 49**, which retired five rounds of heap forensics in one switch by
turning every deallocation into a no-op and showing the run stopped in exactly
the same place. `User::Free` was never the wall.

**Also load-bearing: 50**, which put the fault at `0x139588` and showed it was
the same instruction EKA2L1 cannot run the original N-Gage binary past; and
**36**, the `RFile::Close` fix that ended a month of reboots.

**Build 52 is out and unanswered.** It does not change behaviour: it stamps a
launch counter into the box and gives each launch its own log file, because the
emulator turns out to run the app **twelve times** in a 45-second session and
every log ever read here was whichever launch happened to be last.

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

### What build 51 found

| | build 50 | build 51 |
|---|---|---|
| traced events at the last box write | 128 | **144** |
| last import in the box | `RLibrary::Close` | **`RLibrary::Load`** |
| imports in the log | 248 | **262** |
| records | 971 | **1563** |
| the watched field | -- | a good pointer throughout, `[+0x240]` valid |

And the sequence it runs after the wall is the emulator's, import for import:

```
RLibrary::Load    from 13964c      <- never reached before
CCoeEnv::Static   from 139684
RLibrary::Load    from 13f588      <- nor this
RLibrary::Lookup  from 13f5e4
RLibrary::Close   from 13f610
HBufC16::New      from 1909e4
RLibrary::Lookup  from 13f68c
... deletes, Lookup/Close, Math::Random from e9954, more deletes
```

Two machines, the same new ground, in the same order. The phone stops about
thirty imports short of where the emulator gets (292, and an orderly
`User::Leave`), in the middle of a free's verdict block.

**The store at `0x1082c0` was the wall**, and the value it clobbered was the
right one on hardware as well as in the emulator.

### What build 54 found

Three runs, six log files, and they come in two kinds:

| | records | box | reaches |
|---|---|---|---|
| `g6box5/6/7.log` | 1563 | yes, 144 traced events, last import `RLibrary::Load` | the same place round 51 reached |
| `g6box2/4/9.log` | **2** | **none** | `image loaded at`, `chunk ends at`, and then nothing |

**No reboot.** Build 53's reboot came from one of the three things it carried
past build 51, all of which are reverted here. Which one is not established and
does not need to be: none of them are wanted.

The full launch is 1563 records and 144 traced events -- **identical to round
51** -- which is the check that build 54 changed nothing but the file name.

And the second launch has never been seen before. It gets as far as the two
records the loader writes immediately after replacing the log file, and dies
before the box is written -- there is no box from it at all. That is
`file_replace` of the box, or the stretch of loader between the two, and the
whole of the image load and relocation lies inside that window.

So "one KERN-EXEC 0 and one KERN-EXEC 3 per run" is two launches, not two
faults in one: a full launch that dies at the end, and a second that dies
almost immediately.

### What the instrument still cannot say

**Which of the two is first.** The digit is `TickCount % 10`, and the stub
writes no box, so it carries no tick of its own. Run 1 has the full launch on
digit 7 and a stub on digit 9; run 3 has the full launch on 5 and a stub on 2.
Either order fits. If the stub is *first*, then everything this project has
ever measured is the second launch -- which would be worth knowing before
another theory is built on it.

That is one record: the tick, written into the log rather than only the box.

### What build 56 found

Ten log files across three runs, in three shapes:

| shape | records | what it does |
|---|---|---|
| the first launch | 1535-1551 | 144 traced events, the usual ending -- KERN-EXEC 3 |
| the relaunch | 148 | into the framework, then `RFile::Open` -> **-14 `KErrInUse`** on `6RBC.dat`, and the game bails |
| one more | 69 | armed its box (`0 traced events`, `reached nothing`) and died in `timer slot 3` |

**The relaunch is explained and it is not a bug of ours.** The first process
panics still holding the game's data file; the relaunch opens it, gets
`KErrInUse`, and the game takes its own error path. Fix the first launch and
the relaunch stops existing. Nothing more should be spent on it.

**CONE 2 is new, and it is most likely progress rather than a regression.**
Before build 56 the relaunch died on our bad handle at four records, before the
framework had done anything. It now runs 148 records *into* cone.dll and fails
there. A panic from cone is what a relaunch that gets far enough to fail
properly looks like.

**The KERN-EXEC 0 went away in two runs of three.** Not all three, so the guard
is not the whole of it -- but it is most of it, and what remains is no longer
the first thing in the way.

### Where this leaves the target

The first launch is the only one that matters, and it dies at **144 traced
events with KERN-EXEC 3**. The emulator's first launch reaches **179** and
leaves cleanly. So the phone dies about thirty-five events *before* the
emulator's `User::Leave`, and that gap has never been instrumented -- every
probe in this file sits at or before the wall that came down in build 51.

### Round 56, run 4: the relaunch, explained by hand

A fourth run, driven manually: dismiss each panic quickly and the app relaunches
itself, over and over, about eight times. Nine logs came out of it -- three full
(1551, 1551, 1535) and five stubs (148 x4, 137) -- so it is a *cycle*, not the
one pair earlier rounds saw.

**A panicking Symbian thread stays alive until its dialog is dismissed**, and
it keeps every file handle while that box is on screen. That is the whole
mechanism:

1. launch A reaches 144 events and panics KERN-EXEC 3; its dialog opens and **A
   stays alive holding `6RBC.dat`**
2. the framework relaunches; launch B starts *while A is still alive*, opens
   `6RBC.dat`, gets `KErrInUse` -- the `-14` already in the relaunch log -- and
   fails inside cone: **CONE 2**
3. dismissing quickly keeps a launch permanently in flight and the cycle repeats

Waiting breaks it: given a few seconds B runs its whole doomed startup and exits
by itself, and then there is nothing left to relaunch. The black bar that
appears and vanishes behind the CONE 2 dialog **is B's entire life**.

CONE 2 appearing "first" is dialog stacking, not chronology: B's panic lands on
top of A's, so it is dismissed first.

**The relaunch is pure echo.** CONE 2, the `KErrInUse`, the second panic and the
loop are all downstream of launch A's KERN-EXEC 3. Fix that and they go
together; none of them is separate work.

**A caution this earns.** A black bar is drawn on *every* launch, including the
ones that die at 148 records. This file has treated "the black bar with pixels"
as the port's visible output; it is not evidence that the launch reaching 144
events got anywhere.

The relaunch is not ours: the only `restart` flag in the loader is the DSA
observer's Restart callback, and the box says it never fired.
