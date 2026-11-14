# renderer/eng/edits.py -- round 50: the engine at 3x, as changes to the game's own code (tools/lift.py
# applies them with -DA4_ENG3; without it the lifted C is the game's, exactly). Each entry: the code
# offset of one game instruction, and C to put 'before' it, 'after' it, or in its place ('replace'),
# with 'why'. The C may read and write the registers (r0..r12, lr, sp), the flags (fN fZ fC fV) and the
# engine's state (a4_eng.h: ENG_SUB, the substep 0..2 of the game's 85.33 ms step).
#
# The race step +141B0 runs once a substep (A4_THIRDSTEP: 28.44 ms). Substep 0 is the game's whole step;
# on 1 and 2 only the motion runs: the traffic, every car's control, the collisions, the movement, the
# camera's placement -- the race's rules, timers, sounds, positions and messages wait for the next 0.
EDITS = {
    0x141B0: {'before': 'ENG_BEGIN(r0);',
              'why': 'each race step is a substep: 0 the whole step, 1 and 2 motion only'},
    # after the player car is fetched (str r0, [sp, #0xd8]) -- the substeps skip the race's rules
    # (+14308..+1452C: the start, slow motion's scale race+0x84C, race+0x30, the boost, the jingles) and
    # take the frame slots that part sets (the camera race+0x48, the keys, the start's state ...) as the
    # whole step left them: without them the first 3x run's car rolled during the countdown
    0x1433C: {'before': 'if (ENG_SUB && !ENG->loop) return 0; if (ENG_SUB) { uint32_t k_; for (k_ = 0; k_ < ENG_NFRAME; k_++) MEM32(sp + eng_frame_off[k_]) = ENG->frame[k_]; goto L_1452C; }',
              'why': 'substeps 1-2: straight to the traffic, with the whole step\'s frame'},
    0x1452C: {'before': 'if (!ENG_SUB) { uint32_t k_; for (k_ = 0; k_ < ENG_NFRAME; k_++) ENG->frame[k_] = MEM32(sp + eng_frame_off[k_]); }',
              'why': 'the frame slots the race\'s rules set, for the substeps'},
    # the car loop's start: the whole step reached it (in the race's intro it does not: the fly-in's
    # check, camera mode 0x17, ends the step before the cars) -- r4d: substeps ran it anyway
    0x14672: {'before': 'if (!ENG_SUB) ENG->loop = 1;', 'why': 'the whole step runs the cars'},
    # after the car list's base (r6 = race + 0x3D0): the pursuit mode's position checks (mode 0x17) are rules
    0x14586: {'before': 'if (ENG_SUB) goto L_14672;',
              'why': 'substeps 1-2: the pursuit check waits for the whole step'},
    # the player's keys: built from the key handler on the whole step, replayed on the substeps (+1DE78
    # keeps the last keys in +B1C and finds presses itself, so a replay is a key held, not pressed again)
    0x14754: {'before': 'if (ENG_SUB) { if (ENG_KPEEK(sp)) goto L_1477C; MEM32(sp + 0xD0u) = ENG_KEYS; goto L_14948; }',
              'why': 'substeps 1-2: the whole step\'s keys (A4_LIVEKEYS: built again from a copy, the steering taken)'},
    0x14948: {'before': 'ENG_KMERGE(sp);', 'why': 'A4_LIVEKEYS: a substep\'s steering on the whole step\'s key word'},
    0x14974: {'before': 'if (!ENG_SUB) ENG_KEYS = r1;',
              'why': 'the keys the whole step gave the player\'s control'},
    # after the movement loop: the substeps place the camera as the whole step's +12DEC last did (its
    # arguments saved at each of its +5968 calls: the camera race+0x48, the track, the car it follows,
    # the key object, the mode's stack words) on the moved cars, and end
    0x14CB2: {'before': 'if (ENG_SUB) { eng_cam_substep(sp, r23); return 0; } eng_cam_unblend();',
              'why': 'substeps 1-2: the camera on the moved car (its logic the whole step\'s), then done; the whole step: its own pose back'},
    # the traffic's two clocks (race+0x378, +0x37C, passed to +FB12 in r2 and r3): their change over the
    # whole step, measured there, so the substeps can place the traffic a third and two thirds along it
    0x14574: {'before': 'if (!ENG_SUB) { ENG->tdel[0] = (int32_t)(r2 - ENG->tlast[0]); ENG->tdel[1] = (int32_t)(r3 - ENG->tlast[1]); '
                        'ENG->tlast[0] = r2; ENG->tlast[1] = r3; '
                        'if (ENG->tdel[0] < -0x100000 || ENG->tdel[0] > 0x100000) ENG->tdel[0] = 0; '
                        'if (ENG->tdel[1] < -0x100000 || ENG->tdel[1] > 0x100000) ENG->tdel[1] = 0; } '
                        'else { r2 = ENG_TCLK(0u); r3 = ENG_TCLK(1u); }',
              'why': 'the traffic clocks\' step, for the substeps; the substeps start from the whole step\'s own clocks '
                     '(the game has advanced race+0x378/+0x37C by then: placed from those, substeps 1-2 were a step '
                     'ahead and every traffic car went forward 1 1/3 steps and back 2/3 each step -- phone round 69)'},
}

