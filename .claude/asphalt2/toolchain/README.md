# Toolchain

Verified complete: a clean copy of this directory plus the four inputs below
rebuilds the shipped package **byte-identically**.

## Inputs (not included — they are the game)

| file | where from |
|------|-----------|
| `z_c5e5a984-.../Asphalt23D_...N73.sisx` | the untouched package |
| `lightbar_STOCK.bar` | its `light.bar`, extracted |
| `lightbar_S60v2.bar` | `light.bar` from an S60v2 package (only for the Fireboost flares) |
| `n73_full.img` | its `asphalt2_full.exe`, flate-decompressed, as `header + payload` |

`n73_full.img` must be the **full** image — the 156-byte header prefixed to the
decompressed payload — or every address is off by 156. EKA2L1's `flate::inflater`
is the only decompressor to hand; there is no compressor, which is why the patched
executable ships uncompressed.

## Build

```sh
BAR_Y=292 SPD_Y=310 SPD_X=212 UNIT_X=217 UNIT_Y=310 \
REV_SHIFT=3 REV_BASE=69 python3 patchexe8.py
python3 build_package.py
```

## Modules

| file | what it does |
|------|--------------|
| `sisrw.py` | SIS(X) reader/writer. Descends only the path it needs; everything else stays raw, so it round-trips untouched packages byte-identically — the test that validates any container change. |
| `sishash.py` | Locates the SHA-1 inside each `SISFileDescription`, and strips `SISSignatureCertChain`. |
| `e32crc.py` | E32 image header CRC32 (`Mem::Crc32`, seeded with `KImageCrcInitialiser`). |
| `barfile.py` | `light.bar` read/write. |
| `rleraw.py` | RLE sprite codec, types 0/1/2. No row flip — callers decide, per blit path. |
| `thumb.py` | THUMB assembler: the instructions needed for the patch set, plus `bl` offset encoding. |
| `scan.py`, `sweep.py` | Resilient linear THUMB sweep. Capstone halts at the first undecodable word; `sweep` restarts +2 bytes. |
| `compose7.py` | Slices the three dashboard layers out of `newhud.RLE` and stretches them 176 → 240. |
| `fireboost.py` | Lifts `Fireboostx1/2/3` from the S60v2 archive and scales them to 64×64. |
| `sheet3.py`, `dump.py` | Render any sprite to PNG — the fastest way to check a decode. |
| `patchexe8.py` | The ten executable patches. Env-parameterised geometry. |
| `build_package.py` | Assembles the `.sisx`: swaps both files, fixes lengths and hashes, strips the signature, recompresses the controller. |
| `swav.py` | N-Gage `SWAV` stream reader: header parse plus the IMA ADPCM codec (mono, **high** nibble first, state running unbroken through the file). |
| `imawav.py` | Writes 16-bit PCM back out as an IMA ADPCM `.wav` (tag 0x11). Keeps a track at roughly the N-Gage size instead of the 4x plain PCM costs. |
| `audiostring.py` | Repoints the music player from `intro.mid` to `intro.wav` — same length in both the text section and the SIS install path, so nothing shifts. |
| `build_v17.py` | `build_package.py` plus the intro swap: the third replaced file, its hash, and the controller rename. |
| `verify_pkg.py` | Re-checks a built package the way a device does: every install description's SHA-1 and both lengths against the data unit actually shipped. Run it on `v16` too — a known-good control. |

## Guard rails worth keeping

`patchexe8.py` asserts the original bytes at every patch site before writing, so a
wrong base image fails loudly instead of producing silent garbage. `build_package.py`
asserts the file-description walk agrees with the file-data walk, and that the hash
field is exactly SHA-1 sized. `e32crc.fix` asserts the CRC verifies after writing.
`verify_pkg.py` closes the loop on the whole package after the fact; note that the
last file description is a `FILENULL` (op 8) with no data unit, so only op-1
descriptions line up index-for-index with the data units.
