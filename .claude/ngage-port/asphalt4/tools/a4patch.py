"""a4patch.py <in.exe> <out.exe> <file.c>... -- build Asphalt 4 with functions replaced by new ARM code.

Every C file holds functions named in HOOKS below. They are compiled for ARMv5TE
(ARM state, position independent: -fropi), linked at the address just past the
original code section, and appended to it; the data, import and relocation
sections move down by the growth and the header follows. The original function's
first bytes become an entry veneer -- Thumb `bx pc` into ARM state, then an ARM
`b` to the new code -- so every caller, direct or through a vtable, arrives at
the new code with its own LR. No absolute address is written, so the relocation
sections stay as they are. The header CRC is recomputed.
"""
import os, struct, subprocess, sys, tempfile
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'asphalt2-s60-patch', 'toolchain'))
import e32crc

# symbol -> code offset of the original (Thumb) function it replaces
HOOKS = {
    'a4_fill_alpha': 0x2ABAA,
}
LINK_BASE = 0x8000          # the image's code_base: what its addresses are linked against
CFLAGS = ['--target=armv5te-none-eabi', '-marm', '-mtune=arm1136j-s', '-O2', '-ffreestanding',
          '-fno-builtin', '-fno-jump-tables', '-fropi', '-mno-unaligned-access', '-Wall', '-Werror']

def u32(d, o): return struct.unpack_from('<I', d, o)[0]

def build_blob(sources, link_addr, tmp):
    objs = []
    for src in sources:
        o = os.path.join(tmp, os.path.basename(src) + '.o')
        subprocess.check_call(['clang', *CFLAGS, '-c', src, '-o', o])
        objs.append(o)
    ld = os.path.join(tmp, 'a4.ld')
    open(ld, 'w').write('SECTIONS { . = 0x%X; .text : { *(.text*) *(.rodata*) } '
                        '.data : { *(.data*) *(.bss*) *(COMMON) } '
                        '/DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment) *(.ARM.attributes) } }\n' % link_addr)
    elf = os.path.join(tmp, 'a4.elf')
    subprocess.check_call(['ld.lld', '-T', ld, '-o', elf, '--no-undefined', '-e', '0', *objs])
    secs = subprocess.check_output(['llvm-objdump', '-h', elf], text=True)
    for line in secs.splitlines():
        p = line.split()
        if len(p) > 2 and p[1] == '.data' and int(p[2], 16):
            sys.exit('new code has writable data: it would need a data section of its own')
    binf = os.path.join(tmp, 'a4.bin')
    subprocess.check_call(['llvm-objcopy', '-O', 'binary', '-j', '.text', elf, binf])
    syms = {}
    for line in subprocess.check_output(['llvm-nm', elf], text=True).splitlines():
        p = line.split()
        if len(p) == 3: syms[p[2]] = int(p[0], 16)
    return open(binf, 'rb').read(), syms

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

def main(src_exe, out_exe, sources):
    d = bytearray(open(src_exe, 'rb').read())
    assert u32(d, 0x1C) == 0, 'compressed image: inflate it first'
    code_off, code_size = u32(d, 0x64), u32(d, 0x30)
    new_off = (code_size + 15) & ~15
    with tempfile.TemporaryDirectory() as tmp:
        blob, syms = build_blob(sources, LINK_BASE + new_off, tmp)
    code = bytearray(d[code_off:code_off + code_size])
    code += b'\0' * (new_off - code_size) + blob
    code += b'\0' * (-len(code) % 4)
    for name, off in HOOKS.items():
        if name not in syms: continue
        n = veneer(code, off, syms[name])
        print('%-20s +%06X -> +%06X (%d bytes of veneer)' % (name, off, syms[name] - LINK_BASE, n))
    grow = len(code) - code_size
    tail = d[code_off + code_size:]
    out = bytearray(d[:code_off]) + code + tail
    struct.pack_into('<I', out, 0x30, len(code))
    for field in (0x68, 0x6C, 0x70, 0x74):          # data, import, code reloc, data reloc offsets
        v = u32(out, field)
        if v: struct.pack_into('<I', out, field, v + grow)
    out = bytearray(e32crc.fix(bytes(out)))
    open(out_exe, 'wb').write(out)
    print('code %X -> %X bytes (+%d), image %d bytes' % (code_size, len(code), grow, len(out)))

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2], sys.argv[3:])
