#!/usr/bin/env python3
"""Read the port's execution log, and diff two of them.

    readlog.py <log> [--game <6rbc.app>] [--tail N] [--counts]
    readlog.py <log> --diff <other log>

`gate6.cpp` appends one eight-byte record per event to C:\\g6box.log: what
happened, and the address it was called from as an offset into the loaded
image. Codes under 900 are import indices, which `gen_shim` names; 900 and up
are breadcrumbs planted in the game's own code, which `kCrumb` in gate6.cpp
places.

The point of the log over the sixteen-entry ring it replaced is the diff: run
the same build in the emulator and on the phone, line the two up, and the
first place they part company is the answer.
"""
import struct
import sys

# No default game. This was one hardcoded path -- Asphalt 2's 6rbc.app --
# and `readbox.py` decoded every title's box through it, so round 92's fault
# at import 251 printed as `RLine::EnumerateCall` when this image's 251 is
# `User::AllocL`. The indices are right and the names are another game's,
# which is worse than no names at all. `--game <name>` now, resolved by
# `picture.image`, or a path to the `.app` if that is what is meant.
CRUMB_FIRST = 900


def game_image(arg):
    """`--game`'s value: a title's name, or a path to its image."""
    import picture
    if arg and ('/' in arg or arg.lower().endswith('.app')):
        return arg
    return picture.image(arg)


NOTE_LOG_WRAP = 780
LOG_HDR_BYTES = 24
LOG_HEAD_BYTES = 256 * 1024


def read(path):
    """The log, oldest record first, whether or not the file wrapped.

    The log has a frozen head and a ring behind it. Three header records
    at the front hold the write pointer, where the head ended, and whether
    the ring has come round; the head runs from the header to
    LOG_HEAD_BYTES and is written once; the ring runs from there to the end
    of the file and wraps.

    Before this the file was written once and stopped at a megabyte, so
    every crash late in a run happened past the end of its own log -- round
    92 and round 93 both. A file with no header records reads exactly as it
    used to.

    `logringtest.py` exercises all four states this can be in. The version
    that shipped in build 007 had two header records instead of three and
    dropped the whole ring whenever it had not yet wrapped, which is the
    state a short run ends in -- so the instrument built to catch a late
    crash would have lost the records of one.
    """
    d = open(path, 'rb').read()
    recs = [struct.unpack_from('<II', d, 8 * i) for i in range(len(d) // 8)]
    hdr = LOG_HDR_BYTES // 8
    if len(recs) < 2 or recs[0][0] != NOTE_LOG_WRAP or recs[1][0] != NOTE_LOG_WRAP:
        return recs                      # a log from before the ring
    ring_start = LOG_HEAD_BYTES // 8
    if recs[2][0] != NOTE_LOG_WRAP:
        return _legacy(recs, ring_start)  # builds 007 and 008: two records
    write, head_end, wrapped = recs[0][1] // 8, recs[1][1] // 8, recs[2][1]
    if not (hdr <= head_end <= len(recs)) or not (hdr <= write <= len(recs)):
        return recs[hdr:]                # a header that makes no sense
    head = recs[hdr:min(head_end, ring_start)]
    if not wrapped:
        # The ring has not come round, so it holds one run of records from
        # its start up to the write pointer -- and nothing past it. Reading
        # to the end of the file here is what build 007 got wrong: it read
        # the zeroes after the live records as records.
        return head + (recs[ring_start:write] if write > ring_start else [])
    return head + _ring(recs, ring_start, write)


def _legacy(recs, ring_start):
    """Builds 007 and 008, whose header is two records and cannot say where
    the ring's newest record is while the ring has not yet wrapped.

    Those two builds are on a phone as this is written, so their logs have
    to open. The missing write pointer is recoverable: the file is created
    fresh each run, so the ring's unwritten part is zeroes, and the live
    records end where the trailing run of them begins. A zero record is
    `(0, 0)`, and no real record has code 0.
    """
    hdr = 2
    ring_at, head_end = recs[0][1] // 8, recs[1][1] // 8
    if not (hdr <= head_end <= len(recs)):
        return recs[hdr:]
    head = recs[hdr:min(head_end, ring_start)]
    return head + _ring(recs, ring_start,
                        ring_at if ring_start < ring_at <= len(recs) else 0)


def _ring(recs, ring_start, write):
    """The ring's records, oldest first.

    Two things have to happen and the order matters. The writer wraps
    *before* the block that would not fit, so on the first time round the
    very end of the file is still unwritten -- zeroes -- and those are not
    records. After the second time round there are none, because every byte
    has been written at least once. So trim the trailing zeroes first, then
    rotate at the write pointer, which is where the oldest live record sits.
    """
    ring = list(recs[ring_start:])
    while ring and ring[-1] == (0, 0):
        ring.pop()
    k = (write - ring_start) if write else 0
    if 0 < k <= len(ring):
        return ring[k:] + ring[:k]
    return ring


def names(game):
    """-> {import index: signature}, best effort."""
    try:
        import gen_shim
        rows, _dlls = gen_shim.build(game)
        return {r[0]: (r[3] or '?') for r in rows}
    except Exception as exc:                        # the log still reads fine
        print('(no import names: %s)' % exc, file=sys.stderr)
        return {}


def late_crumbs():
    """Markers planted after the decryptor has run, read out of gate6.cpp."""
    import os, re
    src = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'gate6.cpp')
    try:
        text = open(src).read()
        body = text[text.index('static const u32 kLateCrumb[] = {'):]
        body = body[:body.index('};')]
        return {940 + i: v for i, v in enumerate(re.findall(r'0x0[0-9a-f]+', body))}
    except Exception:
        return {}


def crumbs():
    """-> {marker: planted offset}, read out of gate6.cpp so it cannot drift."""
    import os
    import re
    src = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'gate6.cpp')
    try:
        text = open(src).read()
        body = text[text.index('static const u32 kCrumb[] = {'):]
        body = body[:body.index('};')]
        out = {CRUMB_FIRST + i: v for i, v in enumerate(re.findall(r'0x0[0-9a-f]+', body))}
    except Exception:
        out = {}
    out.update(late_crumbs())
    out.update(probes())
    return out


