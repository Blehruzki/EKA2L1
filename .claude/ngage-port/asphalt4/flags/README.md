# Build flag sets

`CNN.flags` is the `A4_EXTRA_CFLAGS` of round NN's candidate. Build:

    A4_THIRDSTEP=1 A4_SCRATCH_SIZE=0x1A0000 A4_EXTRA_CFLAGS="$(cat flags/C74.flags)" \
      python3 tools/injector.py <handoff baseline asphalt4_ngi.exe, sha256 4baef63a...> out.exe --gl --capture

C74.flags was rebuilt from MBX.md after the scratchpad was lost (round 74) and proven by
reproducing a4_r74_cand.exe byte for byte (sha256 ef2a44ee9e0ce657f9430b24e14d3f510b8071bf2fa4c5766482eafe5fb67d03,
940,352 bytes; clang 18.1.3, this tree at 35f828b). Keep each round's set here, not in a scratchpad.

C75.flags = C74.flags (no flag added: round 75's change, the contact spin spread over the step, is code in the 3x
engine -- a4_eng.h eng_spin, edits.py +23BC6 / +23C06 / +1971E). a4_r75_cand.exe, sha256
ae84c26a0e4ebdb7c324f709b75acb7a7c213c657bdd3efa2304236e37c879c2, 941,372 bytes (the spread keyed by the contact pair;
the first build, keyed by the car alone, was 92663fb7..., 941,952 bytes).

C76.flags = C75.flags + `-DA4_FRC3 -DA4_TELMARK` (round 76: a telemetry build, no game change -- the owner's marks
on the C key, C:\a4markN.dat, and FRC3 beside FRC1; tools/frec3.py reads both). **Its scratch is larger:
`A4_SCRATCH_SIZE=0x1B9000`** (the mark state, FRC3, the sprite list and the list's tiles at 0x5B0000-0x5B9C68;
injector.py refuses less). a4_r76_cand.exe, sha256 4601bd18a5321c5a3250894c1ca51d495e251d1650b2a9c78bd49a7543ae4303,
947,388 bytes. Bench only, never in a phone build: `-DA4_BENCH_AUTOMARK` (a mark asked for 40 renders into a crash /
takedown or news camera).

C77.flags = C76.flags + `-DA4_WRECKFRAC -DA4_BBMARGIN -DA4_LINEALL -DA4_LINEREC` (round 77), scratch `0x1C0000`
(A4_LINEREC's stream row edges at 0x5BB000-0x5C0280). Bench only: `-DA4_LINEMARK` (a mark at each line caught),
`-DA4_LINETEST`, `-DA4_WRECKPROBE`. a4_r77_cand.exe, sha256 4322b80f1592032394fa3df963fe075daa6885b17bc8a70a489e72942c85f583,
969,132 bytes (A4_THIRDSTEP=1 A4_SCRATCH_SIZE=0x1C0000). **Round 77
also lifts the car draw +D3BA (renderer/eng/funcs.txt), and lifted.c is generated with the flag set's hooks** (tools/
lift.py routes +D3BA's bl +A434 through A4_FRC3's hooks): regenerate it with the build's own flags, and to rebuild an
earlier candidate byte for byte take that commit's lifted.c too (r75 / r76 from 931b807's tree).

C78.flags = C77.flags with `-DTEL_EVERY=230` (round 78: a capture every ~10 s, each one stalled the phone ~80 ms);
scratch `0x1C0000`. a4_r78a.exe (C78 + the composited rows of the GPU objects and cars), sha256
1af6c4c5b3e527e9dd06775372a632a725aa9ba32f0d1c0108af32e89232483a, 969,416 bytes. Then `-DA4_COVREDRAW` (the pickups, the searchlight's blocks and the car's shadow drawn over the
road, the cars and objects that cover them drawn again): a4_r78c.exe = the r78 candidate, sha256
a45148853e48e293fcca0e9d29342af64891d4a063b9def79b8af4961c0ef608, 975,180 bytes. Bench only: `-DA4_ROWCHECK`.

