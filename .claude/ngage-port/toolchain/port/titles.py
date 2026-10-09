#!/usr/bin/env python3
"""titles.py -- the titles side by side, computed rather than remembered.

    titles.py            print the generated tables
    titles.py --write    put them into GAMES.md between its markers
    titles.py --check    exit 1 if GAMES.md's copy is stale

Everything here comes from the files that decide a build: each title's
`games/<t>/game.h`, its generated `gate_imports.h` and `shim.cpp`, and the
game image and data tree on the bench's E: drive. GAMES.md carries the
hand-written half (what the numbers mean, what each title lacks, how the
editions reach each one); this keeps the half that goes stale.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import e32imports  # noqa: E402
import readlog  # noqa: E402

TITLES = ('asphalt1', 'asphalt2', 'ashen', 'one', 'ngtest', 'colin', 'colin2')
LABEL = {'asphalt1': 'Asphalt UGT', 'asphalt2': 'Asphalt 2', 'ashen': 'Ashen',
         'one': 'One', 'ngtest': 'ngtest', 'colin': 'Colin McRae 2005',
         'colin2': 'Colin McRae front end'}
GAMES_MD = os.path.join(HERE, '..', '..', 'GAMES.md')
BEGIN, END = '<!-- titles.py begin -->', '<!-- titles.py end -->'

# Identity and packaging: shown in the identity table, not the knob matrix.
IDENTITY = {'GAME_APP_NAME', 'GAME_APP_UID3', 'GAME_BUILD', 'GAME_CAPTION',
            'GAME_INSTALL_TEXT', 'GAME_VENDOR', 'GAME_STEM_CHARS',
            'GAME_STEM_LEN', 'GAME_DATA_UID3'}

# Libraries an S60v3 phone does not have: the N-Gage's own, or ones the
# shim answers with stubs.
NGAGE_ONLY = ('ARENAFRAMEWORK', 'ARENAFOUNDATION', 'GAMECOMMS', 'GAMEUTILS',
              'NOKIAFC', 'PLPVARIANT', 'SYSAGT', 'BLUETOOTH.DLL', 'MSGS',
              'ETEL', 'INSOCK')

# What a title's imports say about how it works: a title has the row if any
# import's name contains the pattern. Names as gen_shim gives them; a few
# N-Gage calls go by an older name and do not show here (the Asphalts' and
# One's CDirectScreenAccess::NewL/StartL are ws32 348/350).
FINGERPRINT = [
    ('display', 'framebuffer poll', 'UserSvr::ScreenInfo('),
    ('display', 'screen device update', 'CFbsScreenDevice::Update('),
    ('display', 'clipping region', 'CFbsBitGc::SetClippingRegion('),
    ('display', 'bitmap rendering', 'CFbsBitmap::Create('),
    ('display', 'window gc', 'CCoeControl::SystemGc('),
    ('display', 'HAL display query', 'HAL::Get('),
    ('control', 'DrawNow', 'CCoeControl::DrawNow('),
    ('control', 'SetExtentToWholeScreen', 'CCoeControl::SetExtentToWholeScreen('),
    ('control', 'DrawableWindow', 'CCoeControl::DrawableWindow('),
    ('sound', 'audio stream', 'CMdaAudioOutputStream'),
    ('threads', 'RThread::Create', 'RThread::Create('),
    ('threads', 'RThread::Open by name', 'RThread::Open(TDesC16'),
    ('threads', 'RThread::Suspend', 'RThread::Suspend('),
    ('threads', 'RSemaphore', 'RSemaphore::'),
    ('threads', 'RMutex', 'RMutex::'),
    ('threads', 'RCriticalSection', 'RCriticalSection::'),
    ('timing', 'CTimer', 'CTimer::'),
    ('timing', 'CPeriodic', 'CPeriodic::'),
    ('timing', 'CIdle', 'CIdle::'),
    ('timing', 'RTimer', 'RTimer::'),
    ('files', 'RFile::Replace', 'RFile::Replace('),
    ('files', 'RFile::Seek', 'RFile::Seek('),
    ('files', 'RFs::MkDir', 'RFs::MkDir'),
    ('files', 'zlib uncompress', 'uncompress('),
    ('maths', 'Math:: (doubles)', 'Math::'),
    ('maths', 'TLex16::Val(double)', 'TLex16::Val(double'),
    ('maths', 'TRealFormat', 'TRealFormat'),
    ('text', 'Format', 'TDes16::Format('),
    ('text', 'FormatList', 'FormatList('),
    ('other', 'RLibrary', 'RLibrary::'),
    ('other', 'RChunk', 'RChunk::'),
    ('other', 'sockets', 'RSocket::'),
]


def game_h(t):
    s = open(os.path.join(HERE, 'games', t, 'game.h')).read()
    out = {}
    for m in re.finditer(r'^\s*#define\s+(GAME_\w+)(?:[ \t]+(.*?))?[ \t]*(?://.*)?$', s, re.M):
        out[m.group(1)] = (m.group(2) or '').strip()
    return out


def short(v, defs, key):
    if v is None:
        return '·'
    if v.startswith('{'):
        n = defs.get(key.replace('S', '_COUNT', 1) if key.endswith('PATCHES') else key + '_COUNT')
        n = defs.get(re.sub(r'(PATCHES|SHIFTS|PRIORITIES|SITES)$', lambda m: {
            'PATCHES': 'PATCH_COUNT', 'SHIFTS': 'SHIFT_COUNT',
            'PRIORITIES': 'PRIORITY_COUNT', 'SITES': 'SITES'}[m.group(1)], key), n)
        return 'list' if not n or n == v else '%s entr%s' % (n, 'y' if n == '1' else 'ies')
    v = v.strip('"')
    return v if len(v) <= 12 else v[:11] + '…'


def shim_counts(t):
    s = open(os.path.join(HERE, 'games', t, 'shim.cpp')).read()
    m = re.search(r'kShimTable\[\]\s*=\s*\{([^}]*)\}', s)
    words = [int(x, 16) for x in re.findall(r'0x[0-9A-Fa-f]+', m.group(1))]
    g = open(os.path.join(HERE, 'games', t, 'gate_imports.h')).read()
    hooks = [l for l in re.findall(r'^\s*(IMPORT_\w+)\s*=\s*(\d+)', g, re.M)]
    present = sum(1 for _, v in hooks if int(v) != 65535)
    dc = re.search(r'GATE_DIVERT_COUNT\s+(\d+)', g)
    divs = [int(x) for x in re.findall(r'\{\s*(\d+),\s*\d+,\s*\d+\s*\}', g.split('GATE_DIVERTS', 1)[1].split('\n', 1)[0])] \
        if 'GATE_DIVERTS' in g else []
    return len(words), sum(1 for w in words if w == 0), present, len(hooks), int(dc.group(1)) if dc else 0, divs


def image_facts(t):
    p = readlog.game_image(t)
    d = open(p, 'rb').read()
    h = e32imports.header(d)
    im = e32imports.imports(d, h)
    tree = os.path.dirname(p)
    n = sz = 0
    for r, _, fs in os.walk(tree):
        for f in fs:
            n += 1
            sz += os.path.getsize(os.path.join(r, f))
    dlls = [x.split('[')[0].upper() for x, _ in im]
    return h, sum(len(o) for _, o in im), dlls, n, sz


def tables():
    defs = {t: game_h(t) for t in TITLES}
    names = {t: readlog.names(readlog.game_image(t)) for t in TITLES}
    facts = {t: image_facts(t) for t in TITLES}
    shim = {t: shim_counts(t) for t in TITLES}
    L = []
    row = lambda cells: L.append('| ' + ' | '.join(cells) + ' |')
    head = lambda first: (row([first] + [LABEL[t] for t in TITLES]),
                          row(['---'] * (len(TITLES) + 1)))

    L.append('### Identity, image and package\n')
    head('')
    row(['stem'] + ['`%s`' % ''.join(re.findall(r"'(.)'", defs[t]['GAME_STEM_CHARS'])) for t in TITLES])
    row(['loader app'] + ['`%s`' % defs[t]['GAME_APP_NAME'].strip('"') for t in TITLES])
    row(['build in the tree'] + ['%03d' % int(defs[t]['GAME_BUILD']) for t in TITLES])
    row(['game UID3'] + ['0x%08x' % facts[t][0]['uid3'] for t in TITLES])
    row(['code'] + ['%d KB' % (facts[t][0]['code_size'] // 1024) for t in TITLES])
    row(['imports / DLLs'] + ['%d / %d' % (facts[t][1], len(facts[t][2])) for t in TITLES])
    row(['imports the shim leaves as stubs'] + ['%d' % shim[t][1] for t in TITLES])
    row(['hooks present / defined'] + ['%d / %d' % (shim[t][2], shim[t][3]) for t in TITLES])
    row(['diversions (old object to wrapper)'] + ['%d' % shim[t][4] for t in TITLES])
    row(['game files / size'] + ['%d / %.1f MB' % (facts[t][3], facts[t][4] / 1e6) for t in TITLES])
    row(['split data package'] + ['yes' if 'GAME_DATA_UID3' in defs[t] else 'no' for t in TITLES])
    row(['N-Gage-only libraries'] + [', '.join(x.replace('.DLL', '').lower() for x in facts[t][2] if x in NGAGE_ONLY) or '--' for t in TITLES])

    L.append('\n### What the imports say\n')
    L.append('`x` where the title imports a call of that kind (any overload). Names as gen_shim gives them; the direct-screen-access calls go by an older name and are not listed (the Asphalts and One use it, Ashen does not).\n')
    head('call')
    for area, label, pat in FINGERPRINT:
        row(['%s: %s' % (area, label)] + ['x' if any(pat in v for v in names[t].values()) else '' for t in TITLES])

    L.append('\n### cone `CCoeControl` calls, for S60 3.0\n')
    L.append('Every `CCoeControl` method the title imports. On S60 3.0 a cone method '
             'run on the game\'s old-layout control reads a flags pointer that is not '
             'there (DEVICES.md); **d** marks a call diverted to the wrapper. An undiverted '
             'one is safe only if it reaches cone through a slot the port routes (base-class '
             'veneers) or never runs on the old object: check on the N80 bench.\n')
    ctl = sorted({n for t in TITLES for n in names[t].values() if n.startswith('CCoeControl::')})
    head('method')
    for n in ctl:
        cells = []
        for t in TITLES:
            idx = [i for i, v in names[t].items() if v == n]
            cells.append('' if not idx else ('d' if any(i in shim[t][5] for i in idx) else 'x'))
        row(['`%s`' % n.replace('CCoeControl::', '')] + cells)

    L.append('\n### Knobs in `game.h`\n')
    L.append('`·` = not defined: the default in `gate6.cpp`, off or empty '
             '(`GAME_SRC_BPP` 16, `GAME_CARD_CID` the generic card). What each does: KNOBS.md.\n')
    keys = sorted(k for t in TITLES for k in defs[t] if k not in IDENTITY)
    keys = sorted(set(keys))
    keys = [k for k in keys if not re.search(r'_COUNT$', k)]
    head('knob')
    for k in keys:
        row(['`%s`' % k] + [short(defs[t].get(k), defs[t], k) for t in TITLES])
    return '\n'.join(L) + '\n'


def main():
    text = tables()
    if '--write' in sys.argv or '--check' in sys.argv:
        doc = open(GAMES_MD).read()
        a, b = doc.index(BEGIN) + len(BEGIN), doc.index(END)
        new = doc[:a] + '\n' + text + doc[b:]
        if '--check' in sys.argv:
            if new != doc:
                print('GAMES.md: the generated tables are stale -- run titles.py --write')
                sys.exit(1)
            print('GAMES.md: generated tables current')
            return
        open(GAMES_MD, 'w').write(new)
        print('GAMES.md: tables written')
        return
    print(text)


if __name__ == '__main__':
    main()
