# The titles, side by side

How the ported games compare as ports: what each one is made of, which
mechanisms they share and which are one title's own, what each has switched
on and off, what each lacks against the others, and how the differences
between S60 editions (`DEVICES.md`) reach each one. It is the reference for
deciding what a fix in one title means for the others, and the material for
the "what carries over" and "results" parts of the write-up (`WRITEUP.md`).

The tables at the end are **generated** by `toolchain/port/titles.py` from
each title's `game.h`, `gate_imports.h`, `shim.cpp`, image and data tree:
`titles.py --write` refreshes them, `titles.py --check` says if they are
stale. Everything above them is hand-written and cites its rounds; KNOBS.md
says what each knob does, BUGBOOK.md why.

## The titles

**Asphalt Urban GT** (`6r67`, loader `gate6a1`, ships **030**). The second
title ported (from round 90) and the smallest real game: 391 imports, 615 KB
of code, seven files. Racing. Draws as Asphalt 2 does (below), with
a source pitch of 176. Protection: `game.lic`. Saves `user.dat`. Imports
zlib's `uncompress`. Confirmed on the N95 (3.1), the N91 (3.0, round 121) and
a C5-00 (3.2, build 027, round 122).

**Asphalt 2** (`6rbc`, loader `gate6`, ships **196**). The first title, and
the one the whole loader was built on (rounds 1-89). 462 imports, 1.5 MB of
code; its folder also carries the N-Gage Arena DLLs. Draws 176x208 in 16 bits
into its own buffer with a pitch of **192** (sixteen columns of every row land
on the next: E133-E135). Protection: Codewave (`6rbc.cwa`, `cis.dat`). zlib is
linked into the image (`GAME_Z_REAL`), not imported. Confirmed on the N95 and
the N91; an E65 report (round 109, build 192) predates the 3.0 fixes and was
never re-tested.

**Ashen** (`6r21`, loader `gate6ashe`, ships **016**). A first-person
shooter, ported from round 111. The only title that **draws through the
window**: it renders into its own `CFbsBitmap`s in 4K colour and blits them
with the control's `SystemGc` (rounds 113-114). So the framebuffer
questions (stride, status pane, `Update`) never reach it, and its picture
modes go through the port's own scaler and area filter (rounds 150-151).
Doubles in FPA order, `FormatList` va-lists, N-Gage font names, `Game.Id`.
Protection: `game.lic` and `\Game.Id`. Saves `options.dat` and `savegameNN.sav`.
Confirmed on the N95 (016, round 153) and the N91 (011, round 124).

**One** (`6r58`, loader `gate6one`, ships **028**, pending the N73). A
fighting game, ported from round 125, and the most demanding: 532 imports,
1.3 MB of code, 1,075 data files (27 MB, so it ships as a loader package and
a data package). Draws like the Asphalts but in **32 bits** (EColor16MU, the
mode it is told; `GAME_SRC_BPP 32`), polling `ScreenInfo` every frame.
Threads of its own beside the main one (the sound thread `g6w0` and the
loading thread `g6w2` among them) with a mutex between them, the most timers, and the only title with game-specific runtime
patches beyond image words: the tick seed of its archive reader, its
ambience list, a vtable moved to EABI's address point, an active-object
priority. Protection: Codewave (`cwp.dat`, `cwivenc.dat`, `cis.dat`) with its
own card ID. Saves `6R58.set` and the profile `6R58.prf`. Confirmed on the
N95 and N95 8GB (022); the N73 (3.0) dies in builds 022-027, fixed on the
N80 bench in 028 (12.ag).

