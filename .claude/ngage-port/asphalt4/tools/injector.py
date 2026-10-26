"""injector.py <in.exe> <out.exe> -- build the Asphalt 4 "general" performance
edition: the verified ARM span rewrites dropped into the shipped Thumb filler
functions, plus the alpha-fill whole-function replacement.

Two kinds of hook:

  * whole-function (FILL_HOOKS): a Thumb->ARM veneer at the function entry sends
    every caller to the ARM replacement, which has the same AAPCS contract and
    returns normally. (This is what a4patch.py did for the fill.)

  * inner-loop (LOOP_HOOKS): a veneer at a function's inner-loop entry sends it
    to a trampoline (renderer/a4_tramp.s) that marshals the loop's live
    registers + stack slots into the verified rewrite, restores exactly the
    registers the surrounding code reads after the loop, and branches back into
    the Thumb function at the given return offset. The branch-back displacement
    is a .word placeholder in the trampoline the linker leaves and we patch here.

All new code is position independent (-fropi), appended past the code section;
later sections shift down and the E32 header CRC is recomputed. No absolute
address is written, so the relocation sections are untouched. Correctness of
each rewrite + trampoline is proven offline by renderer/{exit,tramp,transp_tramp}_
verify.py before anything is injected here.
"""
import os, struct, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REND = os.path.join(HERE, '..', 'renderer')
sys.path.insert(0, os.path.join(HERE, '..', '..', 'asphalt2-s60-patch', 'toolchain'))
import e32crc

LINK_BASE = 0x8000
CFLAGS = ['--target=armv5te-none-eabi', '-marm', '-mtune=arm1136j-s', '-O2', '-ffreestanding',
          '-fno-builtin', '-fno-jump-tables', '-fropi', '-mno-unaligned-access', '-Wall', '-Werror']

SOURCES = [os.path.join(REND, 'a4_tramp.s'),
           os.path.join(REND, 'a4_span_textured.c'),
           os.path.join(REND, 'a4_span_transparent.c'),
           os.path.join(REND, 'a4_vec.s'),
           os.path.join(REND, 'a4_fps.c'),
           os.path.join(REND, 'a4_gl.s'),
           os.path.join(REND, 'a4_gl.c'),
           os.path.join(HERE, '..', 'patch', 'fill.c')]

# MBX/PowerVR spike: link libGLES_CM and fill its IAT (the a4_gl_iat .word slots
# in a4_gl.s) so the loader resolves these ordinals. Order must match a4_gl.s /
# a4_gl.c. Added only with --gl.
GL_IMPORT_DLL = 'libgles_cm.dll'
GL_IMPORT_ORDINALS = [14, 17, 4, 3, 18, 132, 33, 31, 58, 108]
GL_SCRATCH = 0x00401000         # must match A4_GL_SCRATCH in a4_gl.c
GL_SCRATCH_SIZE = 0x480         # headroom over sizeof(struct a4_gl_state)

# fps instrumentation: carve scratch out of the grown .bss, and a User::TickCount
# IAT slot the detour resolves pc-relatively.
FPS_SCRATCH = 0x00400400        # must match A4_FPS_SCRATCH in a4_fps.c
FPS_SCRATCH_SIZE = 0x400        # headroom over sizeof(struct a4_fps_state)
TICKCOUNT_IAT = 0x50CE8         # euser ord 674 (User::TickCount) import slot
HOMETIME_IAT  = 0x50CF0         # euser ord 859 (TTime::HomeTime) import slot
# BLX-insertion detour: overwrite the 4 bytes at this code offset with a Thumb BLX
# to the trampoline; the trampoline re-executes the displaced instructions and
# resolves each listed (placeholder symbol -> IAT code offset) pc-relatively.
INSERT_HOOKS = {0x1E6: ('a4_fps_tramp', [('a4_fps_tick_iat', TICKCOUNT_IAT),
                                         ('a4_fps_home_iat', HOMETIME_IAT)])}
# plain BLX-insertion detours that just re-execute their displaced instructions
# (no IAT to patch): back-buffer-base cache at the textured filler's +0x33412.
CACHE_HOOKS = {0x33412: 'a4_cachebb_tramp'}