def probes():
    """Probe markers, read out of kProbe in gate6.cpp."""
    import os, re
    src = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'gate6.cpp')
    try:
        text = open(src).read()
        body = text[text.index('static const Probe kProbe[] = {'):]
        body = body[:body.index('};')]
        return {990 + i: m for i, m in
                enumerate(re.findall(r'\{\s*(0x0[0-9a-f]+)', body))}
    except Exception:
        return {}


# Notes whose payload is an address: the same run on two machines loads the
# image somewhere different, so the number is worth printing and not comparing.
ADDRESS_NOTES = {850, 851, 852, 853, 854, 855, 856, 857, 858, 859, 875, 876, 891,
                 801, 803, 705, 722, 724, 725, 726, 727, 729, 732, 733, 734, 731}

# 850..857 are reused: the names on the right are what gate6.cpp writes
# there now, and the ones on the left are what the same codes meant when the
# display work was using them. Reading a sound trace as "APP UI VPTR CHANGED"
# is how E201's Open call nearly went unnoticed.
NOTES = {794: 'OLD VTABLE object / vtable / mark (2 doc, 3 app UI)', 795: '   old slot',
         749: 'UI OVERRIDE forwarded: old slot<<24 | offset (7 foreground, 9 system event, 17 command)',
         750: 'HandleCommandL on the wrapper: command',
         751: 'WINDOW GC stand-in built over the real CWindowGc at',
         752: 'CODE PATCH applied at image offset',
         753: 'BITGDI CONTEXT stand-in: real CFbsBitGc (0xDE1 then it: deleted)',
         754: 'IMAGE FOUND: drive<<16 | layout<<8 | bin',
         755: 'OPEN RETRY on C: after an E: failure: the error, then the retry',
         756: 'CTRAPCLEANUP stand-in: real object (0xDE1 then it: deleted)',
         757: 'FONT asked of the screen: px | bold<<8, then CFont* or error, then its height in pixels (0xF2EE then font: released)',
         758: 'GC TEXT: f0 then font (UseFont); 7e then x<<16|y, length, first chars (DrawText)',
         759: 'FILE READ(pos, des, len): pos, len, result, length after',
         760: 'FILE SIZE: size, then result',
         761: 'GAME.ID read (ReadFileSection): the real result, then 1 when the shim answered it',
         762: 'LEAVE raw: the hook sp, then stack words',
         763: 'EXIT asked (CEikAppUi::Exit): the thread exits with reason 0',
         764: 'FPA DOUBLES re-ordered for 9.x: register helpers hooked, then Math functions hooked',
         765: 'SCREEN MODES (window-gc title): w<<16|h of the whole-screen wrapper, then mode<<16|inset',
         766: 'VA_LIST re-pointed for FormatList: the game array, then the va pointer it held',
         768: 'THREAD DONE: a game thread function returned: the function, then its result',
         772: 'CTL VENEER: a bit per old control slot that is a veneer into cone, then the wrapper base Draw and FocusChanged kept',
         773: 'OLD DELETE: a 9.x object the game owns given a vtable it can delete through -- the real vtable (once per class), then the object',
         774: 'DEFAULT PATH: SetDefaultPath -- the length kept, then SetSessionPath; 0x5E55xxxx a later Connect given it (xxxx the answer)',
         775: 'CLEANUP ITEM (bench): 0xC1EA0000|count, then each item of the 9.x cleanup stack, operation then pointer, a null operation marking a level',
         776: 'OLD PUSH: CleanupStack::PushL(CBase*) of a game object, pushed as an item that runs its old destructor',
         777: 'THREAD OPEN: RThread::Open by the game\'s name for a thread the port renamed -- the real answer, then the Duplicate from the game\'s own handle (-1 none matched)',
         778: 'FACTORY: CEikonEnv::AppUiFactory() answered with the null-object factory -- its address, then the dummy every slot of it answers',
         715: 'FPA SELFTEST: import, r0, r1',
         719: 'THREAD CALL REFUSED (0x5050 Suspend, 0x4E50 Resume), then the handle no live object has',
         718: 'AO PRIORITY: the constructor site, the priority it asked, the one given (0x0B1Ennnn: kick object nnnn built, then its address; 0x0BEAiccc: object i completed itself ccc times since the last beat)',
         717: 'WORKER FAULT (0xE5E7nnnn: nn game threads given the handler): 0xFA17000x, last import, handler sp, type, code, fault address, status, cpsr, r0..r15, then 24 stack words',
         716: 'TIMER PAIR: index, the game object, its stand-in (0xD70000nn: pair nn destroyed)',
         714: 'RANGE PROBE: site index, the register out of range, the second register (0x9Axxxxxx: planted at)',
         779: 'OWN CANCEL: CActive::Cancel on an object of the game\'s own class, forwarded -- the object, its iStatus, its iFlags',
         771: 'CTL WINDOW: the word at 0x28, DrawableWindow() answer, scan index<<16|hit, what the old control got, then the session buffer',
         769: 'COEENV word (24, once): the real 9.x CCoeEnv from its first word',
         770: 'DSA BEFORE StartL (once): the resolved StartL, 24 words of the real CDirectScreenAccess, then heap free, biggest, cells, bytes, 0x0ff5e7xx the window index, then gc<<16|dev<<8|rgn offsets',
         767: 'FORMAT CALL (GAME_LOG_TEXT): f0a7 Format / f0a8 FormatList, three words of the format text, the argument word, (bare %s) its two words; f0a9 then the length produced',
         825: 'MODE NOW, after a hold changed it', 824: 'CFG WROTE: mode<<16 | error', 838: 'SCREEN FIT pair', 839: 'SCREEN SRC', 840: 'SCREEN DST: inset<<16 | mode<<12 | first pixel',
         860: 'slot entered', 850: 'Cancel on / ngage lr', 851: 'STRAY Cancel on / SOUND',
         852: 'image loaded at / sound msg', 853: 'chunk ends at / MDA CALL',
         854: 'APP UI VPTR CHANGED to / MDA arg', 855: 'decrypted literal / MDA vtable',
         856: '  and it points at a word / MDA code',
         857: 'r5 = / MDA CALLBACK', 858: '    word', 859: '    text', 870: 'returned', 871: '  by import',
         872: '  asked for', 873: 'about to call import',
         890: 'ordinal asked of a library that is not open',
         875: 'lookup on handle', 876: '   answered', 877: 'DRIVER CALL refused, euser ordinal',
         878: 'tick', 879: 'cell size', 881: 'buffer sits in the cell at',
         880: 'OVERFLOW: the server was given a maximum of',
         882: 'probe b', 883: 'free', 884: '  matched a live cell of',
         885: '  DOUBLE FREE of', 886: '  STRAY: never allocated',
         887: '  cell header word',
         888: '    next cell', 889: '  called from',
         874: 'lookup ordinal old<<16|new',
         891: 'RFile::Read into', 892: '   descriptor word',
         893: '   answered', 894: 'RFile::Size answered',
         897: 'RFile::Open answered', 898: '   name', 899: 'ALLOC FAILED, bytes',
         861: '   descriptor word',
 895: '  >> watched field', 896: '  >> would read at +0x240',
 800: 'zlib uncompress at', 801: '   dest', 802: '   room in it',
 803: '   source', 804: '   compressed bytes', 805: '   stream starts',
 806: '   ZLIB SAID', 807: '   bytes written',
 808: '   heap free', 809: '   biggest cell',
 810: 'heap: cells out', 811: '   bytes out',
 812: 'BIG alloc', 813: '   asked from', 814: 'hist band<<24|count',
 815: 'alloc site', 816: '   calls', 817: '   KB in all',
 818: '   system RAM free', 819: 'heap peak',
 820: 'AknLayoutUtils::LayoutMetricsRect resolved to',
 821: '   main pane corner', 822: '   inset adopted',
 823: 'saved choice read', 824: '   choice written', 825: 'picture mode now',
 826: 'status-pane entry point', 827: '   the CEikStatusPane',
 828: '   pane reports itself gone',
 700: 'heartbeat', 701: '** TIMER CANCELLED **',
 702: '   iStatus', 703: '   iFlags (1 active, 2 pending)',
 704: '   our DoCancel returned (0 nothing, 1 completed, 2 no wrapper)',
 705: 'CActive::Cancel returned on', 706: 'BUILD',
 708: '   completions so far (all, then timer)', 709: '   completing status at',
 710: 'WS EVENT type', 711: '   ws event handled', 712: 'FOREGROUND EVENT (1 gain, 0 lose)',
 713: '   foreground event handled',
 720: '**** FAULT **** TExcType', 721: '   exc code (0 prefetch, 1 data abort, 2 undef)',
 722: '   fault address', 723: '   cpsr', 724: '   sp', 725: '   lr', 726: '   pc',
 727: '   r0..r12', 728: '   pc as image offset', 729: 'bench: sent to back via',
 730: 'SetExceptionHandler answered',
 731: 'ACTIVE SCHEDULER QUEUE, after event', 732: '   object', 733: '      vptr',
 734: '      RunL ->', 735: '      iStatus', 736: '      iFlags', 737: '   objects (or BADx)', 738: 'frame timer priority (game, then ours)', 739: '      priority',
 740: 'mixin: ordinal/thunk/offset/word/vtable, or CA11 self a b', 741: 'bench: DeactivateActiveViewL',
 742: 'DSA drawing region: count, then corners x<<16|y', 743: '   clip: tl, br, then mode (0 none, 1 all, 2 box, 3 rects)',
 744: 'FAULT frame recovered from the stack at',
 745: 'FAULT handler entered with (pointer = frame, small = TExcType)',
 746: 'FAULT raw: handler sp, then stack words',
 748: 'SYSAGT: a11 armed (then the status), ca0 cancelled, bad second notify',
 747: 'TRAP: 5e7 handler installed (orig), e11 enter (TTrap), 1ea leave (reason), 7e5 longjmp (TTrap), f0c bench forced leave',
 781: 'DSA ABORTED by wserv, reason', 782: 'DSA RESTART, reason (0x57a7: StartL by us; 0x57a0 StartL entered, 0x57a1 StartL returned)'}

