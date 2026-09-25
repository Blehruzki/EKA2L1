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

## Where we are

**Furthest: 176 traced events on the phone** (build 59), against 180 core
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

**Best round so far: 59.** It is the first round where a hardware run and an
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