**Colin McRae Rally 2005** (`6r66`, loader `gate6coli`, build **001**, bench
only). Ported from E883 (round 160) and built differently from every other
title: an **Ideaworks3D AirPlay engine**. The `.app` holds no game; the game
is `6r66.nax`, a 5 KB EKA1 loader with a gzip stream appended, which inflates
to an `LXCE` image (1.4 MB, PE-style relocations and an import directory by
ordinal: `lxce.py`). The port lays that image out itself
(`GAME_ENGINE_LXCE`), builds a 9.x app around it and runs the engine in a
thread of its own (`g6eng`, which renames itself COLIN), as the launcher ran
it in a process. 326 imports, 315 answered (the rest Bluetooth multiplayer).
What it needed that no framebuffer title did: EKA2's two-word
`TRequestStatus` in its own SetActive (three code words); a 24-byte `RWindow`
for an 8-byte one; EColor4K as the default mode; its **I3D shared memory**,
which the launcher made (without it the engine exits at once, E913);
16 code words moving its calls on ROM objects from GCC 2.x vtable slots to
EABI's (`CWindowGc` Activate/Deactivate/BitBlt and five deleting
destructors, E921), and its flip made always direct (E931). Draws into a
176x184 EColor4K `CFbsBitmap`, copies it to the `ScreenInfo` address + 0x20
and says `UserSvr::AddEvent(ERedraw)`, which the port answers with a post.
The port also plays the N-Gage **launcher**, I3D participant 2
(`GAME_ENGINE_LAUNCHER`): it answers the engine's mailbox and hands the
engine's own window group the focus, since the engine reads its keys itself
(E962-E971). And it calls one engine function the N-Gage never needed,
0x45b0b0, which sets the block interpreter's 24-bit link base for an engine
loaded above 16 MB (`GAME_ENGINE_INIT`, E965). The game's **front end**,
`6r66_2.app`, is a second loader (`games/colin2`, `gate6col2`, I3D
participant 3), started by the engine's StartApp hook
(`GAME_ENGINE_FRONTEND_EXE`); single player never asks for it (E962).
Data: `cmr05.dat` and per-country `.dz` packs, opened through estlib.
Protection: Codewave (`6r66.cwa`, absent on the bench).

**ngtest** (`ngtest`, loader `gate6ngte`). Not a game: an N-Gage SDK test
application built to measure the platform (rounds 134-135: the N95's audio
stream, thread timeslices). It found three bugs in the shared layer.

## Two ways to draw

The display architecture splits the titles, and decides which edition
differences reach each one.

| | framebuffer titles: Asphalt UGT, Asphalt 2, One | window title: Ashen |
|---|---|---|
| how the game draws | into its own 176x208 buffer, which it finds through `UserSvr::ScreenInfo`; the port hands it that buffer and blits it to the panel | into `CFbsBitmap`s, blitted through the window gc |
| who owns the screen | direct screen access: `CDirectScreenAccess::NewL`/`StartL`, `SetAutoUpdate`, `SetClippingRegion`, `Update(const TRegion&)` | the window server |
| picture modes (hold C) | the port's `screen_layout` maps (Bresenham, no divide) | `GAME_SCREEN_MODES` + `GAME_PORT_SCALER` + `GAME_SCALE_FILTER` |
| what the panel must tell the port | stride, first pixel, depth, status-pane inset | nothing beyond the screen size |
| what minimizing does | a DSA abort and restart; the residual and missed frames (BUGBOOK 8, 9) | a window redraw |

Colin McRae is a third kind, a hybrid: the engine draws into a `CFbsBitmap`
like Ashen, then copies it into the `ScreenInfo` buffer like the framebuffer
titles, and its `UserSvr::AddEvent(ERedraw)` stands where their
`CFbsScreenDevice::Update` does. Left to itself it uses the window gc except
for ten seconds after a key press; the port makes it always copy (E931), so
it takes the framebuffer titles' column above.

## What is shared and what is one title's

**Shared, with no knob (every title, always):** the loader, relocation and
the import shim; the old-shaped stand-ins for the framework classes; the
wrapper control and app UI, and the run-time discovery of the control's
window and the DSA layout (3.0); routing a base-class veneer to the wrapper;
thread stacks clamped and raised; per-thread exception handlers; the free
quarantine; the trap bridge; stream proxy reuse; the watchdog and the fault
box; `.bin` image reads; the red-key exit; `user_ptr`. KNOBS.md's "Shared
mechanisms" table lists each with its round.

**Universal fixes that are on for some titles only.** A universal fix stays
off for a title until a round of its own has run it there (KNOBS.md). These
are the real differences between the ports:

| fix | on for | off for, and why |
|---|---|---|
| `GAME_FIX_APPUI_THIS` (CEikAppUi methods get the real app UI) | UGT, Ashen, One | Asphalt 2: never needed it on three phones |
| `GAME_DIVERT_MATCH` (map the old object, do not substitute) | UGT, Ashen, One | Asphalt 2 ships the older substitution; correct while it has one of each object |
| `GAME_ALLOC_PAD` 512 | UGT, Ashen, One | Asphalt 2 runs without it on three phones |
| `GAME_FPA_DOUBLES` | Ashen, One | **the Asphalts import double helpers and Asphalt 2 imports `Math::`**; off until a round of their own |
| `GAME_VA_LIST` | Ashen, One | UGT has no `FormatList`; Asphalt 2 only `Format` |
| `GAME_UI_FORWARD_EVENTS`, `GAME_CANCEL_ROM_OBJECTS`, `GAME_ANSWER_GAME_ID` | Ashen, One | the Asphalts' own code never needed them |
| `GAME_SCREEN_MODES` | Ashen, One | the Asphalts have their picture modes through `screen_layout` instead |
| `GAME_CANCEL_OWN_OBJECTS`, `GAME_TIMER_MIRROR`, `GAME_MULTI_TIMER` | One | One's timer and active-object patterns only |
| `GAME_MDA_UNDERFLOW_TICKS` | Ashen | Ashen's sound thread restarts only on an underflow the N95 never sends |
| `GAME_DEFER_WORKER_STOP`, `GAME_MDA_POSITION_LEAD_US` | One | One's sound thread protocol |
| `GAME_PREPARE_EXIT_NOOP` | One | One calls `PrepareToExit` on its own app UI |

**One title's own by nature** (addresses inside one image, worthless in
another): Asphalt 2's image watch and zlib hook; Ashen's two code patches and
176x208 control; One's eight code patches, vtable shift, tick seed, ambience
offsets, kick RunL, AO priority and card ID.

## What each title lacks against the others

**The Asphalts are snapshots of round 120.** Builds 030 and 196 were made in
round 120 and confirmed through round 124; the shared layer has changed in
almost every round since, and their last bench regression was E712-E713
(round 134). Not in the packages on phones today:

- every shared fix from round 123 on: the veneer routing (round 123), stack
  raise and per-thread fault handlers (126-127), the `RFsBase::Close` fix
  (127), the watchdog and suspend/resume checks (130, 137), the minimize
  frame replay and restore (131-140), the trap bridge by vtable and stream
  proxy reuse (134), the red-key exit (139), `TLex16::Val` word order (144),
  `.bin` image reads (149), `user_ptr` up to 0xFFFF0000 (155), the thread
  open by name (156), the DrawNow divert (159);
- of those, the ones that matter to a **3.0 phone that loads images high**
  (the N73 class): `user_ptr` and the thread open by name. The Asphalts
  import `RThread::Open` by name; on the N91 they work, but the N91's image
  address was never recorded. An N73 has never run an Asphalt.

A rebuild would carry all of it and needs a bench regression of both first
(they have not run on the bench in 26 rounds).

**Ashen** (built round 152) lacks only `user_ptr` (155); it imports neither
`RThread::Open` nor `DrawNow`. It runs on 3.0 (N91, round 124).

**One** is current. It is the only title not yet confirmed on any 3.0 phone
or on a 3.2 phone, and the only one with a split package.

**Hardware coverage**, by edition: 3.0 -- Asphalts and Ashen on the N91; One
pending (N73). 3.1 -- every title on the N95. 3.2 -- Asphalt Urban GT on a
C5-00 only; every title on the RM-409 bench.

## How the edition differences reach each title

From DEVICES.md, by title. **exposed** = the title takes the path the
difference breaks; the fix is shared unless named.