# How many slots of each wrapper's vtable are a copy of a real one. Past that
# is the margin gate6.cpp pads with, and a call landing there is the framework
# asking for a slot the class was not measured to have.
OBJECTS = {1: ('app', 18), 2: ('doc', 23), 3: ('appui', 45),
           4: ('control', 44), 5: ('timer', 6)}


def slot_name(code):
    obj, i = code >> 8, code & 0xFF
    if obj not in OBJECTS:
        return 'slot %x' % code
    name, n = OBJECTS[obj]
    return '%s slot %d%s' % (name, i, '   PAST THE END' if i >= n else '')


def label(code, imports, marks):
    if code >= CRUMB_FIRST:
        return 'marker %d at %s' % (code, marks.get(code, '?'))
    if code == 860:
        return '-- entered'
    if code in NOTES:
        return '-- %s' % NOTES[code]
    return 'import %-4d %s' % (code, imports.get(code, '?'))


def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        return
    path = args[0]
    game = game_image(args[args.index('--game') + 1]
                      if '--game' in args else None)
    events = read(path)
    imports, marks = names(game), crumbs()

    if '--diff' in args:
        other = read(args[args.index('--diff') + 1])
        if '--no-slots' in args:
            # The framework works its way through our vtables in its own order,
            # and two feature packs do not agree on it. That is noise in a diff
            # and detail in a listing, so it comes out here and stays there.
            events = [e for e in events if e[0] != 860]
            other = [e for e in other if e[0] != 860]

        def same(a, b):
            """A note whose payload is an address says nothing across machines."""
            if a is None or b is None:
                return False
            if a[0] != b[0]:
                return False
            return a[0] in ADDRESS_NOTES or a[1] == b[1]

        for i in range(max(len(events), len(other))):
            a = events[i] if i < len(events) else None
            b = other[i] if i < len(other) else None
            if not same(a, b):
                print('part company at record %d of %d / %d' % (i, len(events), len(other)))
                for k in range(max(0, i - 4), min(max(len(events), len(other)), i + 5)):
                    ea = events[k] if k < len(events) else ('-', 0)
                    eb = other[k] if k < len(other) else ('-', 0)
                    mark = '  <<' if k == i else ''
                    print('%6d  %-44s | %-44s%s'
                          % (k,
                             'end' if ea[0] == '-' else label(ea[0], imports, marks) + ' from %x' % ea[1],
                             'end' if eb[0] == '-' else label(eb[0], imports, marks) + ' from %x' % eb[1],
                             mark))
                break
        else:
            print('identical, %d records' % len(events))
        return

    if '--counts' in args:
        counts = {}
        for code, _from in events:
            counts[code] = counts.get(code, 0) + 1
        for code in sorted(counts, key=lambda c: -counts[c]):
            print('%8d  %s' % (counts[code], label(code, imports, marks)))
        return

    tail = int(args[args.index('--tail') + 1]) if '--tail' in args else 40
    print('%d records' % len(events))
    for i, (code, frm) in enumerate(events[-tail:], start=len(events) - min(tail, len(events))):
        if code == 860:
            shown = slot_name(frm)
        elif code == 859:
            # two UTF-16 characters to a record, low half first
            shown = ''.join(chr(h) if 32 <= h < 127 else '.'
                            for h in (frm & 0xFFFF, frm >> 16))
        elif code == 706:
            shown = '%d (0x%x)' % (frm, frm)        # the build number, in decimal: 0x19 is 25
        elif code == 754:
            shown = '%s: layout %d %s' % (chr(frm >> 16), (frm >> 8) & 0xFF, '.bin' if frm & 0xFF else '.app')
        else:
            shown = '%x' % frm
        print('%6d  %-44s from %s' % (i, label(code, imports, marks), shown))


if __name__ == '__main__':
    main()
