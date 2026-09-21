"""Race music for the S60v3 build.

The game already plays one media file by name -- `intro.mid` -- through
CMdaAudioPlayerUtility, from a wrapper at 0xdad8 whose only caller is the
"start music" routine at 0x10812.  Everything here is built on that: the
filename becomes a template, one character of which is chosen at runtime, and
the race countdown calls the same routine.

  * `intro.mid` -> `bgm_0.wav`, same length, so no offset in the exe or in the
    SIS controller moves.  The tracks install as bgm_0 .. bgm_c (0-9, a-c).
  * Four bytes of BSS (the image declares none) hold the game object pointer,
    the one-shot track index, and the race counter.
  * Cave 1 runs inside the start routine: it saves the game pointer, reads the
    index, writes its character into the widened filename -- positioned from the
    end of the string, because what gets widened is the full path and not the
    bare name -- and resets the index so every other caller (the menus) gets
    bgm_0 back.
  * Cave 2 hangs off the `s_go!` countdown branch: it bumps the counter, sets
    the index, and calls the start routine.

Nothing stops the music at the finish line: the start routine already stops
whatever is playing, and the menu starts its own track on the way back.
"""
import struct
from thumb import *
import e32crc

CODE_OFF, CODE_BASE = 156, 0x8000

OLD_NAME, NEW_NAME = b'intro.mid', b'bgm_0.wav'
DIGIT_FROM_END = 5                 # 'X' in "...bgm_X.wav", counting from the end

BSS_BASE = 0x400008                # data section is 8 bytes; this is the BSS we add
BSS_SIZE = 8                       # +0 game pointer, +4 track index, +5 race counter
HOLDER_OFF = 0xd6c0                # start routine's own base -> music holder offset

START_MUSIC = 0x10812
SFX_SETVOL = 0x276ac               # the call the countdown makes right after `s_go!`

PATCH_IN_START = 0x1087a           # movs r2,#1 / ldr r0,[r4,#0x18] / movs r3,#0 / add r1,sp,#4
GO_CALL = 0x1a7fe                  # bl SFX_SETVOL in the `s_go!` branch

RACE_TRACKS = 10                   # bgm_1 .. bgm_a; bgm_0 is the menu track

STOCK = {
    PATCH_IN_START: '0122a069002301a9',
    GO_CALL: '0cf055ff',
}


# Every address below is an offset into the code section, which is how the
# disassembly is indexed; the file offset is that plus the header.
def fo(off):
    return off + CODE_OFF


def grow_code(img, extra):
    """Append zeroed space to the text section and move every file offset after it.

    Relocations and import entries are expressed as offsets into the code, and the
    space lands at its end, so none of them shift."""
    img = bytearray(img)
    u = lambda o: struct.unpack_from('<I', img, o)[0]
    text_end = u(0x64) + u(0x60)
    assert u(0x30) == u(0x60), 'code size and text size differ; exports in the way?'
    out = bytearray(img[:text_end]) + bytes(extra) + img[text_end:]
    for off in (0x30, 0x60, 0x7c):                       # code size, text size, payload size
        struct.pack_into('<I', out, off, u(off) + extra)
    for off in (0x68, 0x6c, 0x70, 0x74):                 # data, import, code reloc, data reloc
        if u(off):
            struct.pack_into('<I', out, off, u(off) + extra)
    return out