| difference (DEVICES.md) | UGT | A2 | Ashen | One | why |
|---|---|---|---|---|---|
| 3.0: control's window not at 0x28 | exposed, fixed (r120) | exposed, fixed (r120) | exposed, fixed (r120) | exposed, fixed | every title's old control gets its window from the wrapper |
| 3.0: DSA object one word longer | exposed, fixed (r120) | exposed, fixed (r120) | -- | exposed, fixed | direct screen access titles only |
| 3.0: control flags behind a pointer | via veneers: fixed (r123) | as UGT | base `Draw` veneer: fixed (r123) | `DrawNow`: fixed in 028 (r159) | any cone method run on the old control; see the generated `CCoeControl` table |
| 3.0: foreground event at launch | survives | survives | redraw reached the veneer: fixed (r123) | -- | a title whose control does not override `Draw` |
| 3.0: thread open by full name fails | imports it; **not in 030** | imports it; **not in 196** | -- | fixed (r155-156) | titles that open their own threads by name |
| 3.0: image loads high | **not in 030** | **not in 196** | **not in 016** | fixed (r155) | `user_ptr` |
| 3.1: HAL display answers inconsistent | exposed, fixed (r83) | exposed, fixed | -- | exposed, fixed | framebuffer titles |
| 3.1/3.2: status pane height | asked of Avkon | asked of Avkon | -- | asked of Avkon | framebuffer titles lay the picture out below it |
| 3.1: `Update(void)` kills the game | own region posted | own region posted | -- | own region posted | framebuffer titles |
| 3.1: stream never underflows | -- | -- | `GAME_MDA_UNDERFLOW_TICKS` | `GAME_DEFER_WORKER_STOP` and a faked complete | titles whose sound thread waits for a complete |
| 3.2: 2,048-pixel line | exposed, fixed (r83) | exposed, fixed | -- | exposed, fixed | framebuffer titles |
| EKA2: stack cap | fixed | fixed | fixed | fixed (+ raise, r126) | every title creates threads |
| EKA2: bad handle kills | fixed per case | fixed per case | fixed per case | fixed per case | strict handles on the bench |
| EKA2: drive not ready (no card) | found here, fixed (r110) | fixed | fixed | fixed | E:-path rewrite and retry |
| depth: 16 against 32 bits | 16 | 16 | n/a | **32** | One takes the mode it is told |

## Open, by title

- **Asphalt Urban GT, Asphalt 2:** no open fault; packages are round-120
  snapshots (above). Asphalt 2 on the E65 (round 109) never re-tested.
- **Ashen:** no open fault. 12.ad ("Game Deck Memory Full") closed by
  observation in round 148 (saves work); which call had failed was never
  established.
- **One:** the N73 (12.ag), build 028 waiting on the phone.
- **Colin McRae 2005:** plays on the bench, menu to stage: the engine's own
  main menu, the attract demo, and with keys RALLY -> mode, difficulty,
  driver, tag, car, transmission, country, stage, weather, service area,
  RACE -> Finland stage 1 running with its HUD (E966-E976;
  shots/e966-colin-menu.png, e976-colin-stage.png), and driven: 5 throttle,
  7 brake and reverse, the arrows steer, a softkey pauses (E978-E981; the
  engine's own scancode map, shots/e981-colin-driving.png), with sound: the
  engine's own thread plays 16 kHz mono straight into the 9.x stream, music
  with the engine note mixed in, nothing from the port (E982-E986, the
  paced capture backend). Also benched: the N80 firmware (S60 3.0, 352x416:
  menus, keys, a stage, sound; loads slower, E987-E989); the release package
  installed with the emulator's installer on an emptied E: and C: (E990);
  hold C (E992-E993); QUIT to a clean exit (E1001); installed on C:
  (E1006-E1007); an app switch and back, and a DSA abort and restart
  (E1015-E1019); ten minutes of the attract loop (E1026). Open: sound on a
  phone; the multiplayer side, which may start the front end
  (`games/colin2` reaches its UI and frame timer alone, E958, and quits
  there by design without a block); a phone's own app switch (the bench's
  is a model of it); its `.cwa` protection; the
  window-gc path's white (E922-E930); a phone.

## For the write-up

Numbers that tell the story: five images from 25 KB to 1.5 MB, 181 to 532
imports each, 7 to 1,075 data files; two drawing architectures; three S60
editions on real phones; 129 hardware rounds and 882 bench runs as of round
160 (Asphalt 2 from round 1, Urban GT from 90, Ashen from 111, One from 125).
"What carries over" is this file's shared/one-title split; "Results" is the
hardware coverage above with DEVICES.md's phone table.

## Generated tables

<!-- titles.py begin -->
### Identity, image and package

