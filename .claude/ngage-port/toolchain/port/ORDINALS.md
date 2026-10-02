# How far the ordinal tables go

`ordcheck.py` follows every import of a title along the only chain there is
-- old ordinal, old name, 9.x name, 9.x ordinal, RM-409 ROM -- and says
which link is the weakest. `ordcheck.txt` is its full output for the six
titles on this machine (1 October 2026). This page is the reading of it.

## The sources, and what each is worth

| Link | Source | Standing |
|---|---|---|
| old ordinal -> name, euser | `bmarm/7.0-euseru.def` from the Symbian release | authoritative: 1,680 ordinals, the N-Gage ROM has 1,679 |
| old ordinal -> name, every other library | EKA2L1's `epoc6.def`, names in file order | a lead. **Where its export count equals the N-Gage ROM's (`rh-29`), the list is that build and a position is almost certainly the right name. Where it differs, it lists another build, and every position past the first insertion names the wrong function** |
| name -> 9.x ordinal | the release `eabi/*.def`, else EKA2L1's `epoc9.def` | good, with drift: a few defs are newer than the RM-409 ROM |
| 9.x ordinal -> the ROM | `romimg.py` on `z:\sys\bin`, with the ROM's own export count and the function body | the check: an ordinal past the count, or a `bx lr` body, is drift |

Libraries whose `epoc6` count matches the N-Gage ROM (`ordcheck.py --libs`):
esock, estlib, estor, etext, gdi, hal, msgs, sdpdatabase, sysagt, insock,
flogger, nifman, btdevice, btmanclient, charconv, egul, fepbase, mediaclient,
ezlib, bigint, among others. Libraries where it does **not**, and which every
S60 title imports heavily: **cone (309 vs 318), eikcore (278 vs 290), eikcoctl
(1,104 vs 1,219), eikctl (488 vs 386), avkon (2,727 vs 2,274), ws32 (343 vs
357), bitgdi, fbscli, bafl, apparc, efsrv (230 vs 232)**. For these the two
Asphalt ports confirmed each ordinal they use by running it; a new title's
ordinals in them are leads until they have run.

The 9.x side drifts too. `commonengine` resolves to ordinals 126 and 128 in a
ROM library with 83 exports: the def is a later version. `eikctl` 124 lands on
a `bx lr`. Both are caught by the ROM check, which is why it is part of the
chain.

## The titles

"Platform imports" leaves out the imports of DLLs shipped with the game that
the shim stubs whole (gamecomms, gameutils, arenaframework). Every other
shipped DLL is game code and its imports count. "Shared" is the (library,
ordinal) pairs the two Asphalt ports already resolve, which is as close to
proven as a table gets. "The bill" is what no table answers and the shim
generator has no hand answer for yet.

| Title | images | platform imports | shared with Asphalt | the bill | of which new | drift |
|---|---|---|---|---|---|---|
| Asphalt Urban GT (6r67) | 1 app + 1 stubbed dll | 374 | 374 | 27 (all answered by the port's own hooks) | 0 | 0 |
| Asphalt Urban GT 2 (6rbc, zip release with `main.dll`) | 1 app + 1 carried + 3 stubbed | 452 | 432 | 35 | 8 | 2 (commonengine) |
| **Ashen (6r21)** | 1 app + 5 carried + 1 stubbed | 402 | 241 | 57 | 43 | 0 |
| **Colin McRae Rally 2005 (6r66)** | 3 app + 3 carried + 2 stubbed | 632 | 221 | 120 | 108 | 1 (eikctl) |
| One (6r58) | 1 app + 41 carried + 3 stubbed | 1,331 | 333 | 313 | 299 | 3 |
| Call of Duty (6r48) | 1 app + 48 carried + 3 stubbed | 1,356 | 307 | 322 | 309 | 3 |

What the bill is made of, per title:

- **Ashen.** 30 imports new to it and answered by nothing: ten are
  GCC98r2 float and 64-bit helpers of the kind the generator already maps
  for the Asphalts (`__subdf3`, `__ltsf2`, `__gesf2`, `TInt64::DivMod`),
  eight are networking (`insock`, `nifman`, `flogger`, `commdb`) the port
  would stub as it stubs `msgs`, and the rest are one-liners (`TDes8::Num`,
  `RThread::Heap`, `RFs::ReadFileSection`, `CnvUtfConverter`). Its five carried DLLs are the Arena and comms layer
  (`snapcomm`, `arenafoundation`, `sc_lib`, `scxmlparser`, `sustandard`),
  candidates for stubbing whole. Same shape as the Asphalts: one `.app`,
  direct screen access, the N-Gage services around it.
- **Colin McRae Rally 2005.** 85 answered by nothing, and most of them are
  `eikctl` names -- `CEikClock`, `CEikWorldSelector`, hierarchical list
  boxes -- which a racing game does not call. `eikctl` is the library where
  `epoc6` lists 102 exports *more* than the N-Gage ROM has, so those names
  are the misnamed positions the table above warns of, not the game's real
  calls. The real bill for this title is to name its 57 `eikctl` and 125
  `eikcoctl` ordinals by another means (the N-Gage ROM's own function bodies
  and vtables, as `romimg.py` reads them) before anything is built. Three
  `.app` images, one of them 15 KB: a launcher and two stages.
- **One and Call of Duty.** Arena titles: 41 and 48 carried DLLs that are
  the N-Gage online framework (`cafm*`, `http*`, `ssl70`, `ncui*`,
  `rbinterpreterngs`), with 110 imports from libraries that do not exist on
  S60v3 at all. Most of it is online play that would be stubbed, but the
  game's own menus run through that framework, so the first question is how
  much of it single player needs. Not a first candidate.

## What this decides

- The tables are good enough to generate a first shim for a title shaped
  like the Asphalts, with the hand work in the tens of imports, not the
  hundreds. Ashen is that title: 43 new imports on the bill, none of them
  hard.
- They are **not** good enough to resolve a library blind where `epoc6`
  lists a different build. That was the risk the generic loader carried for
  every game at once; per title it is a list to confirm, and the bench
  confirms most of it by running.
- The ROM check belongs in `gen_shim.py` itself: a 9.x ordinal past the
  export count, or on a `bx lr`, should be refused at generation time rather
  than found on a phone (`eikctl` 124, `commonengine` 16/18).
- The generator's answers for the Asphalts transfer: 58 and 59 of their
  imports are answered by its tables, and 35 of Ashen's are already.
