# Asphalt 2: Urban GT (S60v3) — patching reference

> **Not part of EKA2L1.** Parked in this fork so it survives. Ignore unless the
> task is explicitly about this game — see the scope note in the root `CLAUDE.md`.

Everything established while restoring the unused `newhud` dashboard to the
N73/N76/N93 build and getting it onto real hardware. Written so the next session
does not have to rediscover it.

Target: `Asphalt23D_nokiaN76_N93_ML_IGP_v1_0_0_Signed_N73.sisx`, app UID
`0x20008629`, executable `asphalt2_full.exe`, archive `light.bar`.

---

## 1. The headline lesson: EKA2L1 hides hardware-fatal defects

Four integrity mechanisms exist in a SIS package and its executables. **EKA2L1
checks none of them.** A package can install and run perfectly in the emulator
and panic the installer on a real device. This cost four failed hardware
attempts, all reporting `installer USER 130`. Each fix below was verified by
reproducing the stored value in untouched originals before being applied; the
signature strip (§1.4) is what finally installed.

| # | Field | Where | EKA2L1 | Status |
|---|-------|-------|--------|--------|
| 1 | SHA-1 per file | `SISFileDescription.hash` | ignored | **solved** |
| 2 | Header CRC32 | E32 header `0x14` | ignored | **solved** |
| 3 | Controller / data checksums | `SISControllerChecksum` (34), `SISDataChecksum` (35) | ignored | **unsolved** |
| 4 | Signature | `SISSignatureCertChain` (39) | ignored | **strip it — this was the blocker** |

### 1.1 SIS file hashes

Each `SISFileDescription` carries a SHA-1 of that file's **uncompressed** bytes.
A real installer re-hashes what it extracts and compares. Replacing file content
without rewriting this leaves a stale hash.

Verified: all seven hashes in the untouched package equal `sha1(uncompressed data)`.

### 1.2 E32 image header CRC

The executable header holds a CRC32 of itself at offset `0x14`. Any header edit
invalidates it — including setting `compression_type` (`0x1C`) to 0, which is
required when writing the payload back uncompressed.

Algorithm (recovered by matching untouched executables, confirmed on two builds):

```
hdr = header[0 : code_offset]          # code_offset is at 0x64; 156 here
hdr[0x14:0x18] = KImageCrcInitialiser  # 0xC90FDAA2
crc = mem_crc32(hdr)                   # raw reflected CRC-32, poly 0xEDB88320,
                                       # init 0, NO initial or final inversion
```

`Mem::Crc32` is *not* standard zlib CRC-32 — zlib inverts at both ends.

### 1.3 The two checksums — still unsolved

`SISControllerChecksum` and `SISDataChecksum` are the **first two fields in the
file**, so anything wrong there is read before every other field. They differ per
package, so they are genuinely computed, not constants.

Exhaustively searched and **ruled out**: all 65,536 CRC-16 polynomials × both bit
orders × 7 initial values × 2 xor-outs, over six candidate ranges (full field,
field without padding, body, deflate payload only, uncompressed controller,
body with padding), requiring a simultaneous match on two different original
packages. Zero hits. It is not a plain CRC-16 over any framing of the controller.

Consequence: every rebuilt package carries the original's stale values. The
package that installed successfully on hardware had stale checksums, so **the
installer evidently does not verify them** — worth knowing before anyone spends
more time on this.

### 1.4 The signature

**This was the one that mattered.** `SISSignatureCertChain` signs the controller
content preceding it, which includes the install block. Patching file hashes or
lengths invalidates it, and a hacked phone that accepts *unsigned* packages will
still panic on one carrying a signature that fails to verify — the installer died
with `USER 130` on every build that kept the block, and installed cleanly on the
first build without it. Strip it entirely and ship unsigned.

`SISController` parses `while (type == SISSignatureCertChain)` — zero or more — so
removal is structurally legal. Remove the field, subtract its total size
(header + body + padding) from the `SISController` length. In this package:
2428 bytes, controller 4916 → 2488, children become
`[SISInfo, SupportedOptions, SupportedLanguages, Prerequisites, Properties, InstallBlock, DataIndex]`.

