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
    0x14754: {'before': 'if (ENG_SUB) { MEM32(sp + 0xD0u) = ENG_KEYS; goto L_14948; }',
              'why': 'substeps 1-2: the whole step\'s keys'},
    0x14974: {'before': 'if (!ENG_SUB) ENG_KEYS = r1;',
              'why': 'the keys the whole step gave the player\'s control'},
    # after the movement loop: the substeps place the camera as the whole step's +12DEC last did (its
    # arguments saved at each of its +5968 calls: the camera race+0x48, the track, the car it follows,
    # the key object, the mode's stack words) on the moved cars, and end
    0x14CB2: {'before': 'if (ENG_SUB) { eng_cam_substep(sp, r23); return 0; }',
              'why': 'substeps 1-2: the camera on the moved car (its logic the whole step\'s), then done'},
    # the traffic's two clocks (race+0x378, +0x37C, passed to +FB12 in r2 and r3): their change over the
    # whole step, measured there, so the substeps can place the traffic a third and two thirds along it
    0x14574: {'before': 'if (!ENG_SUB) { ENG->tdel[0] = (int32_t)(r2 - ENG->tlast[0]); ENG->tdel[1] = (int32_t)(r3 - ENG->tlast[1]); '
                        'ENG->tlast[0] = r2; ENG->tlast[1] = r3; '
                        'if (ENG->tdel[0] < -0x100000 || ENG->tdel[0] > 0x100000) ENG->tdel[0] = 0; '
                        'if (ENG->tdel[1] < -0x100000 || ENG->tdel[1] > 0x100000) ENG->tdel[1] = 0; }',
              'why': 'the traffic clocks\' step, for the substeps'},
}

# ---- the rate map: the game's per-step changes, a third on each substep -----------------------------
# T: a rate -- the store keeps a third of its change (eng_third, the remainder carried per word).
# G: a counter or a decision's state -- it changes on the whole step only (substeps keep the old value).
def T(a, why): EDITS[a] = {'third': True, 'why': why}
def G(a, why): EDITS[a] = {'gate': True, 'why': why}
def B(a, code, why): EDITS[a] = {'before': code, 'why': why}
def R(a, code, why): EDITS[a] = {'replace': code, 'why': why}

# +1DE78, the car's control and dynamics
T(0x1DEDE, 'B20, steps a key is held (to 0x30): one a step')
G(0x1DF20, 'A2C, a step counter'); G(0x1DF28, 'A34, a step counter (the stuck check every 64)')
G(0x1DF58, 'A7C, a countdown'); G(0x1DF64, 'A7C, a count up')
T(0x1DF88, 'ABC, drained by a table value a step')
G(0x1DF9E, 'AEC, a countdown')
T(0x1DFA8, 'rpm -8 a step while AEC runs')
# the wreck sequence (+034 != -1: rpm -0x100 a step, gears down, the scripted path +2292A) runs on the
# whole step only: its path integrates the car's own velocity, which the substep measure would scale
# (r7: an opponent's wreck flung it across the map). The wreck moves at the game's 11.72 Hz.
B(0x1DFE6, 'if (ENG_SUB) return 0;', 'substeps: a wreck waits for the whole step')
G(0x1E39A, 'C48, the respawn countdown')
T(0x1E602, 'grip 9FC, +0xD55 a step on the road')
B(0x1E684, '{ uint32_t k_; for (k_ = 0; k_ < 12u; k_ += 4u) MEM32(sp + 0x10Cu + k_) = (uint32_t)eng_thirdv(r4 + 0x10u + k_, (int32_t)MEM32(sp + 0x10Cu + k_)); }',
  'the velocity change (target - velocity, traction-limited): a third of a step\'s')
T(0x1E7F8, 'drift 9F8 += f(turn)'); T(0x1E804, 'drift 9F8 += f(turn)')
T(0x1E87E, '81C, the turn held, += A20')
T(0x1E896, 'heading -= A20 (reversing)'); T(0x1E8A2, 'heading += A20')
R(0x1E8C0, '{ uint32_t k_; for (k_ = 0; k_ < 12u; k_ += 4u) { uint32_t ea_ = r0 + k_; MEM32(ea_) = eng_third(ea_, MEM32(ea_), MEM32(ea_) + MEM32(r1 + k_)); } }',
  'position += velocity (+521A0): a third of the step\'s')
# +1B9E0, steering
for a in (0x1BA28, 0x1BA52, 0x1BA7E, 0x1BA9A): T(a, 'A1C, the steering rate, eased a step')
for a in (0x1BA2C, 0x1BA56, 0x1BAA0): T(a, 'steering 7FC += A1C')
T(0x1BAF0, '808, steering eased 1/8 a step'); T(0x1BB1A, '80C, steering eased a step')
# +1BB1E, the brake
G(0x1BB3C, 'A08, steps the brake is held')
for a in (0x1BB4C, 0x1BB6C, 0x1BB76): T(a, 'A04, the brake\'s ramp')
T(0x1BB60, 'rpm -= brake'); T(0x1BB90, 'rpm -= 7DC/2 (key 4)')
# +1BB98, engine drag
T(0x1BC06, 'rpm -= slip'); T(0x1BC2E, 'rpm -= coast'); T(0x1BC54, 'rpm -= A74/64')
G(0x1BC5A, '7C4, the gear change\'s countdown')
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
G(0x1BEE0, 'AE0, the boost\'s countdown'); T(0x1BF22, 'ADC, the boost bar eased 1/8 a step')
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
T(0x1F132, 'heading -= the wall\'s turn'); T(0x1F13A, 'heading += the wall\'s turn')
G(0x1F144, 'B18, the wall\'s countdown'); G(0x1F14E, 'B18, the wall\'s countdown')
# +22832, the measure: a substep's displacement is a third of a step's
B(0x22854, 'if (!ENG->meas1) { MEM32(sp + 4u) *= 3u; MEM32(sp + 8u) *= 3u; MEM32(sp + 0xCu) *= 3u; }', 'velocity per step = 3 x the substep\'s displacement')
B(0x2286C, 'if (!ENG->meas1) { MEM32(sp + 0x1Cu) *= 3u; MEM32(sp + 0x20u) *= 3u; MEM32(sp + 0x24u) *= 3u; }', 'acceleration per step = 3 x the substep\'s change')
# +1EA64 for a wreck: nothing on the substeps; on the whole step its measure is the game's (x 1)
B(0x1EA68, 'if (ENG_SUB && MEM32(r0 + 0x34u) != 0xFFFFFFFFu) return 0;', 'substeps: a wreck does not move')
B(0x1EABC, 'ENG->meas1 = MEM32(r4 + 0x34u) != 0xFFFFFFFFu;', 'a wreck\'s measure is a whole step\'s')
B(0x1EAC0, 'ENG->meas1 = 0;', 'back to substep measures')
T(0x228F2, '0C4, the acceleration eased 1/8 a step')
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
# the pursuers' own driving (cars with +C3C set: +1D20A their target velocity, +1C48E their reaction
# after the move) -- the AI's choices, on the whole step; the target it sets stays in +680 for the substeps
G(0x1E5B2, 'a pursuer\'s target velocity (+1D20A): its AI, on the whole step')
G(0x1E8F2, 'a pursuer\'s reaction (+1C48E): its AI, on the whole step')
# +2292A, the wreck's scripted path: its frame (+034) and step (+048) counters, once a game step
for a in (0x22BBA, 0x22BEA): G(a, '+048, the wreck path\'s step counter')
for a in (0x22BC0, 0x22BE4): G(a, '+034, the wreck path\'s frame')
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