|  | Asphalt UGT | Asphalt 2 | Ashen | One | ngtest | Colin McRae 2005 | Colin McRae front end |
| --- | --- | --- | --- | --- | --- | --- | --- |
| stem | `6r67` | `6rbc` | `6r21` | `6r58` | `ngtest` | `6r66` | `6r66_2` |
| loader app | `gate6a1` | `gate6` | `gate6ashe` | `gate6one` | `gate6ngte` | `gate6coli` | `gate6col2` |
| build in the tree | 030 | 196 | 016 | 028 | 002 | 012 | 001 |
| game UID3 | 0x101fd3fc | 0x101fd42d | 0x101fd3e9 | 0x101fd409 | 0x10205e7a | 0x00000000 | 0x101fd417 |
| code | 615 KB | 1556 KB | 970 KB | 1371 KB | 25 KB | 4 KB | 224 KB |
| imports / DLLs | 391 / 21 | 462 / 21 | 354 / 20 | 532 / 21 | 181 / 7 | 0 / 0 | 583 / 22 |
| imports the shim leaves as stubs | 14 | 14 | 8 | 7 | 7 | 1 | 67 |
| hooks present / defined | 66 / 161 | 72 / 161 | 61 / 161 | 90 / 161 | 29 / 161 | 74 / 161 | 49 / 161 |
| diversions (old object to wrapper) | 14 | 14 | 14 | 15 | 10 | 2 | 13 |
| game files / size | 7 / 9.9 MB | 122 / 27.6 MB | 7 / 15.3 MB | 1075 / 27.4 MB | 2 / 0.0 MB | 28 / 32.4 MB | 28 / 32.4 MB |
| split data package | no | no | no | yes | no | no | no |
| N-Gage-only libraries | bluetooth, etel, gamecomms, msgs, nokiafc | arenaframework, bluetooth, etel, gamecomms, gameutils, nokiafc | arenafoundation, bluetooth, gamecomms, insock, msgs, nokiafc, plpvariant, sysagt | arenaframework, etel, gamecomms, gameutils, nokiafc, plpvariant | -- | -- | sysagt |

### What the imports say

`x` where the title imports a call of that kind (any overload). Names as gen_shim gives them; the direct-screen-access calls go by an older name and are not listed (the Asphalts and One use it, Ashen does not).

| call | Asphalt UGT | Asphalt 2 | Ashen | One | ngtest | Colin McRae 2005 | Colin McRae front end |
| --- | --- | --- | --- | --- | --- | --- | --- |
| display: framebuffer poll | x | x |  | x |  |  |  |
| display: screen device update | x | x |  | x |  |  |  |
| display: clipping region | x | x |  | x |  |  |  |
| display: bitmap rendering | x | x | x |  |  |  |  |
| display: window gc |  |  | x |  | x |  | x |
| display: HAL display query | x | x |  |  |  |  |  |
| control: DrawNow |  |  |  | x |  |  | x |
| control: SetExtentToWholeScreen |  |  |  | x |  |  |  |
| control: DrawableWindow |  |  | x |  |  |  |  |
| sound: audio stream | x | x | x | x | x |  |  |
| threads: RThread::Create | x | x | x | x | x |  |  |
| threads: RThread::Open by name | x | x |  | x |  |  |  |
| threads: RThread::Suspend |  | x |  | x |  |  |  |
| threads: RSemaphore | x | x | x | x |  |  | x |
| threads: RMutex |  |  |  | x |  |  |  |
| threads: RCriticalSection |  | x |  |  |  |  |  |
| timing: CTimer | x | x | x | x |  |  | x |
| timing: CPeriodic |  | x | x | x |  |  | x |
| timing: CIdle |  |  | x |  |  |  |  |
| timing: RTimer |  |  | x | x | x |  | x |
| files: RFile::Replace |  | x | x | x | x |  |  |
| files: RFile::Seek | x | x |  | x |  |  |  |
| files: RFs::MkDir | x | x | x | x |  |  | x |
| files: zlib uncompress | x |  |  |  |  |  |  |
| maths: Math:: (doubles) |  | x | x | x |  |  |  |
| maths: TLex16::Val(double) |  |  |  | x |  |  |  |
| maths: TRealFormat |  |  |  | x |  |  |  |
| text: Format |  | x | x | x | x |  |  |
| text: FormatList |  |  | x |  | x |  |  |
| other: RLibrary |  | x |  | x |  |  | x |
| other: RChunk |  | x |  | x |  |  | x |
| other: sockets | x | x | x | x |  |  |  |

