# Reference index: external Symbian docs/sources (consult every round)

User supplied 4 links (2026-08-23) to help this investigation. This file
exists because earlier rounds consulted only one of them, once, then
stopped. **Check this file at the start of each new sub-investigation
thread and add a row whenever a source gets used for something new.**

## 1. Real Symbian OS source (archive.org, Mercurial bundle) — LOCAL CHECKOUT

- **Local path**: `investigation/ngage-backup-hang/symbian-src/ossrv_repo/`
  (18463 files, `hg`-checked-out working tree; `ossrv.hg.bundle` also kept
  alongside as the raw 88MB bundle).
- **What it is**: the actual Symbian Foundation open-source release
  (`genericopenlibs`, `genericservices`, `lowlevellibsandfws`, `glib`, etc.)
  — real, historical Nokia-authored C++ source, not reconstructed/guessed.
- **Confirmed used for**: `!BackupServer`/`CBaBackupOperationNotifier`
  investigation (round 102) — found `babackup.cpp`, `backup_std.h`,
  `Baksrv.h` in `lowlevellibsandfws/apputils/`, confirmed the `KErrCancel`
  guard bug exactly matches disassembly-derived rounds 57-58.
- **Confirmed NOT containing**: `playserver` (searched full tree,
  2026-08-24, zero hits) — PlayServer is N-Gage 2.0's own proprietary
  game-engine component, not part of the Symbian Foundation's
  open-sourced stack. This tree can't explain PlayServer's own internals,
  but can still explain generic OS mechanisms it relies on (CActive,
  CServer2/RSessionBase, RThread/TRequestStatus semantics, panic
  categories, HAL/property behavior).
- **How to search it fast**: `grep -ri <term> investigation/ngage-backup-hang/symbian-src/ossrv_repo --include=*.cpp --include=*.h`
  (already proven fast, ~18K files).
- **Still unexplored areas potentially relevant to open threads**: generic
  `CActiveScheduler`/`CServer2` implementation (if present in this tree -
  not yet checked) for reference semantics on what "correct" spin-free
  active-object code looks like, to compare against playserver.exe's
  observed spin.

## 2. docs.symbianrevive.xyz — Symbian^3 Application Developer Library

- **What it is**: a hosted mirror of the Symbian^3-era API reference
  (frameset-based old-style SDK docs: `index.html` + `index-toc.html` left
  nav tree + content pages loaded into a frame).
- **Fetched so far**: only the landing frame (2026-08-24) — the actual
  class/API reference pages are separate files under this same host,
  not yet individually fetched.
- **Relevance**: Symbian^3 postdates N95/S60 3rd Ed FP1 (this device), so
  APIs may differ in details, but core kernel/active-object semantics
  (`CActive`, `RThread`, `TRequestStatus`) are stable across these
  versions and this is a legitimate, readable reference for them.
- **When to use**: if `docs.huihoo.com` (below, the exact-version SDK) is
  down/incomplete for a given class, this is the fallback.

## 3. mrrosset.github.io/Symbian-Archive — community archive/link index

- **What it is**: a curated links page (not primary documentation itself)
  pointing to a Mediafire archive (`freaks_r_us` collection), an
  `akawolf.org` full mirror of the Symbian Foundation Wiki + Mercurial
  repo, and assorted SDK/tool downloads collected post-shutdown.
- **Fetched so far**: only the landing page (2026-08-24, 7 sections, all
  just the intro/thanks/missing-items text) — no sub-pages fetched yet.