---

## 2. SIS / SISX container

Layout: 16-byte header, then a `SISContents` field tree. Top-level children here
are `[34 checksum, 35 checksum, 3 SISCompressed(controller), 30 SISData]`.

**Field encoding**: `type(u32) len(u32) data`, padded to 4 bytes. If bit 31 of
`len` is set, a further `u32` high word follows. (`sisrw.py` also handles the
`0xFFFFFFFF` + u64 form seen in some writers.)

**Arrays** (type 2): `[u32 elementType]` then each element as `[u32 len][data]` —
**no per-element type header**. Getting this wrong is the classic first mistake.

**Field type numbering** (from EKA2L1 `sis_field_type`):
`1 String, 2 Array, 3 Compressed, 9 Uid, 12 Contents, 13 Controller, 14 Info,
15 SupportedLanguages, 16 SupportedOptions, 17 Prerequisites, 19 Properties,
21 Signatures, 22 CertChain, 23 Logo, 24 FileDescription, 25 Hash, 26 If,
28 InstallBlock, 29 Expression, 30 Data, 31 DataUnit, 32 FileData,
34 ControllerChecksum, 35 DataChecksum, 36 Signature, 37 Blob, 39 SignatureCertChain,
40 DataIndex, 41 Capabilities`

**`SISFileDescription` body order**:
`String target, String mimeType, [Capabilities], Hash, u32 op, u32 opOptions,
u64 len, u64 uncompressedLength, u32 index`

`len` is the **compressed** size; `uncompressedLength` the real one. The last 20
bytes of the body are `len|ulen|index`, so both lengths sit at `bodyEnd - 20`.
`Capabilities` is optional — peek the type before parsing the hash.

**`SISCompressed`**: `u32 algorithm, u64 uncompressedLength, payload`.
Algorithm 1 = deflate, and the payload is **zlib-wrapped** (`78 9c`), not raw.

**Order of operations when rebuilding**: patch hashes and lengths first, then
strip the signature, then recompress the controller — the strip shifts offsets.

---

## 3. E32 executable

**Header fields**: `0x0C uidChecksum, 0x14 headerCrc, 0x1C compressionType,
0x2C flags, 0x30 codeSize, 0x4C codeBase (0x8000), 0x54 dllRefCount,
0x64 codeOffset (156), 0x6C importOffset, 0x7C uncompressedSize`

**VA ↔ file offset**: `file_offset = VA - 0x8000 + 156`, on the *full* image
(header + payload). Forgetting the 156-byte header prefix shifts everything.

**Compression**: `0x101F7AFC` is Symbian `flate` — a custom Huffman deflate
(`DEFLATE_LENGTH_MAG 8`, `DIST_MAG 12`), **not** zlib. EKA2L1 has an inflater
(`common/flate.h`) but **no deflater**, so a patched payload can only be written
back uncompressed: set `compressionType = 0` and fix the header CRC (§1.2).
The file grows (223,417 → 348,120 here); this is legal and harmless.

**Import table**: `dllRefCount` blocks at `importOffset`, each
`u32 nameOffset, i32 count`, then `count` u32 code offsets. The ordinal is stored
in the code at each offset. Useful for answering "does this build use hardware 3D"
without running it — neither Asphalt build imports `libGLES_CM.dll`; both are
software rasterisers.

---

## 4. `light.bar` archive

`u32 tableSize, u32 dataSize`, then per entry a NUL-terminated name followed by a
**big-endian** u32 offset. Bit 31 of the offset means zlib-compressed, and the
payload is then `BE u32 uncompressedSize` + zlib stream.

Paths use backslashes and mixed case; the executable references them **lowercased**.

---

## 5. RLE sprite formats

Header: `u16 type, u16 width, u16 height, u16 pixelDataSize`, then the palette as
little-endian **ARGB4444**, then pixel data. Magenta `0xFF0F` is the colour key.

