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
           os.path.join(HERE, '..', 'patch', 'fill.c')]

# function entry offset -> replacement symbol (same contract, returns normally)
FILL_HOOKS = {0x2ABAA: 'a4_fill_alpha'}
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
        flags = ['--target=armv5te-none-eabi', '-marm', '-fropi'] if src.endswith('.s') else CFLAGS
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

def main(src_exe, out_exe):
    d = bytearray(open(src_exe, 'rb').read())
    assert u32(d, 0x1C) == 0, 'compressed image: inflate it first'
    code_off, code_size = u32(d, 0x64), u32(d, 0x30)
    new_off = (code_size + 15) & ~15
    blob_base = LINK_BASE + new_off
    with tempfile.TemporaryDirectory() as tmp:
        blob, syms = build_blob(blob_base, tmp)
    # patch each loop hook's return-displacement placeholder: r12 = pc(add)+8 =
    # placeholder_link; want r12 = return_link|1 (Thumb), so word = target - placeholder.
    for entry, (tramp, retsym, ret_off) in LOOP_HOOKS.items():
        ph_link = syms[retsym]
        word = ((LINK_BASE + ret_off) | 1) - ph_link
        struct.pack_into('<I', blob, ph_link - blob_base, word & 0xFFFFFFFF)

    code = bytearray(d[code_off:code_off + code_size])
    code += b'\0' * (new_off - code_size) + blob
    code += b'\0' * (-len(code) % 4)

    for off, sym in FILL_HOOKS.items():
        n = veneer(code, off, syms[sym])
        print('fill   +%06X -> +%06X (%d-byte veneer)' % (off, syms[sym] - LINK_BASE, n))
    for off, (tramp, retsym, ret_off) in LOOP_HOOKS.items():
        n = veneer(code, off, syms[tramp])
        print('loop   +%06X -> %s +%06X, returns +%06X (%d-byte veneer)'
              % (off, tramp, syms[tramp] - LINK_BASE, ret_off, n))

    grow = len(code) - code_size
    out = bytearray(d[:code_off]) + code + d[code_off + code_size:]
    struct.pack_into('<I', out, 0x30, len(code))
    for field in (0x68, 0x6C, 0x70, 0x74):          # data, import, code reloc, data reloc offsets
        v = u32(out, field)
        if v: struct.pack_into('<I', out, field, v + grow)
    out = bytearray(e32crc.fix(bytes(out)))
    open(out_exe, 'wb').write(out)
    print('code %X -> %X (+%d), image %d bytes' % (code_size, len(code), grow, len(out)))

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
