# EKA2L1 (fork)

## Scope

**This repository is the EKA2L1 emulator. That is the default and only subject of
work here.**

`.claude/asphalt2/` holds reverse-engineering notes and tooling for patching a
Symbian game (Asphalt 2: Urban GT). It lives here purely so it survives, and it
is **not part of this project**. Ignore it entirely — do not read it, cite it,
or let it steer emulator work — unless the current task is explicitly about that
game. If the task is about EKA2L1, that directory does not exist.

This is a personal fork. Do not open pull requests, issues, or any other
contribution against the upstream repository, and do not push anywhere but this
fork's branches. Changes stay local unless asked otherwise.

## Build

Configured with CMake, `RelWithDebInfo`, into `build/`; the Qt frontend lands at
`build/bin/eka2l1_qt`. System dependencies that are not vendored and must be
present: SDL2, and Qt6 `LinguistTools`, `Svg`, `Network` and `OpenGLWidgets`.

## Gotchas

- **Translation churn.** Qt's `lupdate` regenerates all 22 `.ts` files on every
  build, so `git status` is dirty after any compile. Revert them; never commit
  them as part of an unrelated change.
- **Running the emulator headless.** It needs a display; `Xvfb :99` works. It
  chdirs to `~/.local/share/EKA2L1/`, so a relative `storage` setting resolves
  there rather than the working directory.
- **Killing it.** Match the process exactly (`pkill -x eka2l1_qt`). A pattern
  match on the command line (`pkill -f`) also matches the shell that launched it.

## Known gaps in the emulator

EKA2L1 accepts SIS packages that a real device rejects, because it verifies none
of the integrity fields a device checks: the per-file SHA-1 in each
`SISFileDescription`, the E32 image header CRC32, `SISControllerChecksum` /
`SISDataChecksum`, and the package signature. Anything validated only against
this emulator may still fail on hardware. Making the installer check these would
be a genuine improvement, and is unimplemented.
