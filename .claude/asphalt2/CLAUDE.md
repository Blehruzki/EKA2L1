# Working on this port

Read this before changing anything under `toolchain/port/`. It is the working
agreement for the Asphalt port, not for EKA2L1 -- the emulator's own CLAUDE.md
is at the repository root and says, correctly, to ignore this directory unless
the task is this game.

## The four rules

The first three came from the person holding the phone, in their words. They
exist because a hardware round is expensive and I had been spending them on
tests already in the record.

1. **Each test result is logged before the next step**, with a confirmation
   message.
2. **Before proposing a test, check the log** that it is not a repeat, and say
   which rows it is not a repeat of.
3. **Keep track of how far we have got**, and which test was best and why.

The fourth came after round 93, for the other half of the problem -- the first
three stop a *test* being repeated and none of them stops a *guess* being
shipped:

4. **Review your own code before it goes out.** Do not just guess, do not just
   code: read back what you wrote, ask whether it actually makes sense, and
   check the things you assumed.

`python3 toolchain/port/rules.py` prints all four, computed from `ROUNDS.md`
rather than remembered, and runs what rule 4 can run. **Print them in every
reply that reports a result or asks for a hardware run.** They were given for
rounds 47 to 50 and then quietly dropped, which is what they now guard against.

## What rule 4 means in practice

Rule 4 is not "be careful". It is a checklist, and the items came from actual
bugs found by doing it:

- **Name the guesses.** Every constant that came from a table rather than a
  measurement is a guess until something moves when you change it. Euser
  ordinal 634 came from `epoc9.def`, whose euser list is 45 entries shorter
  than the RM-409 ROM's export table -- so the index was not a proof. E314 to
  E316 settled it by changing how often the port called the function and
  watching the emulator's count move by exactly that much. Three bench runs
  against one unverifiable constant is a good trade; a wrong ordinal on a
  no-argument function does nothing visible at all.
- **Ask what state the bench run never entered.** Every bug the first review
  pass found was there: a ring log that had not yet wrapped (the state a crash
  leaves), a config file written by the *previous* build, a key held down so it
  auto-repeats. The bench run is one path through the code.
- **A version bump is a compatibility question.** Adding a field to
  `gate6.cfg` bumped `CFG_VERSION`, and `cfg_load` refuses a version it does
  not know -- which would have silently discarded the screen mode every phone
  already had. Read the loader before changing the format.
- **Test the test.** The first version of `logringtest.py`'s critical case did
  not reproduce the state the writer actually produces, so it passed against
  the buggy reader. A case that cannot fail is not a case: check a new test
  rejects the code it was written to catch.
- **A shared fix ships to every title; a shared instrument does not.** The
  framebuffer-shift knob went into both games because the wrap was reported on
  both. One of them works, and a knob that swallows five keys on a game that
  works can only make it worse. Diagnostics are per title
  (`GAME_SHIFT_PICKER`, `GAME_LOG_CLOCK`); fixes live in `gate6.cpp`.
- **A build size is a cheap checksum.** Ten bench runs once went to a binary
  that was not being rebuilt, and an unchanging record count is what caught it.
  If a change should alter the code size and does not, find out why first.
- **An instrument's coverage is a claim, and it needs checking too.** The
  endgame flush was added so that "the last record before the death" would be
  the truth. It hung off `gate6_trace`, so it covered *traced imports* -- and
  everything that runs after the frame timer is cancelled is the port's own
  code, which is not a traced import. Three hardware rounds read "no records
  after the cancel" as "the process dies inside the cancel", when it only ever
  meant "nothing after the cancel was flushed". Before reading an absence as
  evidence, ask what would have had to happen for the record to appear.
- **The binary answers questions a round cannot.** "What does the game do
  after it cancels its timer?" was treated as something only the phone could
  say. It is in the image: find the import veneer, find every `bl` to it,
  disassemble around each. That took ten minutes and found a second `Cancel`
  the port had never accounted for.
