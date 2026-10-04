#!/usr/bin/env python3
"""readstall.py <g6stall-<stem>.dat> [--game <title>]

The watchdog's dump (gate6.cpp, gate6_watchdog), from a thread of the
port's own, when the main thread's heartbeat has stopped for
WATCHDOG_STALL_S seconds. Counters, the last BOX_RING traced calls (oldest
first, as the ring wraps), the kick objects' self-completion counts, and the
main thread's active scheduler queue read from memory.

Since build 008 (round 131) one 6 KB slot per stall, up to four, and after
the queue each thread's registers (RThread::Context: r0-r15, cpsr), a second
pc/lr/sp 100 ms later, and its stack from sp up -- 192 words for the main
thread. Words inside the game's code are shown as game+offset.
"""
import struct, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

SLOT = 1536

def thread_block(w, k, code_base, names):
    h, prio = w[k], w[k + 1]
    ctx = w[k + 2:k + 20]
    pc2, lr2, sp2, top, limit, n = w[k + 20:k + 26]
    stack = w[k + 26:k + 26 + n]
    k += 26 + n
    if prio >= 1 << 31: prio -= 1 << 32
    def where(v):
        if code_base and code_base <= v < code_base + 0x200000:
            return 'game+%x' % (v - code_base)
        return '%08x' % v
    print('  thread handle %08x  priority %d  stack %08x..%08x' % (h, prio, limit, top))
    if not any(ctx):
        print('    no context (refused or not available)')
    else:
        print('    pc %s  lr %s  sp %08x  cpsr %08x' % (where(ctx[15]), where(ctx[14]), ctx[13], ctx[16]))
        print('    r0-r12 ' + ' '.join('%08x' % x for x in ctx[:13]))
        print('    100 ms later: pc %s  lr %s  sp %08x  (%s)' %
              (where(pc2), where(lr2), sp2, 'moved' if (pc2, sp2) != (ctx[15], ctx[13]) else 'same'))
    if stack:
        print('    stack, %d words from sp (game code marked):' % n)
        for i, v in enumerate(stack):
            if code_base and code_base <= v < code_base + 0x200000:
                print('      sp+%03x  %08x  game+%x' % (4 * i, v, v - code_base))
    return k

def slot(w, names):
    beats, traced, last, frames, completes, kicks = w[1:7]
    print('episode %d: heartbeats %d   traced calls %d   last import %d %s   frames %d   completions %d' %
          (w[0] & 0xFFF, beats, traced, last, names.get(last, ''), frames, completes))
    k = 7
    print('kick objects built: %d' % kicks)
    for i in range(16):
        obj, cnt = w[k], w[k + 1]; k += 2
        if obj: print('  kick %2d  %08x  %d self-completions' % (i, obj, cnt))
    ring = w[k]; k += 1
    ent = [(w[k + 2 * i], w[k + 2 * i + 1]) for i in range(ring)]; k += 2 * ring
    start = traced % ring
    print('the last %d traced calls, oldest first:' % ring)
    for i in range(ring):
        idx, frm = ent[(start + i) % ring]
        print('  import %-4d %-46s from %x' % (idx, names.get(idx, '')[:46], frm))
    objs = w[k]; k += 1
    print('main thread scheduler queue, %d objects:' % objs)
    for i in range(objs):
        o, vt, runl, st, fl, pr = w[k:k + 6]; k += 6
        if pr >= 1 << 31: pr -= 1 << 32
        print('  %5d  obj %08x vt %08x RunL %08x  iStatus %08x flags %x' % (pr, o, vt, runl, st, fl))
    if k >= len(w) or (w[k] >> 16) != 0x7EAD:
        return
    nthr = w[k] & 0xFFFF
    code_base, pos, writes, w9m, w9o, p11m, p11o, flags = w[k + 1:k + 9]; k += 9
    print('stream: Position calls %d, WriteL %d (main %d, other %d), Position main %d other %d' %
          (pos, writes, w9m, w9o, p11m, p11o))
    print('screen lost %d, clip mode %d, background mute %d;  code base %08x' %
          (flags & 0xFF, (flags >> 8) & 0xFF, flags >> 16, code_base))
    print('threads, main first: %d' % nthr)
    for i in range(nthr):
        if k + 26 > len(w): break
        k = thread_block(w, k, code_base, names)

def main():
    args = sys.argv[1:]
    game = None
    if '--game' in args:
        i = args.index('--game'); game = args[i + 1]; del args[i:i + 2]
    d = open(args[0], 'rb').read()
    w = list(struct.unpack('<%dI' % (len(d) // 4), d))
    names = {}
    if game:
        import readlog
        n = readlog.names(readlog.game_image(game))
        names = n if isinstance(n, dict) else dict(enumerate(n))
    if (w[0] & 0xFFFFF000) != 0x57A11000:
        sys.exit('not a stall dump (magic %08x)' % w[0])
    if w[0] == 0x57A11000 and len(w) < SLOT:        # build 007: one dump, no slots
        slot(w, names)
        return
    for e in range(len(w) // SLOT):
        sw = w[e * SLOT:(e + 1) * SLOT]
        if (sw[0] & 0xFFFFF000) != 0x57A11000: break
        slot(sw, names)
        print()

if __name__ == '__main__':
    main()
