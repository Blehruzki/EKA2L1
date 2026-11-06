# Asphalt 4: the engine, read from the binary

The owner, after round 47: "disassemble the game and run bench rounds only until you fully
understand how the game engine works. Otherwise we're gonna be spending rounds blindly."
This file is that map. Each claim names its source: **code** (the address in the code section,
`a4code.bin`, read with `tools/tdis.py` / `fdis.py`), **run** (a bench measurement, with its
tag), or **record** (an earlier MBX.md round). Nothing here is a guess unless it says so.

Offsets are the car object's (`+0xxx`) or the race object's (`race+0xxx`); code addresses are
code-section offsets (`+1EA64`).

## The step

- One logic step every 85.33 ms (11.72 Hz); a render between steps draws the state the last
  step left (record: rounds 18, 30). Keys are read once a step (+25E8, record).
- The cars are updated by five functions, called per car per step; our renderer's HCALL list
  records each call as a kind (record, round 43):

  | kind | function | what it does (code) |
  |---|---|---|
  | 1 | +1DE78 | the car's control: engine, speed, steering words (+7D8, +7E4, +678...) |
  | 2 | +1F1F8 | (not yet read) |
  | 3 | +1EA64 | the racing cars' move: locate on the track, measure, then the vertical logic below |
  | 4 | +1FD5C | locate on the track (+226E8), measure (+22832), then `y = +0D0` |
  | 5 | +1FD0E | place the car: position from +6B0 through +52562, heading from +6CC |

## A car's motion words

- **+22832, "measure"** (code): `v = pos - prev` into +010/+014/+018; `a = v - v_old`,
  +0B8..+0C0 = 12a, +0C4..+0CC = (12a + 7 x old) / 8 (a smoothed acceleration); +0B4 =
  ceil(|v| / 256); then prev (+0A8/+0AC/+0B0) = pos. Called from +1EA64, +1FD5C and +FF84.
- **+226E8, "locate"** (code): finds the car's track segment (+24604; segments are 0xE4-byte
  records, the index at +01C) and the position on it (+23E72), then reads the segment's corner
  points (+0x0C/+0x14, +0x30/+0x38, +0x54/+0x5C): the road under the car. +0D0 is the road's
  height there (run y1: +0D0 tracks the step's y whenever the car is on the road).
- So **+014 is not the height change a step ends with**: it is measured before the vertical
  logic moves y. Kind 4 sets `y = +0D0` after the measure (+1FD70), so its +014 is the change
  before the snap; kind 3 changes y afterwards and rewrites +0AC = y at its end (+1EF38).
  Run y1 (the calibrated N95 bench, Veyron, LA, 120 s, the player's step records): +010/+018
  equal the true step change of x/z in 1,301 of 1,314 steps (max miss 20); +014 differs from
  the true change of y in 967 of 1,314 (50% 2, 90% 21, 99% 69, max 127 units).

## The vertical logic (+1EA64, kind 3)

Registers in the function: r4 the car, r6 = car + 0xA40, r7 = car + 0xAC0, r5 = car + 0xB00.
- **Airborne** (y above +0D0, with the race's conditions): +A60 += 1 (steps in the air),
  `y += +A58` (the vertical speed), then, unless +AF0 (a hang counter, counted down) is set,
  `+A58 -= 20` if the byte +A64 is set, else `-= 9` (+1EE28..+1EE4C): integer gravity, one
  step at a time.
- **Landing**: y back to +0D0 (+1EF0A), +A58 = 0, +A64 = 0, counters cleared, +B28 bit 3 off.
- **Floor**: y never below +0D0 - 64 (+1EF26..+1EF32).
- Then +0AC (the previous y) = y (+1EF38).
- So an airborne car's next height is exactly `y + +A58`, and a car on the road follows
  +0D0. Opponents fly too (run y1: slot 3 airborne for six steps down the LA hill, 90-110
  units a step, its +014 0 throughout).

## The chase camera (+4B40, under +5968, the camera's update) -- being read

r4 the camera object (race + 0x48: r4 + 0x10.. is race + 0x58..), r5 the followed car.
- The camera's mode at r4 + 0x4C (race + 0x94; 2 = chase, record) picks a distance base and
  two height/pitch constants: mode 1 (0, 43, -20), 2 (40, 53, -25), 3 (80, 58, -30), 0x2A
  (200, [pc], -220) (+4B8A..+4BD0).
- The distance: base + (r4 + 0xB8) / 4 + car +A90, plus car +A24 when that is negative
  (+4BD2..+4BFA).
- The offset: the direction vector at r4 + 0xAC (race + 0xF4, record: the camera's direction
  vector +F4/+FC) times the distance, >> 12, from the car's position (+4C38..).
- Not yet read: the height, the pitch and yaw smoothing, and +5968's other branches.

## What our frames do with this (round 47, read and measured)

- The drawn height was the step's y plus +014 x the fraction, through the corrector. With
  +014 0 on a falling car and about one frame per step, each step's first frame took the whole
  drop up as the corrector's offset, and the fade runs only between a step's frames: the
  offset grew step after step (+121, +203, +294, ... +497) with the car held in the air, until
  it passed the 512-unit cut and the car snapped to the road (run y1, slot 3, steps
  1026-1033). That is the owner's "flying upwards, then snap back to the road".
- Run y1, every racing car (slots 0-7, 1,595 frames each): the drawn height off the straight
  line between the game's heights at the steps around the frame -- 50% 11-13, 90% 69-79, 99%
  160-214, max 276-550 units (the camera sits ~300 behind). Each worst frame: +014 0, the
  corrector holding 290-500.
- The corrector freezes more than height: whenever a step's first frame is its only frame, the
  offset is renewed from the last drawn value and never fades, so a car whose model misses
  stands still on the screen and then jumps. The phone draws 1.2-1.5 frames a step (r46ph,
  r47ph). This is the corrector's design, not the engine.

## Instruments

- A4_YREC (bench, a4_gl.c): every frame, every car as drawn plus the step's y, +0D0, the
  corrector's y offset and the camera's y at the step ('FRKD' through a4_rec_emit2; jtrun.sh).
- tools/yan.py: the drawn height against the line between steps, and +014 against the true
  change, per car slot. tools/yseq.py: one slot's frames over a range of steps.
