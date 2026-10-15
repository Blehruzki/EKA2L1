# Native 240x320: rendering a title at the phone's resolution

A plan, not work done. Asphalt 2 is the worked example; nothing here has run.
Written after round 181, when the user asked what upscaling would imply.

## What it is, and what it is not

Today every framebuffer title draws 176x208 and the loader enlarges the
finished frame (`screen_layout`, the port scaler and its filter). Native
means the game's own renderer produces 240x320 (or 320x240) pixels. That is
patching the game, title by title, not a loader setting: Asphalt 2's row
width is hard-coded at 176, it never asks how big the screen is (`HAL::Get`
imported, called zero times), and telling it 240x320 changes nothing it
draws (E135, E229/E230). Each title is its own reverse-engineering job.

## What can and cannot improve

| part | native 240x320 gives | needs |
|---|---|---|
| 3D: track, cars, roadside geometry | sharp edges, a wider or taller view | the renderer's projection, clip bounds, row stride and buffer size patched |
| 3D textures | nothing: magnified as now | new art |
| 2D: menus, sprites, fonts, HUD art | nothing: enlarged per sprite instead of per frame | new, higher-resolution art plus the data formats reverse-engineered |
| HUD placement | stays at its portrait coordinates (top-left corner of a bigger frame) | every HUD position patched; mandatory for landscape |
| draw distance, pop-in thresholds | a visible gain at no cost | usually one constant each; worth doing on its own |
| texture filtering | not recommended: multiplies the per-pixel cost in a software rasteriser | -- |

## Performance, estimated (not measured)

- 240x320 is 2.1x the pixels of 176x208; landscape 320x240 the same count.
- The N-Gage ran 176x208 on a 104 MHz ARM9; the N95's 332 MHz ARM11 has
  three to four times the compute, about what 2.1x pixels at the same frame
  rate needs. An S60 3.0 phone (N73, 220 MHz ARM9) would be marginal.
- From Glimmerati's N95 figures (~23 ms a game frame at 1:1, logs/glim-004-n95):
  if about half is pixel filling, a native frame lands near 35 ms, 28-30
  FPS -- and the loader's scaler (about 11 ms in the full mode) is no longer
  needed. So native full screen likely costs about what scaled full screen
  costs today, and looks sharper. A prototype on the N95 is the only
  measurement; the loader's 64-blit instrument gives it.

## The order

1. **Bench, no phone.** Find Asphalt 2's renderer constants: 176, 192, 208 in
   the projection, clip rectangle, rasteriser stride, framebuffer allocation
   and 2D blitter clip (`gdis.py`, the PCTRACE and RWATCH instruments, as for
   Glimmerati's span routines). Write each site down with the row that
   found it.
2. **Bench.** A larger 3D viewport only, HUD left where it is. The loader
   hands a 240x320 buffer (`GAME_W`/`GAME_H`/`GAME_PITCH` per title) and
   blits it one to one; the patches go through `GAME_CODE_PATCHES`, written
   after the Codewave hash as Glimmerati's are (`GAME_PATCH_AT_OPEN_CHARS`;
   Asphalt 2 is Codewave too, BUGBOOK 14.g).
3. **Phone.** One race on the N95: frame rate from the 64-blit reports, the
   picture from a photo. Decide here whether to go on.