- **Relevance**: potential source for SDKs/tools not otherwise available
  (e.g., if a specific S60 3rd Ed FP1 SDK or N-Gage SDK/PDK were ever
  published, this is the kind of place it'd be indexed). Not yet checked
  for anything PlayServer/N-Gage-specific — worth a targeted look if the
  proprietary-source angle becomes worth pursuing.

## 4. docs.huihoo.com — S60 5th Edition C++ Developer's Library v2.1 (Symbian OS 9.4)

- **What it is**: the full official SDK API reference for S60 5th
  Edition/Symbian OS 9.4 (same frameset style as source 2, TOC iframe +
  content frame).
- **Fetched so far**: only the landing frame (2026-08-24) — same
  situation as source 2, individual class docs not yet fetched.
- **Relevance**: closest publicly-documented version above N95's actual
  OS (Symbian OS 9.2/S60 3rd Ed FP1) — most likely candidate for
  authoritative `CActive`/`CServer2`/`RThread`/panic-code reference pages
  if/when needed for the CPU-spin thread.
- **How to pull a specific class page**: construct the URL under the same
  `GUID-.../sdk/doc_source/` path once the TOC (`index-toc.html`) is
  fetched and the target class's GUID is found - not yet done.

## 5. eka2l1.github.io — the emulator's own project homepage

- **What it is**: EKA2L1's official homepage/blog. Confirms it has a
  **Lua scripting API** specifically for tracing, debugging, analyzing,
  and patching specific applications/games at runtime
  (`/update/scripting/2021/10/30/Lua-script-API-documentated.html`).
- **Relevance**: this is a legitimate alternative to the
  edit-source/rebuild/relaunch cycle this investigation has been using for
  every new probe (rounds 104-110) - a Lua script could potentially
  inspect/log live state (thread status, memory, IPC) without needing a
  C++ rebuild each time. **Not yet tried this session** - worth using for
  the NEXT probe instead of another temporary `LOG_ERROR` + rebuild, if
  the scripting API exposes enough (check the actual API docs page, not
  yet fetched).

## 6. ebin.pub — "Symbian OS Internals: Real-time Kernel Programming" (Sales et al., Wiley)

- **What it is**: a book preview/download page for the definitive Symbian
  kernel reference (real-time kernel, scheduling, IPC, memory models,
  platform security, file server, loader, window server, kernel debug).
- **Fetched so far**: only front matter (title page, author bios,
  publisher catalog) and the full **table of contents** - the actual
  chapter bodies are NOT present in what got extracted (this is an HTML
  preview page, not the real 940-page book text; the TOC itself is still
  useful as a chapter/topic map). Relevant chapters for this
  investigation by page number: **3.6 Scheduling (p.98)**, **4 Inter-
  thread Communication** (4.1 Client-server ITC p.117, 4.2 Async message
  queues, 4.3 Kernel-side messages), **14 Kernel-Side Debug** (14.3 The
  kernel debug interface, 14.5 Stop-mode debug API, 14.7 Kernel trace).
  If the dynarmic-JIT-scheduling-interaction thread (rounds 107-109)
  needs real Symbian scheduling semantics again, the actual book (not
  just this preview) would need sourcing some other way.

## 7. docs.huihoo.com — S60 3rd Edition overview (GUID-3D0085C4...)

- **Fetched fully** (2026-08-24): turns out to be a short **general S60
  3rd Edition platform overview** (Symbian OS v9.1 base, new ARM EABI
  compiler, binary break from 2nd Ed, platform security/capability model,
  mandatory `.sis` signing) - **not** the WSERV/FBSERV client-server IPC
  architecture breakdown it was described as. Either the intended page is
  reachable via a link from this one (not followed yet), or the
  description referred to different page in this same doc tree. Flag this
  discrepancy if the user expected specific IPC content from here.

## 8. User-supplied N-Gage 2.0 architecture note (not a URL, direct info)

- **Claim**: N-Gage operates as a wrapper shell, dynamically loading
  dependencies from `C:\sys\bin\` and `E:\private\20007b39\` (the N-Gage
  private UID data folder).
- **Checked directly against this session's actual data mount
  (2026-08-24) - partially confirmed, and this is now a live lead**:
  `C:\sys\bin\` does contain N-Gage-related binaries (`NGageInstaller_
  0x2000A2AE.exe`, `playclient*.dll` family, `ngi*` files). **But
  `E:\private\20007b39\` does not exist at all** in this mount - only
  `E:\private\20007b38\` exists (the Installer/duplicate-caption UID).
  Confirmed via `find`. Meanwhile UID `20007b39`'s real save data (game
  saves, `Asphalt3`, etc.) actually lives under `C:\private\20007b39\`
  instead. If `playserver.exe` (backing UID `20007b39`) expects its own
  private folder on the memory card (`E:`) specifically and finds nothing
  there, that's a concrete, verifiable candidate for why it can't
  complete init - added a live probe (`PROBE_R110`, `fs.cpp:812` in
  `fs_server_client::entry()`) to catch the exact path(s) it checks and
  confirm whether this is really what it's looking for.
  **Result: theory not supported.** `playserver.exe`'s full `entry()`
  trace before each `KErrEof` self-kill is a fixed, generic sequence
  (`eikcore`/`eikpriv`/`eikcoctl`/`avkon`/`aknpriv`/`eikctl`/`eikcdlg`
  resource-file lookups with normal `.r01`→`.r`→`.rsc` localisation
  fallback, then `c:\resource\errrd`) - **identical to what
  `NgiInstaller[20007b38]` does too**, confirming (via the real Symbian
  source, `commonengine/src/textresolver.cpp:30,128`) this is
  `CTextResolver`'s unconditional `BaflUtils::FileExists` check for an
  optional rich-error-description pack - normal `CONE`/`EIKON`
  environment boilerplate every GUI app does, not a symptom of a prior
  error and nothing to do with `E:\private\20007b39`. `playserver.exe`
  never touches that path (or any N-Gage-specific path) at all before
  self-killing - the real failure happens somewhere non-filesystem
  (HAL/property/other IPC) right after this generic EIKON setup
  completes, not yet isolated.

## Discipline going forward

Before starting a new sub-investigation thread (e.g. "why does
playserver.exe spin"), grep the local `ossrv_repo` tree first (fast, free,
already proven decisive once), and only reach for sources 2/4 (fetch the
specific TOC + class page) if the question is about generic Symbian
API/kernel semantics the local tree doesn't cover. Log any new fetch as a
new row/update in this file so the next round doesn't re-fetch the same
landing pages from scratch.
