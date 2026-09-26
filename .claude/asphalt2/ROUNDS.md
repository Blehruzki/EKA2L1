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

## Where we are

**Furthest: round 66, build 135.** Unchanged from round 65 in every record,
which is the result: lending the SoundServer thread the creating thread's
heap does not save it, so it is not dying for want of an allocator. The five
calls it dies among are now all named -- `CTrapCleanup::New`, then
`TTrap::Trap`, `User::AllocL`, `TTrap::UnTrap` and `User::LeaveNoMemory`
inside the game's `operator new` at `0x652f8` -- and none of them has ever
been traced.

**Best round so far: 65**, still: it removed the hang and turned a silence
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

**Runner-up: 51.** It is the one that moved the port rather than
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
