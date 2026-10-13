#!/usr/bin/env python3
"""M4M: restore the original in-race CHEATS entry in the Asphalt Urban GT 2
preview 2.0.157 Pause menu (6RBC.APP).  Standard library only.

usage:  patch_preview_cheats_m4m.py INPUT_6RBC.APP OUTPUT_6RBC.APP
INPUT must be the pristine preview APP or the user-validated M4B APP.

Findings (static, preview 2.0.157 only):
  * Pause builder (case inside 0x100AC660) adds PAUSE/JUKEBOX(0)/SOUND(1)/
    RESUME(2)/RESTART(3)/QUIT(4)/EXIT GAME(7) via addItem 0x100CD9F0.
  * 0x100DB22C = original "add CHEATS entry" (TXT_INGAME_CHEATS, item id 6),
    a virtual override in the Pause class's +0x374 interface (vtable
    0x1015E2B8, thunk 0x100F4B9C).  Nothing ever calls it.
  * Everything else is still live: the builder creates the cheat selector
    widget at Pause+0x37C (0x100AB020); focus id 6 shows it; left/right on
    id 6 -> widget vtbl+0x14/+0x18; select id 6 -> widget vtbl+0x1C.
Patch: the EXIT GAME addItem call (0x100ADEA0) is redirected to a 4-word stub
in the unreferenced developer-widget constructor 0x10023510 which calls
0x100DB22C(menu) and then tail-calls addItem with the original arguments.
"""
import hashlib, struct, sys

CODE_OFF, CODE_LEN, BASE = 0x7C, 0x1615B0, 0x10000000
ORIG_SHA = 'aa9c88194d1012aa3e0c50dc84cc6e5b87e472112f009cb620ad1603312b53b3'
M4B_SHA  = '4fecdaf5d74c40cb303c855777dec0331e55570c8a2637b2d2f5c65454903b6d'
M4B_WORDS = {0x10099538: (0x0A000048, 0xE1A00000),
             0x100A2A50: (0x0A000046, 0xE1A00000),
             0x100A3468: (0x0A000021, 0xE1A00000)}

def b_enc(src, dst, link):
    off = (dst - (src + 8)) >> 2
    assert -(1 << 23) <= off < (1 << 23)
    return (0xEB000000 if link else 0xEA000000) | (off & 0xFFFFFF)

STUB = 0x10023510
ADD_ITEM, ADD_CHEATS, HOOK = 0x100CD9F0, 0x100DB22C, 0x100ADEA0
# va: (expected original word, new word, meaning)
PATCH = {
    HOOK:      (b_enc(HOOK, ADD_ITEM, True),  b_enc(HOOK, STUB, True),        'bl addItem(EXIT) -> bl stub'),
    STUB + 0:  (0xE92D4070,                    0xE92D4007,                     'push {r0,r1,r2,lr}'),
    STUB + 4:  (0xE1A06000,                    b_enc(STUB + 4, ADD_CHEATS, True), 'bl 0x100DB22C (add CHEATS, id 6)'),
    STUB + 8:  (0xE3A00034,                    0xE8BD4007,                     'pop {r0,r1,r2,lr}'),
    STUB + 12: (0xEB0110D0,                    b_enc(STUB + 12, ADD_ITEM, False), 'b addItem (EXIT GAME, id 7)'),
}

def fo(va): return va - BASE + CODE_OFF
def rd(buf, va): return struct.unpack_from('<I', buf, fo(va))[0]
def cksum(buf): return sum(struct.unpack_from('<%dI' % (CODE_LEN // 4), buf, CODE_OFF)) & 0xFFFFFFFF

def main(src, dst):
    buf = bytearray(open(src, 'rb').read())
    sha = hashlib.sha256(buf).hexdigest()
    if sha not in (ORIG_SHA, M4B_SHA):
        sys.exit('refusing: input SHA-256 %s is neither pristine preview nor M4B' % sha)
    if struct.unpack_from('<I', buf, 0x18)[0] != cksum(buf):
        sys.exit('refusing: input E32 code checksum mismatch')
    base_name = 'M4B' if sha == M4B_SHA else 'original'
    for va, (o, n) in M4B_WORDS.items():
        assert rd(buf, va) == (n if sha == M4B_SHA else o)
    for va, (o, n, what) in sorted(PATCH.items()):
        cur = rd(buf, va)
        if cur != o:
            sys.exit('refusing: word at %08x is %08x, expected %08x' % (va, cur, o))
        struct.pack_into('<I', buf, fo(va), n)
        print('%08x  file+%06x  %08x -> %08x  %s' % (va, fo(va), o, n, what))
    ck = cksum(buf)
    struct.pack_into('<I', buf, 0x18, ck)
    open(dst, 'wb').write(buf)
    print('base: %s   new code checksum %08x' % (base_name, ck))
    print('output SHA-256', hashlib.sha256(buf).hexdigest())

if __name__ == '__main__':
    if len(sys.argv) != 3: sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