| type | encoding | palette |
|------|----------|---------|
| 0 | raw uncompressed 4bpp, low nibble first, `ds == (w*h+1)//2` | 16 |
| 1 | RLE, run byte = `(count-1)<<4 \| index`, max run 16 | 16 |
| 2 | RLE, run byte = `(count-1)<<6 \| index`, max run 4 | 64 |

Types 3, 129 and 130 also occur (menu thumbnails, `police_helico`, `trafic\classic`)
and are **not** decoded yet.

### 5.1 Row order — the trap that cost the most time

There is no single answer; it depends on which blitter consumes the sheet.

- **Font / HUD sheets are stored bottom-up.** The glyph blitter at `0x1469c`
  decrements the destination each row. Decode with a row flip to view them.
- **Large menu / loading / splash images are stored top-down.** No flip.
- `0x1f458` copies forward in x and y (top-down); `0x1f54e` starts at
  `dst + (h-1)*stride` (bottom-up). Colour-keys on pixel value 0.
- `0x1f49a` is an additive-blend variant, top-down.

Sanity check before trusting a decode: render `digits.RLE`. If it does not read
`0123456789`, the flip is wrong.

**When importing art across blit paths, match the consumer, not the source.**
`kmhmph.RLE` is a bottom-up font sheet blitted through the top-down `0x1f49a`, so
it must be flipped at build time.

---

## 6. The patch set (N73 build)

All size-neutral, applied to `asphalt2_full.exe` at these VAs.

| # | VA | change |
|---|----|--------|
| 1 | `0x202cc` | dashboard sprite → absolute `(0, BAR_Y)`; reuses the stock `muls` |
| 2 | `0x202f2` | blanked tachometer sweep → km/h \| mph unit label, 17×8, stride 34 |
| 3 | `0x20340` | nitro slice → absolute `(0, BAR_Y)` |
| 4 | `0x20370` | nitro fraction over sprite **width**, not height |
| 5 | `0x20386` | slice blit args: `copyW` = clipped value, `copyH` = full height |
| 6 | `0x203a4` | slice blit `0x1f54e` (bottom-up) → `0x1f458` (top-down) |
| 7 | `0x20414` | code cave in the dead needle-trig block: rev width → `sp+0xec`, strip destination → `sp+0xe8` |
| 8 | `0x20462` | needle line → two `nop`s |
| 9 | `0x204a2` | speed readout → absolute `(SPD_X, SPD_Y)`, **and** force the font's glyph width to 8 |
| 10 | `0x204cc` | 5×5 `larrow` draw → 240×5 rev square strip |

Current geometry: `BAR_Y=292 REV_Y=311 SPD_Y=310 SPD_X=212 UNIT_X=217 UNIT_Y=310
REV_SHIFT=3 REV_BASE=69 REV_MAX=176`.

### 6.1 Stock behaviour worth knowing

- The HUD draw is **inlined** — there is no draw function to hook. Every element
  computes its own bottom-right anchor:
  `movs r0,#0xff; adds r0,#0x41; subs r0,r0,r2; muls r0,r3,r0` = `(240-w, 320-h)`.
  Searching for that 320 constant (`ff 20 41 30`, any low register) locates the
  HUD function in any build — N73 `0x202cc`, N95 `0x202f2`.
- Sprite object: `+4` w (u16), `+6` h (u16), `+0x14` pixels.
- HUD sprite handles: `[sp+0x160] + {0x2c rpm, 0x30 rpm_gradient, 0x34 larrow,
  0x38 boost-slice, 0x3c boost-stick}`, loaded at `0x24e90`.
- Blit stack args (callee reads at `sp+0x24..0x30` after its push):
  `[sp+0] srcX, [sp+4] srcY, [sp+8] copyW, [sp+0xc] copyH`;
  registers `r0` dst, `r1` dst stride, `r2` src pixels, `r3` src stride.
