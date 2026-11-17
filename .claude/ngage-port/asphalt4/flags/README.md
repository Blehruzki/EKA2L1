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