4. **Bench, then phone.** HUD relocation; then landscape (the loader rotates
   the blit; the game's view and HUD are patched for 320x240).
5. **Separately, any time:** draw-distance and pop-in constants.

## Risks

- A Codewave title checks its code; more patches widen the window the hash
  can catch, so every patch stays behind the data-archive open (14.g).
- Patched constants can be shared between the 3D view and the 2D layer; a
  change that fixes one can shear the other (E133-E135's pitch lesson).
- The 2D side gains nothing without new art, which is outside the port.
- Weeks of bench rounds before the first phone build, per title.

## Step 1, done: where Asphalt 2 bakes its 176x208 (E1443-E1444, `nativescan.py`)

How it draws, measured on the bench (image at 0x4700000):
- Its own back buffer of 73,216 bytes (176x208 at 16 bits; the pixels are
  4K, 12 bits in a 16-bit cell, the top nibble masked at present time). The
  screen object keeps `[0]` = the buffer and `[4]` = buffer + 0x1600, row
  16: **the 3D view is 176x176, rows 16-191**, with a 16-row band above and
  below for the HUD. The fills clip both x and y to 175 (img+0xb714,
  0xbb84, 0xbf90).
- The present (img+0xa6c74, called from 0x973fc) clears the two bands,
  optionally blends a 256-pixel-pitch overlay into the view (a 50% average
  of 12-bit pixels), and memcpys the 73,216 bytes to the screen surface's
  pixels (`+0x1c`), which is `iScreenAddress + 0x20` from the loader's
  `ScreenInfo` (img+0x393ec).
- 63 PCs in the image write the back buffer during a boot and the attract
  race (`EKA2L1_WATCHONCE`), in 22 functions: two assembly blitters at the
  image's start (0x434, 0xfd4: parametrised, the caller builds a
  {flags, 176, 208} descriptor on the stack, img+0xba124), the span fills
  (0xb9008 and 0xb92d8, four-byte stores of pixel pairs), a family of
  textured and blended span routines (0xaf3ac-0xb04b0), sprite and text
  blitters (0x34cc8-0x38ba4, 0x50c1c-0x50e38, 0x7eda4), the fills.

What a native 240x320 patch has to change, by kind:

| kind | sites | the shape of the patch |
|---|---|---|
| row stride 352, written as `y*11<<5` (add, add, lsl #5) | 30 sites in 21 functions | 480 = 15<<5: `add t,y,y,lsl#1 ; add t,t,t,lsl#2 ; lsl #5` -- the same three words. Landscape 640 = 5<<7: two words and a nop |
| width 176 / 352 / 175 as immediates (`cmp #0xb0`, `addgt #0x160`, `cmp #0xaf`) | one to six per renderer function; ~60 in the 21 functions, more in callers | 240 = 0xf0, 480 = 0x1e0, 239 = 0xef: all ARM immediates |
| height 208 / 207 | the surface sites and the present | 320 = 0x140 is an immediate; 319 is not (a cmp against 0x140 with the condition changed) |
| buffer size 73,216 | 5 literal words (img+0x8438, 0x5a60c, 0x9eb10, 0xa70f8, 0xac05c) | 153,600 in the literal |
| the bands: 0x1600 (16 rows), 0x10800 (rows 192 on) | the present, the screen object's constructor | 16*480 = 0x1e00 (an immediate); (320-16)*480 = 0x23a00 is not (a literal or two adds) |
| surfaces made 176x208 (`surface_init`, img+0x5409c) | 7 call sites, one 150 wide, 4 from registers; the stack descriptors at 0xba124/0xba1cc | per site |
| the half width 88 and the horizon 125 in the perspective span (img+0xb9008: `#0x58`, `#0x7d`) | the span function; the polygon rasteriser's own centres not yet found | 120; the horizon moves with the view's height |

So roughly 100-130 words, in some 30 functions, all of a few shapes, and
all behind the Codewave hash (written at the data-archive open, as
Glimmerati's). Asphalt Urban GT draws the same way (`nativescan.py` on it:
16 stride sites in 11 functions, no 73,216 literal -- it sizes its buffer
another way), so the method carries, the sites do not.

Still unknown before step 2: the polygon rasteriser's projection centre
(the immediates 0x58/0x68 in the engine's big functions are stack offsets
and unrelated adds, not it); the HUD's coordinates (step 4); whether the
game clips sprites against the surface's own width field (`+4`) or the
immediates -- the span routines use the immediates.

## Step 2, on the bench: the width-only prototype (E1446-E1452, `nativepatch.py`)

`GAME_NATIVE_W 240` in `games/asphalt2/game.h` (a bench knob, 0 when
shipping) includes `native240.h`, generated by `nativepatch.py` from the
image: 143 patch words, each with the word it expects. The loader hands a
240x208 buffer and blits it one to one. Through the whole attract race, no
fault: the 3D view is 240 wide, road, sky, buildings, cars and the tunnel's
ceiling out to column 239, and the HUD (WANTED, money, position, the
speedometer) drawn where it was, in the 176-wide layout at the top and
bottom left. Frame rate on the bench's counter 17-26, as the 176 build.

What it took, run by run:

| run | what was wrong | the fix |
|---|---|---|
| E1446 | died at boot in the engine's constant arithmetic | 13 of the 30 "stride" sites were `y*11` chains that end in `add t,t,t,lsl#5` or an `rsb` against y: not addresses. The rule now wants `add rd, base, t, lsl #5` with a base that is neither y nor t: 17 sites, all in the fills and span families |
| E1447 | a memset of the new size over the old buffer | the screen object allocates 73,216 + 16 KB from its own literal (img+0x8434) |
| E1448 | two scrambled strips, streaked sky, the tunnel clipped at 176 | the sky's row walker (0xb9df0: 176 as the pitch factor and as a row clip, a hard 352 step) and span (0xb9d18) |
| E1449-E1450 | the strips stayed; a write watch on their rows named the text blits | they draw through `{flags, 176, 208, pixels}` descriptors of the screen built in eleven places (0xba110, 0xba1ac, 0x9e920 x4, 0x4aa18, 0x4c008, 0x4ee88, 0xa1db4, 0xae940 x2, 0xbcd44, 0xbd648); the row loops step by the descriptor's width |
| E1451 | HUD and strips right; the tunnel's vanishing point moved but its walls stopped at 176 | the scene renderer's projection centre (img+0xa2fa8: x += 88) and clips (0xa2388, 0xa2c7c); the column walker's x clips (0x11bbfc and its twin) |
| E1452 | the ceiling reaches 239; the near right wall still gives way to sky from about column 180 | open: not the two `0xd5a08(176)` (allocations of 176 bytes), not the engine's `cmp #0x57`s (hash constants), not the 2D layout 88s. A polygon cull at the model level, or geometry the original never showed |

Lessons for the next title: a pattern match on code is a guess (rule 4) --
the stride idiom matched the protection's arithmetic; the screen's size is
baked in three shapes (the surface objects, the stack descriptors, the
immediates in the renderers) and a watch on the broken rows is the fastest
way from a symptom to the shape; 176 and 88 in the UI code are layout, not
width, and stay for a HUD-in-place step.

Still open before step 3: the tunnel wall above; the menus and the race
start on a 240 buffer are untested (the attract race only); the 0x84f4
family (eight sprite blitters clipping to 176) and 0x353d8's 176 are left
as 2D for now.
