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
| E34 | build 59 -- wrap `RFile::Open` too, and log an allocation that comes back empty | 2132 | `0x45933C0` | **Names the files.** The 64 KiB burst is the game reading **its own image**, `E:\system\apps\6rbc\6rbc.app`, twenty-six times; then `cwp.dat` (125 bytes, which is what `RFile::Size` answers), `nc.dat` (16 bytes) twice, and `6rbc.cwa`. Five opens, all KErrNone, no allocation ever fails. So the emulator's heap is not the difference and neither is the read: it is the **open of `6rbc.app`** |
| E35 | build 59 -- close the loader's own handle on `6rbc.app` once the image is read | 2132 | `0x45933C0` | Byte-for-byte E34: 2132 records, 29 reads, 5 opens, nothing fails. The close costs nothing here, which is the point -- EKA2L1's file server does not enforce share modes, so the handle the loader was holding never obstructed anything. **Only the phone can test this one** |
| E36 | build 60 -- an arg_thunk on `User::Leave`, so the reason code is written down | 2143 | `0x45933C0` | **`User::Leave(-2)` -- KErrGeneral -- from `0x2b20`.** Not a leave the framework raised: the game's own code, `mvn r0, #1` immediately before the call, an unconditional give-up at the end of one branch |
| E37 | build 60 with `DUMP_DECRYPTED` on, to read the decrypted image | 2143 | `0x45933C0` | Three regions only, 448 + 1056 + 1092 bytes, and none of them is the check at `0xccbb4` -- that function is plaintext in the file and did not need dumping. The flag is off again |
| E38 | build 61 -- a station on the protection's dispatcher, planted on `0xccc00` | 2143 | `0x45933C0` | **Nothing fired, and that is the answer**: `0xccc00` is the `ldrls pc, [pc, r3, lsl #2]`, not the `cmp`. `crumb_safe` refuses a conditional load into pc, so no station was planted and the run is bit-identical to E37. An off-by-one in reading my own disassembly |
| E39 | build 61 -- the same station moved to the `cmp` at `0xccbfc` | 2192 | `0x45933C0` | **Seven states: 47, 13, 36, 29, 68, 59, 7.** Not sixty-nine -- the check's whole path is seven blocks. State 29 calls `0xe6df8`, compares the result with zero and picks the next key from it; the result was **zero**, so it went to 68, which sets `r8 = 0`, and then to 7, which is `mov r0, r8` into the epilogue. **`0xe6df8` returning zero is the entire failure** |
| E40 | build 62 -- a station on the loop inside `0xe6df8`, on the `ldr r2, [r5]` that fetches its bound | 2222 | `0x45933C0` | **The list has one entry, and the loop runs once.** `r5 = 0x0334ca90`, `[r5] = 1`, counter 0 then 1. So the zero is not an empty list -- the body ran, called `0xe50d8`, and that call's result is what comes back. `0xe50d8` returned zero |
| E41 | build 63 -- patch `subs r5, r0, #0` to `subs r5, r0, r0` at `0xe6e18`, taking the check's own early exit | 2243 | `0x45933C0` | **It works, and the wall moves.** The state path goes 47 13 36 29 **34 4** 59 7 instead of 47 13 36 29 68 59 7 -- state 29 now takes its success key -- and traced events go **179 -> 195**. But state 34 then fails the same way: it allocates 24 bytes, calls `0x10a93c`, gets **zero**, and branches to state 4, which is the same block as 68 (`mov r8, #0`). Still `User::Leave(-2)` |
| E42 | build 64 -- log the first three words of every name handed to `RFile::Open` | 2261 | `0x45933C0` | **A sixth open, and it fails.** Five are `6rbc.app`, `cwp.dat`, `nc.dat` twice and `6rbc.cwa`, all KErrNone, all `{0x4000001c, 0x1c, ptr}` -- type 4, and the buffer at `ptr` begins with its own header, which is why the earlier decode read a word early. The sixth is `{0x40000004, 0x10, ptr}`: **four characters, `0x0008 0x000a`**, and KErrNotFound |
| E43 | build 64 -- a station on state 34's input, `0xcde68` | 2276 | `0x45933C0` | **`r3 = 0x04160a08` and `[r3] = 0x04160a08`** -- the word at the pointer is the pointer, which is this game's way of saying an empty list, already on file from the cell at `0xcc914`. `User::StringLength` over it answers 4, and those four bytes become the filename |
| E44 | build 65 -- override the verdict at `0xe6f34` instead of skipping the body at `0xe6e18` | 2362 | `--` | **The empty list is not ours.** The loop still runs (station 997 fires twice, so one iteration and the exit), `0xe50d8` still executes, and state 34 still reads a list holding its own address. Traced events 195 -> **198** and no exception handler fired at all this run. So something that should fill that list never did, and it is not the patch that stopped it |
| E45 | build 66 -- stations either side of the empty list, at `0xcc8e4` and `0xcc944` | 2392 | `--` | **The list is created empty on purpose, and stays that way.** `cmp r4, #0` sees **1**, so the branch that skips the whole allocation is not taken and the cell at `0xcc914` -- `str r5, [r5]`, this file's oldest landmark -- is made. Then `0xe98c4` on the container at `obj+40` **returns the cell itself**: key 1 is not in the map. Nothing filled it |
| E46 | build 67 -- gate two: `mov r4, #1` at `0x13f6c8`, so `0x13f4c8` always reports success | 2348 | `0xFFC` | **Past state 34, and straight into the cost.** States 47 13 36 29 **34** and then nothing -- no return to the dispatcher, no `User::Leave`, no `User::Exit`. The tail is `Lookup` of old 136 (`RFile::Read`) and then a read of 2048 bytes answering **-8, KErrBadHandle**, on the handle that was never opened, and then a fault. Exactly the cost the patch's own comment predicted |
| E47 | build 67 with `PATCH_GATE_TWO = 0` -- the control | 2384 | `--` | Back to the E44/E45 shape: no fault, the run ends through `User::Leave` as before. The tree is left at the best run there has been, with gate two written down and switched off |
| E48 | build 68 -- answer the MMC CID out of `nc.dat` instead of zeros, big-endian word order | 2384 | `--` | **No change.** States 47 13 36 29 34 4 59 7, identical to E47, and the driver log shows ordinal 490 -- the card-info call -- being answered, so the CID really is delivered. The check patch was still on here, so this only says the CID changes nothing downstream |
| E49 | build 68 with `PATCH_THE_CHECK = 0` -- does the real check pass now? | 2267 | `--` | **No.** States 47 13 36 29 **68** 59 7. The card CID in big-endian word order is not what the protection was missing |
| E50 | build 68, little-endian CID word order, check patch still off | 2259 | `--` | **No.** Same path, 47 13 36 29 **68** 59 7. Neither reading of `nc.dat` satisfies it |
| E51 | build 68 restored -- CID answered, check patch on, gate two off | 2384 | `--` | The control. Identical to E47, so answering the card's real identity costs nothing and is kept: it is the truthful answer even though it does not open the gate |
| E52 | build 69 -- answer the driver with the crack's forged CID, `56785733 10011234 0b70194e 16000400`, check patch off | 1860 | `0xEC49E40` | **A different failure, and an earlier one.** No dispatcher states at all: the run dies at `RFile::Open` after 160 traced events, and the emulator segfaults. With a card that looks real the game takes the card path for the first time -- and the card path needs the rest of the crack |
| E53 | build 69 repeated | 1860 | `0xEC49E40` | Identical. Not flaky |
| E54 | build 70 -- add the read-only card rules on `RFile::Open`, `Create` and `Replace`, static imports and dynamic lookups alike | 2259 | `--` | **The earlier death is gone** -- back to 2259 records and the full state path -- but **zero refusals fire**: the game never asks to write to E: before the check. States 47 13 36 29 **68** 59 7, so the check still fails |
| E55 | build 70 -- answer every `DoControl` rather than only `MMC_CARD_INFO`, as the crack does | 2259 | `--` | No change: 2259 records, same path, same 68. So either the call is not reaching `gate6_mmc_control` or the CID is not what the digest is missing |
| E56 | build 71 -- a ctx3_thunk on `DoControl` so it can say whether it runs at all | 2267 | `0x42B8D80` | **It runs.** Twice per launch, as a pair: op **4** with a null buffer, then op **6** with a pointer. So the original `MMC_CARD_INFO` gate was right, the CID is written into the game's own buffer, and the card's identity is genuinely delivered. The check still goes to 68 |
| E57 | build 71 -- route the *dynamic* `RFile::Open` through the card wrapper too | 2267 | `0x42B8D80` | A real gap closed -- the dynamic route had been skipping the read-only rule the static import got -- and no change: still no refusals, still 68. The game does not try to write to E: before the check |
| E58 | build 71 with the verdict override back on, card answers kept | 2392 | `0x5CBFE00` | **200 traced events**, a new emulator best, up from 198. State path 47 13 36 29 **34 4** 59 7 and the same sixth `RFile::Open` failing on the empty-list name. The card answers cost nothing and are kept |
| E59 | build 72 -- a station on the container at `obj+40`, at `0xcc938` | 2407 | `0x5CBFE00` | **One record, and its key field is 0** -- `{0, 0, 0x596, 0x46}` -- which is the key the search is looking for. So the container is not empty in the way it looked; what came back was the default, and why needed a closer look at the caller |
| E60 | build 73 -- substitute a real filename when the game asks to open rubbish, `cis.dat` | 251 | `--` | **Broke the run at once**: 35 traced events. The substitution fired twice, on names that were perfectly good |
| E61 | build 73, `cwp.dat` | 200 | `--` | Same collapse |
| E62 | build 73, `6rbc.cwa` | 200 | `--` | Same collapse. Something is wrong with the test, not the idea |
| E63 | build 73 repeated, `cis.dat` | 251 | `--` | Identical -- the edit that was meant to fix the decode had failed its assertion and written nothing |
| E64 | build 73 repeated, `cwp.dat` | 200 | `--` | Identical |
| E65 | build 73 repeated, `6rbc.cwa` | 200 | `--` | Identical |
| E66 | build 74 -- **the decode fixed**: these names are type 4 and the buffer at `ptr` begins with its own header, so every reader must skip two shorts | 2407 | `--` | **The run is healthy again** -- 2407 records, full state path -- and the substitution never fires, correctly. But this also means `name_on_the_card` had been reading the length word where it wanted the drive letter, **so the read-only rule had never once applied** |
| E67 | build 74, `cwp.dat` | 2407 | `0x5CBFE00` | Same |
| E68 | build 74, `6rbc.cwa` | 2407 | `0x5CBFE00` | Same. The substitution is not needed |
| E69 | build 75 -- build the logging open thunk **around** the card wrapper, not around the raw efsrv address | 2492 | `0x9AB31D42` | **Past state 34.** The refusal fires for the first time (one open answers **-21**), the run makes **seven** opens instead of six and the last two succeed, and the state path is 47 13 36 29 34 -> **38**, a state no run has ever reached. **No `User::Leave` and no `User::Exit` anywhere in the log** |
| E70 | build 75, `cwp.dat` | 2492 | `0x9AB31D42` | Identical -- the substitution plays no part |
| E71 | build 75, `6rbc.cwa` | 2492 | `0x9AB31D42` | Identical |
| E72 | build 75 with `PATCH_THE_CHECK = 0` -- does the real check pass now? | 1868 | `0xE4D4280` | **Not yet.** No dispatcher states at all and an early death, the same shape E52 had. The card rules change what happens after the check, not the check itself |
| E73 | build 75 final -- card answers on, read-only rule working, verdict override on, substitution off | 2492 | `0x9AB31D42` | The best run this project has had: 2492 records, one launch, no relaunch, no `User::Leave`, past state 34, and a fault after the frame loop instead of an orderly give-up |
| E74 | build 76 -- the check returns a **sixty-four-byte zeroed object** instead of the integer 2: `ldr r2, [pc, #4]` / `b 0xe6f58` at `0xe6f30`, with the address written into the literal at `0xe6f3c` by the loader | 5337 | `--` | **The protection completes.** The state machine runs its full path -- `47 13 36 29 34 38 18 61 32 40 23 24 59 7`, fourteen states -- and does it **five times**, once per call of the loop at `0x2e7c`. **5337 records** against 2492, **336 traced events** against a previous best of 200, **eleven opens with ten succeeding** and the one refusal that should fire. No `User::Leave`, no `User::Exit` |
| E75 | build 76 again -- is E74 reproducible? | 176 | `--` | **22 traced events.** Nothing like E74. Xvfb had died and the emulator was starting blind; the run is not comparable and the row is here because it happened |
| E76 | build 76, display restored | 592 | `--` | **48 traced events.** Still nothing like E74, and now three runs of the same build read 336, 22 and 48 |
| E77 | build 76, third try | 4144 | `--` | Closer, still short. At this point the honest reading was that E74 might have been a fluke |
| E78 | build 76 with `TMO=120` -- give the emulator two minutes instead of forty-five seconds | 5337 | `--` | **E74 is reproducible and the variance was the harness.** Four launches in one session reach **5337, 5337, 5335 and 5335 records**; the short ones were launches the forty-five-second kill caught partway. 246 import events, the protection's fourteen states five times over, and the run ends in a `__builtin_delete` of a 180-byte cell. `emurun.sh` now defaults to 120 |
| E79 | build 76 with `TMO=240` -- is 5337 an ending or a timeout? | 5337 | `--` | **An ending.** Twice the time, the same 42696 bytes. And of **eight launches in the session only one panicked** -- a single `G6MEM`, our loader's own, when `user_alloc(1616972)` failed on a relaunch. The other seven neither panicked nor left nor exited. The startup completes and the game goes quiet, with `frames 1`: one frame drawn and then nothing |
| E80 | build 77 -- log the game's `RunL` on the way in and on the way out | 5338 | `--` | **It goes in at record 65 and never comes out.** Every one of the remaining 5273 records -- the protection, the resource loading, all of it -- happens inside **one** `RunL`. `frames 1` was never a timer fault. And the dynamic lookups name what it does at the end: **`RThread::Create` and `RThread::Resume`**, once each, and the emulator log shows `Thread SoundServer created with start pc = 0x90b8660, stack size = 0x186a0` |
| E81 | build 78 -- a breadcrumb at each worker thread's entry, `0xb8660` and `0xcb710` | 1130 | `--` | **Neither fired**, and neither could have: an `RFs` session belongs to the thread that made it, so a write from one of the game's threads through the main thread's handle answers an error and leaves no record. A silent instrument, which is the same shape as the bug it was hunting
| E82 | build 79 -- the worker crumbs **panic** instead of logging, and a third is planted at `0xcc7e4` as a control | 5338 | `0xB1CD200` | **The control fires and the workers do not.** `G6WRK` exit code **972** -- the control, at a site the log has running on every launch -- so the mechanism works. 970 and 971 never panic. **The two threads the game creates are never entered**
| E83 | build 79 with `PLANT_WORKERS = 0`, after killing eleven stale emulator processes | 5338 | `0x5B22E80` | **No panics at all** -- the `G6MEM` is gone. Those were runs the harness had left behind: `pkill -x` without a follow-up `-9` left eleven emulators alive, each holding its memory and a few per cent of a core, until a later launch could not allocate its image. `emurun.sh` now force-kills after a second
| E84 | build 80 -- wrap `RThread::Create` and `RThread::Resume` where our own lookup answers them | 5343 | `--` | **`RThread::Create` answers `-2`, KErrGeneral, and leaves the handle `0`.** `Resume` is then called on that object and answers `0`, because resuming a null handle costs nothing. So the threads are not failing to start -- **they are failing to be created**, and the game never looks. Repeated on a machine with fifteen gigabytes free and eleven successful thread creations in the same session's other launches, so it is not memory and it is not flaky  **RETRACTED (E88).** This was `frame_thunk`, not the game: the wrapper pushes seven words before calling the target, so a six-argument function reads its stack arguments twenty-eight bytes too low |
| E85 | build 80 with **the emulator patched** to report why a thread create fails | 5343 | `--` | **The arguments are garbage.** `Thread -590328542 NOT created: pc = 0x47cb710, user stack = 0x2000, heap 0..0, allocator = 0x8b2ad8, ptr = 0x40f868, owner = 75282192, total size = 64`. The info block is sound -- the pc is a real image offset and the stack is 8 KB -- but the **name descriptor and the owner type are junk**. And the thread that fails is the *second* one, `0xcb710`; **SoundServer, with its 100 KB stack, is created fine**  **RETRACTED (E88).** Same cause |
| E86 | build 81 -- log the whole create frame the game pushes | 5353 | `--` | **The game's call is impeccable.** `this = 0x3dc8a60`, name a well-formed type-3 descriptor (length 11, max 64), `fn = 0x98cb710`, `stack = 0x2000`, and the three pushed words `0`, `0x3c9f550`, `0` -- which read as `aHeap = NULL`, `aPtr`, `aType = EOwnerThread` exactly as ordinal 289 specifies. Nothing wrong on our side of the call  **The frame is still good evidence** -- the game's call really is impeccable -- but the failure it was explaining was mine |
| E86b | build 81 with the emulator's bridge made null-safe and printing the raw name pointer | 5353 | `--` | **The kernel's `owner` argument is the thread function pointer.** `owner = 75282192` is `0x47cb710`, and `pc = 0x47cb710`. The same word, in both places. The name pointer is not null after all, so the missing null check was hardening rather than the bug  **RETRACTED (E88).** The displaced argument was displaced by our own thunk |
| E87 | build 82 -- try old 289 -> new **1159**, the other `RThread::Create` overload | 322 | `--` | **Died at 322 records**, one launch. Whatever 1159 is in this ROM, calling it with 289's frame is worse. Not the answer, and asked for the wrong reason |
| E88 | build 83 -- **take the wrapper off `RThread::Create`** | 5341 | `--` | **Seventeen thread creations and not one failure.** The `-2`, the garbage owner and the garbage name were all `frame_thunk`: it pushes `{r0-r4, r12, lr}`, seven words, before calling the target, so euser read its three stack arguments out of our saved registers. E84 to E86b are retracted. The run is otherwise unchanged -- 5341 records, one `RunL` that never returns, the same 70 dispatcher passes |
| E89 | build 84 -- worker crumbs back on, control removed, now that create works | 5343 | `--` | **Still nothing.** Eighteen thread creations, no failures, and no `G6WRK`. Create working changed nothing about whether the threads run |
| E90 | build 84 with the emulator logging what state a resumed thread is in | 5343 | `--` | **`thread_resume SoundServer (handle 0x20001c) in state 0`** -- state 0 is `create`, which is the one case that calls `schedule()`. Both threads, every launch, valid handles. So they are queued ready and still never run |
| E91 | build 85 -- make `crumb_plant` say whether it planted | 5343 | `--` | **`planted: [970, 971]`, refused: none.** The crumbs are at the worker entry points. So the chain is complete: created, valid handle, resumed, queued ready, breadcrumb in place -- **and not one instruction executed** |
| E92 | build 85 repeated -- the same build, a different launch of it | 5341 | `--` | 5341 against E91's 5343, which is two records of launch-to-launch noise. Kept because it is a run that happened, and renumbered because `emurun.sh` wrote it before the lock that stops two finishing runs claiming the same row number
| E93 | build 85 again, with the emulator's scheduler traced | 0 | `--` | **No run.** Killed by an overlapping `emurun.sh` finishing its cleanup -- my own race, two runs started at once. Nothing to read. Standing rule 12 already says one at a time; this is what breaking it looks like |
| E94 | the same, retried | 0 | `--` | **No run**, same cause as E93 |
| E95 | the same, retried again | 0 | `--` | **No run**, same cause as E93. Three empty rows in a row is the cost of starting a second run before the first has cleaned up |
| E96 | build 85 run alone, scheduler traced (the pre-rebuild binary, so no process/pc column) | 5343 | `0xE66CC80` | **The answer.** 1340 reschedules, and `reschedule: Gate6 -> SoundServer` seven times -- the worker *is* picked. Records 4 and 5 are note 842 with 970 and 971, so both crumbs really are planted, at image base `0x5800000` and a start pc of `0x58b8660`. And right after every switch to SoundServer: two unused-parameter-slot errors, a rename to `gate6`, then *a whole second CONE startup* -- screen device, `eikcore.r01`, `eikpriv.rsc`. The worker was re-running the application. `thread::reset_thread_ctx` points a new thread at **the process entry point**, not at the requested function; a real EXE is linked against `eexe.lib`, whose `_E32Startup` dispatches on `r4`. Our hand-built `_start` had no such branch, so every `RThread::Create` relaunched the game in the new thread -- which is why the image base climbed `0x47` -> `0x58` -> `0x61` -> `0x6e` -> `0x77` -> `0x82` -> `0x95` across seven SoundServers. **Not an emulator bug, and not a scheduler bug: ours** |
| E97 | build 86 -- `_start` dispatches on `r4`: a new thread calls its own function instead of re-running the app | 5343 | `--` | **Both workers ran.** `Thread SoundServer panicked with category: G6WRK and exit code: 970` and `Thread -439926714 ... G6WRK ... 971` -- the two worker crumbs, from the two worker threads, at last. **`launches: 1`, down from six**, and one SoundServer instead of seven, because the recursive relaunch E96 diagnosed is gone. Nine rounds (E84-E96) asked why the workers never ran; the answer was six instructions of entry-point dispatch. The crumbs panic by design, so the run still ends there -- next build takes them out |
| E98 | build 87 -- worker crumbs off, the two worker threads left to run | 2239 | `0x7D004480` | **The workers run, and the main thread dies.** `KERN-EXEC 3` in `Gate6`, pc `0x483c478` = image offset `0x13c478`, lr `0xe81f0` (a vtable-slot-3 dispatch thunk), with `r4 = 0x5bf18bd0` and `r5 = 0xe70bc80e` -- both garbage, so a frame was restored from a corrupted stack. The last thing before it is a worker opening `E:\system\apps\6rbc\6rbc.cwa` *for the second time* (`raw mode 2`, handle 983054). Fewer records than E97 (2239 against 5343) but the comparison is meaningless: E97's count was six recursive relaunches of the same work. **A blind spot opened with this build**: an `RFs` session belongs to the thread that made it, so every event a worker logs through `c->fs` is dropped, and the box shows only the main thread. That has to be fixed before the fault can be read -- half the program is now invisible |
| E99 | build 88 -- a worker's events go to RDebug instead of the file, since `RFs` is per-thread | 2234 | `0x7D004480` | **The workers are visible, and the smaller one kills itself.** The 8 KB worker (`0xcb710`) starts -- `reschedule: Gate6 -> 188123089 [Gate6[e0001006]0003]` -- calls `RLibrary::Load` from `0xa1dc` and `RLibrary::Lookup` from `0xa208`, logs ordinal 0 / handle 1 / result `0xdc`, and then **panics `G6WR -8`**: that is `CAT_WRITE` with `KErrBadHandle`, our own `log_block` writing the main thread's `RFile` from the wrong thread. The guard was in `log_event` only, and twenty places flush a block directly. The main thread's fault is unchanged (`0x7D004480`, 2234 records against E98's 2239), so it is not caused by the worker's panic |
| E100 | build 89 -- `log_block` guarded by thread too, so a worker cannot panic on the main thread's file | 2234 | `0x7D004480` | **No more `G6WR`, and the worker gets further**: `RLibrary::Load`/`Lookup`/`Close` from `0xa1dc`, `0xa208`, `0xa220`, then a second `Load`/`Lookup` from `0x532c`, `0x5384`, ordinal 645, result `0x651b`. Seven events, then quiet. **SoundServer is now never created at all** -- the main thread dies before it gets there -- and the fault is identical to E98 and E99 to the record: `0x7D004480`, 2234. Also caught reading this one: the `w` sign I gave worker events already meant the low half of an address, so three main-thread notes read as worker events in E99 |
| E101 | **control** -- build 90: the worker thread is created and resumed exactly as before, but its entry returns without running the game's function | 5341 | `0xFFC` | **The main thread's fault belongs to the worker.** With the game's code kept off the second thread the run is back to 5341 records, one launch, and there is no `0x13c478` and no `0x7D004480`. Everything the kernel sees is identical between this and E100 -- same creation, same handle, same resume, same schedule -- so the only variable is whether the game's own code runs on a second thread. (The worker itself now dies inside `User::Exit`, walking upwards from `0x280`; that is the control's own artefact and not the thing being measured.) |
| E102 | build 91 -- which `RLibrary` is euser and which is efsrv is a table of objects, not one slot per kind | 2234 | `0x7D004480` | **No change at all**: 2234 records and the same fault address, to the record. The worker really does open its own euser and its own efsrv and really did displace the main thread's entry, and fixing it changes nothing -- so that was a bug, not *the* bug. Kept, because it would have become one. Also read off this run: the game decrypts exactly three regions of its own image in place -- `0xd5094`+`0x1c0`, `0x10af44`+`0x444`, `0x10b388`+`0x420` -- each **once**, so the fault is not two threads decrypting the same code twice |
| E103 | build 92 -- the control again (`RUN_WORKERS 0`), to diff the main thread's record against E102 event for event | 5341 | `0xFFC` | **The two runs are identical for 806 records and then differ by one word.** Filtering the clock and the allocation ring (shared, so the worker perturbs it without meaning anything), the main thread's stream matches exactly up to marker 990 -- the probe that latches the watched object -- and there the fourth word reads **`0x4900000` in the control and `0x483c1fc` when the worker runs**. `0x4900000` is a local code chunk the game makes; `0x483c1fc` is image offset `0x13c1fc`, which is `mov r3, #1` in the middle of a function. That word is reached through a vtable slot-3 dispatch at `0xe81d0`, which is how the run ends up executing at `0x13c478`. Two more things read off the pair: the main thread gets **190 core events with the worker running and 337 without** (imports and planted markers, the allocation ring excluded), and the worker is not killed by anything of its own -- `category: Domino` means it went down with the process. |
| E104 | build 93 -- latch the watch on the allocation containing the word, so the field has a timeline from the moment it exists | 2234 | `0x7D004480` | **The latch never fired**, so no timeline: `NOTE_WATCH_EARLY` is absent and the first `NOTE_WATCH` is still probe 990's, at record 1149. The object at `0x8b2810` does not come out of `gate6_alloc` -- it is already live at record 120, where it is passed as `a0 = 0x8b2800` to an import, so the latch belongs in the argument wrapper and not the allocator. Run otherwise identical: 2234 records, same fault. (The emulator also segfaulted on its way out this time; the record was already written and the run is the same to the record, so it is noise, not a result.) |
| E105 | build 94 -- latch the watch from the argument wrapper (record 120, not 1139) | 3365 | `0x7D004480` | **The word has a timeline, and it names the culprit.** `0` at record 123, **`0x4900000` at 688** -- the local code chunk's base, correctly stored -- and **`0x483c1fc` at 743**. Records 723-725 are `NOTE_THREAD_RESUME`/`HANDLE`/`CREATE` with handle `0x1a0016`: **the worker is resumed at 723 and the word is poisoned by 743**, with no main-thread event in between. The worker's own first `watch_note` already reads the poisoned value, so the write happens between its entry at `0xcb710` and its first `RLibrary::Load`. **A mistake of mine in the instrument**: the worker's note carried only the low half of `from`, so every call site read as nonsense (`0xa1dc` is a `bx lr`, `0x532c` the middle of a compare). Both halves from the next build |
| E106 | build 95 -- fourteen logging stations through the small worker, and the whole of each `from` | 3416 | `0x7D004480` | **The worker enters at `0xcb710` and spins.** Stations 971 (`0xcb710`) and 972 (`0xcb714`), then a polling loop -- `0xcbe24`, `0xcbf14`, `0xcba28`, `0xcbd14`, `0xcc114` -- round and round. The loop spent the whole 600-note budget, so no `WATCH` record and no import reached RDebug and the poison could not be placed between two stations. The stations themselves are the result: the write is inside that loop or just before it |
| E107 | build 96 -- report the watched word from the worker only when it changes | 3416 | `0x7D004480` | **The write is one instruction, and it is the game's own.** The worker's stations run `0xcb710`, `0xcb714`, then a polling loop; on the pass that matters the path is `0xcba28` -> `0xcb814` -> `0xcb914` -> **`0xcbb14`**, and the word changes before the next event. In that stretch is `0xcbb68: str r12, [r10, #12]`, with `r10 = [sp,#100]` and `r12` the result of running `[r10,#12]` through two multiply-by-constant chains: `0xd249567f` then `0xbcdc697f`, whose product is **exactly 1 mod 2^32**. So the instruction reads the word, transforms it and writes back **the same value -- if and only if `r5` is zero**, because a third chain adds `0xf89b97ff * r5` in between. In the worker `r5 = [sp,#88]` is not zero: solving `g(f(0x4900000) + h(r5)) = 0x483c1fc` gives `r5 = 0xb33dbbfc`. The corruption is the game's own obfuscated in-place update running with a stray operand, not a stray write |
| E108 | build 97 -- a probe on the poisoning store itself, reporting r10 and r5 | 3416 | `0x7D004480` | **Measured, and it matches the algebra bit for bit.** Probe 1002 at `0xcbb68` reports `r10 = 0x008b2810` -- the watched object exactly -- and `r5 = 0xb33dbbfc`, which is the value E107 solved for without measuring it. The eight words at r10 still show `0x04900000` in slot 3 going in, and `0x0483c1fc` coming out. The store's whole semantic is **`[r10,12] += 0x09abfe81 * r5`**, an obfuscated add, and `0x09abfe81 * 0xb33dbbfc` is exactly `0xfff3c1fc`, which is `0x483c1fc - 0x4900000`. So the game means to write `chunkBase + offset` and the offset it has is `routine - chunkBase`, which lands it back on the image original. `[sp,#88]` has exactly one writer in the whole function: **`0xcb8bc: str r6, [sp, #88]`**, with `r6 = mult(r0) + 0x05b64181`. That is the next thing to read |
| E109 | build 98 -- call the game image's entry point with `EDllThreadAttach` at each worker entry, as EKA1 does | 3416 | `0x7D004480` | **It fires (`G6a0001`) and changes nothing.** `r5` is still `0xb33dbbfc` and the word still goes to `0x483c1fc`, bit for bit. The call is kept because it is what the platform does -- EKA2L1's own EKA1 bootstrap makes it before calling a thread function, and our loader only ever made the process call, on the main thread -- but it is not this. It had to be done from a worker-entry crumb, because `_start` has no way to reach the context: this image declares no writable data on purpose, and a static would fault on hardware |
| E110 | build 99 -- watch the image words the dead call runs through | 3422 | `0x7D004480` | **The image itself is rewritten, and all three words go at once.** At record 2311 `0x13c1fc` becomes `0x61ab568d` (the file has `mov r3, #1`), `0x13c424` becomes `0x6b24624e` and `0x13c478` becomes `0x979f568c` -- which is exactly the instruction the emulator disassembles at the faulting pc. So the dead call is not jumping into the middle of a function: it is jumping into a region that no longer holds the code that was loaded. The rewrite did **not** go through the decryptor this file wraps -- still only three regions, `0xd5094`, `0x10af44`, `0x10b388`, none of them covering `0x13c1fc`. The main thread noticed it inside a `User::Free` that does nothing, so the writer is the worker, concurrently |
| E111 | build 100 -- print the scratch code chunk beside the image region it lands in | 3438 | `0x7D004480` | **Not a copy, and the corruption starts exactly at the poisoned pointer.** The image still matches the file at `0x13c1f0`, `0x13c1f4` and `0x13c1f8`, and is garbage from `0x13c1fc` on -- which is precisely the address the worker wrote into slot 3. And the garbage does **not** match the scratch chunk (`0x608f2b39` there against `0x61ab568d` in the image), so nothing was copied out of it. So slot 3 is a **destination pointer**, not a function pointer: it held the base of the game's 0x1000 local code chunk, the worker moved it into the middle of the image, and the game then wrote its data through it, over its own code. Everything after -- the vtable dispatch, the wild pc, `KERN-EXEC 3` -- is consequence. The one thing to fix is the pointer |
| E112 | build 101 -- probe the load the bad offset is derived from | 3438 | `0x7D004480` | **The worker is checksumming the game's own code, and it is reading our instrumentation.** Probe 1003 fires 32 times, and every time `r5` is an *image code address* -- `0x47ca714`, `0x47ca71c`, `0x47cc864` -- and the word it loads is an ARM instruction (`0xeb002a42`, `0xe3a00040`, ...). The whole offset that poisons slot 3 is a transform of instruction words read out of the image. **The last read before the poison is at `0xcc864`, which is our own probe 990 site**, and the word it reads back is `0xea030f81` -- a branch to our trampoline -- not the `add r0, r5, #0x28` that belongs there. So the corruption may be entirely an artefact of the instrument: a self-integrity check reading patched code and answering a wrong offset. Next build takes every planted probe and crumb out |
| E113 | **build 102 -- every planted probe and crumb out of the game's code, workers running** | 5307 | `--` | **No fault.** No access violation, no image rewrite (`NOTE_IMAGE_AT` fires zero times), no `KERN-EXEC 3`, and **SoundServer is created for the first time** -- the main thread had always died before reaching it. 246 core events with both workers running, against 190 with the probes in. The whole `0x13c1fc` corruption, from E98 to E112, was **our own instrument**: the game's worker checksums its own code, our probes replace instructions with branches, and the offset it derives from the patched words sends a destination pointer into the middle of the image. The run now ends somewhere honest: `Thread SoundServer panicked with category: G6IMP and exit code: 464397` -- import 397, **`CServer::CServer(int, CServer::TServerType)`**, unimplemented. Which is exactly what a thread called SoundServer would want |
| E114 | build 103 -- stand-ins for EKA1's `CServer`/`CSession`/`RMessage` | 5307 | `--` | **No change, and the reason is the harness**: `gate4_shim.cpp` is generated by `gen_shim.py`, and `build_gate6.py` compiles it but never regenerates it. So the overrides were written and not built. Identical run to E113 to the record, same `G6IMP 464397`. Regenerated by hand; worth remembering, because a shim change that appears to do nothing will look exactly like this |
| E115 | **build 104 -- the same stand-ins, with the shim table actually regenerated** | 5331 | `--` | **Nothing panics and nothing faults.** Both worker threads run to the end of the run, the watched word holds `0x4900000` throughout, zero image rewrites, 248 core events. The game now stops **deliberately**: `User::Leave(-1)` -- `KErrNotFound` -- from `0xba354`, which is an inlined `LeaveIfError` on the result of `0xba500`, and then `User::Exit`. What it was doing: open `e:\system\apps\6rbc\6rbc.cwa`, read two 2 KB blocks (both `KErrNone`), search, free everything, and give up. `RSessionBase::CreateSession` is never called, so the sound session is not what it wants. It resolves efsrv dynamically on the way -- old 121 -> 93 (`RFile::Open`), old 136 -> 255 (`RFile::Read`), **old 162 -> 263**, which is the one to check |
| E116 | **build 105 -- stop patching the protection; let it succeed for real on the forged CID, as the crack does** | 41420 | `--` | **The protection passes on its own.** The crack never patches the check: it forges the card's CID through `RBusLogicalChannel::DoControl` and lets the real check run, and we already answer that call with the same 20 bytes. Patching the check instead handed the game a **zeroed 64-byte stand-in licence**, and everything downstream of it got zeros. With the patch off: no panic, no fault, 273 core events against 248, and the game does about **8,800 more import calls** than before. It still ends at the same `User::Leave(-1)` from `0xba354`, but after two further `RFile::Read`s from a call site it had never reached (`0x34b00`). Caveat on the record count: 35,288 of the 41,420 are `>> watched field`, my own `watch_note` firing on an address that has since been freed and reused. The instrument is now the bulk of the log and has to come out |
| E117 | build 106 -- the watch off, for a clean measurement | 3195 | `--` | **273 core events, no panic, no fault** -- the furthest this port has been, against 248 in E115, 180 on build 59 and 176 on the phone. The run is short in records now because the instrument is gone, not because the game does less. The end is a torn-down object: a cascade of frees and then `User::Leave(-1)`. The last real work is at `0x34ad8`, a **read-exactly-N helper** -- build a `TPtr8` on the stack, `RFile::Read` the `RFile` at `this+160`, then `cmp r0,#0 / bne` and `length == count`, returning 1 or 0. It answers 0, so the read either errored or came up short. Ruled out first: every file we install is byte-identical to the known-good dump, `6rbc.dat` (12,224,045 bytes) and `6rbc.cwa` included -- we simply have 86 more files than it does. Nothing is truncated |
| E118 | build 107 -- `WATCH_THE_READS` on, to see the last read | 3255 | `--` | **Wrong instrument.** The read watch is an *argument* wrapper: the three words it prints are memory at the return address -- `cmp r0,#0`, `bne`, `ldr r3,[sp]`, the caller's own code -- not the descriptor. It does say the helper at `0x34b00` is used **five** times, three early (records 86, 99, 106, all fine) and twice at the end, so only the last pair fails. What is needed is the result, which is a different wrapper |
| E119 | build 108 -- record what `RFile::Read` answers | 3275 | `--` | **Every read answers `KErrNone` -- all ten of them.** So the helper at `0x34ad8` is failing its *second* test, not its first: the call succeeded and the descriptor came up **short**. A short read with no error is end of file. The file is `6rbc.cwa`, 38,413 bytes, and the game opens it **six times** over the run. With the protection now running for real the whole Codewave sequence is visible in order: `6RBC.dat`, `cis.dat`, `cwivenc.dat`, `6rbc.app`, `cwp.dat`, `nc.dat`, `cwivenc.dat`, `nc.dat`, `cwivenc.dat`, then the six `6rbc.cwa`. So the archive directory hands out an offset past the end of the file. The next instrument is the one thing on that path still unwrapped: **`RFile::Seek`**, resolved dynamically as old 162 -> new 263 and handed back raw |
| E120 | build 109 -- wrap `RFile::Seek` | 3295 | `--` | **The seeks are sane and the emulator is right.** Five of them, all mode 1, to 5523, 1925, 1170, 891 and 0 -- every one well inside a 38,413-byte file, and all answering `KErrNone`. I first read `aPos` coming back unchanged as an emulator bug; it is not. `TSeek` is `ESeekAddress=0, ESeekStart=1, ESeekCurrent=2, ESeekEnd=3`, so mode 1 is **ESeekStart** and the absolute position it returns is the offset it was given. My labels were off by one. EKA2L1 writes the new position to slot 2 exactly as it should. So the short read is not on this handle: these five are the dynamic path on `6rbc.cwa`, and the failing read is the **static** import 110 on the `RFile` at `this+160`, a different object (`0x8b27d8` late against `0x8b22a8` early). That one needs the same descriptor wrapper the dynamic read has |
| E121 | build 110 -- wrap the static `RFile::Read` with its descriptor | 3275 | `--` | **The reads are not short. They are perfect.** The last two ask for 4 bytes and 411 bytes and get 4 and 411, both `KErrNone`. (I misread them once first: the `import 110` trace record interleaves between the descriptor records, so counting a fixed offset from `NOTE_FILE_READ` put the error code one slot out and made every whole-file read look like a garbage error. Read properly the sequence is descriptor in, trace, `answered 0`, descriptor out.) So the helper at `0x34ad8` passes both tests and the `KErrNotFound` is raised **after** the data arrives: the game reads a 4-byte header and a 411-byte record out of the archive and rejects the contents. That is a comparison or a decrypt, not I/O. The next thing to look at is the 411 bytes |
| E122 | **build 111 -- log what the reads deliver** | 3335 | `--` | **The game is past the protection and reading its assets, correctly.** The last two reads are a 4-byte header, `0x00000440` = **1088**, and then 411 bytes beginning `78 9c` -- a zlib header. Those 411 bytes are at **offset 3,406,180 of `6rbc.dat`**, and they inflate cleanly to **exactly 1088 bytes**, the size the header announced. Archive lookup, seek, read, all right. Also read off the same run: `cwivenc.dat` is a white-box AES blob -- magic `0x44332211`, version 2, size `0x28060`, and the ASCII **`WBAESDecrypt`** -- and it is loaded whole, twice, without error. So the `User::Leave(-1)` is raised **after** a correct asset record has been read, not on I/O and not on the protection. `0xba500` calls `0xb86ec`, in the same module as the SoundServer entry, and that is what answers -1 |
| E123 | **build 112 -- answer `RSessionBase::CreateSession` with `KErrNone`** | 3608 | `--` | **The frame loop turns over.** `RunL` is entered **three times and returns twice** -- it had been entered once and never returned since the port began. **388 core events**, against 273. No `User::Leave` and no `User::Exit` at all. `0xb86ec` was the classic connect-or-start-the-server idiom: build the name `SoundServer` (the literal at `0xb87a0`), try to connect, start a thread with the 100000-byte stack the caller passes, try again, give up with `KErrNotFound`. Our `CServer::StartL` registers nothing, so the real `CreateSession` could only ever fail. The new stop is honest and small: `Thread Gate6 panicked with G6IMP 464217` -- **import 217, `memmove`**, which 9.x estlib does not export under that name |
| E124 | **build 113 -- supply `memmove` locally** | 285551 | `--` | **The game runs.** `RunL` entered **4,706** times and returned 4,706 times; `CFbsScreenDevice::Update` called **4,716** times. No leave, no exit, no panic -- the run ends because the 120-second timeout kills it, not because the game does. **109,542 core events**, against 388 in E123, 273 in E117 and 180 on build 59. The frame loop is turning over and the screen is being updated about forty times a second. `memmove` was the last thing in the way: the game imports it from the C runtime, 9.x does not export it under that name, and eight instructions of local implementation replaced a panic |
| E125 | **build 113 run by hand, to look at the screen** | -- | `--` | **Pixels, moving, at 40-42 FPS.** Three captures forty seconds in and three seconds apart differ by 50,354 and 51,486 pixels, and the mean brightness moves with them, so it is a live frame loop and not one frame held. What is drawn is a band across the top of the game area -- roughly 176 by 130 -- of horizontal magenta/green/grey streaks, with the rest of the area white. That is a **pixel format or stride mismatch**, not a content failure: the game is writing a framebuffer in one layout and the screen device is reading it in another. The N-Gage is 176x208 and the RM-409 is 240x320, which is the other half of it. This is the answer to "more than one frame or a black bar with pixels": the loop runs, the screen updates, and what is on it is wrong in a way that is now a display bug rather than a boot failure |
| E126 | build 114 -- log the `TScreenInfoV01` the game is handed | 196818 | `--` | **The screen the game is given, and the mismatch in full.** One `UserSvr::ScreenInfo` call, answering `iScreenAddressValid = 1`, `iScreenAddress = 0xc9200000`, `iScreenSize = 240 x 320`. EKA2L1's screen buffer is `display_mode::color16ma` -- **32 bits per pixel**, a 960-byte line. The game ignores the size it is told and writes a linear **176x208 at 16bpp**, which is 73,216 bytes, which is 76 rows of that 960-byte line: exactly the band on the screen, squashed two-to-one and streaked because two of its pixels are being read as one of the emulator's. `CFbsBitmap::Create`, `DataAddress` and `HAL::Get` are never called, so this raw framebuffer is the whole drawing path |
| E127 | build 115 -- hand the game its own 176x208 16bpp buffer and convert at `Update` | 162 | `0x4` | **Dies at the first `Update`.** 162 records, then `ldr r0, [r3, r1, lsl #2]` at `0x11fbe8` with `r3` zero -- a null vptr on the object at `0x8ad448` -- called from `0x338f4`, immediately after the first `CFbsScreenDevice::Update`. Two things changed at once, which was the mistake: the address handed to the game *and* the size reported to it. Next build changes only the address |
| E128 | **build 116 -- substitute only the framebuffer address** | 192475 | `--` | **The game renders.** The car, the road, billboards, the HUD reading `WANTED` and `$00000`, the dashboard strip along the bottom, at 37 FPS. Giving the game a linear 16bpp buffer of its own and converting it into the emulator's 32bpp screen at `CFbsScreenDevice::Update` is the whole fix. E127's crash was the reported size, not the address: leave `iScreenSize` at the device's 240x320 and the game is happy, even though it draws 176x208 regardless. Colours are wrong -- a heavy green and cyan cast -- so the 16-bit layout is not RGB565 |
| E129 | **build 117 -- `EColor4K` conversion, read instrumentation off** | 181513 | `--` | **The game is playable-looking.** Sunset sky, cliffs, palm trees, the race countdown, the bike, the HUD and the speedometer strip, at 40 FPS, with 63,511 pixels changing between captures five seconds apart. The N-Gage writes `EColor4K` -- `0000RRRRGGGGBBBB` -- not RGB565; reading it as 565 gave the green cast of E128 and the documented 4K layout gives natural colour. The whole display fix is three things: hand the game a linear 16bpp buffer of its own from `UserSvr::ScreenInfo`, leave the *size* it is told alone, and convert 176x208 from 4K into the emulator's 240x320 `color16ma` screen at every `CFbsScreenDevice::Update`, centred |
| E130 | **build 118 -- bridge `OfferKeyEventL` into the game's own control** | 189838 | `--` | **The slot is found and replaced**: `0xc0de0003` says the 9.x `CCoeControl::OfferKeyEventL` sat at index 3 of the copied vtable, located by resolving cone ordinal 26 and scanning for its address rather than hardcoding an index. No key events, because nothing pressed any. The game takes input by overriding **old control slot 1**, which the image names itself: every other control vtable carries a base-class veneer there, and in the `CAknNoteDialog` one that veneer is avkon 1166, `OfferKeyEventL`. The game's control vtable overrides slots 0, 1, 19 and 24 -- destructor, OfferKeyEventL, FocusChanged, Draw |
| E131 | **build 118 driven by hand with `xdotool`** | 128597 | `--` | **The game is playable.** Eight keypresses into the focused window and 97 `NOTE_KEY` records come back: `iCode 0xF809` (`EKeyUpArrow`), scan code `0x10`, event types 3, 1 and 2 -- down, key, up -- and the game answers **1, `EKeyWasConsumed`**, every time. The screen goes from the attract race to the **vehicle selection menu**: a Hummer H2 with H.P. 325, MAX 180k, 0-100 in 11.90s, the vehicle carousel and SELECT/BACK softkeys. One harness trap worth writing down: `xdotool search --name EKA2L1 | head -1` picks the 3x3 *Qt selection owner* window, which cannot take focus and swallows everything. The real window is the one whose name ends `Symbian OS emulator`. Noted for later: text is clipped at the right edge on both the race HUD and the menu, which may mean the game's framebuffer is wider than the 176 we blit |
| E132 | build 119 -- tell the game its screen is 176x208, with the buffer big enough either way | 185349 | `--` | **No crash, and no change to the picture.** So two things at once: E127's death was the **undersized buffer**, not the size report -- reporting 176x208 is harmless once the allocation covers the device's own screen -- and **the game does not take its layout from `UserSvr::ScreenInfo`**, because the clipping is identical with either size. My hypothesis for the clipped right edge was wrong. Measuring the stride from the data instead |
| E133 | build 120 -- dump one frame of the game's framebuffer | 199226 | `--` | **The stride is 176 and the blit is exact.** Rendered from the raw dump at 176 the title screen is pixel-perfect -- logo, cars, `PRESS ANY KEY`, `(c)2005 gameloft`, all centred, no shear. At 192 and at 208 it shears badly. And the game writes **exactly 176x208 and not one byte more**: the 41,984 bytes of the buffer past `176*208*2` are untouched zeros. So nothing we do is cutting the picture |
| E134 | build 121 -- size the lent window to 176x208 | 173710 | `--` | **No change.** The wrapper control's window is the only other place a width could come from and it makes no difference to the layout |
| E135 | build 122 -- report a 192-pixel pitch and blit the visible 176 of it | 175768 | `--` | **Sheared, so the game ignores it.** Its row stride is hardcoded 176 and nothing we report moves it: telling it 240x320 and telling it 176x208 give **byte-identical** frames, measured on the dumps rather than judged by eye |
| E136 | build 123 -- restore the working display configuration | 174465 | `--` | Back to the picture of E129: menu and race render correctly at 32 FPS. `TELL_GAME_ITS_SIZE`, `SIZE_THE_WINDOW` and `DUMP_FRAME` are all off, and each is left in place with what it measured written beside it |
| E137 | **reference attempt** -- run the game on EKA2L1's own N-Gage profiles | -- | `--` | **No reference obtainable, and none needed.** EKA2L1 has `NEM-4` (N-Gage) and `RH-29` (QD) installed with ROMs, and the game launches on them -- `6rbc.app (UID3=0x101FD42D) runtime code: 0xe0000000` -- but dies in the Codewave check at **`ldr r2, [r1, #0x240]`**, the same instruction this project's notes already name. Swapping in the BiNPDA loader does not help: same fault. So the emulator cannot show what an N-Gage would. It does not matter, because the game asks for nothing that could change the layout. Its **only** geometry query is `UserSvr::ScreenInfo`, whose size it demonstrably ignores (240x320 and 176x208 give byte-identical frames), and it imports **no** text rendering at all -- no `DrawText`, no `CFont`, no `TextWidth`, no `HAL::Get`, no `CCoeControl::Size`. Every glyph is its own bitmap font drawn straight into the framebuffer. So the layout width and the row stride are both hardcoded in the binary, and the same binary draws the same picture on any framebuffer. Zoomed 7x the cut is real -- `11.9` and a sliced `0` -- and the `50` at the left edge is a coherent boxed badge hanging off, not wrapped text. **The clipped right edge is what this game looks like; it is not the port's doing and the port cannot change it** |
| E138 | build 124 -- ask HAL for the panel's bits-per-pixel and line pitch | 101141 | `--` | **The assumption that would have broken the phone.** The blit hardcoded 32 bits a pixel and a `width * 4` pitch, which is true of EKA2L1's `color16ma` screen and need not be true of an N95: writing 32-bit pixels into a 16-bit framebuffer puts twice the bytes into every line and runs off the end of each one. `HAL::Get` added as our own import (hal.dll ordinal 1). It answers `EDisplayBitsPerPixel` **24**, `EDisplayOffsetBetweenLines` **960**, `EDisplayOffsetToFirstPixel` **32** -- and 24 for a buffer that is plainly four bytes a pixel, because that attribute reports colour depth, not storage. The offset-to-first-pixel is recorded and deliberately **not** applied: `ScreenInfo` already hands back the first pixel |
| E139 | build 125 -- derive bytes-per-pixel from the pitch, not from the bpp attribute | 168882 | `--` | **Renders correctly, and now it would on a 16-bit panel too.** `pitch / width` cannot lie about storage where `EDisplayBitsPerPixel` can, and the blit takes a 16-bit path as well as a 32-bit one. Two link traps caught here rather than on the phone: a runtime divide pulls in `__aeabi_uidiv`, which this image does not link, so the pitch is *compared* against `w*2` and `w*4`; and the RGB565 branch had the same divides waiting for whenever `SCREEN_4K` is turned off -- now bit replication |
| E140 | build 126 -- prune the milestone trace | 157375 | `--` | **Barely moved it, and said why.** Dropping `UserSvr::DllTls` (90,808 calls), `CCoeEnv::Static` and `CFbsScreenDevice::Update` from the milestone set -- all per-frame or worse now the game runs -- took 175k records to 157k. The flood was somewhere else |
| E141 | **build 127 -- turn the allocation and free watchers off** | 7365 | `--` | **157,375 records to 7,365, twenty-one times less writing.** Codes 883-889 -- every allocation, every free, and the heap cell headers around them -- were 149,000 of the run. They were built to chase a use-after-free, they found it, and on a phone at a flush every eight records they would have been twenty thousand write-and-flush pairs, which looks exactly like a hang. Checked before flipping them: `LEAK_EVERYTHING` and `PAD_THE_ALLOCATIONS` are separate switches, so `gate6_free` still leaks deliberately and nothing about the run's behaviour changed -- only what it says about it. Renders, navigates, no panic |
| E142 | build 127 reference for the round 60 diff | 7344 | `--` | **The baseline that made round 60 readable.** Same build the phone ran, same drive contents, nothing changed -- its only job was to be lined up against `r60/a.log`. It renders and navigates as E141 did. Lined up from each side's first `NOTE_FRAME`, the phone and the emulator match **1,188 records in a row** inside the first `RunL`, code for code, and part company on the 1,189th. Two runs of the phone gave byte-identical logs, so the divergence is deterministic. Also caught a tool bug worth keeping: `scratchpad/dis.py` prints every mnemonic one instruction below its true address, which sent the first reading of `0xba354` to the wrong call. `scratchpad/d2.py` decodes each word at its own address and is what the round 60 disassembly is from |
| E143 | **build 128 -- clamp `RThread::Create`'s stack, and reject a HAL pitch that is not a multiple of the width** | 15253 | `--` | **The clamp works and costs nothing.** Both thread creations now come through `stack_thunk`, which copies the caller's stack arguments down itself instead of leaving the callee to read ours -- the objection that kept `WRAP_CREATE` off since the frame_thunk attempt. The game's own worker asks for 0x2000 and is untouched; the SoundServer start site asks for 100,000 and gets **0x10000**, and `Create` answers **0**. No retry was needed here because the emulator has no cap to hit; the halving loop is for the phone. Frames went 1,402 -> **4,053** and records 7,344 -> 15,253 in the same 120 seconds, so nothing was slowed by the wrapper. And the HAL attribute put on trial answered: **`EDisplayMode` = 11, `EColor16MU`** -- four bytes a pixel, which is the truth the emulator's `EDisplayBitsPerPixel` of 24 does not tell. The pitch check is a no-op here (960 == 240*4, self-consistent) and only changes what happens on a phone |
| E144 | **build 129 -- clamp OFF, against an EKA2L1 that now enforces the EKA2 user-stack ceiling** | 1274 | `--` | **The emulator now fails exactly as the phone does, record for record.** `svc.cpp`'s `thread_create` refuses a user stack over 0x14000 with `KErrTooBig` instead of allocating whatever is asked, and the run ends: `RFile::Read` from `0x34b00` twice, `User::Leave(-40)` from `0xba354`, `User::Exit`. Aligned from each side's first `NOTE_FRAME`, the emulator and the phone's round 60 log are **1,196 records with not one code out of place** -- the whole remaining run -- and every one of the 334 value differences is a heap address, a library handle or the image base. The kernel log also names the thread: **`Thread SoundServer asks for a 100000-byte stack; EKA2 allows 81920`**. This is the run that makes the emulator a valid check before a phone round, for this class of bug: before the patch it accepted what a device refuses, so 142 runs cleared a build that could not work |
| E145 | **build 130 -- clamp back on, against the emulator that now has the ceiling** | 12497 | `--` | **The pair works.** With both halves in place the game runs: 3,147 frames, no `User::Leave`, no `User::Exit`, no refusal in the kernel log. The SoundServer thread is created with 0x10000 instead of 100,000 and `Create` answers 0; the game's own worker asks for 0x2000 and is left alone. So the emulator can now *both* reproduce the phone's failure (E144) and show the fix clearing it, which is what a pre-hardware check has to be able to do. This is the build to send |
| E146 | **build 131 -- guard `box_flush`'s `RFile::Flush` against the wrong thread** | 12509 | `--` | **No regression, and the emulator cannot show the fix.** 3,109 frames against E145's 3,147 in the same 90 seconds, no leave, no exit, and the first 6,078 records identical to E145 before the two drift apart the way two timed frame loops do. That is the whole of what this run can say: EKA2L1 does not enforce the file server's thread affinity, so the unguarded flush was harmless here and removing it changes nothing here either. The evidence for the fix is round 61's phone log, not this. Worth writing down as a limit of the instrument rather than a null result: the emulator caught the stack cap only once it was taught to (E144), and it has not been taught this one |
| E147 | **build 132 -- the box on every traced import, the SoundServer handshake traced, and `RSemaphore::CreateLocal`'s result** | 12104 | `--` | **The window is now legible, and it is thirteen records long.** Where round 62's log had two `RFile::Read` and then the frame ending, build 132 shows the whole handshake: `RSemaphore::CreateLocal` at `0xb8758` answering **0**, `RThread::Create` answering 0 with the clamped 0x10000 stack, `SetPriority`, `Resume`, `Sem::Wait`, the two `RHandleBase::Close` at `0xb87b4` and `0xb87bc`, then `RSessionBase::CreateSession` at `0xba544` -- and only then the frame ends. So the phone's death has thirteen named places to be instead of a thirty-one-event window, and the semaphore the whole handshake hangs off is real at least here. Costs 3%: 12,104 records and 2,940 frames against E146's 12,509 and 3,109 in the same 90 seconds, for what on the phone is about 240 write-and-flush pairs. The SoundServer thread's own imports stay out of the log by design and will show in the box's ring |
| E148 | **build 133 -- the worker gets its own log file, connected on its own thread** | 14331 | `--` | **It works, and it shows the emulator cannot exercise the thing it is for.** The worker's `RFs` is connected on the worker, its `C:\g6wrk.log` is written a record at a time and flushed each time, and the run is *faster* than E147 rather than slower: 14,331 records and 3,708 frames against 12,104 and 2,940. Five records came out, all from the game's polling worker -- `RLibrary::Load` and `Lookup` -- and **none from the SoundServer thread**, because in EKA2L1 that thread never runs its entry function at all: imports 330, 386, 319, 363 and 367 are zero in both files, and `RSemaphore::Wait` returns regardless. On the phone the box caught `CTrapCleanup::New from b866c`, so there the thread does run. One more place the emulator gets past a handshake by not honouring it, and one more thing only hardware can answer |
| E149 | **build 133b -- the worker log marks which worker is writing** | 14467 | `--` | **Readable.** Two threads share the one file, so a `NOTE_WORKER_SP` (849) goes in whenever the writing thread's stack moves by more than 8 KB, and the file reads as separate stories rather than one interleaved mess. First record out is `849 04a01e2c`, then the same five as E148. No cost: 14,467 records, 3,708 frames. This is the build to send |
| E150 | **build 134 -- `worker_log` off; the main thread's `RSemaphore::Wait` in 100 ms slices that flush the box, giving up after five seconds** | 14289 | `--` | **Costs nothing when the handshake is healthy.** `NOTE_SEM_WAIT` (836) goes down twice: `0` on entry and `1` on return, so the emulator's semaphore is signalled inside the **first** slice and the timed wait is indistinguishable from the blocking one -- 3,707 frames, the same as E149. The point of it is the two things a blocking wait costs on hardware and not here: the box stops being flushed the moment the main thread blocks, and the application stops answering, which is `ViewSrv 11`. Slices fix both, and the give-up lets the game past a signal that never comes, which no round has ever seen it do. `worker_log` is off and wrote nothing, as intended -- round 64 named the thread it was killing |
| E151 | **build 134b -- the timed wait gives up after two seconds rather than five** | 14485 | `--` | **Same behaviour, with the watchdog left alone.** All of this happens inside frame 1's `RunL`, and this file already records a phone watchdog reset at about ten seconds of not yielding; five seconds of waiting inside that one call was too close to it for a number chosen carelessly. Signalled in the first slice again, 3,787 frames, `g6wrk.log` absent. This is the build to send |
| E152 | **build 135 -- lend a new thread the creating thread's heap when the game passes `aHeap = NULL`** | 14538 | `--` | **Builds, reads a sane heap, changes nothing here -- and cannot, by construction.** `User::Allocator()` (euser 665) answers **`0x700000`** on the main thread and that is what `stack_thunk` now bakes in and substitutes for the null `aHeap`; the clamped 0x10000 stack still goes through beside it. 3,748 frames, no leave, no exit, no regression. The emulator cannot say more than that: E148 established that EKA2L1 never runs the SoundServer thread's entry function at all -- imports 330, 386, 319, 363 and 367 are still zero here -- so the allocation that is supposed to stop failing never happens. As with round 61's flush guard, the evidence will be the phone or nothing |
| E153 | **build 136 -- trace the game's allocator, but record it only when a worker makes the call** | 14233 | `--` | **The filter holds and costs nothing.** Imports 269, 372, 373, 323 and 315 now carry trace thunks, and the main log contains **zero** records for any of them -- `gate6_trace` drops them unless a worker made the call, so the box is not flushed thousands of times a run and the phone will not spend its budget writing the game's allocator down. 3,689 frames, no leave, no exit. As with E148 and E152 the emulator cannot show the half that matters, because it never runs the SoundServer thread's entry function; what it can show is that the filter works, which is the thing that would have made this build unshippable if it did not |
| E154 | **build 137 -- a worker asks, one instruction before its first import, what its allocator is and whether it can allocate** | 14178 | `--` | **The probe works and one slot is not enough.** Six words in the box's unused crumb region, written by the worker and carried out by the main thread's wait slices, with a progress word set before each step so a fault inside the probe still says which step. It reported: at import 325 (`RLibrary::Load`), sp `0x04a01e7c`, allocator **`0x00700000`** -- the same heap the main thread has -- and `alloc(16)` -> `0x00a27eb0`, all the way through. But that is the game's *polling* worker, which is resumed at record 211, long before the SoundServer thread exists, so on one slot it would always answer for the wrong thread |
| E155 | **build 137b -- one probe slot per worker, told apart by stack** | 14364 | `--` | **Both workers answer, and one long-standing reading is retracted.** Slot 0 is the polling worker at `RLibrary::Load`; **slot 1 is the SoundServer thread at `CTrapCleanup::New`, sp `0x04d0ff90`** -- allocator `0x00700000`, `alloc(16)` -> `0x00d8dd60`, all the way through. So **EKA2L1 does run that thread's entry function after all**: E148 read zero records for imports 330/386/319/363/367 in the *log* and concluded the thread never ran, when the log cannot carry worker events at all -- they were in the box. That is corrected. It also means the emulator gets the whole handshake right, `Signal` included, which is why its wait returns on the first slice. 14,364 records, 3,600-odd frames, no regression. This is the build to send |
| E156 | **build 138 -- `on_main_thread` asks the kernel for the thread id instead of measuring a stack** | 0 | `0x354` | **Dead before its first record, and the reason is an ABI detail worth keeping.** `TThreadId` is a `TUint64` wrapper, so `RThread::Id() const` is a **struct return**: the hidden result pointer goes in r0 and `this` in r1. Declared as a plain `u32 rthread_id(const void*)` the call took `&handle` as its return buffer and wrote eight bytes over a stack local before anything had been logged. The project already has a `KIND_SRET8` for exactly this shape and I did not look |
| E157 | **build 138b -- `RThread::Id` is a struct return; the hidden pointer goes in r0** | 14551 | `--` | **The identity test works and nothing regressed.** 14,551 records, 1,561 frames, and **both worker probes still fill** -- slot 0 the polling worker at `RLibrary::Load`, slot 1 the SoundServer thread at `CTrapCleanup::New` -- so asking the kernel classifies the two workers exactly as the stack test did here, which is the point: the emulator was never where it went wrong. The main thread's id is latched the first time `on_main_thread` runs, which is during setup, and the stack test stays only as the fallback for that one call. This is the build to send |
| E158 | **build 139 -- a table of candidate framebuffer formats, cycled live from the keypad with `*` and `#`** | 11007 | `--` | **Builds, and entry 0 leaves the run exactly as it was.** `NOTE_SCREEN_FMT` records `idx=0 bpp=32 pitch=960`, which is what the derivation produces, and the run is normal: 2,572 frames, no leave, no exit. Eight candidates, including the 24-bit packed `EColor16M` case, which needed a third branch in the blit -- three bytes, blue first, written a byte at a time because there is no alignment to lean on |
| E159 | **build 139b -- a digit picks a format outright, and the picker listens on `EEventKey`** | 10707 | `--` | **Two corrections to the picker, both from the record rather than from testing it here.** A digit `0`-`7` selects a candidate directly, because "press `*` four times" is a worse instrument than "press 4". And the event type: I had written `EEventKeyDown = 1`, but E131 recorded types **3, 1, 2** for a single press and those are `EEventKeyDown`, `EEventKey`, `EEventKeyUp` -- so type 1 is `EEventKey`, which is the only one where `iCode` carries a character at all. The value was right and the name was wrong, which is the kind of thing that is right until it is not. Driving the keys here with `xdotool` did not work: this container has no window manager, so `windowactivate` is refused and the emulator never gets focus. Not chased, because the key path is proven on the phone -- round 69 put **157 key events** through this very function -- and the round is self-correcting either way: every key's `iCode` is logged, so if the picker does not fire, the log says exactly what the phone sends instead |
| E160 | **build 140 -- paint a ruler over the top of the frame so one photograph measures the framebuffer** | 10778 | `--` | **The pattern is right, verified by looking at it.** Captured from the emulator, where the format is known to be correct, it is exactly what it should be: a row of red/green/blue/white bands eight pixels wide across the top, six bands of four rows each below them, a one-pixel white border, and the game's loading screen underneath, undisturbed. That is the control -- whatever the phone shows, the difference from this is the answer. 10,778 records, no leave, no exit, and the cost is one pass over 176x32 pixels a frame |
| E161 | **build 141 -- the picker steps the line by sixteen bytes and switches depth, with presets around 576** | 10881 | `--` | **Builds, runs, entry 0 unchanged.** `NOTE_SCREEN_FMT` now records the bits and the line rather than a table index, which is what matters once the line can be stepped off the table: `bpp=32 pitch=960`, the derived pair, and 2,502 frames with no leave and no exit. The digits are presets around 576, `*` and `#` move the line sixteen bytes at a time between 240 and 4096, and `9` flips between two and four bytes a pixel, so the whole space is reachable by hand. One self-inflicted build error on the way, worth a line: the replacement spliced the new table in *above* the old `screen_format` instead of over it, and clang caught two redefinitions -- the sort of thing that only costs a minute when the compiler is the one reading |
| E162 | build 142: scale 176x208 up to fill the buffer, and blank it once | 14542 | `--` | ran; fit records say buffer 320x240, picture 203x240 at x=58 -- the arithmetic is right, but `SCREEN_KNOWN` forced the N95's 1280-byte line onto the emulator, whose own is a self-consistent 960. Made the measured value a fallback instead |
| E163 | **build 142b -- believe a self-consistent HAL pitch, fall back to round 72's** | 13660 | `--` | **Right in both places now.** The emulator reports 24 bits on a 960-byte line for a 240-pixel screen, and 960 *is* 240 at four bytes, so it is believed and the emulator draws as it always did; the N95 reports 16 bits on a 640-byte line for the same 240, which is consistent with nothing, and there round 72's measured 32/1280 is taken instead. `NOTE_SCREEN_FIT` records the three pairs that matter: buffer **240x320**, picture **240x283**, offset **0,18** -- the width filled, the shape kept, 13,660 records, no leave |
| E164 | **build 142b again, with a screenshot** | -- | `--` | **Looked at, not inferred.** The race runs with the picture scaled to the full width of the emulator's screen: HUD legible across the top, the dashboard bar at the bottom, the car centred, nothing of the shell left round the edges. That is the same code path the phone will take, with different numbers in it -- 320x240 buffer, 203x240 picture, centred at x=58 |
| E165 | **build 143 -- the screen is the reported 240x320, the pitch only a stride** | 13339 | `--` | **The records read as they should.** `NOTE_SCREEN_FIT` gives buffer **240x320**, picture **240x283**, offset **0,18**, and `NOTE_SCREEN_SRC` gives 176/176. Same numbers the emulator produced before, which is the point: the layout no longer depends on the pitch, so the one build is right on a padded line and an unpadded one alike. The clear now covers all 320 rows, and stops at the last row's last *visible* pixel, since the pad after row 319 need not exist |
| E166 | **build 143 with a screenshot** | -- | `--` | **Looked at.** The race fills the width of the screen with the shape kept, nothing of the shell round it. The `$000000` on the HUD is still clipped at the right edge -- that is the source-side wrap, untouched by this build and the thing round 75 is for |
| E167 | **build 143d -- two raw frames of the title screen, to measure the overflow instead of arguing about it** | 17213 | `--` | **The bytes say it is not an overflow at all.** Rendered at a 176 stride the title screen is pixel-perfect, and the artefact is a sixteen-column strip down the left. Three measurements over the frame, each a mean absolute difference between column pairs: the sharpest column boundary in the whole frame is **between 15 and 16, at 13.77 against a mean of 2.76**; the last column of a row against the first of the next is **1.46**, *smoother* than a typical adjacent pair at 4.23; and column 0 against column 16 is 16.28. A picture whose rows run smoothly across the row boundary and break at column 16 is a picture **displaced sixteen pixels along the buffer**. Its own right-hand edge is what comes back through the left |
| E168 | **build 143e -- two raw frames of a race, to see whether the offset is the same elsewhere** | 18650 | `--` | **Same sixteen, and the stride confirmed a third way.** The sharpest column boundary on the race screen is again at 16 (10.4 against a mean of 3.29), so the displacement is not a property of the title screen. And scoring the stride by *vertical* continuity -- column c of row y against column c of row y+1, over the middle of the frame -- picks **176 at 3.76** against 5.46 for its nearest neighbour and 8.5 at 240. Stride 176, origin 16, measured from the game's own bytes on two unrelated screens |
| E169 | **build 144 -- read the game's buffer from pixel sixteen** | 14207 | `--` | **Builds and runs, 14,207 records, no leave.** One term added to the source address in the blit and one field in the context; the layout is untouched, because the picture is the same size and only read from a different place |
| E170 | **build 144 with a screenshot** | -- | `--` | **The wrap is gone.** `$000260` is whole where it read `$00026` with a stray `0` at the far left, `KmH` is whole where the `K` was on the other side of the screen, the dashboard runs edge to edge, and the left column is clean. Same run also confirms the round-74 shape holds: the race fills the screen with the aspect kept |
| E171 | **build 144b -- paint the blit's own outline** | 5031 | `--` | **The strip in E170 was ours, and the instrument said so in one run.** Destination column 0 painted green, column 239 red: the green line came out at screen x=639 and the red at x=637, *beside it*, at the right-hand end of the picture. Every row was starting eight pixels early and wrapping. Eight pixels at four bytes is 32 bytes, and the emulator's own source has that number twice -- `hal.cpp` computes `EDisplayOffsetToFirstPixel` as `sizeof(u16) * WORD_PALETTE_ENTRIES_COUNT` = 32, and `screen.cpp` adds exactly that to the chunk base to find the pixels. **The screen chunk begins with a sixteen-entry word palette and `ScreenInfo` hands back the chunk's base.** I had read HAL's 32 as an emulator quirk and refused it since build 59 |
| E172 | **build 145 -- skip the palette** | 5979 | `--` | **Right here and still right on the phone.** HAL's offset is applied now, but only after a sanity check -- a whole number of pixels and under 4 KB -- because HAL has lied about every other display attribute on the N95. The emulator answers 32 and the emulator needs 32; the **phone answers 0**, which is why build 144, applying nothing, was correct there. One build, both right. The screenshot has the picture edge to edge with no strip |
| E173 | **build 146 -- no screen furniture** | 7923 | `--` | **Avkon takes the flag and the game still runs.** `ENoScreenFurniture` (0x04) added to the flags the game asks for. 7,923 records, the frame loop running, the picture unchanged here -- which is the point: the emulator does not paint a status pane over us, so this run only shows the flag is safe. Whether it clears the band is the phone's to say |
| E174 | **build 146 with a screenshot** | -- | `--` | **Looked at, as a control.** The race renders exactly as it did in E170: full width, shape kept, `$000260` and `KmH` whole, nothing at any edge. Nothing regressed by dropping the furniture |
| E175 | **build 147 -- the furniture flag, plus a keypad inset** | 4138 | `--` | **Runs, layout unchanged at inset zero.** `NOTE_SCREEN_DST` now carries the inset beside the first-pixel offset: 0 and 32 here. The inset shrinks the height the picture is fitted into and pushes `offY` down by the same amount, so the phone can put the picture below the status pane if the flag does not remove it. The clear still covers all 320 rows |
| E176 | **build 148 -- fill mode, bottom-anchored, inset 56, full-screen window** | 4910 | `--` | **The records read exactly as intended.** Picture **240x264 at (0,56)**: full width, bottom edge on row 320, the leftover 56 rows at the top. `NOTE_SCREEN_DST` now packs inset, mode and first-pixel offset together and reads 56 / mode 2 / 32. The window is sized to the whole screen rather than to the game's 176x208, which is the one untried lever on the band |
| E177 | **build 148 with a screenshot** | -- | `--` | **Looked at.** Full width, bottom exact, a black band at the top where the inset is -- which in the emulator is just unused space, because there is no status pane here. The 7 per cent difference between the two scales is not visible in the picture |
| E178 | **build 149 -- the picker off** | 5777 | `--` | **Layout fixed where the phone left it.** 240x264 at (0,56), filled, inset 56, first-pixel 32, and no key is swallowed any more. The phone's own dwell chose those numbers: in the build-148 logs **inset 56 filled held 6,206 records** against 2,600 for the next genuine layout, so the default is what the user settled on, not a guess |
| E179 | **build 150 -- a talking fake for every N-Gage-only import** | 10629 | `--` | **A non-null answer changes the game's path.** With zeroes, five GAMEUTILS entries are called once each and GAMECOMMS never. With fakes: **GAMEUTILS ordinal 17 is called 1,883 times**, once a frame, and GAMECOMMS ordinals 2, 20, 27 and 28 start being called too. The game also called **slot 2** of the fake vtable on two of the objects, so it does treat what these return as polymorphic. MDA is still never reached |
| E180 | **build 150b -- the same probe, recording arguments and call sites** | 20163 | `--` | **Every N-Gage call placed, with its arguments.** GAMEUTILS 19/14/15/10 are constructors -- their results are stored at object offsets 972, 980, 664 and 988 -- 11 is a predicate turned into a boolean, and **17 is a per-frame predicate on the object from 19**: `subs r4, r0, #0; movne r4, #1`, then if set, a call with 1000. So the blanket non-null is answering *yes* to a poll every frame, which is why the probe is off again. NOKIAFC ordinal 1 gets a 520-byte descriptor and two small numbers |
| E181 | **build 151 -- trace the sound path** | 9273 | `--` | **The client is talking constantly and nobody is listening.** `RSessionBase::SendReceive` **835 calls in a minute**, from eight different wrappers (`0xbab48` 365, `0xbaba8` 359, `0xba994` 58, ...). E180's reading that it was never called was wrong: `TRACE_MILESTONES` traces only the imports named in `kMilestone`, and 357 was not one of them, so its absence meant *never watched*, not never called. The server side is on the SoundServer thread, whose records go to RDebug rather than the log, and the emulator's own output has them: `CTrapCleanup::New`, `CActiveScheduler` ctor and `Install`, **`CServer::CServer`**, **`CServer::StartL`**, `RSemaphore::Signal`, `CActiveScheduler::Start` -- the thread builds its server and sits in its loop. `CServer::StartL` is `LOCAL_NOOP`, so nothing was ever really started, and `CreateSession` and `SendReceive` are `LOCAL_NOOP` too: 835 messages a minute into a stand-in that answers `KErrNone` and drops them |
| E182 | **build 152 -- the in-process sound bridge** | 1307 | `--` | **It works, and the panic proves it.** `CreateSession` called the server's `NewSessionL` through vtable slot 6 and the log shows **`CSession::CSession`** -- the game's own session constructed, for the first time. The first `SendReceive` then reached `ServiceL` through session slot 5 with function 1. The run ended in **`G6IMP 464458`**, which is our own unresolved-import panic: 464x1000 + **458**, `CMdaAudioOutputStream::NewL`. The game had never got that far before |
| E183 | **build 152b -- a talking fake for the audio stream** | 12709 | `--` | **The whole sound path runs, and the stream's interface is now known.** No panic, 12,709 records, and the bridge carried functions **1, 4, 5, 15 (x48), 19, 25, 26, 27, 28, 32, 33** across. The game created one stream and called slots **3, 4, 5, 7, 9 and 10** on it |
| E184 | **build 153 -- the real 9.x NewL** | 1313 | `0xFFFFFFFC` | **Resolved and called, then KERN-EXEC 3.** `epoc9.def` -- the same database as `epoc6.def`, for 9.x -- gives mediaclientaudiostream ordinal **3** for `NewL(MMdaAudioOutputStreamCallback&, CMdaServer*)`. The DLL loaded, the ordinal resolved, the game called it and used what came back, and died on the second message. Expected: the two builds order the vtable differently |
| E185 | **build 154 -- a proxy vtable, shifted one slot** | 2112 | `0x11C` | **Further, and still not far enough.** GCC98r2 gives a class one destructor entry and EABI gives it two, so every virtual after it sits one slot along; under that shift the game's observed slots 3/4/5/7/9/10 read as SetAudioPropertiesL, Open, MaxVolume, SetVolume, WriteL, Stop -- exactly the set for playing a stream. The proxy carries messages 1, 28 and 4 and then faults |
| E186 | **build 154b -- the same, with a null ALSA device** | 2112 | `0x11C` | **Rules out the container.** This machine has no sound card and cubeb was failing to start, which could have explained the fault. A null ALSA device (`pcm.!default { type null }`, `CUBEB_BACKEND=alsa`) removes that error entirely -- and the run is byte-for-byte the same, 2,112 records and the same fault. So the remaining crash is the port's, not the bench's: the vtable order is right and something in the *arguments* is not, `Open`'s settings package being the obvious candidate |
| E187 | **build 155 -- log every method the game calls on the stream** | 2127 | `0x11C` | **Three calls, and the arguments name them.** `Open(pkg)`, `Stop()`, then a call with `(0x100, 0x02000000, 0x2b11)`. 0x02000000 is `EMdaPriorityPreferenceQuality`, so that one is `SetPriority`. Cross-checked against the game's own dispatch sites: slot 4 is handed a pointer built at object+48 right after `NewL` (Open), slot 5 takes nothing and its result feeds slot 7 (MaxVolume into SetVolume), slot 9 takes an element of an array of descriptors (WriteL), slot 10 takes nothing (Stop) |
| E188 | **build 156 -- map the slots by name, not by a shift** | 2127 | `0x100` | **The one-slot shift was wrong, and the fault says so precisely.** The two builds do not merely offset: the N-Gage puts `SetPriority` *first* among the virtuals and 9.x puts it sixth. With a by-name table the run still dies, and the emulator names the address: **access violation reading 0x100** -- which is the first argument, 256, dereferenced as a pointer. So the 9.x slot numbers in the table were one too high and `SetPriority` was landing on `WriteL` |
| E189 | **build 157 -- one destructor entry instead of two** | 1320 | `0xFFFFFFFC` | **Wrong the other way.** Assuming a single destructor entry moves everything down one and the run now dies inside `Open` itself, reading `0xFFFFFFFC`. So neither guess at the 9.x vtable layout is right, and guessing costs a run each. What the two failures do establish is the shape of the answer: the game's own slots are known from its call sites, and only the 9.x side is unknown |
| E190 | **build 158 -- dump the real vtable and the code at each slot** | 1323 | `0xFFFFFFFC` | **Nothing came back, and the reason is worth keeping.** The guard only dumped entries whose address was 4-aligned, and every entry in this vtable is **odd**: the 9.x DLL is built for **Thumb**, so the low bit is the instruction-set bit and not part of the address |
| E191 | **build 158b -- the same dump, masked and widened** | 1403 | `0xFFFFFFFC` | **The vtable reads cleanly.** `[+0]` is 0 (offset-to-top), `[+1]` a typeinfo pointer, and **`[+2]` and `[+3]` both begin `push {r4,r5,lr}; ldr r3,[pc]; str r3,[r0]`** -- a destructor reinstalling a vtable, twice, which is the EABI complete/deleting pair. So the virtuals start at **4** and the first table was right about that. `[+4]` is a bare `bx lr` in another module, and the rest are Thumb forwarders of the shape `ldr r0, [r0, #8]; bl impl`. The 0xFFFFFFFC fault is **the dump itself** reading the word past the end of the table |
| E192 | **build 159 -- the map from the dump, and a clamped priority** | 1403 | `0xFFFFFFFC` | **Same fault, same cause: my own dump, still on.** Nothing about the game |
| E193 | **build 159b -- dump off** | 2127 | `0x64` | **The clamp works and the diagnosis moves on.** The priority now goes in as 100 rather than 256, and the fault follows it exactly: **access violation reading 0x64**, which is 100. So it is not the *value* that is wrong -- whatever the game's slot 3 lands on takes a **pointer** as its first argument, and `SetPriority` does not. The vtable's shape is settled; which 9.x slot is which is not |
| E194 | **build 160 -- `CBase::Extension_` accounted for** | 2122 | `0xFFFFFFFC` | **The missing slot, found in the dump.** `[+4]` is the only entry pointing outside the DLL, into euser, and its code is `movs r0,#0; str r0,[r2]; movs r0,#46; mvns r0,r0` -- return **-47**, `KErrExtensionNotSupported`. That is `CBase::Extension_`, which 9.x declares on CBase and EKA1 did not, so the virtuals start at 5 |
| E195 | **build 161 -- calls made from C, so returns are logged too** | 2122 | `0xFFFFFFFC` | **Now the fault has a name.** The two-stage thunk could say a call had started and never that it came back. With the call made from C: **`Open` was called and returned; `Stop` was called and did not**. `Stop` begins `iWaitBufferEndTimer->Cancel()` and then works the buffer queue -- state that exists only after an open completes -- and the game calls Stop defensively before anything is playing |
| E196 | **build 162 -- skip the defensive Stop** | 2134 | `0xFFFFFFFA` | **Three more calls, and the chain is visible end to end.** `Open` returns, `SetPriority` returns, **`MaxVolume` returns** -- and then the game computes **-6** from what MaxVolume gave it and hands that back as a volume, and `SetVolume(-6)` faults. 9.x opens a stream **asynchronously** and the N-Gage did not: the game asks for the volume the instant it has opened, and gets an error back |
| E197 | **build 163 -- sanitise the values crossing the bridge** | 2134 | `0x64` | **The values are sane now and it still faults, which moves the diagnosis.** A failed `MaxVolume` answers 100, a volume is clamped into range, a priority likewise: the log reads `Open` -> returned, `SetPriority` -> returned, `MaxVolume` -> **returned 100**, `SetVolume(100)` -> fault reading **0x64**. 0x64 is both 100 and a plausible structure offset, and three faults have now read an address equal to an argument -- which is the giveaway: these are **null-pointer dereferences at a field offset**, not arguments used as addresses. Something inside the implementation is null because the open never really succeeded |
| E198 | **build 164 -- dump the 9.x `Open`** | 2199 | `0x64` | **Inconclusive, and the instrument is why.** Three dumps came out, not one -- so **`NewL` is called three times** and the game runs more than one stream, which is worth knowing -- but they disagree about where slot 6 points, and only 66 of the 192 words asked for reached the log. Nothing about the settings layout was learned. Two dumps in a row have now produced confusion rather than data (E190 was the other), which is the signal to slow down rather than iterate faster: the offsets wanted here are in a compressed DLL that zlib will not inflate, because Symbian's deflate is its own, and the honest way in is EKA2L1's own decompressor rather than another guess at a memory address |
| E199 | **build 164b -- the tree as it stands** | 2134 | `0x64` | **Builds and runs, dump off.** The state to carry forward: the bridge delivers, the stream is created from the real 9.x `NewL`, and `Open`, `SetPriority` and `MaxVolume` all return before `SetVolume` faults on a null inside an implementation that never finished opening. 2,134 records, the same as E197, which is the point -- nothing regressed while the instruments came off |
| E200 | MDA NewL ordinal 3 -> 9 (read from the inflated patch DLL); vtable map identity from slot 3; all value clamps removed; Stop re-enabled | 1307 | `--` | **The ordinal change was wrong and the run said so in one launch.** Undefined instruction at 0x804D5028, an address holding the text of a class name, reached from our own `blx` in `gate6_mda_newl`. There are two DLLs of this name: the **ROM's** (UID3 0x10003996, 17 exports, loaded at 0x804d3eb8) is the one `RLibrary::Load` opens, and the emulator's patch (UID3 0xEE000001, 55 exports) does not replace it -- `patch/mediaclientaudiostream.dll.map` overwrites individual ROM exports and its lines read `<patch export> <ROM ordinal>`, so `9 3` is the patch's `NewL` installed over **ROM ordinal 3**. Ordinal 9 of the ROM is "typeinfo for CMdaAudioOutputStream". **Ordinal 3 was right all along.** Not a repeat of E188/E189 (vtable slot guesses) or E198 (the in-memory dump): this is the first time the export tables themselves were read |
| E201 | MDA vtable map corrected to the identity from slot 3 (the old map was two slots high: MDA_DUMP_VT prints rvt[k-2] and k was read as the slot); ordinal back to 3; clamps gone; Stop on | 1321 | `0xFFFFFFFC` | **The stream side is right: `Open` was called with the game's own settings package and returned.** The trace reads slot 4 called with (0xd8e2f8, 0, 0x489248c) and slot 4 returning, then the server completed the message and the game sent the next one. Then a fault at pc **0xfffffffc** with lr inside the patch DLL at +0x13bc, which is `ldr r0,[r0,#0x88]; ldr r3,[r0]; ldr r3,[r3]; movs r1,#0; blx r3` -- the implementation calling `iCallback->MaoscOpenComplete(KErrNone)`. **The callback is shifted two slots the other way**, and 0xfffffffc is the offset-to-top of the game's secondary vtable: -4, because its callback is a mixin sub-object four bytes into its sound object. Retracts three readings at once: `SetVolume` faulting on a null field (it was `WriteL` handed the volume 100 as a descriptor, which is why the address was 0x64), the same for 0x100 and 0xFFFFFFFA, and "9.x opens asynchronously so nothing is ready" -- the open is fine |
| E202 | callback proxy: MMdaAudioOutputStreamCallback shifted two slots (GCC98r2 vptr points at the vtable object, EABI at its address point), plus a trace of what the stream tells the game | 695381 | `--` | **The audio chain runs end to end, and this is the furthest the port has ever got.** No fault, no panic, the full 150 seconds, **695,381 records** against 2,134 in the best run before it. The trace in order: `Open` -> returned; **`MaoscOpenComplete(0)`** -- KErrNone, the stream really opened; `SetAudioPropertiesL(0x100, 0x02000000)` = 16000 Hz mono -> returned; `MaxVolume()` -> **10**; `SetVolume(10)` -> returned; `SetPriority(100, 0)` -> returned; `Stop()` x3 -> returned; then **`WriteL` 71,777 times, each answered by `MaoscBufferCopied(KErrNone, buffer)`** (71,775 of them), and one `MaoscPlayComplete`. The emulator's own patch prints `[MediaClientAudioStream] Open complete`, and the file the game opened is `E:\system\apps\6RBC\Streams\bgm_moby_lift_me_up.swav` -- the soundtrack. Not a repeat of anything: every earlier sound run died inside the first `Open`/`SetVolume` sequence |
| E203 | same tree as E202, with ALSA writing the mix to a file so the bench can hear it | 116322 | `--` | **Abandoned, and the method is the finding.** ALSA's `file` plugin over a `null` slave does not pace, so the emulator's mixer ran free and wrote **10 GB in ninety seconds**; the run was killed and the capture thrown away, and the first 8 MB of it -- a fraction of a second of boot -- was all zeros, which proves nothing. There is no way to hear this machine: it has no sound card, and any sink that does not block turns a timing question into a disk-space one. **The bench can prove the chain and not the sound.** Read the buffers instead (E204), and let the phone be the ear |
| E204 | peek at the first four WriteL descriptors, to see whether the buffers hold music or silence | 297786 | `--` | **Half of it: the descriptors are real, the data was not reached.** Each `WriteL` is handed word 0 = `0x200007d0` -- type **2** (`EPtr`, a `TPtr8`) and length **2000 bytes**, which at 16000 Hz mono 16-bit is 62.5 ms a buffer, a sane streaming size, and the same for all four. The peek then read word 1 as the data pointer and got 0x7d0 again: for `EPtr` and above word 1 is `iMaxLength` and the pointer is in **word 2**. My instrument, not the game -- corrected in E205 |
| E205 | peek again, reading the data pointer from word 2 for a TPtr8 rather than word 1 | 263166 | `--` | **The buffers are reached, and the first four begin with 28 bytes of zero.** Which is ambiguous by construction: four buffers is 250 ms, and a track's lead-in looks exactly like a dead decoder over that distance. A peek at the front of the first buffers cannot answer this -- it needs the whole buffer, on writes far enough in that silence cannot account for them. E206 |
| E206 | peek the whole buffer, as an OR and a non-zero count, on writes 0, 40, 160, 320 and 640 | 288610 | `--` | **It is music, not silence, and this settles the bench side.** Every buffer is a `TPtr8` of **2000 bytes** -- 62.5 ms at 16000 Hz mono 16-bit. Write 0 and write 40 are entirely zero, which is the track's lead-in, about 2.5 seconds of it. Writes **160, 320 and 640** (10, 20 and 40 seconds in) each have **all 500 words non-zero and an OR of 0xffffffff** -- full-range PCM. So the chain is proven from `bgm_moby_lift_me_up.swav` to `WriteL`, and everything past that is the device's DevSound, which only the phone can test. Answers what E203 could not |
| E207 | the quiet build: WriteL and MaoscBufferCopied stop logging after four, everything else as E206 | 28143 | `--` | **Same audio sequence, a tenth of the log: 173 MDA records against 646,106.** `Open` -> returned, `MaoscOpenComplete(0)`, `SetAudioPropertiesL(16000 Hz, mono)`, `MaxVolume` 10, `SetVolume(10)`, `SetPriority(100, 0)`, the buffer peek at writes 0/40/160/320/640, and nothing else per buffer. This is what the phone can carry -- 71,777 writes x 8 records would have been the whole box log and a good part of the frame time. **Shipped as build 165** |
| E208 | build 166: box every 1024 traced events instead of every one, log blocks of 256 instead of 8, and a tick either side of every frame | 34362 | `--` | **The clock works and the build is unchanged otherwise.** 2,617 clocked frames: 211 at zero ticks, 2,059 at one, 329 at two, and **eighteen at three or more (0.7%)**. The long ones name themselves -- frames 1 to 6 are the opening (library lookups, then 630 and 124 file reads), and a cluster at 926-931 is the race starting: 558, 224, 121 and 93 reads and 54 `SendReceive`s. Emulator ticks are not phone ticks, so the numbers mean nothing for smoothness; what this run establishes is that `ticks.py` reads the log correctly and that the quieter instrument changed no behaviour. **Shipped as build 166** |
| E209 | drive search across C/E/F/D/G/H and both layouts, the game always told E:, and an E:-to-real-drive translation in the three card file calls -- E: install, so the translation is the identity | 30266 | `--` | **The regression: nothing changed on E:.** 2,395 clocked frames, 166 at zero ticks, 1,944 at one, 266 at two, nine at three or more -- the same shape as E208. The audio sequence is identical: `Open` returns, `MaoscOpenComplete(0)`, `SetAudioPropertiesL`, `MaxVolume`, `SetVolume`, `SetPriority`. `c->dataDrive` is 'E' here so `on_the_real_drive` returns every name untouched, which is the point -- the existing install pays nothing for the new search |
| E210 | the game's files moved to C:\system\apps\6rbc and E: emptied -- does the drive search find them and does the E:-to-C: translation hold | 29611 | `--` | **Yes, both, and the phone never had to be asked.** With nothing at all on E:, the emulator's file server log reads: `C:\system\apps\6rbc\6rbc.app` found by the search, then `6rbc.cwa`, `6RBC.dat`, `cis.dat`, `cwivenc.dat`, `cwp.dat`, `nc.dat`, `nokia_en.rle` and `videos\bg_136x64.mpg` all on C: -- and **`C:\system\apps\6RBC\Streams\bgm_moby_lift_me_up.swav`**, which is the game's compiled-in `E:` path translated on the way into efsrv. 2,397 clocked frames with the same tick profile as E209 (174 at zero, 1,898 at one, 309 at two, seven at three or more), the same audio sequence, and **the read-only-card refusal fired once**, as it does on E: -- because the game is told `E:` whatever drive it is on, so the protection tests the letter it expects. The `GameMgr` folder the game creates on C: survived the test, which is the other half: writes it means for C: still go to C: |
| E211 | the standalone installer: 20.6 MB SIS with the port, all 34 game files, the N-Gage icon and an install-time message -- installed into the emulator and run from where it landed | 30167 | `--` | **The whole package works.** `Package Asphalt 2 registering with UID: 0xe0001006`, `EOpText` processed, `Installation done!`, 39 files written to E: including `E:\resource\apps\gate6.mbm`. The emulator's app list then shows **Asphalt 2** with the original orange swoosh icon, rendered by EKA2L1's own MBM reader -- which is also what corrected my own renderer, whose palette made it blue. Run from the installed state: 2,310 clocked frames (165 at zero ticks, 1,796 at one, 329 at two, twelve at three or more), `Open` -> `MaoscOpenComplete(0)` -> the full audio sequence, music opened under `\system\apps\6RBC\Streams\`. Not verified here: how Symbian's own installer renders the message -- EKA2L1 logs the raw buffer, so the log line shows the BOM rather than the text |
| E212 | the image can be 6rbc.bin as well as 6rbc.app, and the game's own open of .app is redirected when it is -- run here with the original .app, so nothing should change | 29221 | `--` | **Nothing does.** 2,379 clocked frames (184 at zero ticks, 1,894 at one, 289 at two, three at three or more), the same audio sequence, and `6rbc.app` still opened twice -- once by the loader and once by the game. `imageIsBin` is zero on this tree so the redirect never fires; the second name costs one extra failed open per candidate and nothing else |
| E213 | the image renamed to 6rbc.bin on disk -- does the loader find it and does the game's own open of 6rbc.app get redirected | 30724 | `--` | **Both.** The emulator's file server log has exactly two opens and both read `E:\system\apps\6rbc\6rbc.bin` -- the loader's, and the game's own, which asked for `6rbc.app` and was redirected. 2,415 clocked frames (245 at zero ticks, 1,875 at one, 276 at two, ten at three or more), the same audio sequence as every run since E202. So the rename costs nothing and the game cannot tell. What the bench cannot say is whether the *phone's installer* objects to the bytes or only to the name, which is probes 5 and 6 |
| E214 | the image scrambled as 6rbc.bin -- the loader unscrambles its own copy; does the game mind reading the scrambled file through the redirect | 29584 | `--` | **It does not, and that is the whole question answered.** 2,401 clocked frames (195 at zero ticks, 1,922 at one, 267 at two, eight at three or more), the audio sequence identical from `Open` through `MaoscOpenComplete(0)`, and the soundtrack opened as usual. Both opens of the image read `6rbc.bin` -- the loader's, which XORs its copy back before parsing, and the game's own, which gets the scrambled bytes and never notices. So the game reads its image but not the first 32 bytes of it, and none of the machinery I had drawn up for the content case -- tracking the file position to patch reads, or writing a clean copy out at first launch -- is needed. The cheap experiment was worth more than the design |
| E215 | build 170 installed into the emulator over an empty tree, then run from what the installer left | 29860 | `--` | **The package is the only source and it works.** The game's directory was deleted first, so every file the run touched came out of the SIS. `Package Asphalt 2 registering with UID: 0xe0001006`, `Installation done!`, and the directory then holds `6rbc.bin` and no `6rbc.app`. Run: 2,431 clocked frames (200 at zero ticks, 1,946 at one, 269 at two, six at three or more), both opens of the image on `6rbc.bin`, the soundtrack opened, and the audio sequence identical from `Open` through `MaoscOpenComplete(0)`. This is E211 again with the scrambled image in place of the refused one |
| E216 | round 83: the HAL display attributes queried for the live mode instead of mode 0, and the stride believed when it is sane instead of only when it equals the logical width -- ported from the other AI's binary patch, which the C5 confirmed | 20057 | `--` | **The source port reads exactly what their binary patch reads.** Both give `HAL bpp=32 pitch=960 first=32 mode=11 -> 32/960` on the emulator, against `bpp=24 pitch=960 mode=11 -> 32/960` for build 170: asking the live mode gets a straight answer where asking mode 0 got 24 and needed the 24-is-32 rule to rescue it. Same audio sequence, 1,585 frames. The tick profile is worse than E215's but the runs are not comparable -- 75 seconds against 90, different frame counts, and the bench was doing other work |
| E217 | build 171 installed over an empty tree and run from it -- the display fix in source, the MIF icon, and the install message back at the front | 20456 | `--` | **All three land.** `Package Asphalt 2 registering with UID: 0xe0001006`, **`EOpText` processed first this time** rather than last, `Installation done!`, and `\resource\apps\` holds `gate6.rsc`, `gate6.mbm` and `gate6.mif`. Run from what the installer left: 1,690 frames, both image opens on `6rbc.bin`, the audio sequence unchanged, and the display reading `HAL bpp=32 pitch=960 first=32 mode=11 -> 32/960`. Against the package the phone confirmed, 38 of 39 installed payloads are **byte-identical**; only `gate6.exe` differs, being a source rebuild of the same logic |
| E218 | a third party's "PUpatch" installed over build 171 and run -- what does it actually change | 18721 | `--` | **Two bytes, and they are the two layout constants.** The package is 24 KB, one file, `gate6.exe`, declared **installType 2 -- EInstPartialUpgrade** at version 1.0.8 over v5's 1.0.0, same UID and vendor. It installed over 171 and **all 16 entries of the game tree survived**, which is what that declaration is for. Against v5's executable it differs in exactly two ARM immediates: `0xae80` `moveq r1,#2 -> #0` (`c->mode`, MODE_FILL to MODE_ONE_TO_ONE) and `0xae90` `mov r1,#0x38 -> #0` (`c->topInset`, 56 to 0). Everything else, the C5 display fix included, is byte-identical. Measured here: **picture 176x208 at (32,56), no scaling**, against v5's 240x264 at (0,56). Native game resolution, correct 176:208 aspect instead of the fill's 7.5% horizontal stretch, 48% of the screen instead of 82%. The centring is a happy accident: 208 rows in 320 puts the top edge at y=56, which is exactly the status-pane height round 77 measured, so the picture still clears it with the inset removed |
| E219 | the zlib uncompress call sites hooked: both callers of 0xd4f88 wrapped so the true status, the buffers and the sizes are recorded before the game flattens every failure to -4 | 32593 | `--` | **The hook fires and every value is sane, which is what it was built to prove.** A `bl` in place of the `bl` at `0x33a74`, into a thunk that records the destination, the room the caller claims is in it, the source, its length and the stream's first word, then calls `0xd4f88` and records the true status and the length written back. Both call sites planted (`0x33a74` and `0x11562c`, the second one found by scanning the image for branches to `0xd4f88`). Twenty-four decompressions recorded: every one begins `0x78 0x9c` -- a zlib stream at default compression -- every status is 0, and `bytes written` equals `room in it` exactly, so the caller knows the uncompressed size in advance and a Z_BUF_ERROR would mean the stored size disagrees with the data. The instrument the third-party report asked for at `0x33a78` would have caught the status alone; hooking the call instead caught the arguments too. Not a repeat of anything -- no earlier row instruments zlib |
| E220 | the other three exits wrapped -- LeaveIfError (logged only when the argument is negative), LeaveNoMemory and Panic -- each with the heap beside it, plus a heap sample every 64 frames | 30109 | `--` | **Three more exits wrapped, and the import that was silently missing.** `LeaveIfError` is called on every result the game gets, so it is filtered in the handler -- a non-negative argument returns before it writes -- which costs a compare on the path it is always on. `LeaveNoMemory` and `Panic` go down whole. The find is procedural: `rheap_available` had been declared in `gate6.s` and used in the handler but never added to `build_gate6.py`'s import list, and the build **did not complain** -- 41 imports before and after. It would have jumped through a null IAT entry on the first failure, which is the exact path it was built for. The import count is the check: 41 -> 42 once it was listed |
| E221 | the heap note made four numbers -- cells and bytes outstanding from User::AllocSize beside the free list and its biggest cell -- because Available alone reads as 2 KB on a growable heap whatever is going on | 30370 | `--` | **`RHeap::Available` is the wrong question, and the right one says 56 MB.** Available reads 2,320 bytes and falls to 1,664 over the run, which on a heap that grows on demand means only that the free list is short. `User::AllocSize` (euser ordinal 664, sitting beside `User::Allocator` at 665, which this port already resolves correctly) answers what is actually held: **18,415 cells / 27.1 MB** through the menus, **27,833 cells / 56.5 MB** once the race starts, and then flat to the end of the run. `gate6.exe` declares `heapMax` 0x4000000 -- 64 MB -- so the bench never notices. A phone has perhaps twenty megabytes to give |
| E222 | the allocation census: every allocator call bucketed by size, the largest kept with its call site, and anything over 64 KB written down -- because 56 MB across 27,833 cells is an average, and an average hides a whale | 30787 | `--` | **772 allocations in the 32-to-64 KB band, and 46 above it.** The histogram over 25,684 allocator calls: 8,349 at 16-32 bytes, 5,631 at 32-64, 3,120 at 128-256, 1,647 at 1-2 KB, 965 at 8-16 KB, **772 at 32-64 KB**, 46 at 64 KB and up. At least 44.7 MB asked for in all against 56.5 MB held, which is the first number saying that essentially nothing is given back. The large sites named themselves: `0xed824` onward is a run of `mov r0, #147456; bl 0xd5a08` -- the game's own 144 KB, 108 KB and 160 KB buffers, hardcoded immediates -- and `0x65324` is inside the game's `operator new`. The band that holds the memory was below the 64 KB threshold and went unnamed, which is E223 |
| E223 | a per-site allocation table beside the histogram: every call site that asks for 8 KB or more, with its running count and total, so the 32-to-64 KB band has a name instead of a size | 33926 | `--` | **One site, 1,492 calls, 32.1 MB.** A per-site table of everything asking for 8 KB or more: `0x1166c0` accounts for 32.1 MB of the 43.9 MB the table sees, `0x65324` (the game's `operator new`) for 8.4 MB, and the game's own big buffers for 3.5 MB between them. `0x1166b0` disassembles to `push {r4,r5,lr}; mul r5,r2,r1; mov r0,r5; bl <alloc>; subs r4,r0,#0; movne r0,r4; movne r1,r5; blne <memset>` -- **`calloc`**, and at 1,492 calls averaging 22 KB it is zlib's `zcalloc`: a 32 KB window and a state block per `inflateInit`, once per decompression. Live cells (26,648) against total allocator calls (25,684) is the whole finding stated another way: **the process frees nothing** |
| E224 | the leak closed: the three deallocators give back anything 4 KB or larger at once and hold everything smaller in a 512-cell quarantine, in place of the no-op that has answered all three since build 49 | 33070 | `--` | **`LEAK_EVERYTHING = 1`, on since build 49, and it is the crashes.** Build 49 answered the three deallocation ordinals with a do-nothing function to test whether the wall it was stuck at was a use-after-free. It advanced, and the switch stayed on, and it has shipped in every build since -- including the one on the user's phone. Replaced with two rules: anything **4 KB or larger goes back at once**, everything smaller into a **512-cell quarantine** returned when that many further frees push it out, so a just-freed small object is still not handed to anyone else. Measured against E223 on the same tree: **26,648 cells / 51.1 MB -> 11,405 cells / 6.5 MB**, an 87% cut; the free list goes from 1,664 bytes to 149,536. The run goes **further**, 2,744 frames against ~2,400, with the audio sequence unchanged (`Open`, `SetAudioPropertiesL`, `MaxVolume`, `SetVolume`, `SetPriority`, `Stop`), the soundtrack opened, no leave, no panic and no access violation. On a phone the leaked 51 MB cannot be committed, and the first allocation that fails is whichever comes next -- in the recorded failure, zlib's own window inside `uncompress`, which returns Z_MEM_ERROR, which `0x33a7c` flattens to -4, which is `User::Leave(-4)` |
| E225 | build 172 -- the closed leak -- packaged, installed over an emptied tree and run from what the installer left | 31482 | `--` | **The package carries the fix and nothing else moved.** `Package Asphalt 2 registering with UID: 0xe0001006`, `EOpText` processed, `Installation done!`, and the directory holds `6rbc.bin` and no `6rbc.app`. Run from the installed state: 2,258 frames, both image opens on `6rbc.bin`, the soundtrack opened, `Open complete` from the emulator's own MDA patch, no access violation and no zlib failure. **6,847,988 bytes across 11,413 cells at the end** -- 6.5 MB, against 51.1 MB for the same package with the leak in (E223). This is E217 again with the deallocators doing their job |
| E226 | build 173: allocator results wrapped again so a null is recorded (WRAP_ALLOCATORS was off in 172), a heap high-water mark read every frame and reported on a new peak, and HAL EMemoryRAMFree beside every heap note -- the three things the reporter's log could not have shown | 32549 | `--` | **All three land, and the RAM number behaves exactly as predicted.** `EMemoryRAMFree` (HAL attribute 16) answers **134,217,728** here -- 128 MB, the RM-409's whole RAM -- because EKA2L1 sets `free_ram_in_bytes_ = system_ram_size` in `hal.cpp`. So the bench proves the call works and can never prove anything about its value; the phone is the only place it means something. The high-water mark produced **eleven** records for a whole run, tracking the climb 1.94 -> 2.42 -> 2.56 -> 3.27 -> 3.82 -> 3.92 -> 4.64 -> 5.57 -> 5.88 -> 6.61 -> **6.99 MB**, which is the resolution of a per-frame reading at the cost of a per-run one. `WRAP_ALLOCATORS` is on and answered zero failures. 2,265 frames, 6.62 MB held at the end, no zlib failure, no access violation. **The cross-check that matters: the bench peaks at 6.67 MB and the reporter's phone peaked at 6.80 MB** -- the same game holding the same memory on both machines |
| E227 | phase 0 run A: TELL_GAME_ITS_SIZE off (the game told the device's 240x320), frame 400 and 460 of its own buffer dumped raw | 28782 | `--` | **Run A of the phase 0 pair, and the sampling was wrong.** `TELL_GAME_ITS_SIZE` off, so the game is told the device's 240x320; frames 400 and 460 of its own buffer dumped raw. Rendered to PNG (`dumppng.py`, new) the frame is the title screen at 176x208, pixel-perfect RGB444, `(c)2005 gameloft` and all. Content ends at column 175 and row 207 with the rest of the 153,600-byte buffer untouched zeros, which reproduces the E133-E137 measurement exactly. What this run got wrong is the choice of frame and spacing -- see E229 |
| E228 | phase 0 run B: TELL_GAME_ITS_SIZE on (the game told 192x208 instead of the device's 240x320), same frames dumped -- does the reported size move its layout at all | 37124 | `--` | **Run B, and it appeared to overturn the settled answer.** `TELL_GAME_ITS_SIZE` on, so the game is told 192x208 instead. Against run A: **445 of 57,600 words differ**, all of them in rows 153-159, columns 54-153 -- one seven-pixel line of the game's bitmap font. Rendered, run B reads **`PRESS ANY KEY`** and run A is bare background. Frames 400 and 460 are identical *within* each run, which seemed to rule out timing and make it structural. It is not. Retracted by E229 |
| E229 | phase 0 run C: TELL_GAME_ITS_SIZE off again, four shots at 400/417/434/451 instead of two 60 apart -- is the missing PRESS ANY KEY a blink alias | 8637 | `--` | **It is a blink, and the period is sixty frames.** Same tree as E227 with the dump widened to four shots at 17-frame spacing instead of two 60 apart. Frames 400 and 417 have no text, 434 and 451 have `PRESS ANY KEY` -- and 400 == 417, 434 == 451. So the text blinks with a period of **60 frames**, and any two shots 60 apart catch the same phase for ever. E227 and E228 each sampled one phase and the two runs, which do not start in lockstep (28,782 records against 37,124), happened to catch opposite ones. **The instrument was the finding, again** -- the fourth time in this port, after the 40,000-disk-commit framedrops, the `rvt[k-2]` vtable read and `WRAP_ALLOCATORS` being off |
| E230 | phase 0 run D: TELL_GAME_ITS_SIZE on, same four shots -- compare against run C in matching blink phases | 8637 | `--` | **Settled: the game ignores the size it is told.** `TELL_GAME_ITS_SIZE` on, same four shots, compared against E229 by **clustering the shots by exact content** rather than pairing by index or thresholding on brightness (an earlier threshold called all four frames "text on", because the background alone is bright enough). Each run yields exactly two distinct pictures, and **both of E229's occur byte-for-byte in E230's**. The control holds: run D logged `srcPitch` **192**, so the flag genuinely took effect and the result is not vacuous. So `PORTING.md`'s settled section was right and the comment beside `TELL_GAME_ITS_SIZE` in `gate6.cpp` was wrong; the comment is corrected in place. **There is no cheap HUD win, and multi-resolution is purely scaler work** |
| E231 | phase 1: EKA2L1_SCREEN=320x240 -- the emulator posing as a landscape panel (E71 class), to see what the port's own screen query and layout make of it | 30797 | `--` | **The override works end to end, and the landscape case is as bad as predicted.** `EKA2L1_SCREEN=WxH` (39 lines in `window.cpp`, rewriting every mode's size after `wsini.ini` is parsed, no effect when unset) put the emulator on a 320x240 panel and the port ran the whole 90 seconds on it. What it then did: **picture 320x184 at (0,56), aspect 1.7391 against the source's 0.8462 -- a +105.5% error.** Fill mode on a landscape panel is unusable, which settles the default question the other way round from build 173. Three further findings in one run: the picture is **exactly at the 320-entry limit** of `mapX`/`mapY`; the 56-row inset measured on a portrait N95 eats **23%** of a 240-tall screen; and `srcOrigin`/`srcPitch` come through correctly at 16/176. New tool `screenfit.py` reads the layout block out of a log |
| E232 | phase 1: EKA2L1_SCREEN=352x416 -- exactly twice the source, the one panel where an integer scale fits | 0 | `--` | **AVKON panic 61 -- the ROM has no layout for this panel.** `records: 0, launches: 0`: the port never ran. `Thread gate6 panicked with category: AVKON and exit code: 61` during framework construction, before any of our code. Avkon picks its layout tables by screen size and the RM-409 ROM carries them only for the resolutions that device has. Overriding `SCR_WIDTH`/`SCR_HEIGHT` does not conjure layout data to match |
| E233 | phase 1: EKA2L1_SCREEN=360x640 -- the panel where the 320-entry map clamp should actually bite | 0 | `--` | **Same AVKON 61, and the limit is now clear.** Identical failure to E232, ending `Corrupted graphics command list! Emulation halt.` So the override reaches exactly as far as the ROM's own layout data: **240x320 and its 320x240 rotation work; anything else panics Avkon before the port starts.** That is not nothing -- 320x240 landscape is the single case hardware cannot cover, and E231 got it -- but it means the emulator is not the coverage mechanism for 352x416 and 360x640. **The host-side layout harness (phase 2) is, and is promoted from supplement to primary.** Worth noting the failure is honest: the override does not silently produce a wrong-sized framebuffer, it stops |
| E234 | phase 2: the scaler arithmetic lifted into screen_fit.h so the host harness runs the shipping sums -- native 240x320, must be unchanged | 25583 | `--` | **The refactor is behaviour-preserving, and the harness predicted the result exactly.** The scaler's arithmetic moved out of `gate6.cpp` into `screen_fit.h`, which `fittest.cpp` includes and runs on the host; `gate6.cpp` is now an adapter (-70 lines, +30). Native 240x320 after the move: **240x264 at (0,56), fill, +7.4% aspect, 82.5% of the screen** -- and the host harness's row for this panel reads `240x264 at (0,56) aspect +7.4% screen 82.5%`, the same numbers from code that never touched an emulator. That agreement is the point of the extraction: the test drives the shipping sums rather than a second copy of them, which is the failure this project has paid for four times. 25,583 records, no fault |
| E235 | phase 2: the same refactor at 320x240, which must reproduce E231's layout exactly | 27696 | `--` | **Landscape reproduces E231 to the pixel, so the move changed nothing.** 320x184 at (0,56), fill, +105.5% aspect, 76.7% of the screen, still flagged as sitting on the 320-entry clamp -- identical to E231 in every field. With E234's native run matching too, the extraction is confirmed behaviour-preserving on both panels the bench can reach. **What the harness then found without an emulator at all:** the 320-entry clamp breaks aspect mode on four panels (worst: a 320x320 square from a 176x208 source on a 5800), and 1:1 silently crops the source wherever the usable height is under 208 -- 24 rows lost on an E71, 44 on a 6110, **56 on a panel exactly the source's own size**. Neither can occur on the N95, C5-00 or N79. Widening the maps to 1024 clears every clamp failure, checked before the change is written |
| E236 | phase 3: the four modes captured from the composited framebuffer on a 240x320 panel, cycling every 150 frames | 2749 | `--` | **All four modes captured from the real framebuffer, on the panel the phones use.** New test-only pair: `MODE_CYCLE_FRAMES` advances the mode every 150 frames and `DUMP_SCREEN` writes the **composited** screen -- what the panel shows, after the blit -- once per mode, each preceded by a four-word descriptor (width, height, pitch, bits) so the renderer cannot be told the geometry wrongly the way phase 0's was. One run, four pictures: **fill 240x264, integer 176x208, 1:1 176x208, aspect 223x264**, every one matching the host harness's row for this panel. Integer and 1:1 are pixel-identical here, as the table says they must be -- 240x320 has no room for 2x. New tools `shotpng.py` and `sheet.py` |
| E237 | phase 3: the same four modes on a 320x240 landscape panel | 2749 | `--` | **Spoiled by my own harness, and the cause is worth recording.** 2,749 records and **six frames** against the thousands every other landscape run manages -- no panic, no fault, just a run a thousand times too slow that stops mid sound message. I started this while the E236 emulator was still alive: two instances contended for the bench, and then E236's `emurun.sh` reached its `pkill -x eka2l1_qt` and killed this one too. Nothing to do with the port. **`emurun.sh` must not be invoked while another run is in flight**, and an empty or absurdly short result is the shape that has misled this project before, so it is checked rather than read. Re-run as E238 |
| E238 | phase 3: the four modes on a 320x240 landscape panel, re-run on an idle bench after E237 was spoiled by two overlapping emulators | 36183 | `--` | **Landscape, all four modes, and the phase 3 fixes visible.** `fill 320x184` is the +105.5% stretch made plain -- the logo squashed flat. `integer` and `1:1` both give **176x208, the whole picture**, where before phase 3 an exact mode on this panel drew 176x**184** and lost 24 rows off the bottom. `aspect` gives 155x184. The finding worth acting on: **on a landscape panel 1:1 is the bigger picture** (47.7% of the screen against aspect's 37.1%), because an exact mode may sit above the status band to avoid cropping while a scaled one must stay below it. On 240x320 aspect is the larger; on 320x240 it is not, which is an argument for the default being chosen per panel rather than fixed |
| E239 | phase 3 shipping build: maps at 1024, integer mode added, exact modes no longer crop, scaled modes centred vertically -- native 240x320, test flags off | 27247 | `--` | **The shipping build, test flags off, and 240x320 is untouched.** 240x264 at (0,56), +7.4%, 82.5% -- identical to E234 and to build 173, so widening the maps, adding a fourth mode, stopping the exact modes cropping and centring the scaled ones changed nothing on the panel all three phones use. 2,072 frames, heap 6.59 MB, no fault. `runfit.sh` is clean at the shipped map size and still prints the pre-phase-3 size beside it, where the eight failures it fixed remain visible |
| E240 | phase 4: a full-screen mode, and the status-pane inset asked of Avkon (LayoutMetricsRect EMainPane, resolved at run time) instead of the N95's hardcoded 56 | 25947 | `--` | **Avkon answers, and it says 48 where the port assumed 56.** `AknLayoutUtils::LayoutMetricsRect(EMainPane)` resolved at run time through the avkon RLibrary (ordinal 417, to `0x8141a3ed` -- odd, so a Thumb entry, called correctly) and answered a main pane of **(0,48) to (240,293)** on the RM-409. So the status pane is 48 rows here and the softkey pane takes the bottom 27. Adopted: the layout becomes **240x272 at (0,48), 85.0% of the screen, +4.3% aspect error** against 240x264 at (0,56), 82.5% and +7.4%. Bigger *and* less distorted. **This is a deliberate change to the one layout hardware has confirmed**, so round 87 has to look at it: 56 was measured on an N95 (FP1) and 48 is what a 5320 (FP2) reports, and those can legitimately differ -- but if the N95's real band is 56 and Avkon there also says 48, the top eight rows of the picture will sit under the band. Wasting rows is safe; being covered is visible. Three guards keep the worst case at today's behaviour: a null lookup, an implausible rect, or a device that disagrees all fall back to 56, and what was resolved, what it answered and what was adopted are all in the log. Also fixed here: `if (!c->screenW)` guarded only the mode, so the inset was reset on **every** call to screen_info rather than the first -- invisible today, and it would have silently undone the picker's adjustment in phase 5. Full-screen mode added as a fifth option: 100% of every panel at (0,0), -11.4% on an N95 through +168.6% on an E90, and **exactly 0%** on 352x416 and 176x208, which are already 176:208. 2,218 frames, harness gate clean |
| E241 | phase 5: relaunch after the hold test wrote gate6.cfg with mode 4 -- is the choice read back and applied | 18061 | `--` | **The picker works end to end, gesture and persistence both.** Two parts, one experiment. First, a **real held key at the emulator**: `holdtest.sh` drives `xdotool` to hold host Backspace, which EKA2L1 binds to `std_key_backspace` -- the same 0x01 the port watches. A 2.2-second hold gave **exactly three changes**, aspect -> fill -> integer -> full, at gaps of **+0.50 s and +0.53 s**: one at a second, then one every half second, which is the cadence asked for. Three config writes, all `KErrNone`. The file on disk reads `magic 0x46433647 ('G6CF'), version 1, mode 4, inset 48`. Then this relaunch, with no key touched: **`saved choice found: mode 4, inset override 48`** and the layout comes up **240x320 at (0,0), 100% of the screen, -11.4% aspect** -- full-screen mode, restored. 18,061 records, no fault. The timing is driven from the frame loop rather than key auto-repeat, so it does not depend on whether a given phone's clear key repeats; the key handler notes only the down and the up, and a tap is passed through untouched because the key is swallowed only once a hold has actually changed something |
| E242 | phase 5 shipping build with no saved choice: the aspect default, the Avkon inset, hold-to-cycle armed and every test flag off | 21604 | `--` | **The shipping default, measured: 230x272 at (5,48), aspect error -0.1%, 81.5% of the screen.** No saved choice on disk, so the default applies -- mode 1, aspect -- and the inset comes from Avkon at 48. Against what build 173 ships (fill, 240x264, 82.5% and **+7.4%**) that is **the same coverage with seventy-five times less distortion**: one per cent of the screen given up for a picture that is the right shape. `gate6.cfg` correctly absent, hold-to-cycle armed, every test flag off, 21,604 records, no fault. Also decided here and written up: **the `#` inset key promised in phase 4 is dropped rather than half-delivered** -- `#` is a Chr/Fn symbol on a QWERTY S60v3, so it would work on the numeric phones and not the others, and the mode cycle already spans the cases. If Avkon under-reports the band, 1:1 and integer start at y=80 and are immune, full ignores it by design, and aspect and fill lose eight rows of 272 -- three per cent, on two modes of five |
| E243 | build 174 packaged, installed over an emptied tree and run from what the installer left -- the multi-resolution work end to end | 27338 | `--` | **The package carries all of it and nothing regressed.** `Package Asphalt 2 registering with UID: 0xe0001006`, `EOpText` processed, `Installation done!`, the directory holding `6rbc.bin` and no `6rbc.app`, and `\resource\apps\` holding `gate6.rsc`, `gate6.mbm` and `gate6.mif`. Run from the installed state: **aspect by default at 230x272 (5,48), -0.1% aspect error, 81.5% of the screen**, inset 48 from Avkon, 2,030 frames, heap 6.87 MB, the soundtrack opened and `Open complete` from the MDA patch, both image opens on `6rbc.bin`, no fault. Same 40 payloads as build 173. This is the build round 87 tests |
| E244 | round 87 follow-up: the status pane taken out of the way -- SetFullScreenApp plus CEikStatusPane::MakeVisible(EFalse), with IsVisible asked afterwards and the inset dropped to zero only on the pane's own say-so | 44 | `0x0` | **It kills the app: 44 records, zero frames, an access violation at address 0.** All three entry points resolved -- `CAknAppUiBase::SetFullScreenApp` at `0x8141338d`, `CAknAppUi::StatusPane` at `0x81413da7`, `CEikStatusPane::MakeVisible` at `0x81500251` -- and `StatusPane()` handed back a plausible object at `0x700e68`. The log then stops, with the `MakeVisible` address as its last record and no `NOTE_PANE_GONE`, so the fault is in the call itself rather than in the lookup. **The value of the bench here is exactly this: it cannot show the band, but it can show a call that kills the app, and it did so before a phone saw it.** Split into two flags and bisected in E245. Not a repeat of round 77, which tried the `ENoScreenFurniture` construction flag rather than touching the pane object |
| E245 | bisecting E244: SetFullScreenApp alone, MakeVisible off, with a flush either side of the call | 19070 | `--` | **`SetFullScreenApp` alone is safe; `MakeVisible` is the killer.** With `PANE_MAKE_INVISIBLE` off: the lookup resolves to `0x8141338d`, the call **returns** (`NOTE_PANE_STEP` 1), and the run goes 1,580 frames over 19,070 records with no fault. So E244's crash is entirely `CEikStatusPane::MakeVisible`, which is left in the source switched off with its result recorded beside it -- the lookups all resolve and `StatusPane()` hands back a live-looking `0x700e68`, so the next idea starts from a known position rather than from scratch. Most likely that pane belongs to an app UI Avkon set up more thoroughly than our synthetic one. **What the bench cannot say is whether the band actually goes**, because the emulator paints no status pane (E173); `EMainPane` still reports 48 afterwards, which is a layout-table lookup rather than a live measurement and proves nothing either way. The theory this supports: under DSA our writes reach the framebuffer, but the window server restores whatever lies outside the app's own region, and `SetFullScreenApp` is the call that makes that region the whole screen |
| E246 | build 176: post the whole screen -- CFbsScreenDevice::Update(void) (bitgdi 59) in place of Update(region), plus the posted region written out | 50249 | `--` | **The ordinal resolves and the run is clean: 3,336 frames, no fault.** `CFbsScreenDevice::Update(void)` came back non-null (`0x804848b9`) through the bitgdi RLibrary, and every frame now posts the whole device instead of the region the game hands over. Two independent checks say the ordinal is right on a real ROM as well: the shim already reaches bitgdi **58** for `Update(const TRegion &)` and **52** for `SetAutoUpdate(TInt)`, both of which work on the phone, and the source release numbers those two the same way it numbers 59. The region decode, though, is **wrong and was caught here**: `TRegion` is three words, not four, so the rectangles were read one word late and the first corner came out at x=30536. The header it did capture is real -- `iCount 1, iError 0, iAllocedRects 1` -- so the game posts **one rectangle** a frame, which is what the theory needs. Rewritten to send ten raw words down and decode on the host; re-run as E247. Not a repeat of E240/E243 (layout and packaging, which never touched the posting call) or E244/E245 (the status pane itself, which is the wrong object entirely) |
| E247 | build 176 again: the posted region written out as ten raw words, decoded on the host instead of on the phone | 47364 | `--` | **The picture still reaches the screen through the new call, and the region's shape is now known.** 3,249 frames, no fault, and a screenshot taken 75 seconds in shows the splash rendering normally at 30 FPS -- so `CFbsScreenDevice::Update(void)` is a working substitute for `Update(const TRegion &)` and the change costs nothing visible on the bench. The ten words are `1, 0, 1, 5, 0x008d7748, 0, 0, 0, 0, 40`, which reads exactly as the source says an `RRegion` is laid out: `TRegion` is three words (`iCount 1`, `iError 0`, `iAllocedRects 1`) and `RRegion` adds `iGranularity` -- 5, its documented default -- and `iRectangleList`, the heap pointer. **So the game posts one rectangle a frame and it is on the heap at word four**, not at word three where E246 looked for it. The rectangle itself still has to be followed; E248 does that. What the bench cannot show either way is the band, because a framebuffer it scans out directly has no posting step to leave a region out of -- the phone is the only instrument for that half |
| E248 | build 176, third cut: the posted rectangle followed through iRectangleList at word four | 37430 | `--` | **The rectangle reads, and on the bench it is the whole screen: `(0,0)` to `(240,320)`.** 2,699 frames, no fault, one rectangle a frame. That is the reading the theory predicts here and it is why the bench has never shown the band: with nothing above the game's window, the region the window server derives covers everything, so posting it or posting the device comes to the same picture. **The instrument is now good enough to settle the question on the phone in one round.** If an N95 log says `(0,58)` to `(240,320)` -- the main pane rect Avkon already reported in round 87 -- then the top 58 rows were written and never posted, which is the whole of the band, and `Update(void)` is the fix. If it says `(0,0)` the theory is wrong and the band is something else. Either way the log answers it without anyone reading a photograph. Not a repeat of E246 (wrong word offset) or E247 (header only, rectangle not followed) |
| E249 | build 177: the game's own Update(const TRegion &) again, handed a region of ours covering the whole screen instead of the one the window server derived | 29679 | `--` | **Clean, and the substitution takes.** 2,223 frames, no fault, and a screenshot 75 seconds in has the game **racing** -- road, bike, speedometer, HUD -- at 19 FPS. `NOTE_POST_FN` carries `0x00f00140`, so the region built is 240x320, and the game's own region is still read and logged first (`(0,0)`-`(240,320)` here) so the two can be compared on the phone. The bench cannot show the band either way, so what this run is for is the **negative**: the call the phone already tolerates is being made with different numbers, and nothing about the run changed. Not a repeat of E246-E248, which all used the no-argument overload round 88 showed is fatal on an N95 |
| E250 | build 178: one log file, replaced each launch, capped (64 KB for this run), and the ten old rotated names swept at startup | 8416 | `--` | **All three behave.** Ten decoy `g6box0..9.log` of 100 KB each were planted on C: and `KEEPOLD=1` stopped the harness clearing them, so the port had to do it: after the run the drive holds **`g6box.dat` and `g6box.log`, and nothing else**. The cap holds at **67,328 bytes** against a 65,536 ceiling -- the check is made before a block is written, so the block that crosses the line is written whole and the overshoot is one block, 1,792 bytes here, bounded by `LOG_BLOCK`. The game ran normally past the cut-off (the row counts 8,416 records because the harness measures the file, and the file stopped; the run went on). `KEEPOLD` added to `emurun.sh` for exactly this test. Cap restored to 1 MB and re-run as E251 |
| E251 | build 178 at its shipping cap of 1 MB: one log, replaced, and the run unchanged | 32929 | `--` | **Nothing regressed.** 2,219 frames, no fault, the full-screen region still substituted once (`NOTE_POST_FN`), and the drive holds exactly `g6box.log` (257 KB for a two-minute run) and `g6box.dat`. 257 KB for two minutes puts the 1 MB ceiling at roughly **eight minutes of play**, which is the length of the longest session anyone has sent (round 86) -- so the cap bites only on a session longer than any yet recorded, and the box still covers the end of one that does. Not a repeat of E250, which ran the same build at a 64 KB cap to make the ceiling reachable |
| E252 | phase 0: hooks keyed by (DLL, ordinal) and generated into gate_imports.h, plus a UID3 check that the header and the image are the same game | 21898 | `--` | **Nothing moved, which is the whole point.** 1,741 frames, no fault, the posted region substituted once, the band fix intact. The refactor was gated three ways before the run, and the first two are stronger than any run: all **40 generated hook indices equal the hardcoded ones exactly**, the regenerated `gate4_shim.cpp` is **byte-identical** to the checked-in one, and with only the re-keying applied the compiled `gate6_9000.bin` was **byte-identical** too -- a build that cannot behave differently. The 64 bytes of growth are the new UID3 guard, which is the only behaviour added: `gate_imports.h` and `gate4_shim.cpp` are generated as a pair from one game and mean nothing against another, so the loader now refuses an image whose UID3 is not the one they came from (`G6HDR 7`). A hook index from the wrong game is not an error -- it is a call to the wrong function |
| E253 | phase 1: Asphalt Urban GT (6r67) -- its own shim, import indices and game.h, first launch | 6 | `0x483C1FC` | **It loads, and then dies on one of Asphalt 2's addresses. The fault address names the cause outright.** Six records: the image landed at `0x04700000` and ends at `0x047a8a98`, so it is 690,328 bytes. The violation is at `0x0483C1FC`, and `0x04700000 + 0x0013C1FC` is exactly that -- `kImageWatch[0]`, one of three offsets `IMAGE_WATCH` re-reads every milestone to catch a vtable slot being poisoned. They were measured in **Asphalt 2's** image, which is 1.6 MB; in a 690 KB one they are past the end. A second Asphalt-2 address got in before that and did not fault: `NOTE_PLANT_OK 0x00033a74` is the zlib `uncompress` hook, **written into this game's code at Asphalt 2's offset**. It did not crash and that is worse than if it had. Both are per-game facts sitting in shared source, which is the same class phase 0 fixed for imports and did not reach: an address measured in one image means nothing in another. Moving them into `game.h`, off for this game, and bounds-checking the watch against the image size so a stale offset can never fault again |
| E254 | phase 1 again: the image watch and the zlib hook moved into game.h and switched off for this game, plus a bounds check on the watch | 10 | `--` | **The access violation is gone and the failure moves forward.** No fault at all now; instead `G6RET`, which is the marker after `RunApplication` returns -- the framework ran the application and left. Five records before it are `NOTE_SLOT` on the **application** object (slots 3, 14, 7, 5, 1) and nothing on the document or the app UI, so the framework built our `CApaApplication`, asked it a few questions and gave up. That is the exact failure the comment above `gate6_app_dll_uid` describes: the UID it answers is the framework's name for **us**, it was hardcoded to Asphalt 2's `0xE0001006`, and this build registers `0xE0001007`. The framework looked up a UID with no registration behind it and returned `KErrNotFound` before asking for a document. **A comment in the source predicted this failure and the constant under it was still wrong** |
| E255 | phase 1, third: AppDllUid answers GAME_APP_UID3 instead of Asphalt 2's hardcoded UID | 18 | `0x4` | **It gets all the way to the app UI.** The chain is now application (slots 3, 14, 7, 5, 5, 4, 17), then imports 299 and 296 -- **which E256 shows are `CleanupStack::PushL` and `CleanupStack::Pop`, not `RThread::Create` and `RSemaphore::CreateLocal`.** I read those two indices against **Asphalt 2's** import table, in the same breath as writing that import indices are per-game. Nothing has spawned a thread. -- then the document (slots 21, 19), then the app UI (slot 16), and a null dereference at `0x4` inside that. Three launches, three per-game constants found and moved: an image offset that faulted, an image offset that silently patched the wrong function, and a UID that made the framework decline to start. None of them was an import, which is what phase 0 covered; all three were **addresses and identifiers measured in one game and left in shared source**. The app UI is where Asphalt 2 needed its fabricated `CAknAppUi`, so the next thing to read is which slot 16 is and what it found null |
| E256 | diagnostic: every import traced to RDebug, to see what the game's ConstructL reaches before the cone fault | -- | `0x4` | **Retracted -- see below. What this run actually showed is that no *traced* import fires between app UI slot 16 and the fault, and the trace list is Asphalt 2's.** The whole trace is thirteen entries: seven application slots, `CleanupStack::PushL` and `Pop`, two document slots, one more application slot, and app UI slot 16 -- then the access violation, with **no import in between**. The faulting PC is `0x805731c0`, which is inside **cone.dll** at +0x31e8 (ROM code 0x8056ffd8), `this` is null in r0, and the link register is also inside cone. The nearest export below it is ordinal 468, `CCoeEnv::IsResourceAvailableL(TInt) const`, 0x39 bytes in. So `gate6_ui_construct` reaches `old_call(oldUi, OLD_UI_CONSTRUCT)` -- slot **13** of the game's old app UI -- and lands somewhere in cone that dereferences a null environment. For Asphalt 2 slot 13 is the game's own `ConstructL` override, deduced in that image by elimination. **This game is a different class with different overrides, so 13 names something else here** -- the same per-game trap as the imports and the image offsets, now in the old vtable. **The retraction:** `TRACE_MILESTONES = 1` means only the indices in `kMilestone` get a trace thunk, and `kMilestone` is a hardcoded list of **Asphalt 2** positions -- 100, 109, 110, 99, 325, 326, 283, 45, 46, 350 and the sound handshake. In this game those indices are other functions entirely, which is also why 299 and 296 fired at all. So silence in the trace says nothing about what the game called, and the claim that ConstructL never runs was not supported by the run. **Fourth instance of the same trap in one phase**, after the import hooks, the image offsets and the app UID: a list of positions measured in one game, used against another. E258 turns the milestone and hot-skip filters off so every import is traced |
| E257 | read out the old document and app UI vtables, so the game's own overrides can be told from inherited veneers | -- | `0x4` | **Every slot points inside the image, because an EKA1 image reaches its imported base-class methods through veneers of its own -- so "inside" does not separate them. The spacing does.** Two clusters: entries 16 bytes apart in runs (`+0x066a94, +0x066aa4, +0x066ab4 ...` and `+0x067304, +0x067314, +0x067324`) are a stub table, and irregular addresses are code the game wrote. On the app UI that makes the real overrides slots **0, 1, 7, 9, 13, 17 and 30** (`+0x001314, +0x001204, +0x0011a4, +0x001250, +0x00017c, +0x0012b8, +0x02b534`). **So slot 13 is overridden here too** -- it is not a veneer, and the elimination that found it in Asphalt 2 is not obviously wrong. That removes the leading explanation for E255 rather than confirming it, and the dump is worth keeping either way: it is the first measurement of a second game's class layout |
| E258 | every import traced, milestone and hot filters off, to get the real call trace into the game's ConstructL | -- | `0x4` | **The game's ConstructL runs, slot 13 is right, and the last three calls name the fault exactly.** With the filters off the trace ends: app UI slot **0x310**, then `TTrap::Trap`, then **`CCoeEnv::Static()`**, then **`CCoeEnv::AllocReadResourceAsDes16LC(TInt) const`** -- and the access violation. So the game does `CCoeEnv::Static()->AllocReadResourceAsDes16LC(id)` to read a string out of its resource file, and dies in cone. **This is not another stale constant; it is a design limit of the bridge.** `gate6_coeenv_static` hands the game `coeEnvView`, a *copy* of the real `CCoeEnv`'s words shifted by `COEENV_BIAS` so that an old-layout **field read** lands on the right 9.x field. Asphalt 2 only ever read fields through it. This game **calls a method on it**, and cone then runs with `this` pointing at our copy instead of at a real `CCoeEnv`. The port already has the pattern for this elsewhere -- the direct-screen-access wrapper swaps `self` from the shadow back to the real object before calling through. The environment needs the same: the game keeps the view, and any cone entry taking a `CCoeEnv *` gets the real one put back. That is the next piece of work, and it is shared machinery rather than a per-game number |
| E259 | the CCoeEnv swap: cone methods called on the view get the real environment put back in r0 | 89 | `--` | **The swap works. No access violation -- a named Symbian panic instead, which is the difference between a wrong pointer and a real answer: `CONE 14`, `ECoePanicNoResourceFileForId`.** `gen_shim` now names, per game, every import that is a method **on** `CCoeEnv`, and the loader wraps each with a five-instruction thunk that compares r0 against `coeEnvView` and swaps in the real environment when it matches. Asphalt Urban GT has exactly one, import 47, `AllocReadResourceAsDes16LC` -- the call that crashed. Imports the port already diverts are excluded from the list, because the swap would chain in front of that hook and change what it is handed; that is why Asphalt 2's `AddForegroundObserverL` (import 50) is not in its set and only `RemoveForegroundObserver` (75) is. Asphalt 2's shim regenerates byte-identical. **So the remaining problem is a real one, not a bridging artefact:** the game asks the environment for a resource id and no resource file registered with it covers that id. On an N-Gage the framework loads `6r67.rsc` automatically because the application *is* `6r67.app`; here the application is ours and the game's resource file was never added. Next: add it with `CCoeEnv::AddResourceFileL` -- and the offset it returns matters, because the game's ids were compiled against its own file being the app's |
| E260 | E260: register the game's own .rsc with the environment through CCoeEnv::AddResourceFileL, and log the offset it answers | 96 | `0x48` | **The resource file is registered, but this run could not say so.** Fault at `0x48`, `AddResourceFileL` resolved to `0x80573043` -- which is cone ordinal 148 at exactly the address the ROM's own export table gives, so the lookup is right -- and then nothing. The path came out truncated at `E:\system\ap` and no offset was logged. Both are the same artefact, not two findings: `log_event` buffers, and the buffer went down with the process |
| E261 | E261: log the environment pointer and its first words before AddResourceFileL is called on it | 96 | `0x48` | **The instrument could not report, for the second run running.** Added the environment pointer and its first four words to the probe; **none of it reached disk**, and the path was still cut at twelve characters. The cause is mine: E259's version of this function ended with `log_block`, I removed it because `log_block` has no forward declaration that early in the file, and a diagnostic that cannot flush before the thing it is diagnosing is no diagnostic at all. Forward-declared it and put the flush back |
| E262 | E262: the same probe, flushed to disk before the call, so its records survive the fault | 110 | `0x48` | **With the flush in place, everything lands -- and the resource work is done.** The path is exactly `E:\system\apps\6r67\6r67.rsc`; the environment is `0x7004d8` with a ROM vtable pointer at word 0, so it is a real `CCoeEnv`; and **`AddResourceFileL` returns offset `0x8f6f000`**. Then the striking part. E255's register dump caught the resource id the game was asking for: **`r1 = 0x8f6f005`**. The offset it gets here is `0x8f6f000`, so the game's hardcoded id is offset + 5 and **resolves without any biasing at all** -- Symbian derives the offset from the file rather than from load order, so the number this game was compiled against is the number it is handed. Nothing to correct. Also settled: the `.rsc` is byte-identical to Asphalt 2's, 88 bytes holding one string, `Invalid game card`. It is the N-Gage card-check resource, which is why Asphalt 2 never read it and this game does. The fault has moved on: **eikcore.dll +0x28e6, with the link register at `0x4700260` -- inside the game's own image.** So the game is now calling eikcore itself and getting a null back. A new problem, further in |

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
| 58 | Stand in front of `RFile::Read` and `RFile::Size` and log what they answer | 3 (one produced only 137 records) | `RFile::Size` = **125**, one read of a **125-byte** buffer, two of 16 bytes, all KErrNone; the 26 x 64 KiB burst **never happens** | **The gap is an open, not a read.** Every read the phone does make succeeds and fills its buffer exactly; the emulator's extra 26 reads are a separate, earlier file the phone never reads at all. Both machines agree on `cwp.dat` (125) and `nc.dat` (16) |
| 59 | Close the loader's own handle on `6rbc.app` | 3 (two produced only ~110 records) | **`6rbc.app` now opens: 0.** 29 reads, five opens, all KErrNone; **2086 and 2104 records**, up from 1571; **176 traced events**, up from 144 | **The gap is closed.** The phone and the emulator now agree on **178 of 180 core events**, and the only differences left are heap addresses inside two probes. The phone dies where the emulator calls `User::Leave` -- same place, same reason, one orderly and one not. The panic is still CONE 2 / KERN-EXEC 3 |
| 60 | **build 127** -- the whole port, after the game became playable in the emulator (worker threads, screen, input, protection passing for real, the log cut 21-fold) | 2 logged + several more | **1,276 records**, both logged runs byte-identical; `User::Leave(-40)` then `User::Exit`; **KERN-EXEC 0** under a different decimal thread name each run; the N-Gage splash on screen, duplicated and very small | **Two findings, both fixable, neither a mystery.** (1) The phone matches E142 for **1,188 records in a row** inside the first `RunL` and then `RThread::Create` refuses the game's **100,000-byte stack** with `KErrTooBig` at the SoundServer start site `0xb86ec` -- an EKA2 rule EKA1 never had, and one **EKA2L1 does not enforce**, which is why 142 emulator runs never saw it. (2) The splash is measurable: bands 88 pixels wide with seams at columns 16, 104 and 176 are exactly what writing 16-bit pixels on a 640-byte line into a buffer that is really **4 bytes a pixel on a 960-byte line** produces, so **HAL misreports both** on an N95 -- and 640 was never a multiple of 240 at any pixel size, which says so without a phone. See the round 60 section below |
| 61 | **build 130** -- clamp `RThread::Create`'s stack to 64 KB, reject a HAL pitch that is not a multiple of the width | 1 logged | **1,274 records**, no `User::Leave`, no `User::Exit`; frame 1 runs and returns; **KERN-EXEC 0** again, name `-266741334`; screen streaked | **The clamp works on hardware and the panic turns out to be ours.** `RThread::Create` answers **0** with a 0x10000 stack where 100,000 got `KErrTooBig`, and the phone matches the emulator's run of the same build for **1,193 records with no code out of place** -- the whole run to the end of frame 1. The log then stops dead where the emulator goes on to frame 2, which is exactly when the SoundServer thread starts. `box_flush` guards the null handle and `box_write` guards the wrong thread, but **`file_flush` between them is guarded by neither**: every sixteenth traced event, from whichever thread makes it, calls `RFile::Flush` on the main thread's handle. The box's last write is at **208 traced events, 16x13**, and the new thread's first imports land just after it. The same half-applied-fix shape the comment above `box_flush` already describes, one line further down again. Also: HAL's third answer, **`EDisplayMode` = 1, `EGray2`** on a 240x320 colour screen, so all three display attributes lie on an N95 and the rejection rule is what produced 32bpp/960 anyway |
| 62 | **build 131** -- guard `box_flush`'s `RFile::Flush` against the wrong thread | 2 runs, 4 launches logged | **1,274 records every time**, all four launches the same shape; still no leave, no exit; frame 1 runs and returns; **KERN-EXEC 0 -- but now named `gate6`** | **The guard worked and uncovered the next fault.** The panic's name has changed from a different garbage number every run to **`gate6`**, so the thread that dies is no longer the garbage-named worker: it is the main thread. That is the evidence the wrong-thread flush was real and is fixed -- the worker now survives -- and it says the main thread has a bad handle of its own, immediately after frame 1's `RunL` returns. Where exactly is **not** in this round: the log buffers eight records before writing, so its end is +/-7 events, and the box flushes every sixteenth traced import and says only "208 traced events at the last write, so 208..239 in all". Four launches agreeing to the record makes it deterministic. The region the fault is in -- `RThread::SetPriority`, `Resume`, `RSemaphore::Wait`, then two `RHandleBase::Close` at `0xb87b0` and `0xb87b8` -- is traced by nothing, which is why build 132 exists |
| 63 | **build 132** -- the box on every traced import, the SoundServer handshake traced, `RSemaphore::CreateLocal`'s result | several launches | **1,285 records**; **no frame end**; two panics seen -- `886699653 KERN-EXEC 0` and, on one launch, **`gate6 ViewSrv 11`** | **The instrument paid for itself: the window is now named, and the failure turns out to be a hang.** The box records, exactly: `RSemaphore::CreateLocal` -> **0** (so the semaphore is real on hardware, a question the game throws away), `RThread::Create` -> 0 with the clamped stack, `SetPriority`, `Resume`, then **`CTrapCleanup::New` from `0xb866c` -- the SoundServer thread running its own entry function** -- and finally `RSemaphore::Wait` from `0xb8798`. There is **no `NOTE_FRAME_END`**: the main thread went into that `Wait` and never came out, which is what `ViewSrv 11` is -- the view server timing out on an application that is not responding, a hang and not a crash. So the SoundServer thread does not reach its `RSemaphore::Signal` at `0xb86ac`. It also shows `on_main_thread` working correctly on hardware: the worker's `CTrapCleanup::New` is in the box, which any thread writes, and **not** in the log, which only the main thread writes. That is also why the round ends there -- once the main thread blocks, nothing flushes the box again and the worker's own story is stranded in memory. Build 133 gives the worker a file of its own |
| 64 | **build 133** -- the worker gets its own log file, connected on its own thread | several launches | **1,285 records**, box identical to round 63 to the entry; **no `g6wrk.log` at all**; panics `-1879111643 KERN-EXEC 0`, **`SoundServer KERN-EXEC 0`** (twice) and `gate6 ViewSrv 11` | **The thread is named at last -- and the instrument is what names it.** `SoundServer KERN-EXEC 0` has never appeared before; the only change in this build is `worker_log`, and it runs on that thread. It never produced a file, so it died at or before its first file call, which is `RFs::Connect` -- and that call happens **before** `CTrapCleanup::New` itself, because a trace thunk records on the way in. Two faults in one: the design shares one `RFs` and one `RFile` between *both* workers, which is the same cross-thread handle that rounds 60-62 were about, and something in that first call is fatal on that thread regardless. It also confirms the round 63 reading from the other side: `SoundServer` is a real name, so the decimal-numbered panic is a different thread again. Nothing else moved -- box last import `RSemaphore::Wait`, `CTrapCleanup::New from b866c` at 224, no frame end |
| 65 | **build 134** -- `worker_log` off; the main thread's `RSemaphore::Wait` in 100 ms slices that flush the box, giving up after two seconds | 1 | **1,295 records** and **the frame ends**; `NOTE_SEM_WAIT` says `0` then `ffffffff`; panic `SoundServer KERN-EXEC 0` | **The hang is gone and the fault is cornered to two calls.** The wait ran its full twenty slices and was **never signalled**, so the main thread gave up, closed both handles, ran `RSessionBase::CreateSession` and **finished frame 1** -- the first time the game has got past the handshake on hardware. And because the box was flushed every 100 ms for the whole two seconds, the silence from the other thread is now evidence rather than a gap: the SoundServer thread makes **no traced import at all** after `CTrapCleanup::New`. A trace thunk records on the way *in*, so it died between that record and its next one, `CActiveScheduler::CActiveScheduler()`. Two calls sit in that gap -- `CTrapCleanup::New()` itself and the game's `operator new(20)` at `0x652f8` -- and **both allocate**. The game passes `aHeap = NULL` to `RThread::Create`, which means *share the creating thread's heap*; if euser leaves the create info's allocator null and its heap size zero, `UserHeap::SetupThreadHeap` sets nothing up and the thread's first allocation reaches for a heap that is not there -- and KERN-EXEC 0 is a bad **handle**, which is what an `RHeap`'s chunk handle would be. Build 135 substitutes the creating thread's allocator for the null one |
| 66 | **build 135** -- lend a new thread the creating thread's heap when the game passes `aHeap = NULL` | 1 | **1,294 records, identical to round 65 record for record**; wait still times out (`ffffffff`); frame 1 still ends; `SoundServer KERN-EXEC 0` again | **The heap hypothesis is wrong.** The substitution went in -- the phone's `User::Allocator()` is **`0x600000`** and that is what the thunk lent -- and it changed **nothing**. So the SoundServer thread is not dying for want of an allocator, and round 65's leading explanation is retired. What the round does buy is a much tighter reading of the gap, because `operator new` at `0x652f8` has now been read out and it is not one call but four: `TTrap::Trap` (our own stand-in, which writes a zero and returns zero), **`User::AllocL`** (import 269), `TTrap::UnTrap` (a no-op), and `User::LeaveNoMemory` on the error path. None of those four is traced, nor is `CTrapCleanup::New` on the way out, so the gap the thread dies in is five calls wide and not two. Build 136 traces them -- but only on a worker, because on the main thread they are thousands of calls a run |
| 67 | **build 136** -- trace the game's allocator, recorded only when a worker makes the call | 1 | **box identical to round 66 to the entry**; last worker event still `CTrapCleanup::New from b866c`; `SoundServer KERN-EXEC 0` | **It dies in the first euser call it ever makes.** `TTrap::Trap`, `User::AllocL`, `TTrap::UnTrap`, `User::LeaveNoMemory` and `User::Free` all carry trace thunks now and are recorded whenever a worker calls them -- and **not one of them appears**. So the SoundServer thread never reaches the game's `operator new` at all: it dies inside `CTrapCleanup::New()` itself, between the trace record made on the way in and any return. That is the *first* call that thread makes, which reads less like a broken euser export and more like a thread that is not fit to make a call yet -- and `CTrapCleanup::New` allocates, which is the first thing a half-built thread would fail at. Round 66 said lending it a heap changes nothing, but it could not say whether the lend reached the thread. Build 137 asks the thread itself, from inside the trace thunk, one instruction before the call |
| 68 | **build 137** -- a worker probe, one slot per worker, asking each thread about its own allocator | 1 | **`WORKER PROBE never ran`** -- both slots empty -- and **`import 330` is in the MAIN log** | **The probe answered by not running, and it is the answer.** The box still has `CTrapCleanup::New from b866c` at entry 224, so the SoundServer thread did reach `gate6_trace`; the probe is called there whenever `!on_main_thread(c)`, and it did not run. Then the log settles it from the other side: the main log, which **only the main thread writes**, contains that thread's `CTrapCleanup::New` record. So `on_main_thread` calls the SoundServer thread the main thread on this phone, and **every guard built on it is a no-op for that thread** -- `log_block`, `box_write`, `box_flush`, the worker-log gate, the probe. The thread writes into the shared log buffer and, on the eighth record, `log_block` writes **the main thread's `RFile`**: KERN-EXEC 0, named `SoundServer`. Stacks on EKA2 are packed close together within a process; `THREAD_SPAN` is a megabyte; EKA2L1 gives every thread its own chunk megabytes away, which is why the test has always passed there. **This retracts round 63's "`on_main_thread` works on hardware"** -- it was read from the absence of a record in rounds 63 and 64, and that absence was the eight-record log buffer being lost when the process died, not a guard working |
| 69 | **build 138** -- `on_main_thread` asks the kernel for the thread id instead of measuring a stack | 1 | **5,227 records, 836 frames, 968 framework calls, 157 key events, no panic** -- and the game **boots to its main menu on the phone and takes input** | **The port runs on hardware.** The semaphore is signalled on the *first* 100 ms slice, so the SoundServer thread lives, does its whole startup and signals: `NOTE_SEM_WAIT` reads `0` then `1` where every round since 63 read `ffffffff`. Both worker probes fill, and they measure the thing that caused nine rounds of trouble: the two workers' stacks are at `0x00415e7c` and `0x00427f90` -- **72 KB apart**, against a test that allowed a megabyte. Asking euser for the thread id fixed it. The remaining fault is entirely cosmetic and entirely ours: the picture is drawn with the wrong framebuffer geometry, so the game's image appears three times across the screen with alternate lines showing the phone's menu through it |
| 70 | **build 139** -- eight candidate framebuffer formats, selected live from the keypad | 1, eight photographs | **The picker works; none of the eight is right** | **Input reaches the picker and every format is visibly different, so the instrument is sound and the answer is simply not in the table.** Two checks passed on the way: index 4 (32bpp, 960) is indistinguishable from index 0, which is the derived format, so the table and the derivation agree; and the digit keys reach `gate6_control_offerkey` and are swallowed, so the game never sees them. What the eight say: **every one of them stripes**, including 16bpp on a 480-byte line, which is the tightest pitch offered -- if the real line were 480 bytes or more at two bytes a pixel, that one would have laid its rows down contiguously. Index 1, which is exactly what HAL claims (16 bits, 640), gives much the most coherent picture: the word `SELECT` is legible in it. Measuring the rest off photographs is what produced round 60's wrong answer, so build 140 stops measuring pictures of a car and paints a ruler instead |
| 71 | **build 140** -- a ruler painted over the top of the frame, photographed in all eight formats | 1, eight photographs | **The ruler is legible and gives a number: the line is about 576 bytes** | **The ruler behaves exactly as the model says it should, which is the first time the model has been testable.** Its on-screen height grows with the pitch we write: squeezed into a couple of rows at 640, a little taller at 960, and spread into clearly separated red / green / blue / white bands at 1440. Thirty-two source rows occupying *H* display rows means `H = 32 * p_used / P_real`, and the 1440 photograph puts *H* near 80, which gives **P_real ~= 576**. That agrees with a second, independent measurement made a round earlier: round 69's picture repeated every **three** display rows, and three is the period of `960 mod P` for P = 576 and for almost nothing else nearby. And 576 is what a 240-pixel 16-bit line looks like when the hardware pads it to a 64-byte boundary: 480 rounded up. **So HAL was right about the depth and wrong about the padding**, and no candidate in the table was within 64 bytes of it |
| 72 | **build 141** -- the line steppable by hand from the keypad | 1, four logs and a photograph | **The menu is legible and upright: `ARCADE`, the carousel, `SELECT`, the car.** The format is **32 bits a pixel on a 1280-byte line** | **The phone answered, and the answer was in the log rather than in my reading of a picture.** The user swept the line by hand and the log records every step, so the format they settled on is simply the one they stopped on longest: in `g6box2-8.log` the four longest dwells that are not the startup default are **all at pitch 1280**, and the longest of the whole session -- 1,493 records, nearly three times anything else -- is **32bpp / 1280**. They swept up to 1280 and back down to it four separate times. 1280 bytes is 320 pixels at four bytes, and the N95's panel is natively **320x240 landscape**; 240x320 is the rotated logical size `UserSvr::ScreenInfo` reports. **This retracts round 71's 576** -- see below. Two of the four logs turn out to be from build 139, still on the phone from the round before: the launch-numbered log files are not cleared between installs |
| 73 | **build 141 again** -- the same picker, driven deliberately: preset `7`, then `*` eight times | 1, the count itself | **Eight clicks from preset 7 is the picture** | **Independent confirmation of round 72, and it is arithmetic rather than judgement.** Build 141's preset 7 is four bytes on a **1152**-byte line and `*` steps the line by sixteen, so eight clicks land on **1152 + 128 = 1280**, at four bytes a pixel. Round 72 reached 32bpp/1280 by measuring how long the user's hand sweep dwelt on each format; this reached it by counting the keys they pressed. Two different readings of two different runs, same number. The format question is closed |
| 74 | **build 142** -- the picture scaled to fill, and the buffer blanked | 1, a photograph and five logs | **Wrong aspect, and the right-hand side still comes back through the left.** Colours right, composition almost right | **The scaling arithmetic was sound and the shape it was given was not.** Round 72 read the 1280-byte line as a 320-pixel buffer whose rotation is the reported 240x320; build 142 drew 203x240 at column 58 on that basis, and the photograph shows the picture running off the *right-hand edge of the screen* -- which can only happen if the visible width is the 240 `ScreenInfo` reports. So 1280 is a **padded stride** (240 pixels at four bytes is 960) and the reported size is the screen. The five logs also settle that the format itself is right: they sweep it again and `32bpp/1280` holds the longest dwell in every one of them -- 2,606 of 2,893 records in `g6box1-4`, 1,891 of 1,933 in `g6box6-8` -- so the sweeps were the user trying to fix the *shape* with the *format* knob, which was the only knob there was |
| 75 | **build 143** -- the aspect fixed, and the keypad on the game's row length | 1, a log and a video | **The aspect is right.** The whole 160-256 range swept, 97 values, and the wrap survives every one of them | **Both halves answered.** The fit records read buffer **240x320**, picture **240x283** at y=18 -- round 74's correction holds on the phone, and the video shows the picture filling the screen with the shape kept. And the sweep is a clean negative: **176 holds 7,489 records of dwell, the next value 1,589**, and the video shows every other value shearing the picture diagonally. So 176 *is* the stride the game writes with, confirmed on hardware for the first time, and the wrap is not in how we read the buffer. That matches what E137 concluded offline from a raw frame: the game writes 176x208 and nothing beyond, but *draws* up to 192 pixels wide, so the overflow lands on the next row. 4,493 frames, no panic |
| 76 | **build 144** -- the game's buffer read from pixel sixteen | 1, a log and a video | **The wrap is gone on the phone.** The game plays: vehicle select, a tunnel race, the Golden Gate track, nitro. What is left is a band across the top | **The offset was right, and it was right for the phone without any device-specific number.** 16,912 records, `NOTE_SCREEN_FIT` reading 240x320, 240x283 at y=18, `NOTE_SCREEN_SRC` reading origin 16 on a 176 pitch, and no panic. The band across the top is the phone's **status pane**: the port writes the framebuffer directly, so anything the window server paints lands on top of the picture, and the game asks avkon for a standard application with all its furniture. It is visible as itself in the round-74 photograph -- close icon left, battery right -- and as a hazy band over the game once the two repaint in turn. Also settled by this log: the phone's HAL answers **0** for `EDisplayOffsetToFirstPixel` and 0 is right there, while the emulator answers 32 and 32 is right here |
| 77 | **build 147** -- no screen furniture, and an inset on the keypad | 1, two logs and a video | **The band survives `ENoScreenFurniture`.** The user swept the inset and settled on **56**, which clears it but costs a black bar down each side | **The flag reached avkon and did not remove the band.** Record 16 of both logs is the flags our wrapper passed: **4**, `ENoScreenFurniture`. The band is still there, so it is not the app's own status pane -- or not only that. What the sweep does give is its height: 56 held **7,624 records** of dwell against 2,458 for 64, 2,590 for no inset at all and 1,164 for 48, so the band is **more than 48 rows and no more than 56**. At 56 the shape-preserving fit is 223x264 and leaves 8 pixels of black down each side, which is what the user does not want |
| 78 | **build 148** -- fill, bottom-anchored, inset 56 | 1, two logs and a video | **Right first time.** The mode it starts in is the one the user kept; the keys, though, are the game's | **The dwell agrees with them: inset 56 filled held 6,206 records against 2,600 for inset 32 filled and 842 for inset 64.** So the defaults are confirmed rather than assumed, and build 149 turns the picker off -- it was eating keys the game wants, `2` being nitro as well as a mode change. Every question the picker was built to ask has now been answered by the phone: format, stride, source origin, first pixel, inset, fitting |
| 79 | **build 165** -- the first build with sound | 1, a log, a box and a video | **Sound works on the phone.** Music and effects both, confirmed in the video. What came with it is **framedrops during the race, worst on nitro** | **The drops are the instrument, not the sound.** 3,474 frames, a clean `User::Exit`, 27,378 traced events against a few thousand in a round-78 race -- because the bridge traces `SendReceive` and `RMessage::Complete` and those two alone are 24,404 of them. `BOX_EVERY_TRACED` has been **1** since round 62, so every one of those 27,378 events is an `RFile::Write` of 1,264 bytes **and an `RFile::Flush`**: 34.6 MB rewritten over the same offset and 27,378 commits to flash. The log's own blocks of eight add 5,497 more. That is **9.5 disk commits in every frame**, where round 78 had a fraction of one. The reasoning that set it to 1 is in the source and says so outright -- "two hundred and forty write-and-flush pairs on a run of the length the phone reaches" -- and a race is not that run. What the game itself does is modest by comparison: 2,523 `RFile::Read`s from one open file (21 opens in the whole race, so it is streaming, not loading), 1,791 of them inside a frame, in bursts of up to 63. Whether any of that still shows once the instrument is quiet is what build 166 measures, with a tick either side of every frame |
| 80 | **build 167** -- the standalone installer | 1, a video | **It does not install.** The progress bar reaches about nine tenths, pauses, and the phone says "Unable to install". The same on phone memory and on the card | **What the video rules out is most of it.** The installer read the package fine -- "Install 22008 kB to:" with the right size, phone memory 55,628 kB free and the card 13,885,568 kB -- so **it is not space**, and it failed on the card too. It got through the capability prompt and copied files for a minute, and SWI checks a package's whole file list before it copies anything, so **the paths and the executables in it were accepted**. The failure is at the end of the copy, and `sischeck.py` (new, and the thing CLAUDE.md says the emulator lacks) says the package itself is sound: every SHA-1, every compressed and uncompressed length, and the descriptor layout all check out, against a real signed Gameloft S60v3 package parsed the same way. What is new since build 166, which installs: writing to `\system\apps\6rbc\`, forty files instead of three, 20.6 MB instead of 24 KB, an E32 image (`6rbc.app`) outside `\sys\bin`, a changed vendor and name on an already-installed UID, and **a display-text entry that is the last thing in the package** -- the only one of those that acts at the moment the failure happens. Four one-kilobyte probes take one hypothesis each |
| 81 | **four probes**, one hypothesis each | 4 installs | **Probes 1, 2 and 3 install. Probe 4 does not.** So it is not the `\system\apps\6rbc\` directory, not the display-text entry, not the file count -- it is **`6rbc.app`** | **Symbian will not install an E32 executable image anywhere but `\sys\bin`.** Probe 4 carries that one file and nothing else, and the phone refuses it. Probe 3's forty files and probe 2's text entry both went in, which retires the two hypotheses I ranked highest -- the text entry was the leading suspect purely because it acts at the end of an install, and that reasoning was worth nothing against a probe. The user also confirms build 167 was tried with nothing of ours installed and the game files moved aside, so the vendor-mismatch-on-an-existing-UID theory is dead too. `6rbc.app` is the only file in the tree that is an E32 image (uid1 0x10000079, 'EPOC' at 0x10); the `.aif` is a bitmap store and everything else is data. And the game opens it **itself** -- twice in the emulator's file log, mode 1 from our loader and mode 2 from the game's own reader at 0x34a0c -- so any change to the file has to be invisible to the game, which is what probes 5 and 6 are for: the same bytes renamed, against the same bytes with the header scrambled |
| 82 | **probes 5 and 6** -- the same file renamed, against the same file with its header scrambled | 2 installs | **Probe 5 fails, probe 6 installs.** So the installer reads the file, not its name | **The check is on the content.** Renaming `6rbc.app` to `6rbc.bin` changes nothing; XORing the first 32 bytes -- the UID triple, the checksum and the `'EPOC'` signature -- is what lets it travel. Which is the sensible design on Symbian's part: a rename would have made the whole "executables live in `\sys\bin`" rule a formality. The loader XORs those bytes back after reading its own copy, so nothing about the image the game runs changes. What is not yet settled is the game's **own** open of that file, which now reads 32 scrambled bytes at the front -- E214 asks whether it cares |
| 83 | **build 170**, and a second opinion | install on C: and E:, plus C5 and N79 logs and a patched build from another model | **It installs on both drives and boots.** Two things wrong: the icon is an empty box, and the display is broken on the user's other phones | **Both found and fixed while I was out of context, by another model working from the binary, and both readings hold up against the source.** *The icon*: S60v3 draws a **MIF**, not an MBM. Two bytes in the caption resource and a converted icon file; the emulator rendered my MBM happily, which is the third time this project has been caught by the emulator being more permissive than the device. *The display*: `HAL::Get` takes the mode **in** the same integer it answers in, and every build to 170 left it at zero -- so all three display attributes described **mode 0** while `EDisplayMode` said mode 1 was live, logged and ignored. Confirmed against my own source: `int bpp = 0, pitch = 0, first = 0` is the bug, in one line. And the selection rule `pitch == w*2 || pitch == w*4` asks whether the framebuffer line is the *logical screen* wide. On the N95 it is not, so the rule never once matched there and the 1280 fallback carried the display the whole time -- it looked like it worked. On a phone with a wider line it fails outright |
| 84 | **v5 on the N95** | 1 | **It works.** The display fix holds on the phone it was not derived from | **The last doubt about the new display rule, closed.** The N95's live mode had never been measured -- every log queried mode 0 -- and the risk was specific: mode 0 there reports 16 bits on a 640-byte line, the new rule would accept that as sane, and round 60 proved it produces 88-pixel banding. It does not happen. The pixels-per-line rule held: the N95's line is 320 pixels whatever the mode, so the live mode selects the same 32 bits on a 1280-byte line that the fallback was already choosing, and nothing changes there. **The rule is now confirmed on two phones that need different answers from it** -- 320 pixels a line on the N95, 2048 on the C5-00 -- which is worth more than either result alone: a rule that only ever produced one answer was what the old one did. The N79 is untested and predicted to be the C5's case |
| 85 | **build 172** -- the deallocators doing their job again | several races on the N95 | **No crash, not once.** The user played several races end to end | **The crashes were `LEAK_EVERYTHING`, and they are gone.** Nothing else in build 172 differs from 171, so this is a clean single-variable round: the three deallocation ordinals stopped being answered by a do-nothing function, the heap fell from 51.1 MB to 6.5 MB (E223 against E224), and the failure mode disappeared. It confirms the whole chain read out of the bench -- 26,648 cells never freed, zlib's own 32 KB window per `uncompress` the largest single consumer at 32.1 MB, `Z_MEM_ERROR` flattened to -4 by `0x33a7c`, `User::Leave(-4)` -- without a single log having to come back off the phone. Several races is also the test the bench could not do: it plays one, and the leak was cumulative, so this is the case that would have failed worst |
| 86 | **a third party's build-172 log**, 8 minutes of play on their phone, sent with a claim that the game "can still crash from out of memory" | 1 log of 171,261 records (two others sent with it are pre-172 and carry no heap data at all) | **No memory problem of any kind, and the run ends somewhere much more specific.** Heap peak **6.80 MB** across 227 samples, oscillating and falling between races, never a trend | **The claim is not supported, and the log says something better.** 8 min 06 s, 14,547 frames at ~30 fps, 7,361 key events, three race cycles. **97.8 MB asked for cumulatively** -- 68.2 MB of it zlib's `calloc` at `+0x1166c0` -- against a heap that never exceeds 6.80 MB: about 91 MB allocated and given back, which is the quarantine working continuously rather than once. `CMdaAudioOutputStream::Open` called **once** in eight minutes, so no stream leak either. No zlib failure, no `Leave`, no `LeaveNoMemory`, no `Panic`. The run ends **27 records into a music track change byte-identical to the five before it**, immediately after `Stop` returned and before the `RMessage::Complete` that follows it every other time -- and because the MDA proxy calls `log_block` on every return, that tail is a real flush, not a truncation. The death is inside the 255 records after it, which is the second or two between a race ending and the results screen that writes `user.dat`. **Three things build 172 could not have seen, now fixed in 173 (E226):** a null from an allocator (`WRAP_ALLOCATORS` was off), a heap spike between one-second samples, and free *system* RAM, which is what an allocation actually fails on |
| 87 | **build 174** -- the multi-resolution work: five modes on hold-C, aspect by default, the inset from Avkon | 1, plus a video | **The picker works: cycling, and the choice persists across launches.** One thing wrong -- **full-screen mode still leaves a band across the top**, so it is not full screen | **The gesture and the persistence are confirmed on hardware, and the band is identified from the video.** A frame lifted out of it shows a **smooth gradient with none of the dither the game's own picture has**, and a hard edge where the picture starts: that is an Avkon skin background, so the band is the **status pane's own window** painting over ours -- the mechanism round 76 established and round 77 showed `ENoScreenFurniture` does not stop. The layout is innocent: `screen_fit` puts full screen at 240x320 from (0,0) and the harness agrees, so the pixels are written and then covered. **The inset is not answered by this round** -- and an earlier version of this row said it was. Nothing was reported about it because nothing was asked about it: the round's report was about the band, not a checklist being worked through, so silence there is silence, not a pass. What Avkon answers on an N95 is recorded in the log (`NOTE_INSET_RECT` 821, `NOTE_INSET_TAKEN` 822) and settles in one line as soon as a log comes back; until then 48-vs-56 on that phone is unmeasured. Fixed for build 175 with `CAknAppUiBase::SetFullScreenApp`, after the bench caught the obvious-looking companion call killing the app outright (E244/E245) |
| 88 | **build 176** -- the whole screen posted (`CFbsScreenDevice::Update(void)`, bitgdi 59) instead of the region the game hands over, and that region written into the log | 1 log, 1 box, 1 photograph | **The theory is right and the fix is fatal.** The N95 posts **one rectangle a frame, `(0,58)` to `(240,320)`** -- against `(0,0)` to `(240,320)` on the bench (E248), and 58 is exactly what Avkon calls the main pane. And `Update(void)` **kills the game**: six frames, a black screen, a log that stops nine seconds in, the soundtrack thread playing on | **The band is a posting boundary, proved, and it took one round to prove it.** The game turns auto-update off once and then posts by region every frame; the region is its DSA drawing region, which is its window minus the status pane, so the top 58 rows are written every frame and sent never. That is the whole of the band, and it is why no emulator could show it. The no-argument overload resolves on the phone (`0x8064a345`) and is not a safe substitute there, whatever the bench says -- 2,699 clean frames on one machine and six on the other. Build 177 keeps the call the phone is known to be happy with, `Update(const TRegion &)`, and hands it a **region of our own** covering the whole screen: same ordinal, same driver path, same frequency, only the numbers change. `TRegion::RectangleListW` takes the list pointer from word four when `iAllocedRects >= 0`, which is how the game's own region reads, so ours is built to read the same way |
| 89 | **build 177** -- the same `Update(const TRegion &)` the phone already accepts, handed a 240x320 region of ours in place of the window server's | 1 log, 1 box, 1 photograph | **The band is gone. Full-screen mode covers the whole panel.** The photograph has the game edge to edge -- WANTED and the money counter on the top row, the speedometer on the bottom -- with no grey strip and no theme showing through | **The display work is finished.** The log has the game still asking for `(0,58)` to `(240,320)` and our `240x320` going to the driver in its place (`NOTE_POST_FN 0x00f00140`), mode 4 restored from `gate6.cfg`, the layout 240x320 at `(0,0)`, all five modes cycled and saved (`NOTE_MODE_NOW` 0 through 4, each followed by a `NOTE_CFG_WROTE` of 0), 428 frames and a clean `User::Exit`. One call, one substituted rectangle, and four rounds of theories about windows and status panes were all beside the point |

## Round 74 -- the pitch is padding, and the screen is what it says

Build 142 scaled the picture to 203x240 and centred it at column 58, on
round 72's reading that the buffer is 320 pixels wide. The photograph says
that reading was wrong.

**The picture runs off the right-hand edge of the screen.** 58 + 203 = 261,
and if the screen really were 320 wide there would be 59 pixels of black to
the right of it. There is none: it reaches the edge, and the overflow appears
back at the left. The visible width is therefore under 261, and the only
candidate is the **240** that `UserSvr::ScreenInfo` has been reporting all
along.

So the two numbers are not in conflict and never were:

* **240 x 320 is the screen.** What `ScreenInfo` says is what is visible.
* **1280 bytes is the stride**, and it is padded: 240 pixels of four bytes is
  960, and the hardware keeps a 320-pixel line. Rows are 1280 bytes apart and
  only the first 960 of each are shown.

Round 72's reading -- that the panel is natively 320x240 landscape and the
reported 240x320 is its rotation -- is retracted. The measurement it rested
on, 32 bits a pixel on a 1280-byte line, stands, and this round's five logs
confirm it a third time: the user swept the format again and `32bpp/1280`
holds the longest dwell in all five.

### What the sweeps were actually doing

Stepping the line changed the *shape*, because build 142 derived the buffer
width from the pitch. `g6box7-7` walks the line down 1280, 1264, 1248 ... and
the fit records follow it: 320x240, 316x240, 312x240. The user was trying to
fix the aspect with the only control there was, and every step made the
stride wrong as well.

One accident worth keeping: at **16 bits** the derived width came out 640,
wider than the map arrays allow, so the code fell back to the reported 240 --
and the layout records read a correct **240x283 at y=18**. The right geometry
was reachable in build 142, but only through the wrong depth.

### Still open: the right-hand side comes back through the left

Unchanged from before build 142, and independent of everything above: a strip
of the picture's right-hand side reappears at its left. That is what a row
written longer than the buffer it goes into looks like -- the overflow lands
at the start of the next row. `srcPitch` is 176 because 176 is what the game
was *told*, and nothing has ever measured what it actually writes.

Build 143 puts the keypad on that instead of on the format: presets on the
digits (176, 192, 200, 208, 240), one pixel a step on `*` and `#`, eight on
`4` and `6`, and `NOTE_SCREEN_SRC` in the log so the dwell reading works the
way it did for the format. If a value makes the strip go away, that value is
the game's row length. If none does, the wrap is in the game's own layout
rather than in how we read it, and that is a different fix.

## Round 75 -- 176 is the stride, so the wrap is the game's

Two questions went out in build 143 and the phone answered both.

**The aspect is right.** `NOTE_SCREEN_FIT` reads buffer 240x320, picture
240x283 at y=18, and the video shows it filling the screen with the shape
kept. Round 74's correction -- the reported size is the screen, the 1280-byte
line is only a padded stride -- holds on hardware.

**The row length is 176, and that does not fix the wrap.** The user swept the
whole range the picker allows, **all 97 values from 160 to 256**, and the
dwell is not close:

| source pitch | records held |
|---|---|
| pitch **176** | **7,489** |
| pitch 240 | 1,589 |
| pitch 256 | 786 |
| pitch 192 | 594 |
| everything else | under 440 |

176 is where they kept coming back, and the video shows why: every other
value shears the picture into diagonal bands. That is what reading a buffer
at the wrong stride looks like, and it means **176 is the stride the game
writes with** -- measured on hardware for the first time, where before it was
only what the game had been told.

And the wrap is still there at 176. So it is not in how the port reads the
buffer. It cannot be: the only parameter that could cause it has been swept
end to end.

This is the negative result the round was built to get, and it agrees with
what **E137** established offline from a raw frame: the game writes 176x208
and nothing outside it, but *draws* as much as 192 pixels wide, so up to
sixteen columns of a row land at the start of the next one. The port has now
tried every control it has -- reporting 176x208 as the screen size (E132,
byte-identical frames), sizing the lent window (E137), reporting a 192-pixel
pitch (shears), and now sweeping the read stride on the phone. None of them
moves it.

**What is left is the game's own layout constants**, which means patching the
image, the way the protection check is patched. Next step is to look at what
the game actually writes, from a raw frame, rather than to reason about it.

## Round 76 -- the wrap is gone, and the band is the status pane

Build 144 on the phone: the game plays. Vehicle select, a tunnel race, the
Golden Gate track, nitro, the HUD whole. The right-hand side no longer comes
back through the left.

Two things came out of the round.

### The palette in front of the screen, found here rather than there

E170's screenshot still had a strip at the right and I called it fixed. The
user drew a line on it. Painting the blit's own outline (E171) answered it in
one run: destination column 0 landed at screen x=639 and column 239 at x=637,
*beside it*. Every row was starting eight pixels early and wrapping -- 32
bytes.

The emulator's own source has that number twice: `hal.cpp` computes
`EDisplayOffsetToFirstPixel` as `sizeof(u16) * WORD_PALETTE_ENTRIES_COUNT`,
which is 32, and `screen.cpp` adds exactly that to the chunk base to reach
the pixels. **The screen chunk begins with a sixteen-entry word palette, and
`UserSvr::ScreenInfo` hands back the chunk's base.** HAL had been answering
32 since build 59 and the port refused it, on a note that said the address
was already the first pixel and the 32 would shift the picture. Backwards.

The phone answers **0** for the same attribute, and 0 is right there -- which
is why build 144 was correct on the phone and wrong in the emulator. Build
145 applies HAL's answer after a sanity check, and is right in both.

### The band across the top

The port writes the framebuffer directly, underneath the window server.
Anything the window server paints therefore lands on top of the picture, and
the game asks avkon for `BaseConstructL(0)` -- a standard application, with a
status pane at the top and a button group at the bottom.

That band is visible as itself in round 74's photograph, with the close icon
on the left and the battery on the right. Over a running game the two
repaint in turn and it reads as a hazy smear instead.

### The ruler says so too, and gives the height

The user asked why the coloured bands from rounds 70 and 71 -- painted over
the game's own top 32 rows -- are gone now, and why that part of the game is
not being drawn in their place.

Because nothing stopped being drawn. It moved. Rounds 70 and 71 centred a
176x208 picture, so `offY` was **56** and those bands came out at screen rows
56 to 87, just clear of the pane. The scaled picture starts at row **18**, so
the game's own rows land at 18 and upward -- under it.

That also measures the pane. In round 74's photograph the picture starts at
screen row **51** although build 142 placed it at row 0, so the pane is
**51 rows** tall. The band in round 76's video measures 55 to 59, which is
the same thing through a phone camera.

Build 147 therefore carries a fallback in the same install: the keypad sets a
**top inset**, which fits the picture into the screen below the inset instead
of the whole screen. At 51 rows the picture becomes 227x269 -- about a
twentieth narrower -- and every row of it is visible whatever the pane does.

Build 146 adds **`ENoScreenFurniture`** to the flags, which is what a
full-screen game asks for. It is not the flag that went wrong before:
`ENoAppResourceFile` (0x01) took avkon off the rails because it does need the
resource file; `ENoScreenFurniture` (0x04) only says not to build the panes.
Avkon takes it and the game still runs (E173, E174).

## Round 77 -- the band survives the flag, so fill the space instead

`ENoScreenFurniture` reached avkon -- record 16 of both logs is the flags the
wrapper passed, and it is **4** -- and the band across the top is still
there. So it is not (only) the app's own status pane. Its appearance did
change: round 74's photograph had the close icon and the battery in it, and
now it is a plain grey band, so the flag removed the *contents* and left
something else painting the space.

The sweep measures it. Dwell per layout in the longer log:

| layout | records held |
|---|---|
| inset 56: picture 223x264 at (8,56) | **7,624** |
| inset 0: picture 240x283 at (0,18) | 2,590 |
| inset 64: picture 216x256 at (12,64) | 2,458 |
| inset 48: picture 230x272 at (5,48) | 1,164 |
| inset 32: picture 240x283 at (0,34) | 917 |

They tried 48 and moved on; they stayed at 56. **The band is more than 48
rows and no more than 56.**

### The trade, and which way to take it

At an inset of 56 there are 264 rows left. Keeping the shape in 240 x 264
gives 223 x 264 -- a black bar eight pixels wide down each side, an eighth of
the picture's width in dead space. Filling 240 x 264 instead stretches the
picture 1.364 across and 1.269 down: the axes differ by **7 per cent**.

Seven per cent of distortion against an eighth of the width in bars is not a
close call, and it is what the user asked for. Build 148 makes **fill** the
default, anchors the picture to the **bottom** so the leftover is at the top
where the band already is, and defaults the inset to 56.

`8` cycles the three fittings -- 1:1, shape-kept, filled -- and the inset
stays on the keypad, so the smallest inset that clears the band can be found
by stepping down from 56 with `#` (two rows) or `4` (eight).

### The one lever left on the band itself

Build 148 also sizes the lent window to the **whole screen** rather than to
the game's 176x208. E137 sized it to the game's own size and nothing moved,
which says nothing about this: the question is whether the band is another
window showing through where ours does not reach, and only a full-screen
window answers it. If the band goes, the inset can go to 0 and the picture
becomes 240x320 with nothing left over.

**Answered, and the answer is no.** Round 87's log has the full-screen
window going in (`SetExtent` to 240x320, recorded) and the band still there,
so it is not a window showing through where ours does not reach. See "The
band is a posting boundary" below.

### The band is a posting boundary, not a window drawn over us

Round 87's photograph of build 175 settled two things at once, and one of
them was not what the round was for.

The one it was for: the band survives `CAknAppUiBase::SetFullScreenApp`. The
log says the call resolved (`0x82cdb98d`) and returned, and the band is
still 58 rows of the phone's own theme gradient.

The one it was not for: **Avkon on the N95 answers 58**, not the 48 a 5320
reports and not the 56 the port hardcoded for four rounds. `NOTE_INSET_RECT`
has the main pane at `(0,58)` to `(240,293)` and `NOTE_INSET_TAKEN` has 58
adopted, and `gate6.cfg` carries `mode 4, inset 58` back off the phone. So
the risk build 174 took -- that a device might report less than its real band
and tuck the top of the picture under it -- did not happen here: 58 is
*more* than the 56 that worked, and nothing is hidden. That is the question I
should have asked outright in round 87 rather than inferred from silence.

**And the band itself now has an explanation that fits every observation.**
The game calls `CFbsScreenDevice::SetAutoUpdate(EFalse)` once -- the one call
in the whole run that changes what the display driver does -- and from then
on nothing reaches the panel until it is asked. It asks once a frame, with
`CFbsScreenDevice::Update(const TRegion &)`, and the region it hands over is
its direct-screen-access drawing region: the *visible* part of its window, as
the window server computed it. Whatever the status pane occupies is not in
it. So the top of the framebuffer is written every frame and **posted
never**, and the panel keeps showing what the window server put there before
the game took the screen.

Everything lines up with that and nothing argues against it:

* the band is **stable**, not flickering -- nothing is fighting us for those
  pixels, they are simply not being sent;
* the picture is **chopped, not squeezed** -- full-screen mode computes
  240x320 at `(0,0)`, the log says so and the harness agrees, so the pixels
  exist;
* it survived `ENoScreenFurniture` (round 77) and `SetFullScreenApp` (round
  87), because neither changes which region gets posted;
* the band is **exactly** the 58 rows Avkon calls the status pane; and
* **the bench has never once reproduced it** (E244 noted that and treated it
  as a limitation) -- because a framebuffer the emulator scans out directly
  has no posting step for a region to be left out of.

The fix is one ordinal. bitgdi **59** is `CFbsScreenDevice::Update(void)`,
the overload that posts the whole device: same call site, same frequency,
22% more pixels on a 240x320 panel, no region to leave anything out of, and
the original call still stands if the lookup comes back null. Two checks say
the ordinal is right on a real ROM and not only in the source release: the
shim already reaches bitgdi **58** for `Update(const TRegion &)` and **52**
for `SetAutoUpdate(TInt)`, both working on the phone, and the source numbers
all three the same way.

Build 176 did that, and also wrote the posted region down so the phone could
confirm or kill the theory rather than a photograph deciding it. On the bench
the rectangle is `(0,0)` to `(240,320)` (E248) -- the whole screen, which is
what a machine with nothing above the game's window should report.

**Round 88: the N95 says `(0,58)` to `(240,320)`.** One rectangle a frame,
starting at exactly the row Avkon calls the top of the main pane. The band
was never anything but a region boundary, and every round spent on windows,
construction flags and the status pane object was aimed at the wrong thing.

**And the fix was worse than the fault.** `Update(void)` resolves on the
phone -- `0x8064a345` is in the log -- and takes the game down with it: six
frames, a black screen, a log that stops nine seconds in, and the soundtrack
thread still playing, which is why it looks alive. On the bench the same
build ran 2,699 frames and raced. Two machines, the same call, and only one
of them tells the truth about it; that is the fifth time this project has
learned it and the first time it was not a diagnostic that paid for it.

Build 177 keeps the call the phone is known to be happy with -- the same
`Update(const TRegion &)`, same ordinal 58, same driver path, same frequency
-- and gives it a **region of our own**, one rectangle, `(0,0)` to the screen
size. `TRegion::RectangleListW` takes its list pointer from word four when
`iAllocedRects >= 0`, which is how the game's own region reads, so ours is
built to be read the same way. Only the numbers reaching the driver change.

### And it worked: the band is gone (round 89)

Build 177's photograph has the game edge to edge on an N95 -- WANTED and the
money counter on the top row, the speedometer on the bottom, no grey strip,
no theme showing through. The log has the game still asking for `(0,58)` to
`(240,320)` every frame and our `240x320` going to the driver in its place.

So the whole of it was one rectangle. Four rounds went on windows,
construction flags, `SetFullScreenApp` and the status-pane object, and none
of them was ever going to matter, because nothing was drawing over us: the
part of the screen we could not have was the part nobody was asked to send.
The lesson to keep is not "check the region" -- it is that the bench could
not see any of this and said so, and it took **giving the phone an
instrument** (the region, written down raw) rather than another theory.

### The logs: one file, replaced, capped (build 178)

Every launch from build 52 to 177 wrote a log of its own, `g6box0.log`
through `g6box9.log`, so a second launch could not destroy the first one's
record. That was right while the phone was still going down hard and the log
was the only witness. It is wrong now: the crashes closed in round 85, the
box is what survives a hard stop anyway, and ten files that are never cleared
is a pile of stale trace on someone's C: drive -- 1.4 MB for eight minutes of
play, times ten -- plus the round-86 trap, where a log left by an older build
reads back as a newer one's and means nothing.

Build 178: **one name**, `C:\g6box.log`, `file_replace` at launch, so at most
one log exists and it is always this run's. The ten old names are **deleted
once at startup**, so a phone that has been testing since build 52 does not
keep them for ever. And a **1 MB ceiling** on a single run, because replacing
the file bounds how many there are but not how big one gets.

The ceiling splits the two instruments cleanly rather than losing anything:
the log holds the opening, which is where every layout, region, inset and
configuration answer is, and the box holds the last sixty-four events and the
end state, which is where a failure is. 257 KB for a two-minute bench run
puts 1 MB at roughly eight minutes of play -- the length of the longest
session anyone has sent.

`KEEP_A_LOG` turns the whole thing off in one constant, for the day the port
stops needing rounds.

### Not on the table: making the game render 240x320

The game asks nothing about the screen it is drawing on. Its only geometry
query is `UserSvr::ScreenInfo`, whose size it ignores (E132: reporting
176x208 gives byte-identical frames), it imports no text rendering at all,
and every glyph is its own bitmap drawn straight into the framebuffer. Layout
width and row stride are both constants in the binary. Rendering natively at
240x320 would mean patching those constants -- a different kind of work
from anything done so far, and not a small one.

## Where we are

*Multi-resolution (phases 0-6) is built and bench-verified as build 174, and
is waiting on round 87. What it changes: five picture modes (1:1, aspect,
fill, integer, full screen) cycled by **holding C** -- one second, then every
half second -- with the choice kept in `C:\gate6.cfg`; **aspect** as the
default in place of fill, which is the same screen coverage with seventy-five
times less distortion; the status-pane inset **asked of Avkon** instead of the
N95's hardcoded 56, which answers 48 on a 5320 and is the one change that
could look worse on the phone that already worked; and a scaler that no longer
lets an array bound decide the picture size or silently crop the source.*

**Furthest: round 85.** The game installs from one SIS to either drive,
boots, plays with sound at frame rate, carries its own icon, runs on
**three different phones** -- N95, C5-00 and (predicted, untested) N79 --
and **plays several races in a row without crashing**.

Round 85 closed the crashes, and the cause was ours: `LEAK_EVERYTHING`,
a build-49 diagnostic that answered `User::Free`, `operator delete` and
`operator delete[]` with a do-nothing function, was never switched off
and shipped in every build since. The process held 51.1 MB after ninety
seconds on the bench and freed nothing; a phone runs out long before
that, and the first allocation it cannot satisfy is whichever comes next
-- in the recorded failure, zlib's own 32 KB window inside `uncompress`,
returning `Z_MEM_ERROR`, which `0x33a7c` flattens to -4, which is
`User::Leave(-4)`. The replacement gives back anything 4 KB or larger at
once and holds smaller cells in a 512-deep quarantine, so the
use-after-free the switch was hiding still cannot be handed a live
address. 51.1 MB -> 6.5 MB, and several races on hardware with no
crash.

The display is finished, and the rule is no longer the N95's. Ask HAL for
the **live** mode, not mode 0, and believe the stride it reports when it is
sane, treating 24 bits as 32-bit storage. A framebuffer line is a fixed
number of pixels and the mode only says how wide a pixel is: 320 pixels a
line on the N95, 2048 on the C5-00. The old rule -- the line is the logical
screen wide -- never matched on any phone, and the 1280-byte fallback
carried the N95 for eleven rounds while looking like it worked. The rest of
the layout stands as the phone's own dwell chose it in rounds 74 to 78:
picture read from source pixel sixteen, filled to the width, anchored to
the bottom below the 56-row band, picker off so every key reaches the game.

**Sound runs end to end on the bench (E202, E206), and the phone has not
heard it yet.** The game hosts its own client-server pair in this process
and the shim was dropping every message. `CServer::StartL` keeps the
server, `CreateSession` calls its `NewSessionL` (vtable slot 6),
`SendReceive` fills a message -- `iFunction` at 0, `iArgs[0..3]` at 36, 40,
44, 48 -- parks it at `session+16` and calls `ServiceL` (slot 5). The first
run of that ended in our own `G6IMP 464458` panic, which is 464x1000 + 458,
`CMdaAudioOutputStream::NewL`: proof the game had never reached audio
before.

The stream is created from the real `NewL` -- **ROM ordinal 3**, which the
emulator's patch overwrites with its own -- behind a proxy vtable, and the
game's calls reach it. What was left turned out to be one misreading in two
places. **GCC98r2 stores the vtable object's start in the object; EABI
stores its address point**, so a slot number counted from the pointer means
different things on the two sides. For `CMdaAudioOutputStream` it cancels
(two header words and a destructor against two destructors and
`CBase::Extension_`) and the map is the identity from slot 3; for
`MMdaAudioOutputStreamCallback`, a mixin with no destructor, nothing
cancels and it is a plain shift of two, which needs a proxy of its own in
the other direction.

With both in place: `Open` returns, **`MaoscOpenComplete(KErrNone)`**,
`SetAudioPropertiesL(16000 Hz, mono)`, `MaxVolume` 10, `SetVolume(10)`,
`SetPriority(100, 0)`, and then **71,777 `WriteL` calls each answered by
`MaoscBufferCopied(KErrNone, ...)`** in one run, with 695,381 records
against 2,134 in the best run before it. The buffers are 2000-byte `TPtr8`s
and they carry music: writes 160, 320 and 640 have all five hundred words
non-zero. The file the game holds open is
`E:\system\apps\6RBC\Streams\bgm_moby_lift_me_up.swav`.

Retracted with it: the faults at 0x100, 0xFFFFFFFA and 0x64 were `WriteL`
being handed an integer where it wanted a descriptor, not null fields in an
implementation that had not finished opening; 9.x's asynchronous open was
never the problem, and the settings package needed no translation at all --
9.x reads the rate and the channels at exactly the offsets the game writes
them, with the same enum values.

**The bench cannot hear.** This machine has no sound card, and a non-blocking
ALSA sink turns a timing question into a disk-space one (E203 wrote 10 GB in
ninety seconds). Everything past `WriteL` is the device's DevSound, so the
next step is a phone round.

**Best round so far: 72**, narrowly over 69. Round 69 got the game running;
round 72 made it watchable, and did it by putting the instrument in the
user's hands instead of trusting my own reading of a photograph -- which had
by then been wrong twice.

**Previously furthest: round 71.** The ruler works, and the framebuffer's line is
about **576 bytes** -- which is a 240-pixel 16-bit line padded up to a
64-byte boundary, so HAL was right about the depth all along and wrong only
about the padding. Two independent measurements agree on it: the ruler's
height at three different pitches, and the three-row repeat photographed in
round 69. No candidate in build 139's table was within 64 bytes of it.

**Round 69** is the one where the game booted. **Round 71**'s number was
wrong, and round 72 says why.

**Previously furthest: round 70.** The game boots and runs; the only thing left is the
framebuffer's format, and eight candidates have now been tried on the phone
itself. None is right, but the round is not a loss: the picker works, the
derived format and the table agree, and every candidate stripes -- including
the tightest pitch on offer, which says the real line is shorter than
480 bytes at two bytes a pixel, or is not that shape at all. The most
coherent of the eight is exactly what HAL claims, 16 bits on a 640-byte line.

**Best round so far: 69**, still: it is the one where the game booted.

**Round 70** is the one that stopped the guessing being open-ended: the
answer is now one photograph away, because build 140 paints a ruler instead
of a car.

**Previously furthest: round 69, build 138 -- the game boots on the phone.** It reaches
its main menu, runs 836 frames, takes 157 key events and does not panic. The
SoundServer thread lives and signals the semaphore on the first 100 ms slice.
Everything structural this file has been chasing since round 31 is done; what
is left is that the framebuffer's real format is still unknown, so the
picture is unreadable.

**Round 69** is the one the project was for.

**Runner-up: 68**, which found the cause the day before: `on_main_thread`
identified a thread by how far its stack was from the main thread's and
allowed a megabyte, when the two workers' stacks on this phone are **72 KB**
apart -- measured this round, by the probe, at `0x00415e7c` and `0x00427f90`.

**Previously furthest: round 68, build 137.** The cause is found and it is ours. On the
N95 `on_main_thread` calls the SoundServer thread *the main thread*, because
EKA2 packs a process's thread stacks close together and the test allows a
megabyte. Every guard built on it -- `log_block`, `box_write`, `box_flush`,
the worker-log gate, the probe itself -- is therefore a no-op for that
thread, so it writes the main thread's `RFile` and takes a bad handle:
**KERN-EXEC 0, named `SoundServer`**. The emulator has never shown it because
EKA2L1 puts every thread's stack in a chunk of its own, megabytes away, where
the test happens to work.

**Round 68** found the fault, and it found it in the one
place this project keeps finding faults -- its own instrument -- by an
instrument that reported by staying silent. It also retracts round 63.

**Previously furthest: round 67, build 136.** The gap is closed to a single call. With
the whole of the game's `operator new` traced on workers -- `TTrap::Trap`,
`User::AllocL`, `TTrap::UnTrap`, `User::LeaveNoMemory`, `User::Free` -- not
one of them appears, so the SoundServer thread never gets that far. It dies
inside **`CTrapCleanup::New()`**, which is the first euser call it ever
makes, and which allocates.

**Round 67** was, narrowly over 65: five candidate calls to one, on
a pure-instrument build that changed no behaviour and cost nothing. What it
cannot yet separate is a broken euser export from a thread that was never fit
to make a call, and build 137 asks the thread that question directly.

**Previously furthest: round 66, build 135.** Unchanged from round 65 in every record,
which is the result: lending the SoundServer thread the creating thread's
heap does not save it, so it is not dying for want of an allocator. The five
calls it dies among are now all named -- `CTrapCleanup::New`, then
`TTrap::Trap`, `User::AllocL`, `TTrap::UnTrap` and `User::LeaveNoMemory`
inside the game's `operator new` at `0x652f8` -- and none of them has ever
been traced.

**Round 65** removed the hang and turned a silence
into a measurement. Round 66 is a clean negative, which is worth having and
is not the same thing.

**Previously furthest: round 65, build 134.** The hang is gone: the timed wait ran its
full two seconds unsignalled, the main thread gave up, closed both handles and
**finished frame 1** -- the first time on hardware with the sound server in
the picture -- and there was no `ViewSrv 11`. What is left is one thread and
two calls. The box was flushed every 100 ms throughout, caught nothing from
the SoundServer thread after `CTrapCleanup::New`, and a trace thunk records on
the way *in*, so the death is inside `CTrapCleanup::New()` or the game's
`operator new(20)` at `0x652f8`, with nothing else in the gap. Both allocate,
and the game asks for the thread with `aHeap = NULL`.

**Round 65** removed the hang, turned a silence into a
measurement, and narrowed sixty-five rounds of "it dies somewhere" down to two
consecutive calls that do the same thing.

**Previously furthest: round 64, build 133.** The thread is named: **`SoundServer`**
appears in a panic dialog for the first time, and it appears because the
instrument sent to watch it killed it -- `worker_log` never produced a file,
so it died at or before its first `RFs::Connect`, which a trace thunk reaches
*before* the import it is tracing. Everything else is round 63 to the entry.
Three dialogs now name three threads: `SoundServer` (KERN-EXEC 0), a
decimal-numbered one (KERN-EXEC 0), and `gate6` (ViewSrv 11, the main thread
hung in `RSemaphore::Wait`).

**Round 64** gave one name, and the name is the answer to
a question this file has been guessing at since round 40. It also cost the
instrument: `worker_log` is off, and build 134 gets the same story out of the
box instead, by slicing the main thread's wait so that something is still
flushing while the dying thread runs.

**Previously furthest: round 63, build 132.** The failure is now located and it is not
what this file has called it for sixty rounds: **the main thread hangs**. It
goes into `RSemaphore::Wait` at `0xb8798` and never comes out, because the
SoundServer thread does not reach the `RSemaphore::Signal` at `0xb86ac` that
would release it -- and `gate6 ViewSrv 11` is the view server timing out on an
application that is not answering. The box, flushed on every traced import,
names the whole handshake up to that point, including the SoundServer thread
running its own `CTrapCleanup::New`. The semaphore is real (`CreateLocal`
answers 0) and `on_main_thread` is correct on hardware, both now measured
rather than assumed.

**Round 63** turned "the phone panics somewhere after frame
1" into "the main thread is blocked in a named `Wait` and the thread that
should release it dies between two named calls", and it retired the reading
that this was a crash at all. The instrument that did it cost 3%.

**Previously furthest: round 62, build 131.** The wrong-thread flush is fixed and the
proof is the dialog: the panic's name went from a different garbage number
every run to **`gate6`**, so the worker no longer dies and the thread that
does is our own main thread. Four launches agree to the record, so it is
deterministic. What is left is a second bad handle in the thirteen imports
between `RSemaphore::CreateLocal` and the end of frame 1 -- a region nothing
was tracing, which is what build 132 fixes.

**Round 62** turned the second panic from
a property of the port into a located, deterministic fault, and it did it on
a name in a dialog rather than a record, because the record could not reach
that far. Rounds 60 and 61 are what made it possible.

**Previously furthest: round 61, build 130.** The stack clamp works on hardware:
`RThread::Create` answers 0 with a 64 KB stack where 100,000 got
`KErrTooBig`, there is no leave and no exit, frame 1 runs and returns, and the
phone matches the emulator's run of the same build for **1,193 records with no
code out of place**. The run then ends in the **KERN-EXEC 0 that turns out to
be our own instrument**: `box_flush` guards the null handle and `box_write`
guards the wrong thread, and the `RFile::Flush` between them is guarded by
neither, so the SoundServer thread -- the thread the clamp just made
creatable -- flushes the main thread's file handle on its first sixteenth
traced event. One line. It also retires a reading that stood for dozens of
rounds: the KERN-EXEC 0 beside every KERN-EXEC 3 was never the game's.

**Round 61** confirmed a fix on hardware, held a 1,193-record
agreement with the emulator, and found that the second panic this project has
been explaining away since round 40 is a missing guard in our own logging.
Round 60 is what made it readable.

**Previously furthest: round 60, build 127.** The phone runs 1,276 records and matches the
emulator's run of the same build for **1,188 consecutive records** inside the
first `RunL` before parting company. Two phone runs gave identical logs, so the
ending is deterministic, and it is a single named cause:
`RThread::Create` refusing the game's 100,000-byte stack with `KErrTooBig`,
which is an EKA2 rule EKA1 did not have and EKA2L1 does not enforce. The
screen geometry is wrong for a second, separate reason -- HAL misreports both
the bits-per-pixel and the line pitch on an N95 -- and the video measures it
exactly. Neither is a mystery and both are in the shim's reach.

**Round 60** was, by a distance, the best before 61. It is the first round where the
phone got far enough to fail at something specific rather than something
structural, the first where a 1,188-record agreement with the emulator could be
shown, and the first that produced two independent fixable findings from one
run. It also found a gap in EKA2L1 itself.

**Previously best: 59.**

**Furthest before round 60: 176 traced events on the phone** (build 59), against 180 core
events in the emulator on the same build -- and the two now agree on **178 of
those 180**, the remaining two being heap addresses inside probes that were
never going to match. The phone is past the wall that held from build 44 to
build 50 at 128, past the 144 that build 51 reached, through the game's
self-check of its own image, and it stops exactly where the emulator gives up:
at `User::Leave`. The port no longer has a hardware-specific failure ahead of
it. It has the *same* failure as the emulator.

**The emulator now completes the startup.** 5337 records and 336 traced events,
the protection's state machine running its full fourteen-state path five times
over, eleven file opens of which ten succeed, and no `User::Leave`, no
`User::Exit` and no panic (E74, E78, E79). Doubling the emulator's time changes
nothing, so that is an ending and not a cut-off: the game draws one frame and
goes quiet. The previous best was 200 traced events and every run before this
ended by giving up or faulting. None of it has been to hardware yet.

**Round 59** was the first round where a hardware run and an
emulator run of the same build tell the same story from beginning to end.
Closing one file handle took the phone from 1571 records to 2104 and from 144
traced events to 176, and retired the last known divergence between the two
machines.

**Also load-bearing: 51.** It is the one that moved the port rather than
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

## Round 60 -- build 127 on the N95

The first hardware round since the game became playable in the emulator. Two
logs came back byte-identical in size (1,276 records each), a third and later
runs gave no log at all, and every run ended in **KERN-EXEC 0** under a thread
name that was a different decimal number each time -- `909947600`,
`1057530725`, and others. The user also saw, and filmed, the game's N-Gage
splash on screen: **duplicated and very small**.

Three separate things, and the round settles all three.

### 1. Where it stops: `RThread::Create` refuses a 100,000-byte stack

The two phone logs are the same run twice, so the ending is deterministic. It
is:

    import 110  RFile::Read      from 34b00
    import 110  RFile::Read      from 34b00
    import 324  User::Leave(int) from ba354   arg 0xffffffd8 = -40 KErrTooBig
    import 308  User::Exit

Lined up against **E142**, the emulator run of the identical build, the two
machines match **1,188 records in a row** from the first `NOTE_FRAME` and part
company on the next one: where the phone leaves with -40, the emulator's `RunL`
simply returns and goes on to draw 1,401 more frames. Nothing before that
differs -- not one code, not one address.

`0xba354` is the return address of a `blne` at `0xba350`:

    0ba348  bl   0xba500
    0ba34c  cmp  r0, #0
    0ba350  blne 0x118da8        <- the User::Leave stub
    0ba354  stm  sp, {r4, r5}

so the -40 is whatever `0xba500` returned, and `0xba500` is a two-line wrapper
around **`0xb86ec`** -- the connect-or-start-SoundServer site this file has
named since round 51. Read out, `0xb86ec` is the textbook `StartServer()`:

    TFindServer(name); TBuf<256> found; if (Next(found) != KErrNone) {
        RSemaphore sem; sem.CreateLocal(0);
        RThread t;  t.Create(name, threadfn, aStackSize, NULL, &sem);
        if (err) return err;                <- this is the -40
        t.SetPriority(...); t.Resume(); sem.Wait();
    }
    ... RSessionBase::CreateSession(name, version, 4)

and `aStackSize` is `r3`, which comes in from `0xba318` as a literal:

    0ba370  000186a0            = 100,000

**EKA2 caps a user thread's stack; EKA1 did not.** The N95 answers
`KErrTooBig`. Every number in that chain is from the image and the log, not
from a guess: the stub-to-import mapping is `(stub[3] - 0x101849bc) / 4`, which
puts `0x118da8` at import 324 = `User::Leave`, and that is the index the log
recorded.

**The emulator never saw it because EKA2L1 has no such cap.** `thread_create`
in `src/emu/kernel/src/svc.cpp` passes `user_stack_size` straight to
`kernel::thread`, which page-aligns it and allocates. Any size a program asks
for succeeds. That is the same family of gap as the SIS integrity fields in
`CLAUDE.md`: the emulator accepts what a device refuses, so the emulator cannot
be trusted to clear a build for hardware.

### 2. What the splash says: HAL's display geometry is wrong on the N95

The log's `NOTE_SCREEN` block gives the phone's answers in full:

| | phone (N95) | emulator (RM-409) |
|---|---|---|
| `iScreenAddress` | `0xcb400000` | `0xc9200000` |
| width x height | 240 x 320 | 240 x 320 |
| HAL `EDisplayBitsPerPixel` | **16** | 24 |
| HAL `EDisplayOffsetBetweenLines` | **640** | 960 |
| HAL `EDisplayOffsetToFirstPixel` | 0 | 32 |
| derived bpp / pitch | 16 / 640 | 32 / 960 |
| our buffer / source pitch | `0x00798060` / 176 | `0x0089b648` / 176 |

The substitution worked, the allocation succeeded, the game drew into our
buffer and the blit ran. The picture is still wrong, and the video says by
exactly how much. Measured off the frame, against the screen's own 240-pixel
width, the game's image appears as vertical bands with seams at device columns
**16, 104 and 176**, each band 88 pixels wide, washed out, and the whole thing
begins at row ~33.

Every one of those numbers falls out of assuming the buffer is **4 bytes per
pixel with a 960-byte line**, while we wrote it as 2 bytes per pixel with a
640-byte line:

* 176 sixteen-bit writes cover 352 bytes = **88** pixels of a 4-byte-per-pixel
  line, starting at byte 64 = column **16**. Band width and left edge, both.
* our row *y* goes to byte `(y+56)*640`, which in a 960-byte line is row
  `0.667*(y+56)` -- first written row **37**, against ~33 measured -- and
  column `((y+56)*640 mod 960)/4`, which cycles **0, 160, 80**. Add the 16:
  seams at **16, 176, 96**. The three measured seams are 16, 104 (= 16+88) and
  176.
* two 16-bit pixels land inside each 32-bit pixel, which is why the colour is
  bleached rather than merely shifted.

So the reading is: **`EDisplayBitsPerPixel` and `EDisplayOffsetBetweenLines`
both lie on the N95 for the buffer `UserSvr::ScreenInfo` hands out.** E138
already caught `EDisplayBitsPerPixel` lying in the emulator (24 for a four-byte
pixel) and E139 worked around it by trusting the *pitch* instead. Round 60 says
the pitch is not trustworthy either. There is a tell that would have caught it
offline: **640 is not a multiple of 240 at any bytes-per-pixel** -- 240x2 = 480,
240x4 = 960 -- so the pair HAL returned was never self-consistent.

### 3. The KERN-EXEC 0 with the numeric name

`User::Exit` ends the main thread. The garbage-named thread is the game's own
second worker, which this file has described before: it is created with a name
built from uninitialised stack, so it reads as a different decimal number every
launch. It outlives the main thread, touches a handle that has gone with it,
and gets **KERN-EXEC 0** -- a bad handle, which is what the panic has always
been. The varying number is the thread's name, not an address, and it is not
evidence of a different fault each run.

### What the round bought

The port's first hardware failure that is neither a mystery nor a
hardware-specific divergence: the phone and the emulator run the *same* 1,188
records and then the phone hits an EKA2 rule the emulator does not enforce.
Both findings are fixable in the shim, and the first is fixable in EKA2L1 too,
so that the next one of these is caught before the phone sees it.

## Round 61 -- build 130 on the N95

Both of round 60's fixes went to the phone. One of them is settled, the other
is not, and the round found a third thing that was never the game's.

### The stack clamp works, and the two machines are identical up to it

`RThread::Create` answers **0** with the clamped 0x10000 stack, where 100,000
got `KErrTooBig`. There is no `User::Leave` and no `User::Exit` in the log:
frame 1 runs to the end and returns. Aligned from each side's first
`NOTE_FRAME`, the phone and **E145** -- the emulator running the same build --
agree for **1,193 records with not one code out of place**, which is the whole
run as far as the phone got.

### The panic is our own instrument, not the game

The phone's log stops dead one record after frame 1 ends. The emulator's next
records are slot `0x504` and frame 2. What happens in that gap is the
**SoundServer thread starting** -- the thread the clamp just made creatable --
and its first act is a run of imports: `CTrapCleanup::New`, `operator new`,
`CActiveScheduler`, `Install`, and so on.

Every traced import goes through `gate6_trace`, from whichever thread makes
it, and every sixteenth one calls `box_flush`:

    static void box_flush(Context *c)
    {
        if (!c->boxFile[0])
            return;
        box_write(c);            // guarded: returns early off the main thread
        file_flush(c->boxFile);  // NOT guarded
    }

`log_block` has had the wrong-thread guard all along and `box_write` was given
one, but the `RFile::Flush` between them was left open. So the new thread
reaches a multiple of sixteen and calls `RFile::Flush` on **the main thread's
handle**, which on EKA2 is a bad handle: **KERN-EXEC 0**. The box's own
last-write counter says **208 traced events**, which is 16 x 13, and the
SoundServer thread's first imports land immediately after it.

The comment directly above `box_flush` describes this exact mistake being made
once before -- "build 55 put the guard inside `box_write` and left this flush
unguarded ... the same bad handle, one line further down". It was fixed for the
null handle and not for the wrong thread.

**This retracts round 60's reading of the KERN-EXEC 0.** It is not the game's
garbage-named worker touching something the exiting main thread took with it:
the main thread does not exit here and the panic still happens. It is ours, and
it has been ours for every round that has shown a KERN-EXEC 0 alongside a
KERN-EXEC 3.

### HAL lies about the screen three ways out of three

`EDisplayMode` was queried this round to see whether it could be trusted where
the other two could not. On the N95 it answers **1 -- `EGray2`**, for a 240x320
colour screen. So:

| attribute | N95 says | truth |
|---|---|---|
| `EDisplayBitsPerPixel` | 16 | 32 |
| `EDisplayOffsetBetweenLines` | 640 | 960 |
| `EDisplayMode` | `EGray2` | a 16M colour mode |

All three are wrong, and the rejection rule added in build 130 is what
produced the right answer regardless: the pitch was rejected for not being a
multiple of the width, and `realBpp` / `realPitch` came out **32 / 960**, which
is what round 60's video measured. **HAL is finished as a source for this** --
nothing further should be asked of it.

Whether 32/960 is right on the panel is still open. The only frames in this
round's video are from after the panic, with the window server repainting over
whatever was there, so they say nothing either way. The next round's video, of
a run that does not panic, is what settles it.

## Round 62 -- build 131 on the N95

Two runs, four launches, and all four logs the same 1,274 records with the same
ending. Deterministic, and the same ending as round 61: frame 1 runs, `RunL`
returns, and the log stops.

### What changed is the name on the dialog

Round 61 and every round before it showed **a different decimal number** in
the panic dialog -- `909947600`, `1057530725`, `-266741334`. This round it
says **`gate6`**.

That is the whole result. The thread that dies is no longer the game's
garbage-named worker; it is our own main thread. So the unguarded
`RFile::Flush` in `box_flush` **was** killing the worker, the guard fixed it,
and what is left is a second bad handle that was always behind it and could
never be seen while the worker died first.

It also means round 61's finding holds without the emulator ever being able to
confirm it (E146): the evidence is that the name changed.

### Where it is, and why this round cannot say

The main thread dies immediately after frame 1's `RunL` returns. Neither
instrument can place it:

* the **log** buffers eight records before writing, so its last record is up
  to seven events before the death;
* the **box** flushes every sixteenth traced import, and its own header says
  "208 traced events at the last write, so **208..239** in all" -- a window
  thirty-one events wide.

And the code the main thread is in is traced by nothing. Reading `0xb86ec`
out, what follows the `RThread::Create` that now succeeds is:

    RThread::SetPriority(20)        import 362
    RThread::Resume()               import 353
    RSemaphore::Wait()              import 374      <- returns, so the worker signalled
    RHandleBase::Close()            import 281      at 0xb87b0, on the RThread
    RHandleBase::Close()            import 281      at 0xb87b8, on the RSemaphore

None of those five is in `kMilestone`, and neither is anything the SoundServer
thread calls on its way up (`CTrapCleanup::New`, the `CActiveScheduler`, and
the `RSemaphore::Signal` that let the main thread out of its `Wait`). The
`RSemaphore::CreateLocal` at `0xb8754` has its result **thrown away by the
game**, so nothing knows whether that semaphore was ever valid.

`RHandleBase::Close` is worth noting on its own: closing the wrong thing with
it is what caused a month of reboots in this project already, and it is what
the main thread does twice in the window where it now dies.

Build 132 is the instrument for this and nothing else: the box flushed on
**every** traced import instead of every sixteenth, and those imports added to
the milestone set so there is something to flush.

## Round 63 -- build 132 on the N95

Build 132 changed no behaviour. It flushed the box on every traced import
instead of every sixteenth and put the SoundServer handshake into the
milestone set, so that the thirteen-import window round 62 could not see into
would be on record. It worked, and what it found reframes the failure.

### It is a hang, not a crash

The box's ring ends:

    220  RSemaphore::CreateLocal   from b8758
    221  RThread::Create           from 1907b0   (our clamping thunk)
    222  RThread::SetPriority      from b8788
    223  RThread::Resume           from b8790
    224  CTrapCleanup::New         from b866c    <- the SoundServer thread, running
    225  RSemaphore::Wait          from b8798    <- the main thread, about to block

and the log has **no `NOTE_FRAME_END`**. Round 62's log had one. So the main
thread went into that `RSemaphore::Wait` and did not come out.

That is exactly what the third dialog says. **`gate6 ViewSrv 11`** is the view
server timing out on an application that has stopped answering -- a hang.
Every previous round in this file has been read as a crash; this one is an
application sitting in a `Wait` that is never signalled.

The `Signal` that would release it is at `0xb86ac`, in the SoundServer
thread's entry function, after `CTrapCleanup::New`, an `operator new`, the
`CActiveScheduler` and the server's own construction at `0xb7ce0`. The thread
reached the first of those and not the last.

### Two questions answered on the way

**The semaphore is real.** `RSemaphore::CreateLocal` answers **0** on
hardware. The game throws that result away, so nothing could have known it
before this build wrapped the call; the whole handshake hangs off it, and it
is fine.

**`on_main_thread` works on hardware.** The SoundServer thread's
`CTrapCleanup::New` is in the box, which any thread writes to, and **not** in
the log, which only the main thread writes. That is precisely the split the
guards are supposed to produce, and it rules out the worry that the main
thread's stack and a new thread's are close enough on EKA2 to confuse a
1 MB test.

### Why the round stops where it does, and what build 133 is for

The guard that makes the worker safe is also what blinds us to it: a worker
may not touch the box's file, so its records sit in memory waiting for the
main thread to flush them -- and the main thread is blocked in `Wait`. Every
import the SoundServer thread makes after `CTrapCleanup::New` is stranded.

Build 133 gives the worker **its own** file: its own `RFs` session, connected
on the worker's own thread, its own `C:\g6wrk.log`, one record per write,
flushed each time, capped so a polling worker cannot fill the disk. Nothing
shared, so nothing to panic on.

### The other dialog

`886699653 KERN-EXEC 0` is still there, and still a decimal number rather than
a name. It is not the SoundServer thread: that one is created with a real name
from the image -- the emulator's kernel log prints it as `SoundServer` -- and
`RThread::Create` would have answered `KErrBadName` for a bad one, where it
answered 0. On the evidence so far it is the game's other worker, the 0x2000
one created at record 205 and resumed at 211. Build 133's worker log covers
both threads, so the next round says which.

## Round 65 -- build 134 on the N95

Two results, and the second one is only legible because of the first.

### The main thread is out of the hang

`NOTE_SEM_WAIT` goes down twice: `0` on the way in and **`ffffffff`** on the
way out. That is the give-up: twenty 100 ms slices, never signalled. The main
thread then did what it would have done anyway --

    281  RHandleBase::Close   b87b4
    281  RHandleBase::Close   b87bc
    298  RSessionBase::CreateSession  ba544
    866  FRAME END

-- and **frame 1 completed**, which has never happened on hardware with the
sound server in the picture. No `ViewSrv 11` this round either, because the
application never stopped answering.

### And the silence is now evidence

The box was flushed every 100 ms for the whole two seconds the other thread
was alive. It caught nothing. So the SoundServer thread makes **no traced
import at all** after `CTrapCleanup::New`, and that is a measurement rather
than a gap in the record.

A trace thunk records on the way *in*, so `CTrapCleanup::New` being the last
entry means the thread died somewhere between that record and its next one.
Reading `0xb8660` out, exactly two calls sit in that gap:

    0b8668  bl  CTrapCleanup::New()          <- the trace fires here, before the call
    0b8674  mov r0, #20
    0b8678  bl  0x652f8                      <- the game's operator new(20)
    0b8684  bl  CActiveScheduler ctor        <- would have been traced

**Both of them allocate**, and nothing else in the gap does anything at all.

### The leading explanation, and it is not yet proved

The game creates this thread with `aHeap = NULL`:

    0b8758  stm sp, {r5, r6}     ; [sp+0] = 0 = aHeap, [sp+4] = &semaphore
    0b875c  str r5, [sp, #8]     ; owner

On EKA1 and on EKA2 alike, a null `aHeap` in that overload means *share the
creating thread's heap*. Whether euser resolves that null into the creating
thread's allocator when it fills `SStdEpocThreadCreateInfo`, or leaves it null
for `UserHeap::SetupThreadHeap` to deal with, decides whether this thread has
a heap at all -- and if `iAllocator` is null **and** `iHeapInitialSize` is
zero, `SetupThreadHeap` sets up nothing. The thread's first allocation then
reaches for a heap that is not there.

That fits the panic number: **KERN-EXEC 0 is a bad handle**, not a bad
pointer, and an `RHeap` holds an `RChunk` handle it adjusts when it grows.
It also fits why no emulator run has ever shown it: EKA2L1 does not run this
thread's entry function at all (E148).

It is a hypothesis with one round's worth of evidence behind it -- the thread
dies in its first allocation and in nothing else. Build 135 tests it in the
cheapest possible way: `stack_thunk` already stands in front of
`RThread::Create`, so when the game passes a null `aHeap` it now passes
`&User::Allocator()` instead, which is the creating thread's heap said out
loud. If the hypothesis is right the thread lives; if it is wrong, nothing
else changes and the next round looks at `0x652f8` instead.

## Round 66 -- build 135 on the N95

A clean negative and a better map.

**The heap was not it.** `User::Allocator()` answers `0x600000` on the phone,
`stack_thunk` lent it to the new thread in place of the null the game passes,
and the log is **identical to round 65 record for record**: the wait still
times out, frame 1 still ends, and the panic is still `SoundServer
KERN-EXEC 0`. So the SoundServer thread is not dying for want of an
allocator, and round 65's explanation is retired. The substitution is left in
because it is the right semantics -- a null `aHeap` means *share the creating
thread's heap* -- and because it demonstrably costs nothing.

**The gap is five calls, not two.** Round 65 said the thread dies between
`CTrapCleanup::New` and `CActiveScheduler::CActiveScheduler()`, with
`operator new(20)` at `0x652f8` in between. Reading `0x652f8` out, that is
not one call:

    065310  bl  TTrap::Trap(TInt&)      import 372 -- our own stand-in
    065320  bl  User::AllocL(20)        import 269
    065328  bl  TTrap::UnTrap()         import 373 -- a no-op
    065334  blne User::LeaveNoMemory()  import 323 -- only if the trap fired

So the full list of what the thread does between its last record and its next
one is: `CTrapCleanup::New()`, `TTrap::Trap`, `User::AllocL`, `TTrap::UnTrap`,
and on the failure path `User::LeaveNoMemory`. **None of the five is traced.**

`TTrap::Trap` is worth a look on its own. It is `LOCAL_TRAP_ENTER` in our
shim -- three instructions, `mov r0,#0; str r0,[r1]; bx lr` -- which is the
EKA1 trap mechanism stubbed out to "first pass, no error". That is right for
the happy path and says nothing at all about a leave, which is a thing to
remember if `User::AllocL` ever does leave.

**Why build 136 cannot simply trace them.** Those five are the game's
allocator. On the main thread they are thousands of calls a run, and with the
box flushed on every traced import that is thousands of file writes. So they
are traced, and `gate6_trace` drops them **on the main thread only**: on a
worker they cost nothing but memory, because a worker may not flush, and the
main thread's 100 ms slices pick them up.

## Retracted: "the emulator never runs the SoundServer thread"

Said in E148 and repeated in E152, E153 and three replies. It was read off
the *main log*, which contains zero records for imports 330, 386, 319, 363
and 367 -- and the main log cannot contain them, because `log_event` diverts
every worker event away from it (that guard is what rounds 60 to 62 were
about). They were in the box all along.

**E155 settles it from the other side.** The worker probe's second slot is
filled by a thread whose stack is `0x04d0ff90`, at import `CTrapCleanup::New`
-- that is the SoundServer thread, in the emulator, running its own entry
function. It gets an allocator, allocates, and goes on to signal the
semaphore, which is why the emulator's wait returns on its first 100 ms slice
where the phone's never returns at all.

So the emulator is not blind to this path. It runs it correctly, which is a
different and more useful thing: the difference between the two machines is
now narrow enough to be a single call.

## Round 68 -- build 137 on the N95

The probe was meant to say what the SoundServer thread's allocator is. It
printed **`WORKER PROBE never ran`**, and that is a better answer than the
one it was built to give.

### The chain

1. The box still has `CTrapCleanup::New from b866c` at entry 224. So the
   SoundServer thread *did* reach `gate6_trace`.
2. `worker_probe` is called from there whenever `!on_main_thread(c)`. Both
   slots are empty, so that test was false: **`on_main_thread` said the
   SoundServer thread is the main thread.**
3. The main log -- which only the main thread writes, by that same guard --
   contains **`import 330`**, that thread's `CTrapCleanup::New`. Two
   independent measurements, same conclusion.
4. So every guard built on `on_main_thread` is a no-op for that thread:
   `log_block`, `box_write`, `box_flush`, the `worker_log` gate,
   `worker_probe`. The thread writes into the shared `logBuf` and, on the
   eighth record, `log_block` writes **the main thread's `RFile`**.
5. Which on EKA2 is a bad handle. **KERN-EXEC 0, named `SoundServer`.**

### Why the test is wrong, and why nothing here could see it

    enum { THREAD_SPAN = 0x100000 };        // no thread's stack is a megabyte deep

    static int on_main_thread(Context *c)
    {
        const u32 sp = (u32)__builtin_frame_address(0);
        if (!c->spTop) return 1;
        const u32 d = c->spTop > sp ? c->spTop - sp : sp - c->spTop;
        return d < (u32)THREAD_SPAN;
    }

The comment above it says "the kernel gives every thread its own chunk and
they are megabytes apart". **That is EKA2L1's behaviour, not a device's.** On
a real EKA2 phone a process's thread stacks are packed together, so a 64 KB
stack allocated beside an 8 KB one is a few kilobytes away, not a megabyte.
The emulator's worker sits at `0x04d0ff90` against a main thread megabytes
off and the test passes; the phone's does not.

### Retracted: "on_main_thread works on hardware" (round 63)

Round 63 argued that the guard was working because the worker's
`CTrapCleanup::New` was in the box and **not** in the log. It was not in the
log because the log buffers eight records and the process died before that
block was written. Rounds 65 to 68, where the main thread survives the wait
and flushes, all have `import 330` in the log. The guard has never worked on
this phone.

Three rounds were spent downstream of that mistake -- 65's heap hypothesis,
66's disproof of it, 67's narrowing to `CTrapCleanup::New` -- and none of them
was looking at the instrument. The rule this earns is the one the file
already has and I did not apply: **when a phone fault has no counterpart in
the emulator, suspect the thing that differs between them, and the instrument
is one of those things.**

### The fix

`on_main_thread` must not infer identity from a stack address. euser exports
`RThread::Id() const` at ordinal 1793, and an `RThread` holding the current
thread's pseudo-handle `0xFFFF8001` answers for whichever thread asks. The
main thread's id is recorded once at setup; every later call compares against
it. The stack test stays only as a fallback for the window before the id is
known.

## Round 69 -- build 138 on the N95: it boots

The game starts, reaches its main menu, and answers the keypad. On the phone.

| | round 68 | round 69 |
|---|---|---|
| records | 1,294 | **5,227** |
| frames in / out | 1 / 1 | **836 / 835** |
| framework calls into our slots | 5 | **968** |
| key events | 0 | **157** |
| `NOTE_SEM_WAIT` | `0`, `ffffffff` | **`0`, `1`** |
| panic | `SoundServer KERN-EXEC 0` | **none** |

`NOTE_SEM_WAIT` reading `1` is the whole story in one number: the semaphore
was signalled inside the **first** 100 ms slice, which means the SoundServer
thread got through `CTrapCleanup::New`, the `CActiveScheduler`, the server's
own construction at `0xb7ce0`, and reached its `RSemaphore::Signal`. It has
died at the first of those in every round since 63.

### What the probe measured

Both slots filled, and between them they say why nine rounds went wrong:

    WORKER PROBE 0  RLibrary::Load       sp 0x00415e7c   allocator 0x00600000
    WORKER PROBE 1  CTrapCleanup::New    sp 0x00427f90   allocator 0x00600000

**The two worker stacks are 72 KB apart.** `THREAD_SPAN` allowed a megabyte,
on the strength of a comment describing EKA2L1's memory model. Both threads
also report the same allocator, `0x00600000`, which is the heap the round 65
substitution lends them -- so that change was right even though it was not
the fix.

### What is left, and it is only the picture

The framebuffer geometry. The game's image appears **three times across the
screen** with alternate lines showing the phone's own menu through it, which
is what writing 176 pixels at four bytes each, on a 960-byte line, does to a
framebuffer that is none of those things.

Three attempts have now been made to deduce the real format from photographs
-- round 60 said four bytes on a 960-byte line, and this round's picture is
not what that would give -- and HAL has lied about it three ways out of three
(`EDisplayBitsPerPixel` 16, `EDisplayOffsetBetweenLines` 640, `EDisplayMode`
`EGray2`). Deduction has had its turn.

**Build 139 asks the phone instead.** The game now takes input, so the port
can carry a table of candidate formats and a key that cycles through them
live. One round, one key held down, and the format is whichever one makes the
picture stand still.

## Round 70 -- build 139: eight formats, none of them right

The picker works. Eight digits, eight visibly different pictures, and the
game kept running through all of them. Two things are confirmed by
construction: index 4 (32 bits, 960-byte line) is indistinguishable from
index 0, which is the derived format, so the table agrees with the
derivation; and the digits reach `gate6_control_offerkey` and are swallowed,
so the game never saw them.

| key | format | what it looks like |
|---|---|---|
| key 0, 4 | 32bpp, 960 | small blocky repeats, heavy striping |
| **key 1** | **16bpp, 640** -- what HAL claims | **much the most coherent; `SELECT` is legible** |
| key 2 | 16bpp, 480 | two copies, striped |
| key 3 | 16bpp, 512 | two copies, different phase |
| key 5 | 32bpp, 1024 | finer stripes, fragmented |
| key 6 | 24bpp packed, 720 | fragmented |
| key 7 | 32bpp, 1440 | shredded, spilling over the whole screen |

**Every one of them stripes** -- alternate lines show the phone's own menu
through the picture. Format 2 offers the tightest line the table has, 480
bytes, which is 240 pixels at two bytes; if the real line were that long or
longer at two bytes a pixel, that format would have laid its rows down
contiguously with no gaps at all. It did not.

### Why this round stops here

Three readings have now been taken off photographs of the game's own
artwork, and one of them -- round 60's "four bytes on a 960-byte line" --
was wrong and cost four rounds. A photograph of a car tells you very little
about a pitch.

So build 140 paints a **ruler** into the top of the frame instead: eight
source pixels per colour band across, four source rows per colour band down,
in red, green, blue and white, with a one-pixel white border round the whole
176x208 image. Then a single photograph answers all of it arithmetically --
the width of a band across gives the bytes per pixel, the height of a band
down gives the line pitch, and the sideways drift from one band to the next
gives the remainder. The game's picture stays underneath, and the picker
stays, so the same photograph can be taken in any of the eight formats.

## Round 71 -- build 140: the ruler reads 576

The ruler is 32 source rows tall: eight rows of colour bands across, then six
bands of four rows down. On screen it occupies

    H  =  32 * p_used / P_real       display rows

so photographing it at several `p_used` and reading *H* gives `P_real`
directly. That is what the eight photographs do, and they behave exactly as
that formula says: at 640 the whole ruler is squeezed into a couple of rows,
at 960 it is a little taller, and at **1440** it opens out into clearly
separated red, green, blue and white bands.

At 1440 the ruler covers something near 80 display rows, which puts

    P_real  =  32 * 1440 / 80  ~=  576

**A second measurement agrees.** Round 69's picture repeated every **three**
display rows. The repeat period is the smallest *k* with `k * 960` divisible
by `P_real`, and for 576 that is exactly 3 -- 960 leaves 384, 1920 leaves
192, 2880 leaves nothing. Very few nearby pitches give 3.

**And 576 is a number hardware would choose.** 240 pixels at two bytes is
480; rounded up to a 64-byte boundary it is 576. So HAL's
`EDisplayBitsPerPixel` of 16 was **right all along** -- the depth was never
the problem -- and its `EDisplayOffsetBetweenLines` of 640 is wrong by one
padding rule. Build 139's table had 480, 512 and 640 in it and not one
candidate within 64 bytes of the answer.

### Build 141 stops offering a menu

A fixed table of eight was the wrong instrument: it can only be right by
luck. Build 141 keeps the digits as coarse presets but adds a **fine step**:
`*` and `#` move the line by 16 bytes at a time, and `9` switches between two
and four bytes a pixel. The whole space is then reachable by hand, so the
round ends when the picture stands still rather than when the table runs out.

## Round 73 -- eight clicks, counted

The user ran build 141 again and said what they did rather than what they
saw: **preset `7`, then `*` eight times**.

Preset 7 in build 141 is `{ 4, 1152 }` and `*` adds `FMT_STEP` = 16 bytes to
the line. Eight of them is 128 bytes. `1152 + 128 = `**`1280`**, at four
bytes a pixel.

That is round 72's answer, reached without touching round 72's evidence.
Round 72 counted how long each format stayed live in the log and took the
longest dwell; this counts keypresses. Nothing is shared between the two
readings except the phone.

**32 bits a pixel, 1280 bytes a line.** Closed.

## Round 72 -- 32 bits a pixel, 1280 bytes a line

The user swept the line by hand and the log recorded every step, so the
answer did not need to be read off a photograph at all. It is simply the
format they stopped on.

Dwell, in records, per format in `g6box2-8.log`:

| format | records held |
|---|---|
| **32bpp, 1280** | **1,493** |
| 32bpp, 960 (the startup default, before any key) | 1,269 |
| 16bpp, 1280 | 588 |
| 16bpp, 1280 | 585 |
| 16bpp, 1280 | 360 |
| 16bpp, 576 | 356 |
| everything else | under 270 |

They swept up to 1280 and back down to it **four separate times**, at two
different depths, and the longest hold of the whole session -- nearly three
times anything else -- is 32 bits a pixel on a 1280-byte line. The
photograph, showing `ARCADE`, the carousel, `SELECT` and the car all upright
and legible, is that format.

**1280 bytes is 320 pixels at four bytes**, and the N95's panel is natively
**320 x 240 landscape** -- it is a slider that opens that way. The 240x320
that `UserSvr::ScreenInfo` reports is the rotated logical size, not the
buffer's shape.

### Retracted: round 71's 576, and round 69's three-row repeat

Both were wrong, and they were wrong the same way.

* Round 71 read the ruler's height off a photograph as "near 80 rows" at a
  1440-byte line. With the real line at 1280 it is `32 * 1440 / 1280` = **36**
  rows. I was out by a factor of two.
* Round 69 counted the repeat as three display rows. With the real line at
  1280 the column offset cycles `960, 640, 320, 0` -- a period of **four**.

I presented these as two independent measurements agreeing. They were not
independent: both were my eye on a photograph of a phone screen, which is
the exact failure mode round 70 was written to avoid, and which I then
repeated twice in the same round. The ruler *method* was sound -- it is what
made the sweep legible enough to do by hand -- but reading it by eye was not.

**What worked was giving the instrument to the person holding the phone**
and letting the log record the answer.

### A trap worth keeping

Two of the four logs in this round are from **build 139**, not 141. They
decode as build 139's table -- presets `(2,640) (2,480) (2,512) (4,960)
(4,1024) (3,720) (4,1440)` -- and its older `NOTE_SCREEN_FMT` packing, which
put the table index in the top byte. The launch-numbered log files
`g6box0..9.log` are **not cleared when a new build is installed**, so a log
pulled off the phone can be from any build that has run since the file was
last overwritten. Read the packing before trusting the contents.

### Retracted: "round 87 closes the inset risk"

Round 87's row claimed that, because the report said nothing about the
48-row inset, Avkon's 48 must be right on the N95 too. That is an argument
from silence and it is worthless here: the person was reporting the thing
they could see -- the band in full-screen mode -- not ticking off a list I
never gave them. **Nothing was asked, so nothing was answered.**

I then tried to get the number out of the round's video instead of asking
for it, on the reasoning that another hardware round is expensive. That did
not work either and is logged as a failure, not a result: the glass bounds
auto-detected to the phone body (aspect 0.865 against the expected 0.750),
and the per-frame picture bounding boxes came back uniformly near-full-width
(692-704 of 705) so the modes could not be told apart frame to frame. A
hand-measured crop puts the band at roughly **56 rows**, which agrees with
round 77's independent range (more than 48, no more than 56) -- but that is
my eye on a photograph again, which is the exact method rounds 70-72
retracted, so it decides nothing.

The band's height on the glass is not the question anyway. The question is
what *that phone's Avkon* answers, because that is what the port adopts, and
the only instrument that can say is the log: `NOTE_INSET_RECT` (821) records
the main-pane rect Avkon returned and `NOTE_INSET_TAKEN` (822) the inset
adopted from it. One `g6box?.log` off the phone settles it, and it can ride
along with the build-175 round rather than costing a round of its own.