def build_caves(cave_va):
    """Returns (bytes, cave1_va, cave2_va). Literal pools sit at the end of each cave."""
    # --- cave 1: runs inside the start routine, just before it calls the wrapper ---
    c1 = cave_va
    body1, n = [], 0
    def at(i):
        return c1 + i * 2
    # placeholders resolved after the layout is known
    lit1 = None
    ins = []
    ins.append(('ldr_pc', 2))        # r2 = BSS_BASE
    ins.append(('ldr_pc', 3))        # r3 = HOLDER_OFF
    ins.append(subs_r(3, 4, 3))      # r3 = game base
    ins.append(str_r(3, 2, 0))       # save it
    ins.append(ldrb_r(3, 2, 4))      # r3 = track index
    ins.append(movs(1, 0))
    ins.append(strb_r(1, 2, 4))      # one-shot: back to the menu track
    ins.append(cmp_i(3, 9))
    ins.append(('ble', 'digit'))
    ins.append(adds_i(3, ord('a') - ord('0') - 10))
    ins.append(('label', 'digit'))
    ins.append(adds_i(3, ord('0')))
    # The routine widened a full path, not the bare name, so count back from the
    # end: the character sits five before the terminator in "...bgm_X.wav".
    ins.append(ldr_sp(1, 0x10))          # the length the routine stashed
    ins.append(subs_i(1, DIGIT_FROM_END))
    ins.append(lsls(1, 1, 1))
    ins.append(strh_rr(3, 6, 1))
    ins.append(movs(2, 1))           # the four instructions the call site gave up
    ins.append(ldr_r(0, 4, 0x18))
    ins.append(movs(3, 0))
    ins.append(add_sp(1, 4))
    ins.append(bx_lr())
    c1_words = [BSS_BASE, HOLDER_OFF]

    # --- cave 2: hangs off the countdown's `s_go!` branch ---
    ins2 = []
    ins2.append(push([14]))
    ins2.append(('bl', SFX_SETVOL))
    ins2.append(('ldr_pc', 2))       # r2 = BSS_BASE
    ins2.append(ldrb_r(1, 2, 5))     # race counter
    ins2.append(adds_i(1, 1))
    ins2.append(cmp_i(1, RACE_TRACKS))
    ins2.append(('ble', 'keep'))
    ins2.append(movs(1, 1))
    ins2.append(('label', 'keep'))
    ins2.append(strb_r(1, 2, 5))
    ins2.append(strb_r(1, 2, 4))     # the index cave 1 will consume
    ins2.append(ldr_r(0, 2, 0))      # saved game pointer
    ins2.append(cmp_i(0, 0))
    ins2.append(('beq', 'out'))
    ins2.append(('bl', START_MUSIC))
    ins2.append(('label', 'out'))
    ins2.append(pop([15]))
    c2_words = [BSS_BASE]

    def assemble(ins, words, base):
        # first pass: addresses
        addr, labels = base, {}
        for i in ins:
            if isinstance(i, tuple) and i[0] == 'label':
                labels[i[1]] = addr
            elif isinstance(i, tuple) and i[0] == 'bl':
                addr += 4
            else:
                addr += 2
        pool = (addr + 3) & ~3
        out, addr = bytearray(), base
        widx = 0
        for i in ins:
            if isinstance(i, tuple) and i[0] == 'label':
                continue
            if isinstance(i, tuple) and i[0] == 'ldr_pc':
                out += struct.pack('<H', ldr_pc(i[1], addr, pool + widx * 4)); widx += 1; addr += 2
            elif isinstance(i, tuple) and i[0] == 'bl':
                out += bl(addr, i[1]); addr += 4
            elif isinstance(i, tuple) and i[0] == 'ble':
                out += struct.pack('<H', ble(addr, labels[i[1]])); addr += 2
            elif isinstance(i, tuple) and i[0] == 'beq':
                out += struct.pack('<H', beq(addr, labels[i[1]])); addr += 2
            else:
                out += struct.pack('<H', i); addr += 2
        out += bytes((pool - addr) % 4)
        for w in words:
            out += struct.pack('<I', w)
        return bytes(out)

    b1 = assemble(ins, c1_words, c1)
    c2 = c1 + len(b1)
    b2 = assemble(ins2, c2_words, c2)
    return b1 + b2, c1, c2


def patch(img):
    img = bytearray(img)
    assert img.count(OLD_NAME) == 1, 'expected exactly one %r' % OLD_NAME
    img[img.index(OLD_NAME):img.index(OLD_NAME) + len(OLD_NAME)] = NEW_NAME

    for va, expect in STOCK.items():
        got = bytes(img[fo(va):fo(va) + len(expect) // 2]).hex()
        assert got == expect, 'stock mismatch at %#x: %s != %s' % (va, got, expect)

    struct.pack_into('<I', img, 0x44, BSS_SIZE)          # the image declares no BSS; give it some

    cave_va = struct.unpack_from('<I', img, 0x60)[0]
    probe, _, _ = build_caves(cave_va)
    img = grow_code(img, (len(probe) + 3) & ~3)
    caves, c1, c2 = build_caves(cave_va)
    img[fo(cave_va):fo(cave_va) + len(caves)] = caves

    img[fo(PATCH_IN_START):fo(PATCH_IN_START) + 8] = bl(PATCH_IN_START, c1) + struct.pack('<HH', nop(), nop())
    img[fo(GO_CALL):fo(GO_CALL) + 4] = bl(GO_CALL, c2)

    out = e32crc.fix(bytes(img))
    assert e32crc.stored(out) == e32crc.compute(out)
    return out, c1, c2


if __name__ == '__main__':
    src = open('asphalt2_full_patched.exe', 'rb').read()
    out, c1, c2 = patch(src)
    open('asphalt2_full_patched_audio.exe', 'wb').write(out)
    print('caves at %#x / %#x; image %d -> %d bytes' % (c1, c2, len(src), len(out)))
