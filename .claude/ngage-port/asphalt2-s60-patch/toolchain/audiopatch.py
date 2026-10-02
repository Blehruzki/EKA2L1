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
  * Cave 2 hangs off race entry, just past the point where the menu track is torn
    down: it makes the call it replaced, bumps the counter, sets the index, and
    calls the start routine.
  * The initial volume scale the call site passes goes from 0 to full, so a track
    is audible without the menu flow that would otherwise raise it.

Nothing stops the music at the finish line: the start routine already stops
whatever is playing, and the menu starts its own track on the way back.
"""
import os
import struct
from thumb import *
import e32crc

CODE_OFF, CODE_BASE = 156, 0x8000

OLD_NAME, NEW_NAME = b'intro.mid', b'bgm_0.wav'
DIGIT_FROM_END = 5                 # 'X' in "...bgm_X.wav", counting from the end

BSS_BASE = 0x400008                # data section is 8 bytes; this is the BSS we add
BSS_SIZE = 20                      # +0 game pointer, +4 media index, +5 race counter,
                                   # +6 the "bgm_X" the sound engine is asked for,
                                   # +12 the sound bank, caught from the game
BSS_NAME = 6
BSS_BANK = 12
HOLDER_OFF = 0xd6c0                # start routine's own base -> music holder offset

START_MUSIC = 0x10812
RACE_ENTER_CALL = 0x10e5c          # whatever the race-entry branch does after it

PATCH_IN_START = 0x1087a           # movs r2,#1 / ldr r0,[r4,#0x18] / movs r3,#0 / add r1,sp,#4

# Race entry, found by probe rather than by reading: a diagnostic build made the
# stop wrapper fault and print the caller, and it was the teardown at 0x11756 --
# not the `s_go!` countdown branch, which never runs at all. The hook goes on the
# call right after that teardown, so the player it would otherwise destroy is the
# old one and the track we start is the new one.
RACE_ENTER = 0x1176c

# The game's own sound engine. Its effects keep playing through a race because a
# separate server thread mixes them, reached over client-server IPC -- whereas a
# media player lives on the main thread, whose race loop never services the open
# that would start it. So a race track goes through the engine as one more sound.
SFX_UNLOAD = 0x27878               # (bank, id)
SFX_LOAD = 0x2780e                 # (bank, id, name) -> reads Sounds\<name>.znd
SFX_PLAY = 0x27898                 # (bank, id, loop)
# Every place the game plays a sound, it follows with this: the per-sound volume
# byte at entry + 0x14 starts at zero, so a sound played without it is silent.
SFX_VOLUME = 0x276ac               # (bank, id, volume, 0)
MUSIC_VOLUME = 0x40                # what the game uses for its own effects
# The bank is reachable from an object graph that is not worth reconstructing, so
# cave 4 simply notes the one the game passes in every time it loads a sound.
SFX_LOAD_BODY = 0x27810            # movs r4,r0 / movs r6,r1
# s_3 and s_2 are registered and never played: their countdown branch is dead
# code, so the slot is free.
MUSIC_SOUND_ID = 0x13

RACE_TRACKS = 10                   # bgm_1 .. bgm_a; bgm_0 is the menu track

# The stock call starts the player at scale 0 and the menu flow raises it once the
# menu is up. Nothing raises it for a race, so the track opens, plays, and is
# inaudible -- and on a device whose maximum volume is small, (0 * max) >> 8 is
# exactly zero. The scale reaches the player through the open-complete callback,
# which reads it from the object, so it has to be right before the player exists:
# too late to set it after the call.
INITIAL_VOLUME = 0xff

# The player is created at EMdaPriorityNormal, and on the device the race's own
# sound stream already holds the audio policy by the time a track starts -- the
# track runs to completion unheard and only becomes audible once the race ends.
# This asks the policy for the device instead. If the engine sounds go quiet in
# exchange, two media clients cannot share this device at all and the music has
# to go through the game's own mixer rather than beside it.
MEDIA_PRIORITY = 100               # EMdaPriorityMax

NEW_FILE_PLAYER = 0x34ff4          # the import stub PlayFile calls
PLAYER_CALL = 0xdaf6               # blx NEW_FILE_PLAYER
PLAY_FILE_TAIL = 0xdafa            # str r0,[r4,#0x14] / pop {r3-r7,pc}

STOCK = {
    PATCH_IN_START: '0122a069002301a9',
    RACE_ENTER: 'fff776fb',
    SFX_LOAD_BODY: '04000e00',
    PLAYER_CALL: '27f07eea',
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
    ins.append(movs(2, 1))           # the four instructions the call site gave up,
    ins.append(ldr_r(0, 4, 0x18))    # except for the initial volume scale
    ins.append(movs(3, INITIAL_VOLUME))
    ins.append(add_sp(1, 4))
    ins.append(bx_lr())
    c1_words = [BSS_BASE, HOLDER_OFF]

    # --- cave 2: hangs off the countdown's `s_go!` branch ---
    ins2 = []
    ins2.append(push([4, 5, 6, 14]))
    ins2.append(mov_r(4, 0))         # r0 is the game object here
    ins2.append(('bl', RACE_ENTER_CALL))
    ins2.append(('ldr_pc', 2))       # r2 = BSS_BASE
    ins2.append(str_r(4, 2, 0))
    ins2.append(ldrb_r(1, 2, 5))     # race counter
    ins2.append(adds_i(1, 1))
    ins2.append(cmp_i(1, RACE_TRACKS))
    ins2.append(('ble', 'keep'))
    ins2.append(movs(1, 1))
    ins2.append(('label', 'keep'))
    ins2.append(strb_r(1, 2, 5))
    # spell "bgm_X" into the BSS, since the engine wants a name and code is read-only
    _known = os.environ.get('DIAG_SOUND')     # a stock sound, to test the call path
    for offset, ch in enumerate(_known[:4] if _known else 'bgm_'):
        ins2.append(movs(0, ord(ch)))
        ins2.append(strb_r(0, 2, BSS_NAME + offset))
    if _known:
        ins2.append(movs(0, ord(_known[4])))
    else:
        ins2.append(mov_r(0, 1))
        ins2.append(cmp_i(0, 9))
        ins2.append(('ble', 'digit'))
        ins2.append(adds_i(0, ord('a') - ord('0') - 10))
        ins2.append(('label', 'digit'))
        ins2.append(adds_i(0, ord('0')))
    ins2.append(strb_r(0, 2, BSS_NAME + 4))
    ins2.append(movs(0, 0))
    ins2.append(strb_r(0, 2, BSS_NAME + 5))
    ins2.append(adds_i2(6, 2, BSS_NAME))   # r6 = the name, kept across the calls
    ins2.append(ldr_r(5, 2, BSS_BANK))   # r5 = the bank cave 4 saw
    ins2.append(cmp_i(5, 0))
    ins2.append(('beq', 'done'))
    ins2.append(mov_r(0, 5))
    ins2.append(movs(1, MUSIC_SOUND_ID))
    ins2.append(('bl', SFX_UNLOAD))
    ins2.append(mov_r(0, 5))
    ins2.append(movs(1, MUSIC_SOUND_ID))
    ins2.append(mov_r(2, 6))
    ins2.append(('bl', SFX_LOAD))

    if os.environ.get('DIAG_LOADED'):
        # did the engine actually take the sound? its pointer lands at
        # bank + id*12 + 0x10; fault when it did not, so the dump names the case
        ins2.append(ldr_r(0, 5, MUSIC_SOUND_ID * 12 + 0x10))
        ins2.append(cmp_i(0, 0))
        ins2.append(('bne', 'loaded'))
        ins2.append(('ldr_pc', 3))
        ins2.append(ldr_r(3, 3, 0))
        ins2.append(('label', 'loaded'))
    ins2.append(mov_r(0, 5))
    ins2.append(movs(1, MUSIC_SOUND_ID))
    ins2.append(movs(2, 1))          # looping
    ins2.append(('bl', SFX_PLAY))
    ins2.append(mov_r(0, 5))
    ins2.append(movs(1, MUSIC_SOUND_ID))
    ins2.append(movs(2, MUSIC_VOLUME))
    ins2.append(movs(3, 0))
    ins2.append(('bl', SFX_VOLUME))
    ins2.append(('label', 'done'))
    ins2.append(pop([4, 5, 6, 15]))
    c2_words = [BSS_BASE] + ([0xDEAD0000] if os.environ.get('DIAG_LOADED') else [])


    def assemble(ins, words, base):
        # first pass: addresses
        addr, labels = base, {}
        for i in ins:
            if isinstance(i, tuple) and i[0] == 'label':
                labels[i[1]] = addr
            elif isinstance(i, tuple) and i[0] in ('bl', 'blx'):
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
            elif isinstance(i, tuple) and i[0] == 'blx':
                out += blx_imm(addr, i[1]); addr += 4
            elif isinstance(i, tuple) and i[0] == 'ble':
                out += struct.pack('<H', ble(addr, labels[i[1]])); addr += 2
            elif isinstance(i, tuple) and i[0] == 'beq':
                out += struct.pack('<H', beq(addr, labels[i[1]])); addr += 2
            elif isinstance(i, tuple) and i[0] == 'bne':
                out += struct.pack('<H', bne(addr, labels[i[1]])); addr += 2
            else:
                out += struct.pack('<H', i); addr += 2
        out += bytes((pool - addr) % 4)
        for w in words:
            out += struct.pack('<I', w)
        return bytes(out)

    ins4 = []
    ins4.append(('ldr_pc', 3))       # r3 = BSS_BASE
    ins4.append(str_r(0, 3, BSS_BANK))
    ins4.append(mov_r(4, 0))         # the two instructions the call site gave up
    ins4.append(mov_r(6, 1))
    ins4.append(bx_lr())
    c4_words = [BSS_BASE]

    ins3 = []
    ins3.append(movs(2, MEDIA_PRIORITY))
    ins3.append(('blx', NEW_FILE_PLAYER))
    ins3.append(str_r(0, 4, 0x14))   # PlayFile's own tail, which we jumped over
    ins3.append(pop([3, 4, 5, 6, 7, 15]))

    b1 = assemble(ins, c1_words, c1)
    c2 = c1 + len(b1)
    b2 = assemble(ins2, c2_words, c2)
    c3 = c2 + len(b2)
    b3 = assemble(ins3, [], c3)
    c4 = c3 + len(b3)
    b4 = assemble(ins4, c4_words, c4)
    return b1 + b2 + b3 + b4, c1, c2, c3, c4


def patch(img):
    img = bytearray(img)
    assert img.count(OLD_NAME) == 1, 'expected exactly one %r' % OLD_NAME
    img[img.index(OLD_NAME):img.index(OLD_NAME) + len(OLD_NAME)] = NEW_NAME

    for va, expect in STOCK.items():
        got = bytes(img[fo(va):fo(va) + len(expect) // 2]).hex()
        assert got == expect, 'stock mismatch at %#x: %s != %s' % (va, got, expect)

    struct.pack_into('<I', img, 0x44, BSS_SIZE)          # the image declares no BSS; give it some

    cave_va = struct.unpack_from('<I', img, 0x60)[0]
    probe, _, _, _, _ = build_caves(cave_va)
    img = grow_code(img, (len(probe) + 3) & ~3)
    caves, c1, c2, c3, c4 = build_caves(cave_va)
    img[fo(cave_va):fo(cave_va) + len(caves)] = caves

    img[fo(PATCH_IN_START):fo(PATCH_IN_START) + 8] = bl(PATCH_IN_START, c1) + struct.pack('<HH', nop(), nop())
    img[fo(RACE_ENTER):fo(RACE_ENTER) + 4] = bl(RACE_ENTER, c2)
    img[fo(PLAYER_CALL):fo(PLAYER_CALL) + 4] = bl(PLAYER_CALL, c3)
    img[fo(SFX_LOAD_BODY):fo(SFX_LOAD_BODY) + 4] = bl(SFX_LOAD_BODY, c4)

    out = e32crc.fix(bytes(img))
    assert e32crc.stored(out) == e32crc.compute(out)
    return out, c1, c2, c3, c4


if __name__ == '__main__':
    src = open('asphalt2_full_patched.exe', 'rb').read()
    out, c1, c2, c3, c4 = patch(src)
    open('asphalt2_full_patched_audio.exe', 'wb').write(out)
    print('caves at %#x / %#x / %#x / %#x; image %d -> %d bytes'
          % (c1, c2, c3, c4, len(src), len(out)))