- Draw-number `0x147c6` **right-aligns** at the given X. Glyph width comes from
  the font object `[font+0]`, spacing from `[font+8]`, advance = width + spacing.
  The sheet supplies only the row stride. The speed font object is
  `*(base + 0x43C)`, reached as `r5 + 0x400` then `[r0,#0x3c]`.
- Units flag: `base + 0x741D`, a bool. Set → speed × 62/100. Not exposed in
  Options in this build (Volume / Accelerator / Difficulty / Language only).
- Nitro fill: `copyH = slice_h * level / 2304`.
- Needle angle: `880 + 1828*(V-1000)/(2*carfield)`, `carfield = *(base+0x340+20*i+0x28)`,
  `V = *(base+0x480)`. Revs **dip** when nitro fires (upshift), they do not rise.

### 6.2 Stack slots

`sp+0xe0..0xf7` are live locals reused early each frame (around `0x20060`) — a
value stashed there by the cave at `0x20414` is gone by the next frame's
`0x202f2`. Anything the draw code needs must be computed inline at the draw site,
not carried across frames.

---

## 7. Asset findings

**Dead in the S60v3 package** (no string in the executable or any data file — the
loader uses explicit literals, no format strings):
`hud\newhud.RLE`, `hud\digits.RLE`, `interf\loading_02/03/04.RLE`,
`interf\menu_track15.RLE`, `interf\orange_logo.RLE`, `neon_pink.RLE`,
plus 67 car and track LOD variants.

**Only in the S60v2 package** (and dead even there):
`Fireboostx1/2/3.RLE` (32×32 x1/x2/x3 nitro starbursts), `start_1/2/3/go.RLE`
(3-2-1-GO countdown), `hud\rpm_center.RLE`, and all of `Tracks\TRACK15\` (London).

**The reference collage is 176×208** — an S60v2-resolution capture shown upscaled
~1.36×. Measurements taken from it must be divided by that factor before being
compared with a 240×320 build. `newhud` is dead in the S60v2 build too, so the
collage is from neither S60 port — most likely the Java ME or N-Gage version.

**Not dead, despite appearances**: `shadow.RLE`, `nitro.RLE`, `boost1b/2b/3b.RLE`
are all named by both executables. The collage's richer flares are the
`Fireboostx*` starbursts, not a disabled feature.

`newhud.RLE` is 176 wide and is stretched to 240 horizontally only — the HUD is
already non-uniformly scaled, so matching the collage's proportions exactly is
not possible with the shipped art.

---

## 8. Method notes

- **Localise an effect with a marker texture.** Replacing `boost1b/2b/3b` with a
  flat colour and racing one lap identified the flare quads in minutes, after
  static analysis of the particle system had gone nowhere.
- **Capture pixel-exactly or not at all.** A 900×640 emulator window gives a
  435×580 game area for a 240×320 screen — scale 1.8125. Point-downsampling that
  back to 240×320 *drops and duplicates rows*, which reads convincingly as a
  rendering bug. Size the window 900×700 so the game area is exactly 480×640,
  then halve. Several "bugs" reported to the user were this artifact.
- **Verify a decode against known content** before building on it (digits, text).
- **Reproduce a stored value before writing one.** Both integrity fields were
  solved by matching untouched originals first; both checksum attempts failed
  that test and were correctly not shipped.
- `pkill -f emu.sh` matches the agent's own wrapper shell. Use `pkill -x`.
- EKA2L1 chdirs to `~/.local/share/EKA2L1/`; `--install` needs the SIS path to be
  outside the scratchpad to avoid permission prompts.

---

## 9. Open questions

- The checksum algorithm (§1.3).
- The Fireboost x1/x2/x3 → `boost1b/2b/3b` mapping assumes the selector index is
  the nitro level. Colour progression supports it; never confirmed on a three-nitro
  burst.
- Porting the patch set to the N95/N96 build: assets are identical, but the
  executable is a separate compile and none of the ten addresses transfer. The
  320-constant search (§6.1) is the way in.