# whole-function entry offset -> replacement symbol (same contract, returns normally)
FILL_HOOKS = {0x2ABAA: 'a4_fill_alpha',   # translucent rectangle fill
              0x51D96: 'a4_vec_add',       # vec4 += vec4  (Lib3D math leaf)
              0x51DB8: 'a4_vec_mulsub'}    # vec4 -= s*vec4 (Lib3D math leaf)
# inner-loop entry offset -> (trampoline symbol, return-placeholder symbol, return offset)
LOOP_HOOKS = {
    0x3357A: ('a4_tex_tramp',    'a4_tex_ret',    0x3361C),
    0x33B16: ('a4_transp_tramp', 'a4_transp_ret', 0x33B42),
}

def u32(d, o): return struct.unpack_from('<I', d, o)[0]

def build_blob(link_addr, tmp):
    objs = []
    for src in SOURCES:
        o = os.path.join(tmp, os.path.basename(src) + '.o')
        # assembly doesn't take the C-only codegen flags (-Werror would reject them)
        extra = os.environ.get('A4_EXTRA_CFLAGS', '').split()
        flags = (['--target=armv5te-none-eabi', '-marm', '-fropi'] if src.endswith('.s')
                 else CFLAGS + extra)
        subprocess.check_call(['clang', *flags, '-c', src, '-o', o])
        objs.append(o)
    ld = os.path.join(tmp, 'a4.ld')
    open(ld, 'w').write('SECTIONS { . = 0x%X; .text : { *(.text*) *(.rodata*) } '
                        '.data : { *(.data*) *(.bss*) *(COMMON) } '
                        '/DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment) *(.ARM.attributes) } }\n' % link_addr)
    elf = os.path.join(tmp, 'a4.elf')
    subprocess.check_call(['ld.lld', '-T', ld, '-o', elf, '--no-undefined', '-e', '0', *objs])
    for line in subprocess.check_output(['llvm-objdump', '-h', elf], text=True).splitlines():
        p = line.split()
        if len(p) > 2 and p[1] == '.data' and int(p[2], 16):
            sys.exit('new code has writable data: it would need a data section of its own')
    binf = os.path.join(tmp, 'a4.bin')
    subprocess.check_call(['llvm-objcopy', '-O', 'binary', '-j', '.text', elf, binf])
    syms = {}
    for line in subprocess.check_output(['llvm-nm', elf], text=True).splitlines():
        p = line.split()
        if len(p) == 3: syms[p[2]] = int(p[0], 16)
    return bytearray(open(binf, 'rb').read()), syms

def veneer(code, off, target_link):
    """Thumb bx pc (+ nop) into ARM, then ARM b target, at code offset off."""
    at = off
    if at & 2:
        code[at:at + 2] = struct.pack('<H', 0x46C0); at += 2        # nop: bx pc must sit on a word
    code[at:at + 4] = struct.pack('<HH', 0x4778, 0x46C0)            # bx pc ; nop
    arm = at + 4
    disp = (target_link - (LINK_BASE + arm + 8)) >> 2
    assert -(1 << 23) <= disp < (1 << 23)
    code[arm:arm + 4] = struct.pack('<I', 0xEA000000 | (disp & 0xFFFFFF))
    return arm + 4 - off

def blx_insert(code, off, target_link):
    """Overwrite the 4 bytes at code offset `off` with a Thumb BLX to an ARM
    target (two-halfword, ARMv5 encoding). Self-checks the encoding by
    disassembling it back with capstone -- a wrong BLX would be a hard fault."""
    import capstone
    pc = LINK_BASE + off
    offset = target_link - ((pc + 4) & ~3)          # BLX bases on Align(PC+4,4)
    assert offset % 2 == 0 and -(1 << 22) <= offset < (1 << 22), offset
    hw1 = 0xF000 | ((offset >> 12) & 0x7FF)
    hw2 = 0xE800 | ((offset >> 1) & 0x7FF)          # H=01 -> BLX
    code[off:off + 4] = struct.pack('<HH', hw1, hw2)
    md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_THUMB)
    ins = next(md.disasm(bytes(code[off:off + 4]), pc))
    want = '#0x%x' % (target_link)
    if ins.mnemonic != 'blx' or want not in ins.op_str:
        raise AssertionError('BLX encode check failed: %s %s (wanted blx %s)'
                             % (ins.mnemonic, ins.op_str, want))