# ---- the rate map: the game's per-step changes, a third on each substep -----------------------------
# T: a rate -- the store keeps a third of its change (eng_third, the remainder carried per word).
# G: a counter or a decision's state -- it changes on the whole step only (substeps keep the old value).
def T(a, why): EDITS[a] = {'third': True, 'why': why}
def G(a, why): EDITS[a] = {'gate': True, 'why': why}
def GE(a, car, why): EDITS[a] = {'gate': 'end', 'car': car, 'why': why}
def B(a, code, why): EDITS[a] = {'before': code, 'why': why}
def R(a, code, why): EDITS[a] = {'replace': code, 'why': why}

# +1DE78, the car's control and dynamics
B(0x1DE7C, 'ENG_VP(1u, r0, r1, MEM32(r0 + 0x10u), MEM32(r0 + 0x14u), MEM32(r0 + 0x18u), MEM32(r0 + 8u));',
  'bench (A4_VELPROBE, phone round 68): the player car\'s control entered, its keys and velocity')
T(0x1DEDE, 'B20, steps a key is held (to 0x30): one a step')
G(0x1DF20, 'A2C, a step counter'); G(0x1DF28, 'A34, a step counter (the stuck check every 64)')
G(0x1DF58, 'A7C, a countdown'); G(0x1DF64, 'A7C, a count up')
T(0x1DF88, 'ABC, drained by a table value a step')
GE(0x1DF9E, 'r4', 'AEC, a countdown (rpm -8 a step while it runs)')
T(0x1DFA8, 'rpm -8 a step while AEC runs')
# Round 65 (A4_WRECK3, ENG_WRECK3): the wreck at 3x too -- every substep runs it, its rates a third (the rpm's
# -0x100; +2292A's push from the path, +1C/+18 x the entry, and its damping; the position a third of the move,
# the height approaching the path's own point by the step's end), its path's counters on the step's last substep.
# Without it, as round 50:
# the wreck sequence (+034 != -1: rpm -0x100 a step, gears down, the scripted path +2292A) runs on the
# whole step only: its path integrates the car's own velocity, which the substep measure would scale
# (r7: an opponent's wreck flung it across the map). The wreck moves at the game's 11.72 Hz.
B(0x1DFE6, 'if (ENG_SUB && !ENG_WRECK3) return 0;', 'substeps: a wreck waits for the whole step (A4_WRECK3: runs)')
EDITS[0x1DFEE] = {'third': 'ENG_WRECK3', 'why': 'A4_WRECK3: the wreck\'s rpm -0x100 a step, a third a substep'}
G(0x1E39A, 'C48, the respawn countdown')
T(0x1E602, 'grip 9FC, +0xD55 a step on the road')
# the velocity's change toward its target (+1E5CA: d = +680 - +010, its length clamped to the traction
# T / 16 a step, T from the grip +9FC): the game reaches the target in one step when it is within the
# traction and moves T / 16 toward it when not. A substep reaches it within T / 48 (r1-r9 thirded d,
# which reaches 70% of the way in a step: the car lagged its target at a launch and after a wall)
B(0x1E684, '{ int32_t m_ = (int32_t)MEM32(sp + 0x104u), t_ = (int32_t)MEM32(sp + 0x108u), g_, l_; uint32_t k_; '
  'ENG_VP(2u, r4, (uint32_t)m_, (uint32_t)t_, MEM32(sp + 0x10Cu), MEM32(sp + 0x110u), MEM32(sp + 0x114u)); '
  'g_ = (m_ > t_) ? t_ : m_; l_ = t_ / 3; '
  'if (t_ > 0 && g_ > l_ && g_ > 0) { int32_t f_ = (int32_t)(l_ < 0x80000 ? eng_udiv((uint32_t)l_ << 12, (uint32_t)g_) : eng_udiv((uint32_t)l_, ((uint32_t)g_ >> 12) | 1u)); '
  'for (k_ = 0; k_ < 12u; k_ += 4u) MEM32(sp + 0x10Cu + k_) = (uint32_t)(int32_t)(((int64_t)(int32_t)MEM32(sp + 0x10Cu + k_) * f_) >> 12); } '
  'ENG_VP(3u, r4, MEM32(sp + 0x10Cu), MEM32(sp + 0x110u), MEM32(sp + 0x114u), MEM32(MEM32(sp + 0x150u) + 0x3Cu), MEM32(sp + 0x140u)); }',
  'the velocity\'s change: the target within a third of the traction')
T(0x1E7F8, 'drift 9F8 += f(turn)'); T(0x1E804, 'drift 9F8 += f(turn)')
T(0x1E87E, '81C, the turn held, += A20')
T(0x1E896, 'heading -= A20 (reversing)'); T(0x1E8A2, 'heading += A20')
R(0x1E8C0, '{ uint32_t k_; ENG_VP(4u, r4, MEM32(r1), MEM32(r1 + 4u), MEM32(r1 + 8u), r1, r0); for (k_ = 0; k_ < 12u; k_ += 4u) { uint32_t ea_ = r0 + k_; const uint32_t o_ = MEM32(ea_); MEM32(ea_) = eng_third(ea_, o_, o_ + MEM32(r1 + k_)); if (!ENG_SUB) eng_pown(r0 - 4u, k_ / 4u, (int32_t)(MEM32(ea_) - o_)); } }',
  'position += velocity (+521A0): a third of the step\'s')
