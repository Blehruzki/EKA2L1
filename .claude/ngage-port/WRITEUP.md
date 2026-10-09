# The write-up (to do at the end of the project)

**The request.** Once the catalogue is ported (or the user has had enough),
write a public document about the whole process: how a compatibility layer
between N-Gage games and S60v3 phones was built. It is for people to read and
share, not a manual for the toolchain.

**The style reference** the user chose: the Dolphin emulator progress report
for release 2603,
<https://dolphin-emu.org/blog/2026/03/12/dolphin-progress-report-release-2603/>.
Match its tone, language and formatting: easy to follow, without dropping the
technical terms. The page refused automated fetching on 2026-10-03 (HTTP 403
from the bench), so **before writing, ask the user to paste the article's text
or save it beside this file**, and check the style notes below against it.

## Style notes (provisional, from Dolphin's progress reports in general)

To be confirmed against the 2603 article itself:

- Conversational and confident, with dry humour. Enthusiastic about the
  hardware, honest about what went wrong.
- Explain the concept before the fix: a section first says how the machine
  works (here: EKA1 against EKA2, GCC98r2 against EABI, a vtable, an active
  object), then what broke, then how it was found and fixed.
- Technical terms are kept, and each is introduced in plain words the first
  time. Analogies only where they make the idea clearer.
- One heading per story, named after the symptom or the feature, not the
  function. Screenshots with captions, which are often wry, and before/after
  pairs where there is something to compare (bench captures in `shots/` and the
  scratch captures of each round).
- Dead ends are told, not hidden: the wrong theories, the instrument that was
  the bug, and the fixes that broke another title.
- Credit people by name or handle (the user, the testers who ran builds on the
  N95, N91, C5-00 and N79, and anyone whose patch or reading helped). Ask the
  user how each wants to be named.
- Close with what is next, and how to report bugs or help.

## Rough outline

1. **Introduction.** What the N-Gage was, why its games never ran on later
   Symbian phones, and the goal: one SIS per game, install and play.
2. **Two generations of Symbian.** EKA1 against EKA2, the 7.0s SDK against
   9.x, GCC98r2 against ARM EABI, and capabilities and `\sys\bin`. Why a
   recompile was impossible: there is no source, only the card dump.
3. **The plan: a loader and a shim.** `gate6` loads the old E32 image itself,
   relocates it and answers its ~500 imports; `gen_shim.py` and the ordinal
   tables; the stand-in objects for the framework classes.
4. **The bench.** EKA2L1, the emulator patched for the project, `emurun.sh`
   and the E-series of runs, the log and fault box, range probes. Why a
   hardware round is a person's evening, and why the bench is not the phone
   (BUGBOOK section 10).
5. **The stories, one per heading**, chosen from `BUGBOOK.md`, one or two per
   title, for example:
   - Asphalt 2: "It does not install" -- executables only live in `\sys\bin`,
     so the image is scrambled (rounds 80-82).
   - The display that was mode 0 all along (round 83) and the picture modes.
   - The reboots that were our own handle (BUGBOOK section 1).
   - Backgrounding: the crash and the residual frame (sections 8 and 9).
   - Ashen: doubles the other way round, the bass pulses (round 113).
   - S60 3.0 keeps the control's window one word elsewhere (rounds 118-120).
   - One: the crash that was Asphalt 2's patch (E516-E527), and the fighters
     who never moved: a vtable two words off (E527-E566).
   - One's first phone run (round 125): three bad handles the emulator had
     quietly forgiven, found by making it panic like a phone (E567-E569).
6. **What carries over.** The checklist a new title goes through
   (`newgame.py`, `ordcheck.py`, BUGBOOK section 12), and the knobs table.
7. **Results.** Every title, every phone it was confirmed on, and known
   issues.
8. **Credits, thanks, what's next.**

## Material

`GAMES.md` (the titles compared: the "what carries over" and "results" material), `DEVICES.md` (editions and phones), `PORTING.md` (the long-form narrative already kept), `ROUNDS.md` (every run
and hardware round, with its result), `BUGBOOK.md`
(symptom, cause, fix and how it was found), `SYMBIAN.md` (the platform facts
with citations), `KNOBS.md`, `RELEASE.md`, `shots/`, the per-title
`game.h` comments, and the git history of the `ngage-port` branch.

Add to this list as the project goes: when a story would make a good section,
note it here with its rows.
