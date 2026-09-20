"""Point the game's music player at a WAV instead of the stock MIDI.

The filename lives as a plain 8-bit string in the text section and the install
path lives as UTF-16 in the SIS controller; both replacements are the same
length, so nothing moves and no offset in either file shifts.
"""
import e32crc

OLD, NEW = b'intro.mid', b'intro.wav'


def patch_exe(img):
    assert img.count(OLD) == 1, 'expected exactly one %r in the image' % OLD
    out = e32crc.fix(img.replace(OLD, NEW))
    assert e32crc.stored(out) == e32crc.compute(out)
    return out


def patch_controller(cbuf):
    old, new = OLD.decode().encode('utf-16-le'), NEW.decode().encode('utf-16-le')
    assert cbuf.count(old) == 1, 'expected exactly one install path ending %r' % OLD
    return bytearray(bytes(cbuf).replace(old, new))
