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
    # set the frame slots that part sets: race+0x48 (the camera), +0x800, +0x340, +0x9C0, the keys 0
    0x1433C: {'before': 'if (ENG_SUB) { MEM32(sp + 0x124u) = r4 + 0x48u; MEM32(sp + 0x11Cu) = r4 + 0x800u; '
                        'MEM32(sp + 0x114u) = r4 + 0x340u; MEM32(sp + 0x110u) = r4 + 0x9C0u; MEM32(sp + 0xD0u) = 0u; goto L_1452C; }',
              'why': 'substeps 1-2: straight to the traffic'},
    # after the car list's base (r6 = race + 0x3D0): the pursuit mode's position checks (mode 0x17) are rules
    0x14586: {'before': 'if (ENG_SUB) goto L_14672;',
              'why': 'substeps 1-2: the pursuit check waits for the whole step'},
    # the player's keys: built from the key handler on the whole step, replayed on the substeps (+1DE78
    # keeps the last keys in +B1C and finds presses itself, so a replay is a key held, not pressed again)
    0x14754: {'before': 'if (ENG_SUB) { MEM32(sp + 0xD0u) = ENG_KEYS; goto L_14948; }',
              'why': 'substeps 1-2: the whole step\'s keys'},
    0x14974: {'before': 'if (!ENG_SUB) ENG_KEYS = r1;',
              'why': 'the keys the whole step gave the player\'s control'},
    # after the movement loop: the substeps place the camera (+5968 with the step's own arguments: the
    # camera race+0x48, the track G+0x10, the player car, the key object; stack 0, 0, 0) and end
    0x14CB2: {'before': 'if (ENG_SUB) { MEM32(sp) = 0u; MEM32(sp + 4u) = 0u; MEM32(sp + 8u) = 0u; '
                        '{ uint64_t rr_ = a4_eng_call(GAMEFN(0x5968), MEM32(sp + 0x124u), MEM32(r5 + 0x10u) + 0x10u, MEM32(sp + 0xD8u), '
                        'MEM32(MEM32(r4 + 0x38u) + 0x1114u), sp, r23); (void)rr_; } return 0; }',
              'why': 'substeps 1-2: the camera on the moved car, then done'},
}