### cone `CCoeControl` calls, for S60 3.0

Every `CCoeControl` method the title imports. On S60 3.0 a cone method run on the game's old-layout control reads a flags pointer that is not there (DEVICES.md); **d** marks a call diverted to the wrapper. An undiverted one is safe only if it reaches cone through a slot the port routes (base-class veneers) or never runs on the old object: check on the N80 bench.

| method | Asphalt UGT | Asphalt 2 | Ashen | One | ngtest | Colin McRae 2005 | Colin McRae front end |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `ActivateL()` | d | d | d | d | d |  | d |
| `CCoeControl()` | x | x | x | x | x |  | x |
| `ComponentControl(int) const` | x | x | x | x | x |  | x |
| `ConstructFromResourceL(TResourceReader &)` | x | x | x | x | x |  | x |
| `CountComponentControls() const` | x | x | x | x | x |  | x |
| `CreateWindowL()` | x | x | x | x | x |  | x |
| `Draw(const TRect &) const` |  |  | x | x |  |  | x |
| `DrawDeferred() const` |  |  |  |  |  |  | x |
| `DrawNow() const` |  |  |  | d |  |  | d |
| `DrawableWindow() const` |  |  | d |  |  |  |  |
| `FocusChanged(TDrawNow)` | x | x | x | x | x |  | x |
| `GetColorUseListL(CArrayFix<TCoeColorUse> &) const` | x | x | x | x | x |  | x |
| `GetHelpContext(TCoeHelpContext &) const` | x | x | x | x | x |  | x |
| `HandlePointerBufferReadyL()` | x | x | x | x | x |  | x |
| `HandlePointerEventL(const TPointerEvent &)` | x | x | x | x | x |  | x |
| `HandleResourceChange(int)` | x | x | x | x | x |  | x |
| `HasBorder() const` | x | x | x | x | x |  | x |
| `InputCapabilities() const` | x | x |  | x | x |  | x |
| `IsFocused() const` | d | d |  | d |  |  |  |
| `MakeVisible(int)` | d | d | d | d | d |  | d |
| `MinimumSize()` | x | x | x | x | x |  | x |
| `OfferKeyEventL(const TKeyEvent &, TEventCode)` |  |  | x | x | x |  | x |
| `PositionChanged()` | x | x | x | x | x |  | x |
| `PrepareForFocusGainL()` | x | x | x | x | x |  | x |
| `PrepareForFocusLossL()` | x | x | x | x | x |  | x |
| `Rect() const` |  |  | d |  | d |  | d |
| `Reserved_2()` | x | x | x | x | x |  | x |
| `SetAdjacent(int)` | x | x | x | x | x |  | x |
| `SetContainerWindowL(const CCoeControl &)` | d | d | d | d | d |  | d |
| `SetDimmed(int)` | x | x | x | x | x |  | x |
| `SetExtentToWholeScreen()` |  |  |  | d |  |  |  |
| `SetFocus(int, TDrawNow)` |  |  |  |  |  |  | x |
| `SetFocusing(int)` |  |  |  |  |  |  | x |
| `SetNeighbor(CCoeControl *)` | x | x | x | x | x |  | x |
| `SetPosition(const TPoint &)` |  |  |  |  |  |  | x |
| `SetRect(const TRect &)` |  |  |  |  | x |  |  |
| `SetSize(const TSize &)` |  |  |  |  |  |  | x |
| `Size() const` |  |  |  |  |  |  | x |
| `SizeChanged()` | x | x | x | x | x |  | x |
| `SystemGc() const` |  |  | x |  | x |  | x |
| `Window() const` | x | x |  | x |  |  |  |
| `WriteInternalStateL(RWriteStream &) const` | x | x | x | x | x |  | x |
| `~CCoeControl()` | x | x | x | x | x |  | x |

### Knobs in `game.h`