# +1B9E0, steering
# A1C, the steering rate: each call the game eases it toward the held key's target ((A1C + k)/2, (A1C - k)/2) or,
# with no key, back toward centre ((A1C +- 3k)/4), then +7FC += A1C. Phone round 70 (quick left-right "as if it was
# slipping"): thirded, the rate crossed zero a third at a time -- on a release the first substep's rate kept the old
# sign (-1800 -> -900 where stock jumps to +901) and +7FC ran on past the game's own -4096 to -4376 before turning,
# every reversal a step late (o1w / o3w). Now the rate is the game's step value: computed on the whole step as stock
# does, kept on the substeps (+7FC still moves a third of it each), and computed afresh on a substep whose branch
# differs from the one its step took (a live key pressed, released or reversed mid-step: A4_LIVEKEYS)
for a, br in ((0x1BA28, 1), (0x1BA52, 2), (0x1BA7E, 3), (0x1BA9A, 4)):
    R(a, '{ uint32_t ea_ = r2 + 0x1Cu; if (eng_steer_upd(r12, %du)) L32(ea_) = r1; else r1 = L32(ea_); }' % br,
      'A1C, the steering rate: the game\'s step value, kept on the substeps')
for a in (0x1BA2C, 0x1BA56, 0x1BAA0): T(a, 'steering 7FC += A1C')
T(0x1BAF0, '808, steering eased 1/8 a step'); T(0x1BB1A, '80C, steering eased a step')
# +1BB1E, the brake
G(0x1BB3C, 'A08, steps the brake is held')
for a in (0x1BB4C, 0x1BB6C, 0x1BB76): T(a, 'A04, the brake\'s ramp')
T(0x1BB60, 'rpm -= brake'); T(0x1BB90, 'rpm -= 7DC/2 (key 4)')
# +1BB98, engine drag
# the slip check once a step (the game's decision on its step's state), with the whole penalty
B(0x1BBD8, 'if (ENG_SUB) goto L_1BC08;', 'the slip check is the whole step\'s')
T(0x1BC2E, 'rpm -= coast'); T(0x1BC54, 'rpm -= A74/64')
# 7C4, the gear change's delay (no engine drag while it runs): counted at the step's end, as the game
# counts it after its step's check -- counted on substep 0 its last step lost substeps 1-2 to the drag,
# and an AI that lets off the throttle for the delay's three steps fell back a gear every 4 steps (r13)
GE(0x1BC5A, 'r5', '7C4, the gear change\'s delay')
# +1BC5E, the handbrake's rpm drop
T(0x1BCBC, 'A0C ramp'); T(0x1BCC8, 'A0C ramp'); T(0x1BCEE, 'rpm -= A0C*28/16')
# +1B954, the drift's decay without steering
T(0x1B9A8, 'drift 9F8 decays a step'); G(0x1B9C0, '820, steps since the drift')
# +1BD1C, off the road
G(0x1BD6E, 'AB8, steps off the road (respawn past 5)')
# +1BDEC, surface drag
for a in (0x1BE30, 0x1BE4E, 0x1BE6C): T(a, 'rpm -= the surface\'s drag')
# +1BE70, the boost: its start and chain are decisions of the whole step
B(0x1BE86, 'if (ENG_SUB) goto L_1BED6;', 'substeps: no boost start or chain (the whole step decides)')
GE(0x1BEE0, 'r4 - 0xAC0u', 'AE0, the boost\'s countdown'); T(0x1BF22, 'ADC, the boost bar eased 1/8 a step')
# +1BF26, the engine
for a in (0x1BF84, 0x1BF92, 0x1BF9C, 0x1BFA2): T(a, 'rpm += throttle')
# +1C276, the body's pitch
for a in (0x1C2CE, 0x1C2E0, 0x1C2FA, 0x1C322, 0x1C32E): T(a, 'A24, the pitch, a step\'s change')
# +1CEC2, the AI's speed wave
G(0x1CFD8, 'C38, the speed wave\'s phase'); G(0x1D1AC, 'C38, the speed wave\'s phase')
# +1B560, the drift: start and end are decisions of the whole step
B(0x1B5DA, 'if (ENG_SUB) { MEM32(sp + 0x2Cu) = r3; goto L_1B794; }', 'substeps: no drift start or end')
T(0x1B808, 'drift 9F8 += 83C (state 3)'); T(0x1B892, 'drift 9F8 += 83C (state 1)')
G(0x1B83A, '838, steps in the drift state')
T(0x1B8CA, '830, the drift\'s ramp'); T(0x1B916, 'drift 9F8 += steering'); T(0x1B93A, 'drift 9F8 decays 15/16')
# +1EA64, contact
G(0x1EBAC, 'E4, steps pushing into a contact (a crash past 12)')
# +1ED4C, height
T(0x1EDE4, 'y += the ramp\'s rise (hang)'); T(0x1EE2E, 'y += A58 (in the air)'); T(0x1EEB2, 'y += A58 (the launch)')
T(0x1EE46, 'A58 -= gravity (0x14)'); T(0x1EE4C, 'A58 -= gravity (9)')
G(0x1EE26, 'A60, steps in the air'); G(0x1EE38, 'AF0, the hang\'s countdown')
G(0x1EF42, 'A68, a countdown'); G(0x1EF4C, 'A6C, a countdown')
T(0x1F030, 'steering -0x400 off the wall'); T(0x1F062, 'steering +0x400 off the wall')
# the car's orientation from its angles two substeps late: the game's step of lag from a heading change
# to the motion it steers (eng_alag; +704 feeds the slip check and the target velocity)
EDITS[0x1972E] = {'after': 'r1 = eng_alag(r6 + 0x38u, r1);', 'why': 'the heading +678 as two substeps ago'}
EDITS[0x19736] = {'after': 'r1 = eng_alag(r6 + 0x34u, r1);', 'why': 'the angle +674 as two substeps ago'}
EDITS[0x1973E] = {'after': 'r1 = eng_alag(r6 + 0x3Cu, r1);', 'why': 'the angle +67C as two substeps ago'}
# the wall contact's start (+B18 from 0 to -5/5: five steps of the heading pushed off the wall): the
# game decides it on the step's end position; a substep's position a third of the way along found
# contacts the game's step did not have (r12: the car still against the wall at substep 0 of the step
# after a scrape, a re-arm, and the push's floor of 20 turned it). The contact itself -- the position
# held out of the wall, the speed it takes -- stays every substep; its start waits for the step's last
# substep (substep 0 for a wreck, which moves on the whole step only)
B(0x1F038, 'if (!ENG_SUB_END(r5 - 0xB00u)) goto L_1F03C;', 'the contact starts at the step\'s end')
B(0x1F06A, 'if (!ENG_SUB_END(r5 - 0xB00u)) goto L_1F06E;', 'the contact starts at the step\'s end')
# the wall's heading push: the first one whole at the contact's substep (eng_kick), later ones thirded
EDITS[0x1F03A] = {'after': 'eng_kick_mark(r7 + 0x38u);', 'why': 'a contact starts (+B18 from 0): the first push is the whole step\'s'}
EDITS[0x1F06C] = {'after': 'eng_kick_mark(r7 + 0x38u);', 'why': 'a contact starts (+B18 from 0): the first push is the whole step\'s'}
R(0x1F132, 'MEM32(r7 + 0x38u) = r0 = eng_kick(r7 + 0x38u, r0);', 'heading -= the wall\'s turn (eng_kick)')
R(0x1F13A, 'MEM32(r7 + 0x38u) = r0 = eng_kick(r7 + 0x38u, r0);', 'heading += the wall\'s turn (eng_kick)')
# B18, the wall contact's countdown (5 steps): the game counts it after the step's contact check (a
# contact re-arms it only from 0, at the step after it ran out); counted on substep 0 the substeps
# after it saw 0 while the car was still against the wall and re-armed it (r11: a second push, the
# heading 14 off stock's after one scrape). Counted on the step's last substep -- on 0 for a wreck,
# whose movement runs on the whole step only (+1EA68)
for a_ in (0x1F144, 0x1F14E):
    GE(a_, 'r5 - 0xB00u', 'B18, the wall\'s countdown, at the step\'s end')
