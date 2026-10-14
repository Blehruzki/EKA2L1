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