def add_gl_import(out, iat_code_off):
    """Append a libgles_cm.dll import entry (EABI import section) whose import
    blocks point at the a4_gl_iat slots (code offsets), so the loader resolves
    GL_IMPORT_ORDINALS in place. Rebuilds the import section, shifts the reloc
    sections after it, bumps dll_ref_count, and re-verifies the slots hold the
    expected ordinals. Returns the new image bytes."""
    code_off   = u32(out, 0x64)
    dll_count  = u32(out, 0x54)
    imp_off    = u32(out, 0x6C)
    code_reloc = u32(out, 0x70)
    data_reloc = u32(out, 0x74)
    old_sect   = out[imp_off:code_reloc]

    # parse existing entries: each {name_off(u32), count(u32), count*u32 words}
    entries = []                              # (raw_name_bytes, [words])
    o = 4
    for _ in range(dll_count):
        name_off, count = struct.unpack_from('<II', old_sect, o); o += 8
        words = list(struct.unpack_from('<%dI' % count, old_sect, o)) if count else []
        o += 4 * count
        end = old_sect.index(b'\0', name_off)
        entries.append((bytes(old_sect[name_off:end]), words))

    # the new entry's import blocks are the a4_gl_iat slot code offsets; verify
    # each slot currently holds its expected ordinal (rule 4: test the assumption)
    gl_words = []
    for i, ordinal in enumerate(GL_IMPORT_ORDINALS):
        slot = iat_code_off + 4 * i
        have = u32(out, code_off + slot)
        assert have == ordinal, 'a4_gl_iat[%d]=%d, expected ord %d' % (i, have, ordinal)
        gl_words.append(slot)
    entries.append((GL_IMPORT_DLL.encode('latin1'), gl_words))

    # rebuild: [size][entries...][names]; name_off is relative to imp_off
    hdr_bytes = 4
    entries_bytes = sum(8 + 4 * len(w) for _, w in entries)
    names_base = hdr_bytes + entries_bytes
    names, name_offs, seen = bytearray(), [], {}
    for name, _ in entries:
        if name not in seen:
            seen[name] = names_base + len(names)
            names += name + b'\0'
        name_offs.append(seen[name])
    content = bytearray()
    content += struct.pack('<I', hdr_bytes + entries_bytes + len(names))   # size word
    for (name, words), noff in zip(entries, name_offs):
        content += struct.pack('<II', noff, len(words))
        content += struct.pack('<%dI' % len(words), *words) if words else b''
    content += names
    content += b'\0' * (-len(content) % 4)                                 # pad to 4

    delta = len(content) - len(old_sect)
    out = out[:imp_off] + content + out[code_reloc:]
    struct.pack_into('<I', out, 0x54, dll_count + 1)                        # dll_ref_count
    struct.pack_into('<I', out, 0x70, code_reloc + delta)                  # code reloc offset
    if data_reloc: struct.pack_into('<I', out, 0x74, data_reloc + delta)   # data reloc offset
    print('import +libgles_cm.dll (%d ords), section %#x -> %#x (dll_ref %d)'
          % (len(gl_words), len(old_sect), len(content), dll_count + 1))
    return out