# +22832, the measure: a substep's displacement is a third of a step's
B(0x22854, 'if (!ENG->meas1) { MEM32(sp + 4u) *= 3u; MEM32(sp + 8u) *= 3u; MEM32(sp + 0xCu) *= 3u; }', 'velocity per step = 3 x the substep\'s displacement')
B(0x2286C, 'if (!ENG->meas1) { MEM32(sp + 0x1Cu) *= 3u; MEM32(sp + 0x20u) *= 3u; MEM32(sp + 0x24u) *= 3u; }', 'acceleration per step = 3 x the substep\'s change')
# +1EA64 for a wreck: nothing on the substeps; on the whole step its measure is the game's (x 1)
B(0x1EA68, 'if (ENG_SUB && !ENG_WRECK3 && MEM32(r0 + 0x34u) != 0xFFFFFFFFu) return 0;', 'substeps: a wreck does not move (A4_WRECK3: it does)')
B(0x1EABC, 'ENG->meas1 = !ENG_WRECK3 && MEM32(r4 + 0x34u) != 0xFFFFFFFFu;', 'a wreck\'s measure is a whole step\'s (A4_WRECK3: a substep\'s, x 3)')
B(0x1EAC0, 'ENG->meas1 = 0;', 'back to substep measures')
T(0x228F2, '0C4, the acceleration eased 1/8 a step')
# bench (A4_VELPROBE, phone round 68): +1FDEC's box test (+239B6) hit -- the two cars and their positions
B(0x1FE02, 'ENG_VP(5u, r4, r5, MEM32(r4 + 4u), MEM32(r4 + 0xCu), MEM32(r5 + 4u), MEM32(r5 + 0xCu)); '
  'ENG_VP(6u, r5, r4, MEM32(r5 + 4u), MEM32(r5 + 0xCu), MEM32(r4 + 4u), MEM32(r4 + 0xCu));',
  'bench (A4_VELPROBE): a car-to-car hit, the player in it')