`·` = not defined: the default in `gate6.cpp`, off or empty (`GAME_SRC_BPP` 16, `GAME_CARD_CID` the generic card). What each does: KNOBS.md.

| knob | Asphalt UGT | Asphalt 2 | Ashen | One | ngtest | Colin McRae 2005 | Colin McRae front end |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `GAME_ALLOC_PAD` | 512 | 0 | 512 | 512 | 512 | 512 | 512 |
| `GAME_AMBIENCE_ENTRY` | · | · | · | 0x64 | · | · | · |
| `GAME_AMBIENCE_ID` | · | · | · | 0x5c | · | · | · |
| `GAME_AMBIENCE_LIST` | · | · | · | 0x58 | · | · | · |
| `GAME_ANSWER_GAME_ID` | 0 | 0 | 1 | 1 | 1 | 1 | 1 |
| `GAME_AO_PRIORITIES` | · | · | · | 1 entry | · | · | · |
| `GAME_BENCH_CALL_AT` | · | · | · | · | · | 0 | · |
| `GAME_BENCH_CALL_FN` | · | · | · | · | · | 0x7df80 | · |
| `GAME_BENCH_DSA_ABORT_AT` | · | · | · | · | · | 0 | · |
| `GAME_BENCH_DSA_ABORT_KICK` | · | · | · | · | · | 0 | · |
| `GAME_BENCH_DUMP_AT` | · | · | · | · | · | 0 | · |
| `GAME_BENCH_ENDKEY_AT` | · | · | · | · | · | 0 | · |
| `GAME_BENCH_ENDKEY_ENGINE` | · | · | · | · | · | 0 | · |
| `GAME_BENCH_ENGINE_DIES` | · | · | · | · | · | 0 | · |
| `GAME_BENCH_MAIN_HANG_AT` | · | · | · | · | · | 0 | · |
| `GAME_BENCH_SHUTDOWN_AT` | · | · | · | · | · | 0 | · |
| `GAME_BENCH_SWITCH_AT` | · | · | · | · | · | 0 | · |
| `GAME_BENCH_SWITCH_FOR` | · | · | · | · | · | 10 | · |
| `GAME_BMU_YIELD` | · | · | · | · | · | 1 | · |
| `GAME_BUNDLE_DATA` | 1 | 1 | 1 | 1 | 1 | 1 | 1 |
| `GAME_CANCEL_OWN_OBJECTS` | 0 | 0 | 0 | 1 | 1 | 1 | 1 |
| `GAME_CANCEL_ROM_OBJECTS` | 0 | 0 | 1 | 1 | 1 | 1 | 1 |
| `GAME_CAPABILITIES` | · | · | · | · | · | 0 | · |
| `GAME_CARD_CID` | · | · | · | 0x567857f1,… | · | · | · |
| `GAME_CODE_PATCHES` | 0 entries | 0 entries | 2 entries | 8 entries | 0 entries | 21 entries | 0 entries |
| `GAME_CONTROL_H` | 0 | 0 | 208 | 0 | 0 | 0 | 0 |
| `GAME_CONTROL_W` | 0 | 0 | 176 | 0 | 0 | 0 | 0 |
| `GAME_DEFER_WORKER_STOP` | · | · | · | 1 | · | · | · |
| `GAME_DIR_CHARS` | · | · | · | · | · | · | '6','r','6'… |
| `GAME_DIVERT_MATCH` | 1 | 0 | 1 | 1 | 1 | 1 | 1 |
| `GAME_DUMP_FRAME` | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| `GAME_DUMP_SCREEN` | 1 | 0 | 0 | 0 | 0 | 0 | 0 |
| `GAME_ENGINE_FRONTEND_EXE` | · | · | · | · | · | 'g','a','t'… | · |
| `GAME_ENGINE_INIT` | · | · | · | · | · | 0x45b0b0 | · |
| `GAME_ENGINE_LAUNCHER` | · | · | · | · | · | 1 | · |
| `GAME_ENGINE_LXCE` | · | · | · | · | · | 1 | · |
| `GAME_ENGINE_SHM_CHARS` | · | · | · | · | · | 'I','3','D'… | · |
| `GAME_FIX_APPUI_THIS` | 1 | 0 | 1 | 1 | 1 | 1 | 1 |
| `GAME_FPA_DOUBLES` | 0 | 0 | 1 | 1 | 1 | 1 | 1 |
| `GAME_FREE_BACK_FROM` | 4096 | 4096 | 4096 | 4096 | 4096 | 4096 | 4096 |
| `GAME_GLOBAL_FN` | · | · | · | 0x000aae0c | · | · | · |
| `GAME_HOOK_UNCOMPRESS` | 0 | 1 | 0 | 0 | 0 | 0 | 0 |
| `GAME_IMAGE_WATCH` | 0 | 1 | 0 | 0 | 0 | 0 | 0 |
| `GAME_IMAGE_WATCH_SITES` | 0 | 0x0013c1fc,… | 0 | 0 | 0 | 0 | 0 |
| `GAME_KICK_RUNL_FN` | · | · | · | 0x000265ac | · | · | · |
| `GAME_KICK_RUNL_SLOT` | · | · | · | 0x001562a4 | · | · | · |
| `GAME_LAUNCHER_CLEARS_1` | · | · | · | · | · | 1 | · |
| `GAME_LEAK_ALL` | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| `GAME_LOG_CLOCK` | 1 | 0 | 1 | 1 | 1 | 1 | 1 |
| `GAME_LOG_TEXT` | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| `GAME_MDA_OPEN_CHANNELS` | · | · | · | 0 | · | · | · |
| `GAME_MDA_OPEN_RATE` | · | · | · | 0 | · | · | · |
| `GAME_MDA_POSITION_LEAD_US` | · | · | · | 100000 | · | 150000 | · |
| `GAME_MDA_RDEBUG` | · | · | · | · | · | 0 | · |
| `GAME_MDA_UNDERFLOW_TICKS` | · | · | 32 | · | · | · | · |
| `GAME_MDA_WRITER_CB` | · | · | · | 0xc | · | · | · |
| `GAME_MULTI_TIMER` | · | · | · | 1 | 1 | 1 | 1 |
| `GAME_PICTURE_MEASURED` | 1 | 1 | 0 | 0 | 0 | 0 | 0 |
| `GAME_PORT_SCALER` | · | · | 1 | · | · | · | · |
| `GAME_PREPARE_EXIT_NOOP` | · | · | · | 1 | · | · | · |
| `GAME_SCALE_FILTER` | · | · | 1 | · | · | · | · |
| `GAME_SCREEN_FONTS` | 0 | 0 | 1 | 0 | 0 | 0 | 1 |
| `GAME_SCREEN_MODES` | 0 | 0 | 1 | 1 | 1 | 1 | 1 |
| `GAME_SHIFT_PICKER` | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| `GAME_SOCK_RECV_KEEP0` | · | · | · | · | · | 0x000025d4 | · |
| `GAME_SOCK_RECV_KEEP1` | · | · | · | · | · | 0x00002840 | · |
| `GAME_SOCK_WRITE_KEEP` | · | · | · | · | · | 0x00002734 | · |
| `GAME_SOUNDMGR_OFF` | · | · | · | 0x6914 | · | · | · |
| `GAME_SRC_BPP` | · | · | · | 32 | 16 | 16 | 16 |
| `GAME_TICK_SEED_LR` | · | · | · | 0x000c5d2c | · | · | · |
| `GAME_TICK_SEED_VALUE` | · | · | · | 0x12c | · | · | · |
| `GAME_TIMER_MIRROR` | 0 | 0 | 0 | 1 | 1 | 1 | 1 |
| `GAME_UI_FORWARD_EVENTS` | 0 | 0 | 1 | 1 | 1 | 1 | 1 |
| `GAME_VA_LIST` | 0 | 0 | 1 | 1 | 1 | 1 | 1 |
| `GAME_VTABLE_SHIFTS` | · | · | · | 1 entry | · | 1 entry | · |
| `GAME_WATCHDOG_KILL_S` | · | · | · | · | · | 12 | · |
| `GAME_Z_REAL` | 0 | 0x000d4f88 | 0 | 0 | 0 | 0 | 0 |
| `GAME_Z_SITES` | 0 | 0x00033a74,… | 0 | 0 | 0 | 0 | 0 |
<!-- titles.py end -->