def main(src_exe, out_exe, measure_only=False, gl=False):
    # measure_only: ship the STOCK renderer with ONLY the fps gauge wired in
    # (INSERT_HOOKS + CACHE_HOOKS + grown bss), skipping the span/vec rewrites
    # (FILL_HOOKS + LOOP_HOOKS). This is the baseline build: same gauge, same
    # scale, so a user can read stock fps by eye against the optimized build.
    fill_hooks = {} if measure_only else FILL_HOOKS
    loop_hooks = {} if measure_only else LOOP_HOOKS

    d = bytearray(open(src_exe, 'rb').read())
    assert u32(d, 0x1C) == 0, 'compressed image: inflate it first'
    code_off, code_size = u32(d, 0x64), u32(d, 0x30)
    new_off = (code_size + 15) & ~15
    blob_base = LINK_BASE + new_off
    with tempfile.TemporaryDirectory() as tmp:
        blob, syms = build_blob(blob_base, tmp)
    # patch each loop hook's return-displacement placeholder: r12 = pc(add)+8 =
    # placeholder_link; want r12 = return_link|1 (Thumb), so word = target - placeholder.
    for entry, (tramp, retsym, ret_off) in loop_hooks.items():
        ph_link = syms[retsym]
        word = ((LINK_BASE + ret_off) | 1) - ph_link
        struct.pack_into('<I', blob, ph_link - blob_base, word & 0xFFFFFFFF)
    # fps detour: patch each IAT placeholder (pc at the add == placeholder addr)
    for entry, (tramp, iats) in INSERT_HOOKS.items():
        for iatsym, iat_off in iats:
            ph_link = syms[iatsym]
            word = (LINK_BASE + iat_off) - ph_link
            struct.pack_into('<I', blob, ph_link - blob_base, word & 0xFFFFFFFF)

    code = bytearray(d[code_off:code_off + code_size])
    code += b'\0' * (new_off - code_size) + blob
    code += b'\0' * (-len(code) % 4)

    for off, sym in fill_hooks.items():
        n = veneer(code, off, syms[sym])
        print('fill   +%06X -> +%06X (%d-byte veneer)' % (off, syms[sym] - LINK_BASE, n))
    for off, (tramp, retsym, ret_off) in loop_hooks.items():
        n = veneer(code, off, syms[tramp])
        print('loop   +%06X -> %s +%06X, returns +%06X (%d-byte veneer)'
              % (off, tramp, syms[tramp] - LINK_BASE, ret_off, n))
    for off, (tramp, iats) in INSERT_HOOKS.items():
        blx_insert(code, off, syms[tramp])
        print('fps    +%06X -> %s +%06X (4-byte BLX detour)' % (off, tramp, syms[tramp] - LINK_BASE))
    for off, tramp in CACHE_HOOKS.items():
        blx_insert(code, off, syms[tramp])
        print('cache  +%06X -> %s +%06X (4-byte BLX detour)' % (off, tramp, syms[tramp] - LINK_BASE))

    grow = len(code) - code_size
    out = bytearray(d[:code_off]) + code + d[code_off + code_size:]
    struct.pack_into('<I', out, 0x30, len(code))
    # grow .bss so the fps (and, with --gl, GL) scratch is backed by zero-mapped
    # writable data
    data_base, data_size, bss_size = u32(out, 0x50), u32(out, 0x34), u32(out, 0x44)
    top = FPS_SCRATCH + FPS_SCRATCH_SIZE
    if gl: top = max(top, GL_SCRATCH + GL_SCRATCH_SIZE)
    need = top - (data_base + data_size)
    if need > bss_size:
        assert FPS_SCRATCH >= data_base + data_size, 'fps scratch overlaps initialised data'
        struct.pack_into('<I', out, 0x44, need)
        print('bss    %#x -> %#x (scratch top %#x)' % (bss_size, need, top))
    for field in (0x68, 0x6C, 0x70, 0x74):          # data, import, code reloc, data reloc offsets
        v = u32(out, field)
        if v: struct.pack_into('<I', out, field, v + grow)
    if gl:
        out = add_gl_import(out, syms['a4_gl_iat'] - LINK_BASE)
    out = bytearray(e32crc.fix(bytes(out)))
    open(out_exe, 'wb').write(out)
    print('code %X -> %X (+%d), image %d bytes' % (code_size, len(code), grow, len(out)))

if __name__ == '__main__':
    flags = {'--measure-only', '--gl'}
    args = [a for a in sys.argv[1:] if a not in flags]
    main(args[0], args[1], measure_only='--measure-only' in sys.argv,
         gl='--gl' in sys.argv)