# bench (A4_VELPROBE, phone round 69): a car on rails (+1F1F8's first branch: placed at its reference car's track
# index plus its own offset) -- the car, its type (+C70), the reference's index, the offset, the index it takes
B(0x1F24C, 'ENG_VPA(8u, r5, MEM32(r5 + 0xC70u), MEM32(r0 + 0x1Cu), MEM32(r4 + 0x1Cu), r1, r0);',
  'bench (A4_VELPROBE): a car on rails')
# bench (A4_TRAFDUMP, phone round 70): the traffic update's first argument, for the probe's traffic array
EDITS.setdefault(0x14574, {})
EDITS[0x14574]['before'] = 'ENG_TDARG(r0); eng_tsnap(r0); ' + EDITS[0x14574]['before']
# bench (A4_TRAFDUMP, round 71): the traffic before and after +FB12 (r4 is the race), to place a hit car's knock
EDITS[0x14574]['before'] = EDITS[0x14574]['before'] + ' ENG_TDREC(r4, 1u);'
B(0x14578, 'ENG_TDREC(r4, 2u);', 'bench (A4_TRAFDUMP): the traffic after +FB12')
# bench (A4_VELPROBE, phone round 69): a traffic car placed (+FC34, after +F60A): its place, the node's fraction,
# the node, the clock-derived index -- per substep, any car
B(0xFC34, 'ENG_VPA(9u, r5, MEM32(r5 + 4u), MEM32(r5 + 0xCu), S32(0x58u), S32(0x64u), S32(0x4Cu));',
  'bench (A4_VELPROBE): a traffic car placed')
# +2217C, the contact book
G(0x22198, 'E0, steps since a contact')
# +1F1F8, the AI: its choices (and the stuck check) on the whole step, its keys replayed
B(0x1F310, 'if (ENG_SUB) goto L_1F3CC;', 'substeps: no stuck check')
B(0x1F3CC, 'if (ENG_SUB) { uint32_t s_ = eng_ai_slot(r5); r6 = ENG->aik[s_]; MEM32(sp + 0x70u) = ENG->aia[s_]; goto L_1FCB0; }',
  'substeps: the whole step\'s AI keys')
B(0x1FCB4, 'if (!ENG_SUB) { uint32_t s_ = eng_ai_slot(r5); ENG->aik[s_] = r6; ENG->aia[s_] = MEM32(sp + 0x70u); }',
  'the AI keys the whole step chose')
# +FB12, the traffic: its place on the road from the clocks, a third and two thirds along on the substeps
for a in (0xFB58, 0xFB6A): R(a, 'r0 = eng_tmul(r0, r1, 0u); fN = r0 >> 31; fZ = r0 == 0;', 'clock 0 x speed, at the substep')
for a in (0xFB5E, 0xFB70): R(a, 'r1 = eng_tmul(r2, r1, 1u); fN = r1 >> 31; fZ = r1 == 0;', 'clock 1 x speed, at the substep')
G(0xFC4A, 'a traffic slot\'s step counter (to 45)'); G(0xFC90, 'a traffic slot\'s step counter (its path)')
# +12DEC, the camera: its placement's arguments, for the substeps
for a in (0x12E44, 0x12EA6, 0x12F8C, 0x12FDE, 0x13004): B(a, 'eng_cam_save(r0, r1, r2, r3, sp);', 'the camera placement the whole step made')
# round 63: after the whole step's camera update (+12DEC at +14EEC, its last placement), the chase camera's pose
# taken as an offset from the car and drawn a step late, the substeps moving it a third of the step's change each
B(0x14EF0, 'if (!ENG_SUB) eng_cam_whole(sp, r23);', 'the chase camera: its offset from the car, a step late')
# the pursuers' own driving (cars with +C3C set: +1D20A their target velocity, +1C48E their reaction
# after the move) -- the AI's choices, on the whole step; the target it sets stays in +680 for the substeps
G(0x1E5B2, 'a pursuer\'s target velocity (+1D20A): its AI, on the whole step')
G(0x1E8F2, 'a pursuer\'s reaction (+1C48E): its AI, on the whole step')
# Phone round 69: a pursuer (+C3C: the helicopter, the cops) is also moved by the game's logic once a step, not by
# its physics: +1C48E places it at an eased point off the car it follows (+1C696) or adds a hover wobble (+1C4FC,
# state 6), and a police car of type 2 is placed beside the player by the whole step's logic too. The measure in
# +1EA64 (+22832) takes 3 x (position - +0A8) as the step's velocity, so such a jump -- a whole step's -- came out
# x3: the next substeps carried the car twice the jump past, the next placement pulled it back, and the error
# flipped and doubled each step (cs69p: the helicopter flung 14,000 units; cs70p: police slot 9, 45,000). Counted
# once and spread over the step (eng_psnap / eng_pown / eng_pfix / eng_pstep, a4_eng.h): each pursuer's place and
# +0A8 at the whole step's start; its own move (+1E8C0) added up; at its move (+1EA64's entry) the rest of its
# change is the jump w -- the car is left 2w/3 short and the substeps give back w/3 each, so the measure reads its
# own move x3 plus w (stock: the step's move plus w) and the jump is drawn over the step, not at 11.72 Hz. Left
# alone when +0A8 itself was set in the step (a respawn, a hit) and for a wreck or type 5.
EDITS[0x1EA68]['before'] = 'if (!ENG_SUB) eng_pfix(r0); else eng_pstep(r0); ' + EDITS[0x1EA68]['before']
# Phone round 69: +2202C sets a car's velocity v (a whole step's move) by +010 = v and +0A8 = position - v, so the
# next measure (position - +0A8) reads v. At 3x the measure is 3 x that: v came out x3. The police reaction (+1CC18,
# from +1C48E: the cop placed at the car it follows plus an offset, +1CD14, then given that car's velocity, +1CD1E)
# made every following cop run three times the player's speed on the substeps and be pulled back each step -- the
# "comical overshoot" (cs70r: police slot 14, -127 on the whole step, +118 and +116 on the substeps); the police spawn
# (+20520, its call at +20610) the same once. For those two callers (by the return address the veneer passes),
# +0A8 = position - v/3. The other callers (traffic, rails, the stuck check, collisions) are left as they were.
B(0x2209E, 'if (ENG_IS3 && (lr0 == GAMEFN(0x1CD22) || lr0 == GAMEFN(0x20614))) { const uint32_t c_ = r4 - 0xA8u; '
           'r0 = MEM32(c_ + 4u) - (uint32_t)eng_sdiv((int32_t)MEM32(c_ + 0x10u), 3); '
           'r1 = MEM32(c_ + 8u) - (uint32_t)eng_sdiv((int32_t)MEM32(c_ + 0x14u), 3); '
           'r2 = MEM32(c_ + 0xCu) - (uint32_t)eng_sdiv((int32_t)MEM32(c_ + 0x18u), 3); }',
  'a pursuer given a velocity (the police reaction, the police spawn): +0A8 a third of it back, for the 3x measure')
