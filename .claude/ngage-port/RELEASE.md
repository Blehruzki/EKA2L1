# The release post

The text that goes out with every public build, as the person posts it.
The variables are the title's; the facts behind them are in KNOBS.md
("Where each title saves") and the title's `game.h`. Check them against
the record before posting: the cfg name is `gate6-<stem>.cfg` at the root
of C: (E423, E440 read it back), the save folder and file names are the
game's own (E445 for Ashen; the Asphalts' `user.dat` from their images).

```
**<Title> (N-Gage) ported to S60v3 devices**

First public build of my port of <Title> (N-Gage) to S60v3

Features:
**- Full game, wrapped up in a nice, convenient .sis file.** Just install & play
**- Screen mode**: Press and hold the C button (Backspace on QWERTY devices) to cycle through the different screen modes. Release the button to set the desired mode. Your choice is now saved to gate6-<stem>.cfg in C:\
**- Save file persistence**: Your <save file> save file will stay untouched in case you want to uninstall the game or update it to a new version. It is always stored in C:\system\apps\<stem>
**- Game files location**: in !:\system\apps\<stem> (where "!:" can be either C:\ or E:\, depending on where you decided to install the game).

Please report any bugs you find. Happy <verb>ing!🙂
```

| title | stem | cfg | save file(s) |
|---|---|---|---|
| Asphalt Urban GT | `6r67` | `gate6-6r67.cfg` | `user.dat` |
| Asphalt 2 | `6rbc` | `gate6-6rbc.cfg` | `user.dat` |
| Ashen | `6r21` | `gate6-6r21.cfg` | `savegameNN.sav` (progress), `options.dat` (settings) |
| One | `6r58` | `gate6-6r58.cfg` | `6R58.prf` (the fighter profile), `6R58.set` (settings) |
| Colin McRae 2005 | `6r66` | `gate6-6r66.cfg` | `cmr_save_info.dat`, `gameinfo.dat` |

One 022's post (round 149) is `One-build022-post.md` in the session scratchpad: it says 021 could not start on an install without the card dump's `6r58.app`, and that 022 runs from the package alone.

Later builds of a title say what changed since the last post instead of
"First public build"; the feature lines stay, since every build carries them.

## A zip release (the files and the loader)

For a file manager or a card reader: the `system` folder, the loader-only SIS
(`build_release.py --split`; it stops at the data package when the title has
no `GAME_DATA_UID3`, after the loader is built), and a README, as One 022-028
and Colin McRae 002 shipped. **Stage the folder with `stage_zip.py`**, which
copies build_release's own file list, so the zip is the bundled package's
layout byte for byte. One's 021 zip was laid out by hand, never run, and
quit on every phone without the dump's `.app` (BUGBOOK 12.ae). Then run it
on a bench emptied of the title, its saves and any leftovers: the loader
installed, the folder copied by hand, no `<stem>.app` anywhere (Colin:
E1028 the drive on E:, E1029 the data on C: with the loader on E: and QUIT,
E1030 an app switch). Check the zip by extracting it and diffing it against
the tree the bench ran.

## Before any build goes out: the package itself

`python3 toolchain/port/sischeck.py <package.sis>` must exit 0: both checksums
right and, for a package whose exe asks for a capability (`GAME_CAPABILITIES`),
a self-signature openssl verifies. A phone refuses an unsigned package that
asks for a capability (round 169); the emulator installs anything.

## Before any build goes out: the phone's handle rules

Run the title once under `EKA2L1_STRICTHANDLE=2` and `EKA2L1_KERNREQ=1` (exported
before `emurun.sh` or `runinstalled.sh`) through start, keys and quit, and see no `BAD HANDLE` in
the log. EKA2L1 answers a stale, foreign-thread or garbage handle with an
error; a phone panics the thread KERN-EXEC 0. Colin McRae build 002 passed
thirty lenient bench runs and died at launch on the N95 on two of them
(round 161, BUGBOOK 13.x). `emurun.sh` now logs them on every run (=1) and
prints the count.

`EKA2L1_KERNREQ=1` completes requests as EKA2's kernel does, the status word
only; the emulator's own completion also cleared the pending bit in the word
after, and Colin build 003 took no keys on the N95 because of it (round 162).
`emurun.sh` sets it by default.
