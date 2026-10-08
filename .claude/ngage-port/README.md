# The N-Gage port

N-Gage (Symbian 6.1, EKA1, GCC98r2) titles running on S60v3 phones (Symbian
9.x, EKA2, EABI) without recompiling them: a loader maps the original image in,
resolves its imports against the 9.x ROM, and a shim stands between the two
ABIs wherever they disagree -- vtable shapes, calling conventions, double
word order, VA_LIST, the classes the framework builds for the game. Three
titles so far, each with a hand-written `game.h` of knobs over the shared
loader.

> Not part of EKA2L1. It lives in this fork so it survives; the emulator's own
> CLAUDE.md at the repository root says to ignore it unless the task is this port.

| what | where |
|---|---|
| the working agreement, the four rules, the source ranking | `CLAUDE.md` |
| the loader and shim, the per-title layers, the tools | `toolchain/port/` (its own `README.md` is the gate-by-gate history) |
| ngtest: an N-Gage test app built with the period SDK (scratchpad, not here), run through the port to measure the platform -- streams, Stop, workers, timeslices | `toolchain/ngtest/` |
| every knob and fix, per title | `KNOBS.md` |
| what differs per S60 edition and per phone | `DEVICES.md` |
| every bench run and hardware round, in order | `ROUNDS.md` |
| what went wrong and why, as a reference | `BUGBOOK.md` |
| Symbian facts, from the sources | `SYMBIAN.md` |
| the long-form narrative of the port | `PORTING.md` |
| the post that goes out with a build | `RELEASE.md` |
| the public write-up of the whole project, for the end (style reference and outline) | `WRITEUP.md` |
| shipped packages (loader-only ones tracked; the data-carrying ones are not) | `build/` |
| screenshots cited by the record | `shots/` |
| the first week's raw phone logs, cited by PORTING.md | `logs/` |
| the N-Gage import, vtable and shim tables the early gates measured | `ngage-*.txt` |
| the Asphalt 2 N-Gage image used for ordinal checks | `crack/` |
| **historical, unrelated to the port:** the Asphalt 2 S60 HUD patch this directory began as | `asphalt2-s60-patch/` |

Titles: Asphalt Urban GT (`6r67`, `gate6a1`), Asphalt 2 (`6rbc`, `gate6`),
Ashen (`6r21`, `gate6ashe`). A new one starts with `toolchain/port/newgame.py`.