# +2292A, the wreck's scripted path: its frame (+034) and step (+048) counters and its end, once a game step --
# on the wreck's last substep of the step (the whole step without A4_WRECK3, where the wreck runs on it only)
for a in (0x22BBA, 0x22BEA): GE(a, 'r4', '+048, the wreck path\'s step counter')
for a in (0x22BC0, 0x22BE4): GE(a, 'r4', '+034, the wreck path\'s frame')
GE(0x22C08, 'r4', '+034 = -1, the wreck path\'s end')
# A4_WRECK3: the rest of +2292A at a third a substep. Its height before the path moves it (the approach below)
B(0x2292A, 'if (ENG_WRECK3) { ENG->wy0 = L32(r0 + 8u); ENG->wyset = 0; }', 'A4_WRECK3: the wreck\'s height before this substep')
# the falling wreck (its controller's +B28 bit 0x800): position += velocity, y - 2 -- a third a substep
for a in (0x22964, 0x22972): EDITS[a] = {'third': 'ENG_WRECK3', 'why': 'A4_WRECK3: the falling wreck\'s move, a third'}
# its height: y + vy - 2 a step, the 2 a gravity on the velocity the measure takes -- a third of vy and a ninth
# of the 2 a substep (thirding y + vy - 2 whole would take 2 off the measured velocity every substep: x 3 gravity)
R(0x22976, '{ const uint32_t ea_ = r4 + 8u; if (ENG_WRECK3) { const int32_t vy_ = (int32_t)(r0 + 2u - L32(ea_)); '
           'r0 = L32(ea_) + (uint32_t)eng_thirdv(ea_, vy_) - (uint32_t)eng_thirdv(r4 + 0x1124u, eng_thirdv(r4 + 0x1128u, 2)); } L32(ea_) = r0; }',
  'the falling wreck\'s height (A4_WRECK3: a third of its velocity, a ninth of the step\'s 2)')
# the path's push on the velocity (+1C's vector x the entry's first word, +18's x its third) -- a third a substep
for a in (0x22A02, 0x22A48):
    B(a, 'if (ENG_WRECK3) { uint32_t k_; for (k_ = 0; k_ < 12u; k_ += 4u) S32(0x1Cu + k_) = (uint32_t)eng_thirdv(r4 + 0x1100u + k_, (int32_t)S32(0x1Cu + k_)); }',
      'A4_WRECK3: the push along +1C, a third')
B(0x22A8A, 'if (ENG_WRECK3) { uint32_t k_; for (k_ = 0; k_ < 12u; k_ += 4u) S32(0x58u + k_) = (uint32_t)eng_thirdv(r4 + 0x110Cu + k_, (int32_t)S32(0x58u + k_)); }',
  'A4_WRECK3: the push along +18, a third')
