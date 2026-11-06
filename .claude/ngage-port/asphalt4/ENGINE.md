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
- **The road** (code): the track is an array of 0xE4-byte segments (base at track + 4, count at
  track + 0); a segment's point is at +0x30/+0x34/+0x38 (x, height, z). +24604(track, pos,
  &index) finds the segment holding a position, starting from the index given. +24524(pos, seg,
  prev, mode, lane, ..., &frac, ...) projects the point on the line prev -> seg in x-z, giving
  the fraction t (0..4096, clamped) along it, writes t out, and returns the road height: with
  mode bits 10-12 clear, `prev.h + (seg.h - prev.h) t >> 12` (+245F4); otherwise one of four
  variants through +2435A (lane-dependent). +226E8 ends with it on the car: mode = car +0D4,
  lane = car +024, t into **+020** (the car's fraction along its segment -- not the wheels, as
  rounds 31-47 called it), the height into **+0D0** (+22828). So the road under a car is a
  straight line within a segment (in mode 0): +0D0's change per step is constant until the car
  crosses into the next segment (a segment is ~6 steps long at 135 km/h, +020's rate 666 a step).
- The render keeps its camera above the road: +246F8 on the camera position (+D55A) finds the
  segment, takes the road height and raises y to at least it plus an offset.
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

## The chase camera (+5968 -> +4B40, then the shake +49C4)

The camera object is race + 0x48 (r4 below; r4 + 0x10.. is race + 0x58..). +5968 is called from
the step (+14F58, its stack arguments 0, 0, 0) and from the between-steps routine +12DEC (+12E44,
arguments 7, 0, ...) (code).
- **+5968** picks the followed car's track segments (previous, current, next: 0xE4-byte records
  around car +01C) and, for the chase modes 1, 2, 3, 0x2A (+5D60 switch), builds the direction
  vector: a rotation about y by `A = heading (+678) + race+0xE4 + arg6` (minus car +808 >> 7 when
  +B28 bit 12 is set) applied to (0, 0, L) (+5B50..+5B9C, +3227E/+32342/+32302 build the matrix,
  +522F0 applies it). The vector has no y part.
- **+4B40** places the camera (code):
  - the mode (r4 + 0x4C = race + 0x94) gives a distance base and two constants: mode 1 (0, 43,
    -20), 2 (40, 53, -25), 3 (80, 58, -30), 0x2A (200, a literal, -220);
  - distance = base + (r4 + 0xB8) / 4 + car +A90 (+ car +A24 when negative);
  - x, z = car x, z - vector x distance >> 12; y = car y + car +A8C + 53 (chase);
  - pitch (race + 0x64) = car +674 - 25; yaw (race + 0x68) = heading + arg6 + race+0xE4, the
    difference wrapped once (> 1024 -> -2048); plus a sway angle (r4 + 0xBC) or a term from car
    +9F8;
  - a slope term s = (|h_cur - h_prev| (4096 - car +020) + |h_next - h_cur| car +020) >> 15 from
    the segments' heights (+0x34): y += s, pitch -= s;
  - car +B28 bit 12 (a drift-like state): roll = car +808 >> 7, y -= |+808 >> 8|, yaw += +808 >> 6;
  - the focal length (r4 + 0x44 = race + 0x8C) eases by 5 a step toward 260, or toward a nitro
    value (B28 bit 2 with car +AE0 > 28).
- **+49C4**, the shake (code): a generator seeded from car +A2C; above speed 166 (+7D0) it adds
  random pitch/yaw (and x, y offsets) scaled by the speed's excess, under nitro more, and a crash
  term from car +A68.
- **Measured** (run y1, the step records, 1,238 chase steps): camera yaw less the car's heading
  is -1..0 (the shake), race+0xE4 is 0; camera y = car y + 52 (+A8C) + 53 + s, with s 0..20 and
  pitch = car +674 - 25 - s, every step; x-z distance 312-347 (+A90 240, focal 260).
- So **the game's chase camera is rigid on the car at each step** -- in position, height and
  yaw -- with no lag. The "camera takes 3/4 of a turn" that our frames used since round 43
  (A4_KD_TURN, hp_sub's `3 * dh / 4`) has no basis in the engine.

## The heading

- Measured (run y1, the player's 1,313 step pairs, the autopilot holding the throttle): the
  heading turns 0 (1,096 steps), -20 (130), +20 (34), now and then 24-64; the next step repeats
  the last turn in 87% of steps, and misses at a turn's start or end (by ~20 /2048). Who sets the
  turn (+1DE78, the control): not yet read.

## The traffic (+FB12)

- Sixteen slots; per traffic car: locate (+226E8), then y = +0D0 + 3/4 of a per-model height
  BEFORE the measure (+FF4A..+FF84) -- so the traffic's +014 is its true height change, unlike
  the racing cars'.

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

## Frames on the engine's rules (A4_ENGDRAW, engine map 2)

- Per racing car, from the step's state: x, z along +010/+018 (exact in 99% of steps); heading by
  the whole last turn; height by +A58 in the air (y + +A58 is the next step's y) and by +0D0's own
  rate on the road (exact within a segment), landing on the drawn road; +020 clamped at the
  segment's end. A respawn (over 2,048 units in a step) carries no rate.
- The chase camera: the game's offset from the car at the step, turned by the drawn turn, on the
  drawn car (x, z), its height above the drawn car kept, its yaw the step's plus the drawn turn --
  rigid, as +4B40 places it.
- The corrector (continuity at a step's first frame, faded over a step) continues the last frame's
  model part at the new velocity with its offset still fading.
- Bench (run y2b against y2a, ENGINE-rule frames against r47's): all racing cars' height off the
  game's path 99% 161 -> 10, x-z 97 -> 30, heading 47 -> 12; physics identical to stock as long
  as any build's real-time run stays so (race step 1,100).

## Open (not yet read)

- +1DE78 in full: who sets the turn (keys, speed, the road-follow) -- a frame could then predict
  a turn's start and end instead of repeating the last step's.
- +1F1F8 (kind 2).
- The render's per-car pass: it reads +01C with the drawn position (+E04C: the segment, then
  +2421A on the position, probably the shadow on the road); where it reads +020 is not found, so
  the clamp of +020 is a guess.
- The road's modes 1-4 (+2435A, lane-dependent): which tracks use them.

## Instruments

- A4_YREC (bench, a4_gl.c): every frame, every car as drawn plus the step's y, +0D0, the
  corrector's y offset and the camera's y at the step ('FRKD' through a4_rec_emit2; jtrun.sh).
- tools/yan.py: the drawn height against the line between steps, and +014 against the true
  change, per car slot. tools/yseq.py: one slot's frames over a range of steps.
