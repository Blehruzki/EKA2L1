#!/usr/bin/env python3
# predres.py <trace.regs> <spec-mark-pc> [skip]: round 41. The speculation's prediction against
# what the game's next step really did. Each speculation marks (a4_spec_mark .. mark4, at
# consecutive addresses 4 bytes apart) the player car and the camera as the last whole step
# left them (S) and a whole step on as the replayed step predicts (P). Speculations with the
# same S belong to one step; the next group's S is that step's successor. Prediction error =
# (P - S) - (S' - S): the car's heading, its place along and across its path, the camera's
# place and yaw, and the car against the camera (what the screen shows). A frame drawn at
# fraction q of a step shows q times that error, gone at the next step: "back and forth".
import sys, struct, math, statistics as st
d = open(sys.argv[1], 'rb').read(); a0 = int(sys.argv[2], 16)
skip = int(sys.argv[3]) if len(sys.argv) > 3 else 50
s32 = lambda v: v - (1 << 32) if v & 0x80000000 else v
wrap = lambda a: ((a + 1024) % 2048) - 1024
G = []; cur = {}
for i in range(len(d) // 68):
    e = struct.unpack_from('<17I', d, i * 68)
    k = e[0] - a0
    if k == 0: cur = {'S': (s32(e[1]), s32(e[2]), e[3] & 0x7FF), 'q': e[4] / 65536.0}
    elif k == 4 and 'S' in cur: cur['P'] = (s32(e[1]), s32(e[2]), e[3] & 0x7FF); cur['t'] = e[4]
    elif k == 8 and 'P' in cur: cur['CS'] = (s32(e[1]), s32(e[2])); cur['CP'] = (s32(e[3]), s32(e[4]))
    elif k == 12 and 'CS' in cur:
        cur['CSy'] = e[1] & 0x7FF; cur['CPy'] = e[2] & 0x7FF
        if G and G[-1]['S'] == cur['S']: G[-1]['qs'].append(cur['q']); G[-1]['same'] &= (G[-1]['P'] == cur['P'])
        else: cur['qs'] = [cur['q']]; cur['same'] = True; G.append(cur)
        cur = {}
G = G[skip:]
print('%d steps with a speculation; %d with the same prediction at every render of the step' % (len(G), sum(g['same'] for g in G)))
H = []; L = []; C = []; Y = []; A = []; QS = []
for g, h in zip(G, G[1:]):
    S, P, N = g['S'], g['P'], h['S']
    dp = math.hypot(P[0] - S[0], P[1] - S[1]); dn = math.hypot(N[0] - S[0], N[1] - S[1])
    if dn < 20 or not (0.6 < dp / max(dn, 1e-9) < 1.6): continue      # stopped, or not one step later
    QS.append(max(g['qs']))
    ux, uz = (N[0] - S[0]) / dn, (N[1] - S[1]) / dn
    ex, ez = P[0] - N[0], P[1] - N[1]
    L.append(((ex * ux + ez * uz) / dn, (ez * ux - ex * uz) / dn, max(g['qs'])))
    dh_true = wrap(N[2] - S[2]); dh_pred = wrap(P[2] - S[2])
    H.append((dh_pred - dh_true, dh_true, max(g['qs'])))
    cn = math.hypot(h['CS'][0] - g['CS'][0], h['CS'][1] - g['CS'][1])
    C.append((math.hypot(g['CP'][0] - h['CS'][0], g['CP'][1] - h['CS'][1]), cn))
    Y.append((wrap(g['CPy'] - g['CSy']) - wrap(h['CSy'] - g['CSy']), wrap(h['CSy'] - g['CSy'])))
    # the car against the camera: the car's offset from the camera, predicted against true, and its heading against the yaw
    rel = lambda car, cam: (car[0] - cam[0], car[1] - cam[1])
    rp, rn = rel(P, g['CP']), rel(N, h['CS'])
    A.append((math.hypot(rp[0] - rn[0], rp[1] - rn[1]), wrap((P[2] - g['CPy']) - (N[2] - h['CSy'])), wrap((N[2] - h['CSy']) - (S[2] - g['CSy']))))
def q(v, f): v = sorted(v); return v[min(len(v) - 1, int(f * len(v)))]
print('%d steps one step apart; the largest q drawn in a step: median %.2f, 90%% %.2f' % (len(H), q(QS, .5), q(QS, .9)))
T = [x for x in H if abs(x[1]) >= 6]
print('car heading: |error| median %.1f, 90%% %.1f, 99%% %.1f /2048 a step; turning steps (|true| >= 6 /2048, %d): error/true median %+.2f, 10-90%% %+.2f..%+.2f, beyond +-0.5: %.0f%%; |true| median %.1f'
      % (q([abs(x[0]) for x in H], .5), q([abs(x[0]) for x in H], .9), q([abs(x[0]) for x in H], .99), len(T),
         q([x[0] / x[1] for x in T], .5), q([x[0] / x[1] for x in T], .1), q([x[0] / x[1] for x in T], .9),
         100.0 * sum(1 for x in T if abs(x[0] / x[1]) > 0.5) / max(len(T), 1), q([abs(x[1]) for x in T], .5) if T else 0))
print('car place: error along the path (share of the step) median %+.3f, 10-90%% %+.3f..%+.3f; across |.| median %.3f, 90%% %.3f, 99%% %.3f'
      % (q([x[0] for x in L], .5), q([x[0] for x in L], .1), q([x[0] for x in L], .9), q([abs(x[1]) for x in L], .5), q([abs(x[1]) for x in L], .9), q([abs(x[1]) for x in L], .99)))
print('camera place: |error| median %.1f, 90%% %.1f, 99%% %.1f units against a step of median %.1f'
      % (q([c[0] for c in C], .5), q([c[0] for c in C], .9), q([c[0] for c in C], .99), q([c[1] for c in C], .5)))
TY = [y for y in Y if abs(y[1]) >= 6]
print('camera yaw: |error| median %.1f, 90%% %.1f, 99%% %.1f /2048; turning steps (%d): error/true median %+.2f, 10-90%% %+.2f..%+.2f'
      % (q([abs(y[0]) for y in Y], .5), q([abs(y[0]) for y in Y], .9), q([abs(y[0]) for y in Y], .99), len(TY),
         q([y[0] / y[1] for y in TY], .5) if TY else 0, q([y[0] / y[1] for y in TY], .1) if TY else 0, q([y[0] / y[1] for y in TY], .9) if TY else 0))
print('car against the camera: offset |error| median %.1f, 90%% %.1f, 99%% %.1f units; heading-less-yaw |error| median %.1f, 90%% %.1f, 99%% %.1f /2048 (its true change a step: median |%.1f|, 90%% %.1f)'
      % (q([a[0] for a in A], .5), q([a[0] for a in A], .9), q([a[0] for a in A], .99),
         q([abs(a[1]) for a in A], .5), q([abs(a[1]) for a in A], .9), q([abs(a[1]) for a in A], .99), q([abs(a[2]) for a in A], .5), q([abs(a[2]) for a in A], .9)))