# the damping near the ground (v x (n - 1) / n, n the path's frames left): x (3n - 1) / 3n a substep
EDITS[0x22AC4] = {'after': 'if (ENG_WRECK3) S32(0xCu) = 3u * S32(0xCu);', 'why': 'A4_WRECK3: the damping, a third'}
# the position (+0A8, the last measured one, + the velocity): + a third of the velocity
EDITS[0x22B2E] = {'after': 'if (ENG_WRECK3) { L32(r4 + 4u) = L32(r4 + 0xA8u) + (uint32_t)eng_thirdv(r4 + 0x1118u, (int32_t)S32(0x34u)); '
                           'L32(r4 + 0xCu) = L32(r4 + 0xB0u) + (uint32_t)eng_thirdv(r4 + 0x1120u, (int32_t)S32(0x3Cu)); }',
                  'why': 'A4_WRECK3: the wreck\'s move along the path, a third'}
# the height: set from the path's frame (+ the ground, - the bounce); at 3x it closes on that point by the step's end
EDITS[0x22B46] = {'after': 'if (ENG_WRECK3) ENG->wyset = 1;', 'why': 'A4_WRECK3: the height is the path\'s'}
B(0x22B86, 'if (ENG_WRECK3 && ENG->wyset) { const int32_t t_ = (int32_t)L32(r4 + 8u), y0_ = (int32_t)ENG->wy0; '
           'const int32_t k_ = ENG_SUB_END(r4) ? 1 : 3 - (int32_t)ENG_SUB; L32(r4 + 8u) = (uint32_t)(y0_ + eng_sdiv(t_ - y0_, k_)); ENG->wyset = 0; }',
  'A4_WRECK3: the height closes on the path\'s point by the step\'s end')
# bench (A4_ENGDBG): the path the race step takes, per substep (eng.h's 'ENGS' record)
import os as _os
if '-DA4_ENGDBG' in _os.environ.get('A4_EXTRA_CFLAGS', ''):
    def D(a, bit, extra=''):
        e = EDITS.setdefault(a, {'why': 'bench: path bit %d' % bit})
        e['before'] = ('ENG->dbg[0] |= %du; %s' % (1 << bit, extra)) + e.get('before', '')
    D(0x14754, 0); D(0x146B2, 1); D(0x146D6, 2); D(0x146FC, 3)
    D(0x14974, 4, 'ENG->dbg[1] = r1; ')
    D(0x144FC, 5); D(0x1F2D0, 6); D(0x1FCBC, 7, 'ENG->dbg[2] = r1; '); D(0x1F226, 8); D(0x1433C, 9); D(0x14574, 10)
# +1FD0E, a car moved without its physics (+691E's modes): position += +6B0, a third on a substep
T(0x1FD28, 'position += 6B0 (the kinematic move)')
G(0x1FD56, 'A34, a step counter (the kinematic move)')
# +239B6, the car-car response, conjugated by the step: it reads each car's displacement so far (its
# position - +0A8, the previous position) as the step's velocity -- at 3x a substep's, a third: x 3 on
# reading (+23A50 the car, +23A5A the other), so its thresholds, constants and momentum see the game's.
B(0x23A54, 'MEM32(sp + 0x98u) *= 3u; MEM32(sp + 0x9Cu) *= 3u; MEM32(sp + 0xA0u) *= 3u;', 'the car\'s displacement as a step\'s')
B(0x23A5E, 'MEM32(sp + 0x8Cu) *= 3u; MEM32(sp + 0x90u) *= 3u; MEM32(sp + 0x94u) *= 3u;', 'the other\'s displacement as a step\'s')
# It gives each car its velocity after the hit by setting its previous
# position (+0A8 = position - the step's new displacement), which +22832 then measures. A substep's
# measure is 3 x the displacement, so the response's displacement is a third of the step's: +0A8 =
# position - third(position - +0A8). r6: the first 3x full race tripled the velocity a step after a
# contact (B4 154 -> 516 -> 1468) and the wreck it caused flung the car across the map.
for a, pa, pp in ((0x23D16, 0xBC, 0xC0), (0x23D56, 0xC4, 0xC8)):
    EDITS[a] = {'after': '{ uint32_t a_ = MEM32(sp + 0x%Xu), p_ = MEM32(sp + 0x%Xu), k_; for (k_ = 0; k_ < 12u; k_ += 4u) '
                         'MEM32(a_ + k_) = MEM32(p_ + k_) - (uint32_t)eng_thirdv(a_ + k_, (int32_t)(MEM32(p_ + k_) - MEM32(a_ + k_))); }' % (pa, pp),
                'why': 'the hit\'s new velocity, as a substep\'s displacement'}
# +1FF56, the car thrown over a traffic car: placed at its previous position + the traffic car's
# velocity (+010, a step's) + a lift; on a substep the traffic moved a third of that
B(0x20154, '{ uint32_t k_, a_ = MEM32(sp + 0x90u); for (k_ = 0; k_ < 12u; k_ += 4u) MEM32(sp + 0x30u + k_) = MEM32(a_ + k_) + (uint32_t)eng_thirdv(a_ + 0x40u + k_, (int32_t)MEM32(r4 + k_)); }',
  'the traffic car\'s substep, not its step')