- **Check what a box word means before trusting a hundred rounds of it.**
  `BOX_EXC` and `BOX_MDA` were the same word, and the audio stream's NewL
  address was read as "the exception handler is NOT installed" for a hundred
  rounds -- which steered every theory away from the one instrument that
  names a fault's instruction. Two enums at the same offset is a grep.
- **A dump of the queue beats any theory about the queue.** Six rounds of
  hypotheses about what dies on backgrounding were replaced by one walk of
  the active scheduler's list, logged at three moments, which named the
  object by elimination: the only one whose flags changed between the last
  two walks. When the question is "which object", read the list.
- **Ask the framework to do on the bench what the phone does.** The
  backgrounding death was "not reproducible on the bench" for a hundred
  rounds because nothing on the emulator takes the foreground away. The
  phone's chain was a view deactivation, and `CCoeAppUi::DeactivateActiveViewL`
  asks the view server for one from inside the process -- which reproduced the
  fault in a single run and verified the fix in the next. When a path cannot be
  entered from outside, look for the API that enters it from inside.
- **An object built from a base constructor and a copied vtable has only the
  bases' mixins.** The wrapper app UI was `CAknAppUiBase`'s constructor plus
  `CAknAppUi`'s primary vtable; the mixins `CAknAppUi` itself adds had no
  vtable pointer at all, and the first framework call through one was the
  crash. Every class level's constructor writes its own secondary vtable
  pointers; a vtable copy replaces one.
- **A picture is not a measurement.** Three readings of one screenshot gave
  three answers. Measure the bytes, or give the instrument to the person
  holding the phone and let them turn it until it is right.

## Sources, and which is which

Rank them, and say in the record which one a claim rests on. Getting this
wrong cost four rounds: the platform's behaviour was inferred from an
emulator's reimplementation of it, and the inference was wrong.

0. **`SYMBIAN.md`, in this directory.** Everything already read out of the
   platform's own sources and documentation, with each claim's file or page
   named: the panic codes, the `User::RequestComplete` contract, the app UI
   vtable slot map, how Avkon shuts an application down, what direct screen
   access does on an abort. **Read it before asserting anything about the
   platform, and add to it instead of re-deriving.**
1. **The binaries on this machine.** The N-Gage images, the RM-409 ROM and
   its extracted `z:\sys\bin`. `romimg.py`, `e32imports.py`, `epocdb.py`
   read them. Primary: vtable layouts, export addresses, real ordinal
   counts.
2. **Symbian's own published source.** `toolchain/port/getsources.sh`
   clones it under `/home/user/symbiansource/` -- classicui (cone, uikon,
   Avkon), kernelhwsrv (euser, the kernel), graphics (the window server).
   Not in this repository, and ten minutes to get back in a fresh
   container. This is the platform. Use it before anything below.
3. **SDK documentation** -- the panic references, the class reference. Good
   for contracts and panic meanings.
4. **Measurements** -- bench runs and the phone's logs and boxes.
5. **EKA2L1's source.** A reimplementation. Useful for seeing what the
   *bench* will do, which is a different question from what a device does.
   Never cite it for platform behaviour without checking 2 or 3.
6. **Training memory of Symbian.** Unverifiable here and wrong before now.

Web search and `WebFetch` are available and were not used for the first
ninety-five rounds of this project. That was the single largest avoidable
source of guessing in it. When a new platform question comes up: search,
read the source, write the answer into `SYMBIAN.md` with its citation, and
only then write code.

## Self-tests

`logringtest.py` covers the log format -- eight states, including the legacy
two-record header that builds 007 and 008 write, because those are on a phone.
Run it, and everything else in `rules.py`'s `SELF_TESTS`, before a build goes
out. Add to it whenever a format grows a state.

## Hardware rounds

Minimise them. A round costs a person's evening; a bench run costs ninety
seconds. Before asking for one, ask what it can establish that `emurun.sh`
cannot, and say so. Never run `emurun.sh` while another run is in flight.
