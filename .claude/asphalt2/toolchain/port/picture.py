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
    m = re.search(r'#define\s+%s\s+(\S+)' % name, src)
    if m:
        return m.group(1).strip('"')
    m = re.search(r'\b%s\s*=\s*([^,}]+)' % name, src)
    return m.group(1).strip() if m else None


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