C80.flags = C78.flags with `-DA4_LINEFILL` (round 80: the grey line's row filled from its neighbour at the composite).
r80 candidate sha256 da090fd0cc324347f29d142d39205c044dcb46ac3f59eb6aa508e59b496c34a7, 976,784 bytes. Bench only: `-DA4_LINETEST` (blanks screen row 120: the fill's test).

C82.flags = C80.flags with `-DA4_CLIPG` (round 82: the game's triangles with a corner more than 1,024 px off the panel cut to
that band on the CPU, perspective-correct -- renderer/a4_clip.h, tools/cliptest.c). The r82 candidate (also: 3-6-row gaps
recorded, automatic marks in a race) was r82d, sha256 b20c7d9b75a4cadc767d6094ee06916023264c131ea22620cc95e286b37f683d, 982,020 bytes;
now r82e (same flags; the list's faces, the GPU's objects and cars that would reach beyond the band handed back to the game, CRK1
40 words) sha256 f41865b5085e720d9236791e33371cc40c23892f3381b4f9d04004757f40809c, 982,724 bytes; now r82g (same flags; the game's
projection wrap undone before the cut, the exact band tests, q carried by the clipper, CRK1 48 words and the CUT1 ring)
sha256 1ed9c80aca9aa978205dc5c53023f52e6b50210f3ba2b650167e4c4ce570a45d, 985,184 bytes. Bench only: `-DA4_LINETEST3`,
`-DA4_LINEFILLVIS`, `-DA4_WRAPPROBE` (CRK1 [40-44]).

C83.flags = C82.flags without `-DA4_LINEALL` and `-DA4_LINEREC` (round 83: the line counter back to every 16th frame,
no line records -- lte_take's per-triangle copies and LIN1 gone; the crack fill stays, every frame, and skips the
drawn-mask pass on rows the ARMv6 composite saw fully drawn). r83a: sha256 457c20abeb530d2d..., 983,292 bytes. Bench
only: `-DA4_AUTOMARK` (the automatic marks at a 64 px gap), `-DA4_ABCYCLE17` (the skip by window), `-DA4_MASKCHECK`,
`-DA4_MSKIP_ALT` / `-DMSKIP_PAR`, `-DA4_MSKIP_NEVER`, `-DA4_MSKIP_PROF`, `-DA4_PROFSYM`.

C84.flags = C83.flags with `-DA4_SHADBATCH` (round 84: the car shadow's faces drawn on the GPU in one batch a depth
bucket -- a4_gl.c shb_*, injector.py's +27402 hook; scratch 0x5BA000-0x5C0280 under the same `A4_SCRATCH_SIZE=0x1C0000`).
r84b sha256 67b12d53..., r84c (the two-scale blit no longer composites; the shadow insert held to race frames)
d72aa53aaedaaade..., 987,576 bytes. Bench only: `-DA4_SHADCHECK`, `-DA4_SHADCOUNT`, `-DA4_SHADPROBE`, `-DA4_CARPROBE`
/ `2` / `3`, `-DA4_COMPALL`, `-DA4_CARSOLO`, `-DA4_CARNOCULL`, `-DA4_CARNODEPTH`, `-DA4_CARFLAT`, `-DA4_CARLATE`,
`-DA4_CARTRI`, `-DA4_CARSTAGE`, `-DA4_OBJFLAT`, `-DA4_FLUSHPROBE`, `-DA4_BB2FLUSH`.
Round 85 builds with C84.flags (no flag added: the fix is code in the 3x engine -- a4_eng.h ENG_BEGIN, edits.py
+14574): a4_r85a_cand.exe sha256 c911262fd468d7369620817e3e17a5940d66bef85b53b339cd70566ab8ca4199, 987,636 bytes.

C86.flags = C84.flags with `-DA4_PHTIMERS` (round 86: phone telemetry -- CRK1 [52..63] the scene's blocks in ticks, 'TIM2'
the GL calls by class and block; still `A4_SCRATCH_SIZE=0x1C0000`). r86c (without TIM2) sha256 840b0eca..., 991,796 bytes;
r86d (TIM2) 996,340 bytes, not sent. Bench only: `-DA4_GOBJ86CHECK`, `-DA4_GOBJINSSORT`, `-DA4_CAREARLYCHECK`,
`-DA4_OBJWHY`, `-DA4_BLENDCEN`, `-DA4_PROFSEG=lo -DA4_PROFSEG_HI=hi`.

C87.flags = C86.flags with `-DA4_FRC4 -DA4_GOBJNG=20u` (round 87: 'FRC4', each render's split written into every capture;
the GPU objects take meshes of up to 20 texture groups, 1,440 meshes held, the take refusing an object whose groups
would pass the flush's 768). **Its scratch is larger: `A4_SCRATCH_SIZE=0x1C3000`** (FRC4 at 0x5C1000-0x5C3070).
r87a candidate sha256 2e1c0ae56f67a3a5..., 998,188 bytes. Bench only: `-DA4_NGWHY`. C86.flags still reproduces r86d
byte for byte with this tree.

C88.flags = C87.flags with `-DA4_NITROBLUR` (round 88 step 1: the nitro radial blur +1054C, exact, run after the
composite at +12A12). Bench only: `-DA4_BLURCHECK`, `-DA4_BLURFORCE`, `-DA4_BLURI=n`, `-DA4_BLURC`.

C89.flags = C88.flags with `-DA4_GPUSKY -DA4_FLAREFLUSH` (round 88 step 2: the sky on the GPU, its routine's stores kept,
the blended copies within its rows skipped; the frame composited before the sun's flare +37FA4). r89a candidate
sha256 22089b745913f4c9..., 1,003,248 bytes (not sent). Bench only: `-DA4_SKYCHECK`, `-DA4_SKYTRACE`,
`-DA4_SKYTRACE_EVERY=n`, `-DA4_SKYCHECK_MARGIN=n`, `-DA4_SKYGATE`, `-DA4_SKYSTORESKIP`, `-DA4_M1WHERE`, `-DA4_DEDUPCEN`,
`-DA4_PROFSYM`.

C90.flags = C89.flags with `-DA4_SHADFAST` (round 88: each shadow face's batch record straight from the shadow list,
exact). Bench only: `-DA4_SHADFASTCHECK`. Same scratch as C87 (`A4_SCRATCH_SIZE=0x1C3000`).

A4_CARANIM (round 88, after r90b; for the next candidate, not in C90/C91): the player's car mesh as +1B084 animates it
each render -- the tyres' two frames (the spin), the front wheels' steer and a bounce -- which A4_GPUCAR's bake froze.
Needs lifted.c with the +DCE2 hook (regenerated; the macro is empty without the flag, and C90/C91 rebuild to r90a's and
r90b's bytes). Bench only: `-DA4_WHEELCEN` (the live mesh against the bake at every take, CRK1 [40..47] and CUT1's
place), `-DA4_WHEELGATE` (opens EKA2L1_WATCHGATE's word 0x5C30F0 at the census's first difference).

C92.flags = C91.flags with `-DA4_CARANIM -DA4_M1LIVE -DA4_PAINTFWD -DA4_SNOWFAST` (round 88, after r90b: the player's
tyres spin, the car paint's reflection made again after each composite (paced by its cost; TIM2 [40..48]), the
painter's forward rows, New York's snow in C with clipped flakes skipped). Same scratch (`A4_SCRATCH_SIZE=0x1C3000`).
r91a as sent: sha256 a9006828e0088011..., 1,015,124 bytes (the pace tightened; the first build was 352030fb77f498c0...). Bench only: `-DA4_WHEELCEN`, `-DA4_WHEELGATE`,
`-DA4_PAINTCEN`, `-DA4_PAINTCHECK` (counts also in TIM2 [50..57]), `-DA4_SNOWCHECK` (scratch 0x220000),
`-DA4_SNOWMUTANT`, `-DA4_SNOWREC`, `-DA4_SNOWGATE`, `-DA4_BLURFAST2` (exact, no gain on the model: not in C92).

C93.flags = C91.flags with `-DA4_CARANIM -DA4_M1SKY -DA4_PAINTFWD -DA4_SNOWFAST` (round 88, after r91a's phone round:
the paint's reflection on the GPU with no texture changed during a race -- A4_M1SKY replaces A4_M1LIVE, whose uploads
cost the phone 18 ms a refresh). Same scratch (`A4_SCRATCH_SIZE=0x1C3000`). r91b candidate sha256 d81b952798606cc4...,
1,021,728 bytes; C90 / C91 / C92 rebuilt from the same tree give r90a's, r90b's and r91a's bytes. Bench only:
`-DA4_M1SKYCEN` (TIM2 [48], [50..59]: the faces against the game's map, why the half sky is made again, the
registrations, how each frame's mapping went).
After r91b's round A4_M1SKY reads the frame itself (each composite's top rows into a fresh texture) and imports
glDeleteTextures (ordinal 47) with the flag: C93 now builds the r91c candidate, sha256 eb1526dbf07bc0a0..., 1,020,896
bytes (r91b was 539751517cf14cb7... from commit 78e4dc3). Bench only: `-DA4_M1SKYCEN`, `-DA4_M1SKYMUT` (the capture all
white, then all black, a render each).

C94.flags = C93.flags with `-DA4_CARNEARBOX` (round 88, after r91c's round: the owner, "reflections don't work when going
reverse ... no reflections there" on the front windshield). The GPU cars' near-plane test on the mesh's box
(GCBND, grown with the animated corners at each take) instead of its bounding sphere: reversing, the camera at the car's
front came close enough for the sphere to reach the near plane though no vertex did, the game drew the car with the
paint baked once, and A4_M1SKY's live reflection never ran. Same scratch (`A4_SCRATCH_SIZE=0x1C3000`). r91d candidate
sha256 d8d6da2eea769e66..., 1,021,108 bytes; C90 / C91 / C92 / C93 rebuild to r90a's, r90b's, r91a's and r91c's bytes.
Bench only: `-DA4_BENCH_REVERSE=<step>` (`_LEN`; with A4_CARREC, A4_AUTOPILOT and A4_AUTOPILOT_TAP: the brake from that
step, the car reverses), `-DA4_CARWHY` (the player's car takes by outcome, CRK1 [40..47]), `-DA4_M1SKYFACE` (the paint's
faces and the reflecting ones by half, CRK1 [40..47] / TIM2 [50..55]), `-DA4_M1MAPCHG` (the map's pointer and a sampled
hash each render, CRK1 [40..45]), `-DA4_M1STINT` (the paint's reflecting texels green).

C95.flags = C94.flags with `-DA4_M1HOOK` (round 89, after r91d: the owner, "It's not fixed ... It needs to work on all
player's vehicles where applicable. I tried this time with the mini cooper"). When the camera is nearer the car than the
GPU's near plane (the Mini's front in reverse), the game draws the car itself through the hook; its paint went from a
var-1 bake against a table with no picture in it (flat light blue). Now a paint triangle there is matched to its face
(a hash of the paint's faces by their texture points), drawn as var 3, and the reflecting ones drawn again after the
batch over the last capture at the face's own points -- the GPU car's pass on the game's own triangles. Every big
mode-1 paint of the player's mesh now takes part (a bike's second texture), not only the first. Same scratch
(`A4_SCRATCH_SIZE=0x1C3000`); the arena gives the registration ~19 KB more (the hash, the batch's points and list).
C90 / C91 / C92 / C93 / C94 rebuild to r90a's, r90b's, r91a's, r91c's and r91d's bytes. Telemetry: TIM2 [37] paint
triangles matched, [38] of them reflecting, [39] second passes, [59] paint triangles matching no face (frec3.py timers).
Bench only: `-DA4_M1PDCEN` (the player's mesh's mode-1 groups with a texel to reflect, in the pass or left out and why,
CRK1 [40..47]), `-DA4_M1TINTR` (a bake's reflecting texels magenta: a reflection drawn from a bake, not the live pass).

C96.flags = C95.flags with `-DA4_GPUCHECK -DSTART_AFTER=1u` (round 89: the owner, "check at startup which graphics chip
is present ... Also make it so that the gpu kicks in from the first moment"). Bring-up asks the GL for its renderer
(glGetString, libgles_cm ordinal 70 -- read from the RM-320 ROM's own code -- imported at a4_gl.s [47] with the flag)
and runs the GPU path only on a PowerVR ("MBX" / "PowerVR" in the name); otherwise the game's own renderer for the
session. A failed bring-up is tried again after 20,000 triangles instead of at every one. The GPU from the first triangle
the hook sees (the garage's car in the menus included) instead of the 50,000th. And `-DA4_GLDYN` (the owner: "if it has
no opengl es library at all, shouldn't just fall back to software rendering?"): no libgles_cm import -- the blob loads
the library at its first triangle with the game's own RLibrary::Load and resolves a4_gl_iat's 48 ordinals with
RLibrary::Lookup into a RAM table (0x5C3800); a library or ordinal missing leaves the table empty and the game's own
renderer draws the session. Same scratch. Bench only: `-DA4_GPUCHECK_NOPVR` (the refusal forced), `-DA4_GLDYN_FAIL`
(a library name no phone has), `-DA4_GLDYN_MISSORD=<slot>` (that slot asks ordinal 999: a library without the function).

C97.flags = C96.flags with `-DA4_BLDEFER` (round 89, toward 30 fps: the owner, "So what's left to get 30 fps?" ...
"go with that plan"). The see-through copies the game draws in software below the GPU's sky -- a tree passing the
camera, ~170 rows, up to the whole screen; the profile's biggest game routine, the alpha span +33EA8 at 4.3% of the
bench frame -- are listed instead of drawn at their turn, with a copy of what the game's path reads, and after the
composite the game draws them again from that copy only on the rows where a pixel stayed undrawn (the span calls
+2E826/+2E870/+2E8B8/+2E8EA and +2F612/+2F65C/+2F6A4/+2F6D6 filtered, each such row kept and put back where the GPU
drew). A billboard's blit or an anti-aliased line (CFakeZAALine3DGradient's +2BC9A, its calls +333A8/+333E2
hooked) over a listed copy has the list drawn first on its rows (a band); a triangle left to software, the two-scale
blit, a full list or no composite draw it whole. **Scratch `A4_SCRATCH_SIZE=0x1C8000`** (the list and its bands at
0x5C4000-0x5C8C28); its other state at 0x460000-0x463C80. Telemetry: CRK1 [40] copies listed, [41] drawn again after
the composite, [42] rows drawn again (high half: rows drawn in bands), [43] drawn whole early, [44] lists dropped (high
half: copies drawn in bands) (frec3.py mode `defer`). Bench only: `-DA4_BLDEFERCHK` (built without `-DA4_BBPROBE`,
`A4_SCRATCH_SIZE=0x23E000`; frec3.py mode `deferchk`, scratchpad py/dfk.py for its notes at 0x412D00): each copy drawn
into a cleared screen of its own at its turn and again from the list's copy, compared (CRK1 [45]); hole pixels under
a copy changed after its turn ([46]); pixels under a copy changed after its turn before an early draw or band ([47]);
the holes against what the composite and fill wrote, and the span's writes outside the columns kept (notes [119],
[121]) -- all must stay 0. `-DA4_DEFERGATE` (EKA2L1_WATCHGATE's word, 0x5C30F0, open while copies are listed: with
EKA2L1_WATCH over the surface, what else writes then). `-DA4_AALINEALL` (every line draws the list first).
`-DA4_COPYCEN` and `-DA4_DEFERCEN` (the censuses that led to it, CRK1 [40..47]).

C98.flags = C97.flags with `-DA4_SKYSTORESKIP` (round 89): the sky routine's stores (the top fill, the band's three
memmoves +36124 / +36152 / +3615E, the bottom fill) recorded and not run in a GPU-sky frame -- the composite overwrites
those rows with the GPU's backdrop. A mode-1 bake about to read the back buffer as its table has them put first
(sky_due; census 0x412D00 [75] puts, [76] replays before a GPU frame; tools/skychk.py). Scratch as C97
(`A4_SCRATCH_SIZE=0x1C8000`). Bench only: `-DA4_SKYRGATE` (EKA2L1_WATCHGATE's word 0x5C30F0 open from the first
recorded sky store to the composite: with EKA2L1_RWATCH over the surface, every read of the skipped rows);
`-DA4_SKYPUTCHECK` with `-DA4_SKYCHECK` (on C96 + the skip, not with A4_BLDEFER: both keep state at 0x460000): the
stores put at each GPU frame's start and compared with the GPU's backdrop ([51] must stay 0); `-DA4_SKYPUTMUTANT` its
test (a band row left out every 64th frame).

C99.flags = C96.flags with `-DA4_CARDEFORM -DA4_BBREDRAWFIRST` (round 89, the owner's r91f bugs; not C97/C98's work):
A4_CARDEFORM hashes each car mesh's vertices at its bake and at every take (the 24 tyre vertices out) and gives a moved
mesh to the game for that render (the hashes at 0x4665A0, between GCBND and GCMESH); A4_BBREDRAWFIRST has a sprite over
items pending the cover redraw draw the batch and the redraw first. Scratch as r91f (`A4_SCRATCH_SIZE=0x1C3000`).
Bench only: `-DA4_CARDEFORMCEN` (CRK1 [40..45]), `-DA4_WHEELDESC`, `-DA4_WHEELSHOW` (with WHEELDESC), `-DA4_SPINCEN`,
`-DA4_ANIMTINT`, `-DA4_CARITEMTINT`, `-DA4_CVRTINT`, `-DA4_SHADTINT`, `-DA4_CARMESHLOG` (MBX.md round 89).

C100.flags = C99.flags with `-DA4_CARTWIN` (round 89, the owner: "the front wheels are spinning. But the back wheels
dont"): every car mesh's bake leaves out each face lying on a face of lower index (the same corners at rest, the same
winding) -- the copy the game's table always draws first and covers. The 350Z and the 599 GTB carry ~800 faces twice
(the tyres' still twins among them); a few faces on other models. The 12 KB table lives in the objects' staging. Scratch
as C99 (`A4_SCRATCH_SIZE=0x1C3000`). sha256 78c7906003fad043..., 1,029,056 bytes. Bench only: `-DA4_TWINCEN` (CUT1's
place 'TWN2'; not with A4_TYREMAP), `-DA4_TYREMAP` / `-DA4_TYREMAP2` ('TYR1' / 'TYR2': the tyres' faces),
`-DA4_TYRETINT` (with A4_ANIMTINT: static faces on a tyre descriptor green), `-DA4_FRAMETINT` (with A4_ANIMTINT and A4_WHEELDESC: the animated
tyre faces coloured by the frame -- 1 magenta, 2 violet, 0 blue; the reflection pass after them untinted), `-DA4_SPINCHECK`
(with A4_FRAMETINT: each wheel's hub projected at the car draw and its square read after the composite -- CUT1's place
from [90], 'SPC1' at [100]; build with `-UTEL_EVERY -DTEL_EVERY=40` so a capture falls after a bench drive; readers
spc.py / spv.py in the scratchpad).

C101.flags = C100.flags with `-DA4_LAMPFIX` (round 89, the owner on r91g: the Z1000's brake light): the Z1000's model
(carsb.bar Cars\Z1k.obj) lists no lamp vertices, so the six lamp words at car+80 are all vertex 0 and its brake glow
sat inside the bike; a car whose six are 0 and whose mesh is the Z1000's (516 vertices, 810 faces, four vertices where
its file has them) is given 359 359 388 436 388 436 (head lamp, tail lamp). Scratch as C100. sha256 fb7c27f7a7b7d531...,
1,029,432 bytes. Bench only: `-DA4_LAMPSCAN` (CUT1's place 'LMP1': the player's car object scanned for its lamp words).

C102.flags = C101.flags with `-DA4_SHADZFAR=3` (round 89, the owner on r91h: the Z1000's shadow "in pieces", four photos
of five): a shadow face only GPU items cover (round 78's rule: drawn over everything with its own depth, those items
drawn again over it) whose box meets none of its bucket's car candidates is nudged ZK >> 3 (1/8) nearer instead of
1/64, so a GPU road standing above the shadow's plane no longer lands over it; a face a car may cover keeps 1/64.
Scratch as C101. sha256 8e46cee60bad67ec..., 1,029,800 bytes. Bench only: `-DA4_SHADZSH=n` (the 1/64 nudge's shift).