# ---- applied in every build that lifts the function (not only the 3x engine's): the port's own hooks
# that a detour put into the game's code, which a lifted function replaces whole and so must call itself
ALWAYS = {
    # the HUD sprite painter (+29736, lifted for speed: round 50, 19% of the race's modelled CPU cycles):
    # a4_hud_tramp's call at +2973A, the end of the 3D pass -- a4_gl_hud composites the frame's GPU
    # image before the HUD is painted over it (the injector leaves the detour out when it is lifted)
    0x2973A: {'before': 'a4_gl_hud();', 'why': 'the end of the 3D pass: the frame composited before the HUD'},
    # the billboard blit +3015E: the injector's capture hook (+30252, a4_bbq_tramp) after the two loads it
    # displaces -- a4_gl_bb(r0..r7 as the trampoline pushes them, the blit's own sp): 1 = queued as a GPU
    # quad, straight to the epilogue
    # the mesh routine +349F8: a call of it is a generation of A4_MESHMEMO's corners (a4_gl.c), bumped at entry
    0x349F8: {'before': 'MESHMEMO_BUMP();', 'why': 'A4_MESHMEMO: a new call of the mesh routine'},
    0x30256: {'before': '{ uint32_t rg_[8] = { r0, r1, r2, r3, r4, r5, r6, r7 }; if (a4_gl_bb(rg_, (const uint32_t *)(uintptr_t)sp)) goto L_3031C; }',
              'why': 'the billboard capture hook +30252 (a4_bbq_tramp)'},
    # its mode 2 (additive, each channel saturating) row walk +30412..+30484, the game's Thumb in ARM C by
    # hand (a4_eng.h bb_row2: every register and stack word the walk leaves that is read again, set as the
    # game sets them); A4_BBCHECK runs it beside the lifted walk on the same row and compares at +30486
    0x30412: {'before': 'if (bb_row2(&r0, r1, r3, r4, r5, &r12, lr, spp)) goto L_30486;', 'why': 'the mode 2 row by hand'},
    0x30486: {'before': 'BB_ROW2_CHECK(r0, r12, spp);', 'why': 'A4_BBCHECK: the row by hand against the lifted walk'},
    # +CE8C, a mesh instance's set-up (lifted for speed): its two unpack loops by hand (a4_eng.h ce_verts,
    # ce_faces) at their guards; the registers they leave are all written again before any read, so only
    # memory is theirs. A4_CECHECK runs each beside the lifted loop on the same arrays and compares at its exit.
    # round 60, A4_GPUOBJ: an object the GPU draws itself (a4_gl.c a4_obj_take) skips the game's path; r0-r3 are
    # still the call's at +CE90 (the push and the sub have run), and +CF6A unwinds them
    0x0CE90: {'before': 'if (A4_OBJ_TAKE(r0, r1, r2, r3)) goto L_0CF6A;', 'why': 'A4_GPUOBJ: the object drawn by the GPU'},
    # round 67, bench (A4_CARCENSUS): the mesh routine's call after its transform -- the ctx, the vertices, their
    # count, the faces, their count, the mode and the caller (+34B16: a CBasicMesh, the cars)
    0x34A10: {'before': 'A4_CAR_CENSUS(S32(0x14u), S32(0x18u), S32(0x1Cu), S32(0x20u), S32(0x38u), S32(0x40u), lr0);',
              'why': 'A4_CARCENSUS: the car meshes as the GPU would take them'},
    # round 67, A4_GPUCAR: after the transform and the whole-mesh outcode check, a car the GPU draws skips the faces
    # (cam, verts, n, faces, n, the mode [sp+0x40], the no-back-face flag [sp+0x44], the caller)
    0x34A18: {'before': 'if (A4_CAR_TAKE(S32(0x14u), S32(0x18u), S32(0x1Cu), S32(0x20u), S32(0x38u), S32(0x40u), S32(0x44u), lr0)) goto L_34AE6;',
              'why': 'A4_GPUCAR: the car drawn by the GPU'},
    0x0CEC0: {'before': 'if (ce_verts(r0, r1, r2)) goto L_0CEDC;', 'why': 'the vertex unpack +CEC2..+CEDA by hand'},
    0x0CEDC: {'before': 'CE_CHECK(0);', 'why': 'A4_CECHECK: the vertex unpack by hand against the lifted loop'},
    0x0CEEA: {'before': 'if (ce_faces(r0, r1, r2, r4, L32(spp + 0x1Cu))) goto L_0CF4A;', 'why': 'the face unpack +CEEC..+CF48 by hand'},
    0x0CF4A: {'before': 'CE_CHECK(1);', 'why': 'A4_CECHECK: the face unpack by hand against the lifted loop'},
}

# Round 71 (phone round 70, "a ghost car"; yt73): a hit traffic car's knock -- +FB12 places it absolutely from a table
# entry the whole step alone steps (+FC90, G), so it jumped an entry a step. The whole step's places before +FB12 are
# kept (eng_tsnap, at +14574); a car placed through the knock branch (+FF7C's store) is drawn on the line from there
# to the game's place over the step (eng_tknock), after its measure (+22832 at +FF84), at the slot loop's +FF88.
B(0xFF7E, 'ENG->tkp = r5;', 'a knocked traffic car placed (its knock entry\'s height added)')
B(0xFF88, 'if (ENG->tkp) { if (ENG->tkp == r5) eng_tknock(r5); ENG->tkp = 0; }', 'a knocked traffic car: drawn on its step\'s line')
