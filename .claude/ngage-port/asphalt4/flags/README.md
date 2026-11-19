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
