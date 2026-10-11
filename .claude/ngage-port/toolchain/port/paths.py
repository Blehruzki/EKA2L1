"""paths.py -- where the toolchain finds what does not live in this directory.

Five scripts used to name the development machine's paths outright: the
Symbian source release (gen_shim.py's .def files), EKA2L1's own epoc6/epoc9
lists, its palette header (aificon.py), the SIS template (mksis.py) and the
emulator's E: drive (build_release.py, newgame.py, picture.py). That is what
kept the toolchain from leaving this machine.

Each path now resolves in this order:

1. an environment variable (NGPORT_*), if set;
2. the portable kit's own copy, when this directory carries `kitdata/`
   (mkkit.py builds that folder: the .def files, the template, the header,
   and an E: drive of the kit's own for the cards it is given);
3. the development machine's path, as before -- so nothing changes here.
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))
KIT = os.path.join(HERE, 'kitdata')
IN_KIT = os.path.isdir(KIT)


def _pick(env, kit_rel, default):
    value = os.environ.get(env)
    if value:
        return value
    if IN_KIT:
        return os.path.join(KIT, *kit_rel.split('/'))
    return default


# The Symbian Foundation source release, or the part of it the shim generator
# reads: every eabi/*.def (9.x ordinals) and kernel/eka/bmarm/7.0-euseru.def
# (the N-Gage's euser), laid out as in the release.
SYMBIAN_SOURCE = _pick('NGPORT_SYMBIAN', 'symbian', '/home/user/symbiansource')

# EKA2L1's ordinal lists, the fallback for libraries the release has no .def for.
EPOC6_DEF = _pick('NGPORT_EPOC6', 'epoc6.def',
                  '/home/user/EKA2L1/src/emu/bridge/include/bridge/epoc6.def')
EPOC9_DEF = _pick('NGPORT_EPOC9', 'epoc9.def',
                  '/home/user/EKA2L1/src/emu/bridge/include/bridge/epoc9.def')

# The N-Gage's 256-colour palette, which the icon converter reads.
PALETTE_H = _pick('NGPORT_PALETTE', 'palette.h',
                  '/home/user/EKA2L1/src/emu/services/include/services/fbs/palette.h')

# The SIS every package is built from: EKA2L1's own EKA2L1HW sample.
SIS_TEMPLATE = _pick('NGPORT_SIS_TEMPLATE', 'EKA21LHW.sis',
                     os.path.normpath(os.path.join(HERE, '..', '..', '..', '..',
                                                   'native', 'EKA2L1HWD', 'sis', 'EKA21LHW.sis')))

# The card: where a title's own files live, <E_DRIVE>/system/apps/<stem>. On
# the bench that is the emulator's E: drive; a kit keeps one of its own.
E_DRIVE = _pick('NGPORT_E_DRIVE', 'e', '/root/.local/share/EKA2L1/data/drives/e')
APPS = os.path.join(E_DRIVE, 'system', 'apps')
