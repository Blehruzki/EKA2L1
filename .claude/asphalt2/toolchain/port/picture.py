#!/usr/bin/env python3
"""Where a game's picture geometry comes from -- and why no tool may default.

`dumppng.py`, `bandpng.py` and `cmpdump.py` all used to default to pitch 176,
height 208 and **origin 16**, with a docstring explaining that sixteen is
where the blit reads from. All three numbers are Asphalt 2's. Point one of
those tools at another game's buffer and it renders that game sixteen columns
out of phase, which looks exactly like a horizontal wrap -- the one this port
spent rounds 74 to 78 chasing for real, and then spent a day chasing again
after E230 when the renderer, not the port, was the thing out of phase.

So: no defaults. A tool asks for `--game <name>` and gets that title's
numbers out of `games/<name>/game.h`, or it is given all of them explicitly,
or it refuses to run. And when the numbers in `game.h` have never been
measured -- which is the state every new game starts in -- `measured` is
false and the caller is expected to say so in its output, because a rendering
made from a guess must not be mistaken for a rendering of the game.

    from picture import geometry, take_game_arg
    game, argv = take_game_arg(sys.argv[1:])
    g = geometry(game)              # or geometry(pitch=..., height=..., origin=...)
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
GAMES = os.path.join(HERE, 'games')


def games():
    """The titles that have a games/<name>/game.h."""
    if not os.path.isdir(GAMES):
        return []
    return sorted(d for d in os.listdir(GAMES)
                  if os.path.isfile(os.path.join(GAMES, d, 'game.h')))


def setting(game, name):
    """One #define or enum value out of a game's game.h, without a compiler."""
    path = os.path.join(GAMES, game, 'game.h')
    if not os.path.isfile(path):
        raise SystemExit('picture: no such game %r (have: %s)'
                         % (game, ', '.join(games()) or 'none'))
    src = open(path).read()
    # A quoted value first, and the whole of it. `(\S+)` stops at the first
    # space, which turned GAME_CAPTION "Asphalt 2" into "Asphalt" in every
    # build that read it -- harmless in the emulator, wrong on a phone's
    # application list, and wrong in an installer's text.
    m = re.search(r'#define\s+%s\s+"([^"]*)"' % name, src)
    if m:
        return m.group(1)
    m = re.search(r'#define\s+%s\s+(\S+)' % name, src)
    if m:
        return m.group(1)
    m = re.search(r'\b%s\s*=\s*([^,}]+)' % name, src)
    return m.group(1).strip() if m else None


# The game's own N-Gage image, which is where import names come from.
#
# `readlog.GAME` was one hardcoded path -- Asphalt 2's `6rbc.app` -- and
# `readbox.py` read every box through it. Point it at another title's box and
# the events still decode, the indices are still right, and every *name* is
# the wrong game's: round 92's fault at import 251 printed as
# `RLine::EnumerateCall` when this image's 251 is `User::AllocL`. A wrong name
# is worse than no name, because it reads as a finding. Same rule as the
# geometry: ask for a game, or be given the path, or refuse.
IMAGE_ROOTS = (
    '/root/.local/share/EKA2L1/data/drives/e/system/apps',
    '/root/.local/share/EKA2L1/data/drives/e.ngage/system/apps',
)


def stem(game):
    """The four characters a title's files live under, out of GAME_STEM_CHARS.

    Parsed rather than written down twice: the loader builds its paths from
    that same list, and two spellings of one name is how a package installs
    to a directory the loader never looks in.
    """
    path = os.path.join(GAMES, game, 'game.h')
    if not os.path.isfile(path):
        raise SystemExit('picture: no such game %r (have: %s)'
                         % (game, ', '.join(games()) or 'none'))
    m = re.search(r"#define\s+GAME_STEM_CHARS\s+(.*)", open(path).read())
    if not m:
        raise SystemExit('picture: no GAME_STEM_CHARS for %r' % game)
    return ''.join(re.findall(r"'(.)'", m.group(1)))


def image(game=None, path=None):
    """The title's `<stem>.app` -- or an exit saying which game to name."""
    if path:
        return path
    if not game:
        raise SystemExit('picture: no game image. Pass --game <%s> or the '
                         'path to the title\'s .app. There is no default: '
                         'one game\'s import table names another game\'s '
                         'calls, and a wrong name reads as a finding.'
                         % ('|'.join(games()) or 'name'))
    st = stem(game)
    tried = []
    for root in IMAGE_ROOTS:
        cand = os.path.join(root, st, st + '.app')
        tried.append(cand)
        if os.path.isfile(cand):
            return cand
    raise SystemExit('picture: no image for %r (stem %s). Tried:\n  %s'
                     % (game, st, '\n  '.join(tried)))


def geometry(game=None, pitch=None, height=None, origin=None, width=None):
    """{width, pitch, height, origin, measured, game} -- or an exit.

    `pitch` and `origin` are in **pixels**, as gate6.cpp uses them.
    """
    if game:
        def num(name):
            v = setting(game, name)
            return None if v is None else int(v, 0)
        g = dict(game=game, width=num('GAME_W'), pitch=num('GAME_PITCH'),
                 height=num('GAME_H'), origin=num('GAME_SRC_ORIGIN'),
                 measured=bool(num('GAME_PICTURE_MEASURED')))
    else:
        g = dict(game=None, width=width, pitch=pitch, height=height,
                 origin=origin, measured=True)
    if width is not None:
        g['width'] = int(width)
    if pitch is not None:
        g['pitch'] = int(pitch)
    if height is not None:
        g['height'] = int(height)
    if origin is not None:
        g['origin'] = int(origin)
    if g['width'] is None:
        g['width'] = g['pitch']
    missing = [k for k in ('pitch', 'height', 'origin') if g[k] is None]
    if missing:
        raise SystemExit('picture: no %s. Pass --game <%s> or give all of '
                         'pitch, height and origin explicitly. There is no '
                         'default: one game\'s numbers render another out of '
                         'phase, which reads as a horizontal wrap.'
                         % (', '.join(missing), '|'.join(games()) or 'name'))
    return g


def banner(g, out=sys.stderr):
    """One line saying where the numbers came from, and a warning if they are
    a guess. Printed by every tool that renders, so a picture made from
    placeholders cannot be read as a picture of the game."""
    where = 'games/%s/game.h' % g['game'] if g['game'] else 'the command line'
    print('picture: width %s pitch %s height %s origin %s  (from %s)'
          % (g['width'], g['pitch'], g['height'], g['origin'], where), file=out)
    if not g['measured']:
        print('picture: ** THESE NUMBERS HAVE NEVER BEEN MEASURED FOR THIS '
              'GAME. ** Anything rendered with them is a guess, and a wrong '
              'origin looks like a horizontal wrap.', file=out)


def take_game_arg(argv):
    """Pull `--game <name>` out of an argv list. Returns (game, rest)."""
    game = None
    rest = []
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == '--game' and i + 1 < len(argv):
            game = argv[i + 1]
            i += 2
            continue
        if a.startswith('--game='):
            game = a.split('=', 1)[1]
            i += 1
            continue
        rest.append(a)
        i += 1
    return game, rest


if __name__ == '__main__':
    g, rest = take_game_arg(sys.argv[1:])
    banner(geometry(g, *rest), out=sys.stdout)
