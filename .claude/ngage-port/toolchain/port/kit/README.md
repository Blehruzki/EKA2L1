# S60v3 compiler kit

Build native programs for S60 3rd Edition phones (Symbian 9.x: N95, E-series,
…) with a stock LLVM, and no Symbian SDK. You write C, C++ or ARM assembly;
`build.py` turns it into an installable `.sis` file.

## Install

- **Python 3.8+**: python.org (Windows: tick *Add python.exe to PATH*).
- **LLVM**: you need `clang`, `ld.lld`, `llvm-objcopy`, `llvm-nm` and
  `llvm-readelf` on the PATH. Any recent version works (tested with 18).
  - Windows: get it from github.com/llvm/llvm-project/releases. The
    `clang+llvm-<version>-x86_64-pc-windows-msvc.tar.xz` archive has all five
    tools. Unpack it and add its `bin` folder to the PATH.
  - Debian/Ubuntu: `sudo apt install clang lld llvm python3`
  - macOS: `brew install llvm`, then put `$(brew --prefix llvm)/bin` first on
    the PATH (Apple's clang has no `ld.lld`).

## Build the example

    python build.py hello

That makes `hello/out/hello.sis`. Install it on a phone (*App. manager →
Settings → Software installation: All*), or in the EKA2L1 emulator
(github.com/EKA2L1/EKA2L1), which is quicker for testing. Launched, it prints
"Hello from clang" to the emulator's log and closes with the panic `HELLO 42`.
That panic shows the program was loaded, relocated, and that its imports
resolved.

## Writing your own

Copy `hello/` and edit it. `project.py` gives the name, the UID and the import
list. The rules:

- **No SDK headers.** Write out the types you need, the way `hello.cpp` does
  for a descriptor (`TPtrC16`: a length word with the type in its top four
  bits, then a pointer). Calls follow the ARM EABI. A C++ member function
  takes `this` in r0.
- **Imports are declared by ordinal.** Each function you call from a system
  DLL gets an `IMPORT name, ordinal` line in the assembly (see `start.s`),
  and its name goes in `project.py`. The 9.x ordinals are in the `.def` files
  of Symbian's published source (`eabi/euseru.def` for euser.dll, and so on;
  the Symbian Foundation release is mirrored on GitHub). Check every new
  ordinal: a wrong one calls a different function, often without visible
  effect. The five in `start.s` are known good.
- **No writable statics.** The image has no data section. `const` data is
  fine; anything that changes goes on the stack or the heap. `build.py`
  refuses a build that has writable statics, because a phone faults on the
  first write (the emulator does not).
- **UID3** in 0xE0000000-0xEFFFFFFF needs no signing. The package is unsigned
  and asks for no capabilities, which S60v3 installs with a warning. Calls that
  need a capability (Bluetooth, network, …) then fail on a phone even though
  they work in the emulator.
- `start.s` is the entry point an SDK build would get from `eexe.lib`. Keep
  it, and put your program in `hello_main` (or rename it).

`tools/` holds the steps after the linker: `mke32.py` (the E32 executable
header, with the import and relocation sections), `mkreg.py` and `mkloc.py`
(the resource files that put the program in the menu) and `mksis.py` (the
package, built from EKA2L1's sample `.sis` in `tools/`).

## The copy-protection method (One)

N-Gage cards from 2004 on carry Codewave's protection (`CWPROTLIB`). You can
spot it on a card by `cwp.dat`, `cwprtag.dat`, `cwivenc.dat`, `cis.dat`,
`game.lic`, `nc.dat` and encrypted archives such as `*.cwa` or `data.zip`.
It asks three questions:

1. **The card's hardware ID (CID)**, read through the N-Gage memory-card
   driver (`MBusDev`, an `RBusLogicalChannel`). The game's content keys are
   bound to it, so the data only decrypts on the card it was sold on.
2. **Whether the card is read-only** (`IsMMCWriteable1`). Opening a file for
   writing on the game's own drive must fail, as it does on a game card.
3. **`E:\Game.Id`**, a six-byte file holding "N-Gage" at the card's root.

The method: **don't patch the check, answer it.** Whatever runs the game
(on our side, a loader that hosts the N-Gage binary on S60v3) gives the
original card's answers:

- the driver call returns the **original card's four CID words**;
- a write-mode open on `E:` returns `KErrAccessDenied` (-21);
- a missing `\Game.Id` reads back as "N-Gage".

The real check then passes and decrypts the data itself. Forcing the check to
say "yes" was tried on One first and does not work: its result feeds the
decryption, so a forced pass produced a zeroed licence and garbage after it.
The idea came from an old Asphalt 2 crack, which also used a forged CID and
a read-only drive.

**Where the CID comes from.** Card dumpers record it in the dump's folder
name: `MMC-ID b1e97d1-d8011855-b37d817-6000400` is the four words
`0x0b1e97d1, 0xd8011855, 0x0b37d817, 0x06000400`. Pad each to eight hex
digits.

## Glimmerati notes

- The press build (v0.50) ships its linker map (`6R37.APP.map`), which names
  the protection's object files: `MMCCHECKS.o` (CID and read-only checks) and
  `CW_FGNOTIFIER.o` (suspends and resumes threads on focus changes). In that
  build a linked address is the code offset + 0x10001000.
- With the three answers above, the press build gets past the check, decrypts
  `data.zip`, and reaches its menu and attract-mode race on S60v3 (tested in
  the emulator).
- Two of its imports have no S60v3 counterpart and needed stand-ins:
  `INTCONNINIT.DLL` (Series 60 2nd Edition's internet-connection helper,
  dropped in S60v3). Ordinal 5, `CIntConnectionInitiator::NewL`, got a
  do-nothing object; ordinal 1, `GetActiveIap`, got `KErrNotFound`.
  `UserHeap::ChunkHeap` also needed a stand-in: S60v3 added two parameters,
  so the old four-argument call needs the two defaults passed for it.

## Licences

`tools/EKA21LHW.sis` is EKA2L1's sample package (GPL-2.0-or-later). The
rest of the kit is its author's.
