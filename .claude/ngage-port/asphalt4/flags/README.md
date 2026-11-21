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
