# N-Gage → S60v3 port kit

This kit turns an N-Gage game card dump into an installable package (`.sis`)
for S60 3rd Edition phones (N95, E-series, …). It does not recompile the
game: the original N-Gage binary runs unchanged inside a loader, `gate6`,
that lays it out in memory and answers every call it makes to the old
system with the S60v3 equivalent (the "shim"). The game's copy protection is
not patched either; it is answered (see *How the copy protection is
handled*).

No game data is included. Bring your own card dump.

## What you need

- **Python 3.8 or newer.** Windows: python.org installer, tick *Add
  python.exe to PATH*. Linux/macOS: usually already there.
- **LLVM**, for `clang`, `ld.lld`, `llvm-objcopy`, `llvm-nm` and
  `llvm-readelf`, on the PATH. Any recent version works (the port is built
  with LLVM 18). No Symbian SDK is needed.
  - Windows: the LLVM release from github.com/llvm/llvm-project/releases
    (`LLVM-<version>-win64.exe`, choose *Add LLVM to the system PATH*). If
    `build.py` then reports one of the five tools missing, use the full
    archive from the same page instead (`clang+llvm-<version>-x86_64-pc-
    windows-msvc.tar.xz`), unpack it and add its `bin` folder to the PATH.
  - Debian/Ubuntu: `sudo apt install clang lld llvm python3`
  - macOS: `brew install llvm`, then put `$(brew --prefix llvm)/bin` first
    on the PATH (Apple's own clang has no `ld.lld`).

Open a new terminal afterwards and check: `clang --version`, `ld.lld --version`.

## Build a game

    python build.py "path/to/Glimmerati (v04215-0.50-press-1605PAO) (MMC-ID b1e97d1-d8011855-b37d817-6000400) (G2.0 NOT FOR SALE)"

The argument is the dump: the folder (or a `.zip` of it) that holds
`system/apps/<stem>/<stem>.app`. Keep the dumper's folder name: the card ID
in it (`MMC-ID …`) is what the copy protection checks.

The result is `out/<appname>.sis` (for Glimmerati, `out/gate6glim.sis`,
about 51 MB with the game's files inside). Options:

- `--name <title>` — the title's folder name under `port/games/`
  (lower-case letters and digits). Default: the existing title for this card
  if there is one, otherwise a new one named after the dump.
- `--out <dir>` — where the package goes.
- `--loader-only` — two packages instead of one: the loader alone and the
  game's data alone, so a rebuilt loader is a small reinstall.

Install the `.sis` on the phone with *App. manager → Settings → Software
installation: All*. The package is unsigned and asks for no capabilities,
which S60v3 allows with a warning.

**Testing on a PC first** saves phone installs: the EKA2L1 emulator
(github.com/EKA2L1/EKA2L1) runs S60v3 and installs these packages.

## What a new game needs

`build.py` makes a new title with `port/newgame.py`, which writes
`port/games/<name>/game.h` with conservative defaults and generates the
shim from the game's own import table. It prints how many of the game's
imports it could answer. An unanswered one ends the game with a panic named
**G6IMP** when it is called: the number is (imports + 2) × 1000 + index, so
`G6IMP 398386` on a 396-import game is import 386. List the imports with

    python -c "import sys; sys.path.insert(0,'port'); import e32imports as e; d=open('path/to/<stem>.app','rb').read(); [print(i,l,o) for i,(l,o) in enumerate((l,o) for l,os in e.imports(d) for o in os)]"

and the fix goes in `port/gen_shim.py` (its tables `BY_ORDINAL` and
`MANUAL`, each entry explained beside it), then regenerate:

    python port/gen_shim.py <card>/system/apps/<stem>/<stem>.app port/games/<name>/shim.cpp

Everything a title can switch is a `#define` in its `game.h`, each with the
reason beside it; `docs/KNOBS.md` is the table of them.

## How the copy protection is handled

N-Gage cards from 2004 on carry Codewave's protection (`CWPROTLIB`; a card
shows it as `cwp.dat`, `cwprtag.dat`, `cwivenc.dat`, `cis.dat`, `game.lic`,
`nc.dat` and encrypted archives such as `*.cwa` or `data.zip`). Glimmerati's
press build ships its linker map (`6R37.APP.map`), which names the parts:

- `MMCCHECKS.o` reads the **card's hardware ID (CID)** through the N-Gage
  memory-card driver (`MBusDev`, an `RBusLogicalChannel`), and checks the
  card is **not writeable** (`IsMMCWriteable1`: a write-mode open on the
  game's own drive must fail, as it does on a read-only game card).
- The game's content keys are bound to that card: the encrypted data only
  decrypts on the card it was sold on.
- `CW_FGNOTIFIER.o` suspends and resumes threads on focus changes.
- The engine also reads `E:\Game.Id` ("N-Gage") at the card's root.

The port does not touch any of that code. It answers the questions the
protection asks, the way the original card would:

- **The card ID.** The loader answers the driver's call with the four words
  in `GAME_CARD_CID` (`gate6_mmc_control` in `port/gate6.cpp`). A dumper
  records the card's CID in the dump's folder name
  (`MMC-ID b1e97d1-d8011855-b37d817-6000400` → `0x0b1e97d1, 0xd8011855,
  0x0b37d817, 0x06000400`: restore the leading zeros), and `build.py` copies
  it into `game.h`.
- **The read-only card.** A write-mode open on `E:` is refused with
  `KErrAccessDenied`, whatever drive the game is really installed on: the
  game is always told it lives on `E:` and the file calls are translated
  after the refusal is decided on the name it asked for.
- **`Game.Id`.** A failed read of `\Game.Id` is answered with its six bytes
  (`GAME_ANSWER_GAME_ID`).

The real check then runs and passes, and decrypts the game's data itself.
Patching the check was tried once (One) and is the wrong road: the check's
result feeds the decryption, so a forced "yes" produced a zeroed licence and
garbage downstream. `docs/PORTING.md` has that story (search for
`PATCH_THE_CHECK` and `ANSWER_THE_CARD`), and Asphalt 2's crack — a forged
CID plus a read-only drive, decoded in the same section — is where the idea
came from.

## How Glimmerati was brought up

1. `newgame.py` on the press dump: 396 imports, 386 answered by the existing
   shim tables. The card ID from the dump's name into `game.h`.
2. First launch on the emulator: the game's thread panicked `G6IMP 398386` →
   import 386 → `INTCONNINIT.DLL` ordinal 5. The press build's map named it:
   `CIntConnectionInitiator::NewL`, called from `PlatformNGage::NewLC`, with
   ordinal 1 (`GetActiveIap`) called on every resume. That class is Series 60
   2nd Edition's internet-connection helper and S60v3 dropped it. Answered
   with a do-nothing object for `NewL` and `KErrNotFound` ("no connection")
   for `GetActiveIap` (`LOCAL_STANDIN`, `LOCAL_NOTFOUND`).
3. `UserHeap::ChunkHeap` had not matched: S60v3 added two parameters after
   the old four, defaulted. Called with four, it would read the other two off
   the caller's stack; a small thunk passes the defaults (`KIND_ARGS56`).
4. Second launch: the splash, the main menu, and the attract-mode race, with
   no fault. The Codewave check passed on the card ID and `data.zip`
   decrypted.

What was used: `port/e32imports.py` (the import table), the map (names for
addresses: a linked address is code offset + 0x10001000 in that build), and
the emulator's log. The whole project's record of what every title needed is
in `docs/`: `BUGBOOK.md` (every bug fixed, by symptom), `SYMBIAN.md` (the
platform facts, each with its source), `PORTING.md` and `PORT-HISTORY.md`
(the long story), `GAMES.md` (the titles side by side).

## Status

Glimmerati (press v0.50) boots to its menu and attract race on the EKA2L1
emulator. Not yet checked: playing a race with the keys, sound, saving,
picture fit and centring (it sits one-to-one at the top left; hold C to cycle
the fit modes), and a real phone. The other titles in `port/games/`
(Asphalt Urban GT, Asphalt 2, Ashen, One, Colin McRae Rally 2005) are the
project's earlier ports, with their own settings.

## Licences

The port's code is its author's. `port/kitdata/epoc6.def`, `epoc9.def`,
`palette.h` and `EKA21LHW.sis` come from EKA2L1 (GPL-2.0-or-later);
`port/kitdata/symbian/` holds `.def` files from the Symbian Foundation source
release (EPL-1.0).
