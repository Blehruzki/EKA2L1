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
          '-fno-builtin', '-fno-jump-tables', '-fropi', '-mno-unaligned-access', '-fno-delete-null-pointer-checks',
          '-Wall', '-Werror']

SOURCES = [os.path.join(REND, 'a4_tramp.s'),
           os.path.join(REND, 'a4_span_textured.c'),
           os.path.join(REND, 'a4_span_transparent.c'),
           os.path.join(REND, 'a4_vec.s'),
           os.path.join(REND, 'a4_fps.c'),
           os.path.join(REND, 'a4_gl.s'),
           os.path.join(REND, 'a4_gl.c'),
           os.path.join(REND, 'a4_prim.c'),
           os.path.join(REND, 'a4_tel.c'),
           os.path.join(REND, 'a4_eng.s'),
           os.path.join(REND, 'a4_px.s'),
           os.path.join(REND, 'a4_eng.c'),
           os.path.join(HERE, '..', 'patch', 'fill.c')]

# MBX stage 3a: capture textured-triangle data at draw_primitive +0x300B2 (just
# after its prologue). BLX detour that re-executes the two displaced moves. Added
# only with --capture; grows .bss for the capture scratch.
CAPTURE_HOOKS = {0x300B2: 'a4_prim_capture_tramp',
                 0x2973A: 'a4_hud_tramp',     # round 11: the end of the 3D pass (HUD painter)
                 0x30252: 'a4_bbq_tramp',     # round 12: the billboard blit, a GPU quad
                 0x305A2: 'a4_bb2_tramp',     # its two-depth sibling: composite there
                 0x2AB0C: 'a4_fill_tramp',    # round 13: the rectangle fill (letterbox), a GPU quad
                 0x1EE: 'a4_pres_tramp'}      # round 16: after the game's present call (its timer)
# the displaced halfwords each capture hook's trampoline re-executes; checked before the
# BLX goes in, so a different image cannot be patched blind
CAPTURE_DISPLACED = {0x300B2: (0x0005, 0x0014),     # movs r5, r0 ; movs r4, r2
                     0x2973A: (0x9C1C, 0x0007),     # ldr r4, [sp, #0x70] ; movs r7, r0
                     0x30252: (0x693B, 0x687A),     # ldr r3, [r7, #0x10] ; ldr r2, [r7, #4]
                     0x305A2: (0x469C, 0x000D),     # mov ip, r3 ; movs r5, r1
                     0x2AB0C: (0xA90A, 0xC962),     # add r1, sp, #0x28 ; ldm r1, {r1, r5, r6}
                     0x1EE: (0x2000, 0x66E0)}       # movs r0, #0 ; str r0, [r4, #0x6c]
# The structural step (A4_MESH): the one ordering-table insert in the mesh routine +349F8
# (`bl +349B0` at +34ADC) becomes a BLX to a4_mesh_insert(cam, face), which keeps an
# eligible face for the frame's GPU mesh list and passes the rest to +349B0 itself.
# Added only when A4_EXTRA_CFLAGS defines A4_MESH.
MESH_HOOKS = {0x34ADC: 'a4_mesh_insert'}
MESH_DISPLACED = {0x34ADC: (0xF7FF, 0xFF68)}      # bl +349B0
MESH_PLACEHOLDERS = {'a4_otins': 0x349B0 | 1,     # the ordering-table insert (Thumb)
                     'a4_tfvt': 0xB08EC,          # TFace's vtable (RTTI, MBX.md map 1)
                     'a4_bbret': 0x31364 | 1,     # TBillboard's draw: the blit call's return (Thumb lr)
                     'a4_tfzvt': 0xB08CC}         # TFakeZFace's vtable (key +522C2 returns 1: drawn last)
CAPTURE_SKIP_RET = 0x300E0      # draw_primitive epilogue; GPU-drawn prims return here
BB_SKIP_RET = 0x3031C           # the billboard blit's epilogue; GPU-queued sprites return here
FILL_SKIP_RET = 0x2AB4C         # the rectangle fill's epilogue; GPU-queued fills return here
PRIM_SCRATCH = 0x00402000       # A4_TRI_SCRATCH in a4_prim.c (shared with a4_gl.c)
PRIM_SCRATCH_SIZE = 0x2200

# MBX/PowerVR spike: link libGLES_CM and fill its IAT (the a4_gl_iat .word slots
# in a4_gl.s) so the loader resolves these ordinals. Order must match a4_gl.s /
# a4_gl.c. Added only with --gl.
GL_IMPORT_DLL = 'libgles_cm.dll'
GL_IMPORT_ORDINALS = [14, 17, 4, 3, 18, 132, 33, 31, 58, 108,
                      67, 29, 125, 127, 56, 90, 82, 99, 57, 131,
                      120, 39, 54, 116, 123, 68,
                      1, 55, 48, 35,   # [26] eglChooseConfig [27] glDrawElements
                      6, 22, 52,       # [30] eglCreateWindowSurface [31] eglSwapBuffers [32] glDisable (was 36 = glClearStencil)
                      28, 128,         # [33] glAlphaFuncx [34] glTexSubImage2D (A4_TEL colorkey)
                      30, 59, 69,      # [35] glBlendFunc [36] glFlush [37] glGetIntegerv (round 10)
                      117, 119, 36,    # [38] glStencilFunc [39] glStencilOp [40] glClearStencil
                      40,              # [41] glColorMask (round 11)
                      84, 49]          # [42] glLoadMatrixx [43] glDepthMask (A4_MESH)
                                       # [28] glDepthFunc     [29] glClearDepthx
                                       # must match a4_gl.s a4_gl_iat
# A4_WINDOW: the window the GPU presents into is our own (a4_gl.c, bringup);
# these are the a4_ws_iat slots in a4_gl.s, same mechanism as the GL table.
WS_IMPORT_DLL = 'ws32.dll'
WS_IMPORT_ORDINALS = [76, 70, 69, 158, 157, 148, 211, 278, 275, 277, 107, 81, 99, 268, 212, 197]
CONE_IMPORT_DLL = 'cone.dll'
CONE_IMPORT_ORDINALS = [182]        # CCoeEnv::Static() only (717 RootWin is 3.2+, absent on the N95's 3.1 -> loader error -5)
# A4_TEL: on-device telemetry file writer (a4_tel.c). The game imports no efsrv,
# so add one. Ordinals from the fileserver eabi def, all <= the N95 efsrv.dll's
# 351 exports (append-only, so the same functions). Must match a4_gl.s a4_efs_iat.
EFS_IMPORT_DLL = 'efsrv.dll'
EFS_IMPORT_ORDINALS = [68, 108, 101, 96]   # RFs::Connect, RFile::Replace, RFile::Write(pos), RFile::Flush
GL_SCRATCH = 0x00401000         # must match A4_GL_SCRATCH in a4_gl.c
GL_SCRATCH_SIZE = 0x22000       # GL state + strip readback + batch buffers + texture-cache provenance (0x413000)
                                # + the batch reorder (0x414000-0x421000, A4_REORDER) + the A/B aggregates (A4_AGG)
                                # + the atlas state (0x422000, A4_ATLAS)
# a bench diagnostic may need more (A4_SEQDUMP: 0x3B000, A4_DEFERCHECK: 0x2E000); A4_SCRATCH_SIZE overrides
# A4_MESH: the mesh list from 0x423000 (a4_gl.c MESH_TOP) -- 0x3A000 at MESH_MAXT 2048
GL_SCRATCH_SIZE = int(os.environ.get('A4_SCRATCH_SIZE', hex(GL_SCRATCH_SIZE)), 0)
_xf = os.environ.get('A4_EXTRA_CFLAGS', '').split()
# round 53: a half-step build keeps A4_MESHMEMO's table past its scratch (renderer/a4_memo.h, 0x460000-0x461004)
assert not ('-DA4_MESHMEMO' in _xf and '-DA4_HALFSPEC' in _xf) or GL_SCRATCH + GL_SCRATCH_SIZE >= 0x462000, \
    'A4_MESHMEMO with A4_HALFSPEC: A4_SCRATCH_SIZE must reach 0x462000 (0x61000 or more)'
# round 54, bench: A4_SKYCEN's bitmap of the frame's skippable blended boxes, 0x460000-0x462580 (a4_gl.c SKYBM)
assert '-DA4_SKYCEN' not in _xf or GL_SCRATCH + GL_SCRATCH_SIZE >= 0x463000, \
    'A4_SKYCEN: A4_SCRATCH_SIZE must reach 0x463000 (0x62000 or more)'
                                # (pos/uv/idx/tex from 0x408000, end 0x40FD00); kept as
                                # small as the batch allows -- a large image .bss corrupts
                                # the game's load-time layout

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
# A4_STEPTIME (round 30): the race update +1670E's calls, timed (a4_gl.c a4_step_*)
STEP_HOOKS = {0x16796: ('a4_step_count', (0xF7FF, 0xF9C1)),    # bl +15B1C, the step count
              0x167B4: ('a4_step_main', (0xF7FF, 0xF94D)),     # bl +15A52, once a step
              0x16966: ('a4_step_cam', (0xF7FC, 0xF9F8)),      # bl +12D5A, before the render
              0x1696C: ('a4_step_render', (0xF7FC, 0xF811))}   # bl +12992, the render
STEP_PLACEHOLDERS = {'a4_stepcnt': 0x15B1C | 1, 'a4_stepmain': 0x15A52 | 1,
                     'a4_stepcam': 0x12D5A | 1, 'a4_steprend': 0x12992 | 1,
                     'a4_hc_1de78': 0x1DE78 | 1, 'a4_hc_1f1f8': 0x1F1F8 | 1,
                     'a4_hc_1ea64': 0x1EA64 | 1, 'a4_hc_1fd5c': 0x1FD5C | 1,
                     'a4_hc_25e8': 0x25E8 | 1, 'a4_hc_12dec': 0x12DEC | 1,
                     'a4_hc_1fd0e': 0x1FD0E | 1, 'a4_hc_fb12': 0xFB12 | 1}
# A4_HALFMOTION (round 31): +141B0's car calls, recorded and replayed on the odd half-steps
HALFMOTION_HOOKS = {0x14974: ('a4_h_1de78', (0xF009, 0xFA80)),   # bl +1DE78 (physics loop)
                    0x1495E: ('a4_h_1f1f8', (0xF00A, 0xFC4B)),   # bl +1F1F8 (physics loop)
                    0x14C94: ('a4_h_1ea64', (0xF009, 0xFEE6)),   # bl +1EA64 (movement loop)
                    0x14CA2: ('a4_h_1ea64', (0xF009, 0xFEDF)),   # bl +1EA64 (movement loop)
                    0x14C68: ('a4_h_1fd5c', (0xF00B, 0xF878)),   # bl +1FD5C (movement loop)
                    # round 31 phone follow-up: the loop's other cars (every car but the player's)
                    # take +1F1F8 at +146FC, or +1FD0E at +146D6 in the +691E mode; unhooked, the
                    # opponents never moved on the speculative frames (carsplit.py, spec5)
                    0x146FC: ('a4_h_1f1f8', (0xF00A, 0xFD7C)),   # bl +1F1F8 (physics loop, other cars)
                    0x146D6: ('a4_h_1fd0e', (0xF00B, 0xFB1A))}   # bl +1FD0E (physics loop, other cars)
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

def build_blob(link_addr, tmp, clean=False, gl=False):
    # clean builds carry none of the measurement code: drop the fps/GL sources,
    # and the fps/cache trampolines in a4_tramp.s are #ifdef'd out via -DA4_CLEAN.
    # GL builds render for real, so a4_fps.c suppresses its on-screen fps gauge
    # (the green bar) under -DA4_GL while still driving the GPU frame flush.
    MEASURE_SRCS = {'a4_fps.c', 'a4_gl.c', 'a4_gl.s', 'a4_prim.c', 'a4_tel.c'}
    sources = [s for s in SOURCES if not (clean and os.path.basename(s) in MEASURE_SRCS)]
    objs = []
    for src in sources:
        o = os.path.join(tmp, os.path.basename(src) + '.o')
        # assembly doesn't take the C-only codegen flags (-Werror would reject them);
        # a4_tramp.s has #ifdef, so it is always run through the asm preprocessor.
        extra = os.environ.get('A4_EXTRA_CFLAGS', '').split()
        if src.endswith('.s'):
            flags = ['--target=armv5te-none-eabi', '-marm', '-fropi']
            if os.path.basename(src) == 'a4_tramp.s':
                # forward only -D defines from A4_EXTRA_CFLAGS (asm rejects C codegen flags)
                defs = [f for f in extra if f.startswith('-D')]
                flags += ['-x', 'assembler-with-cpp'] + defs + (['-DA4_CLEAN'] if clean else [])
        else:
            flags = CFLAGS + extra + (['-DA4_CLEAN'] if clean else []) + (['-DA4_GL'] if gl else [])
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
    if os.environ.get('A4_SYMFILE'):
        # bench aid: every symbol's code offset, sorted, to name a traced PC
        with open(os.environ['A4_SYMFILE'], 'w') as sf:
            for name, a in sorted(syms.items(), key=lambda kv: kv[1]):
                sf.write('+%06X %s\n' % (a - LINK_BASE, name))
    for name in filter(None, os.environ.get('A4_PRINTSYMS', '').split(',')):
        # bench aid: a symbol's run address (code at 0x70000000 on the bench's RM-320)
        print('sym    %s +%06X (0x%08X)' % (name, syms[name] - LINK_BASE, 0x70000000 + syms[name] - LINK_BASE))
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

def add_gl_import(out, iat_code_off, dll=GL_IMPORT_DLL, ordinals=GL_IMPORT_ORDINALS):
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
    for i, ordinal in enumerate(ordinals):
        slot = iat_code_off + 4 * i
        have = u32(out, code_off + slot)
        assert have == ordinal, '%s iat[%d]=%d, expected ord %d' % (dll, i, have, ordinal)
        gl_words.append(slot)
    entries.append((dll.encode('latin1'), gl_words))

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
    print('import +%s (%d ords), section %#x -> %#x (dll_ref %d)'
          % (dll, len(gl_words), len(old_sect), len(content), dll_count + 1))
    return out


def main(src_exe, out_exe, measure_only=False, gl=False, clean=False, capture=False):
    # measure_only: ship the STOCK renderer with ONLY the fps gauge wired in
    # (INSERT_HOOKS + CACHE_HOOKS + grown bss), skipping the span/vec rewrites
    # (FILL_HOOKS + LOOP_HOOKS). This is the baseline build: same gauge, same
    # scale, so a user can read stock fps by eye against the optimized build.
    # clean: ship ONLY the performance rewrites (FILL_HOOKS + LOOP_HOOKS), with
    # no fps gauge, no back-buffer cache, no GL, no grown .bss -- a pure v5 with
    # none of the measurement instrumentation installed.
    fill_hooks = {} if measure_only else FILL_HOOKS
    loop_hooks = {} if measure_only else LOOP_HOOKS
    insert_hooks = {} if clean else INSERT_HOOKS      # fps detour
    cache_hooks = {} if clean else CACHE_HOOKS        # back-buffer cache
    capture_hooks = dict(CAPTURE_HOOKS) if capture else {}  # stage-3a primitive capture
    if clean: gl = False; capture = False

    d = bytearray(open(src_exe, 'rb').read())
    assert u32(d, 0x1C) == 0, 'compressed image: inflate it first'
    code_off, code_size = u32(d, 0x64), u32(d, 0x30)
    new_off = (code_size + 15) & ~15
    blob_base = LINK_BASE + new_off
    with tempfile.TemporaryDirectory() as tmp:
        blob, syms = build_blob(blob_base, tmp, clean=clean, gl=gl)
    # patch each loop hook's return-displacement placeholder: r12 = pc(add)+8 =
    # placeholder_link; want r12 = return_link|1 (Thumb), so word = target - placeholder.
    for entry, (tramp, retsym, ret_off) in loop_hooks.items():
        ph_link = syms[retsym]
        word = ((LINK_BASE + ret_off) | 1) - ph_link
        struct.pack_into('<I', blob, ph_link - blob_base, word & 0xFFFFFFFF)
    # fps detour: patch each IAT placeholder (pc at the add == placeholder addr)
    for entry, (tramp, iats) in insert_hooks.items():
        for iatsym, iat_off in iats:
            ph_link = syms[iatsym]
            word = (LINK_BASE + iat_off) - ph_link
            struct.pack_into('<I', blob, ph_link - blob_base, word & 0xFFFFFFFF)
    # capture/skip trampoline: patch a4_skip_ret to draw_primitive's epilogue
    # (+0x300E0, where the GPU-drawn path returns, skipping the software rasterizer)
    if capture:
        ph_link = syms['a4_skip_ret']
        word = ((LINK_BASE + CAPTURE_SKIP_RET) | 1) - ph_link
        struct.pack_into('<I', blob, ph_link - blob_base, word & 0xFFFFFFFF)
        ph_link = syms['a4_bbq_skip']
        word = ((LINK_BASE + BB_SKIP_RET) | 1) - ph_link
        struct.pack_into('<I', blob, ph_link - blob_base, word & 0xFFFFFFFF)
        ph_link = syms['a4_fill_skip']
        word = ((LINK_BASE + FILL_SKIP_RET) | 1) - ph_link
        struct.pack_into('<I', blob, ph_link - blob_base, word & 0xFFFFFFFF)
    mesh = '-DA4_MESH' in os.environ.get('A4_EXTRA_CFLAGS', '').split() and gl
    if mesh:
        for sym, off in MESH_PLACEHOLDERS.items():
            ph_link = syms[sym]
            word = (LINK_BASE + off) - ph_link
            struct.pack_into('<I', blob, ph_link - blob_base, word & 0xFFFFFFFF)
    steptime = '-DA4_STEPTIME' in os.environ.get('A4_EXTRA_CFLAGS', '').split() and gl
    if gl:      # patched in every GL build, so no 0xDEADBEEF is left whether or not they are hooked
        for sym, off in STEP_PLACEHOLDERS.items():
            ph_link = syms[sym]
            word = (LINK_BASE + off) - ph_link
            struct.pack_into('<I', blob, ph_link - blob_base, word & 0xFFFFFFFF)
    # the race loop's two re-present return sites (A4_SKIPREPRES compares DoPresent's caller)
    if not clean:
        for sym, ret in (('a4_rep1', 0x955), ('a4_rep2', 0x983)):
            ph_link = syms[sym]
            word = (LINK_BASE + ret) - ph_link
            struct.pack_into('<I', blob, ph_link - blob_base, word & 0xFFFFFFFF)

    # round 50 (A4_ENGINE): the game's code base for the engine rewrite, and its replaced functions
    if 'a4_g_base' in syms:
        ph_link = syms['a4_g_base']
        struct.pack_into('<I', blob, ph_link - blob_base, (LINK_BASE - ph_link) & 0xFFFFFFFF)
    code = bytearray(d[code_off:code_off + code_size])
    code += b'\0' * (new_off - code_size) + blob
    code += b'\0' * (-len(code) % 4)

    for sym in sorted(syms):
        if sym.startswith('a4_engv_'):
            off = int(sym[len('a4_engv_'):], 16)
            n = veneer(code, off, syms[sym])
            print('engine +%06X -> %s +%06X (%d-byte veneer)' % (off, sym, syms[sym] - LINK_BASE, n))
    for off, sym in fill_hooks.items():
        n = veneer(code, off, syms[sym])
        print('fill   +%06X -> +%06X (%d-byte veneer)' % (off, syms[sym] - LINK_BASE, n))
    for off, (tramp, retsym, ret_off) in loop_hooks.items():
        n = veneer(code, off, syms[tramp])
        print('loop   +%06X -> %s +%06X, returns +%06X (%d-byte veneer)'
              % (off, tramp, syms[tramp] - LINK_BASE, ret_off, n))
    for off, (tramp, iats) in insert_hooks.items():
        blx_insert(code, off, syms[tramp])
        print('fps    +%06X -> %s +%06X (4-byte BLX detour)' % (off, tramp, syms[tramp] - LINK_BASE))
    for off, tramp in cache_hooks.items():
        blx_insert(code, off, syms[tramp])
        print('cache  +%06X -> %s +%06X (4-byte BLX detour)' % (off, tramp, syms[tramp] - LINK_BASE))
    for off, tramp in capture_hooks.items():
        # a capture hook inside a function the engine replaces whole (its veneer at the entry): the
        # lifted function calls the hook's handler itself (edits.py ALWAYS) -- round 50, the painter
        if any(sym.startswith('a4_engv_') and int(sym[len('a4_engv_'):], 16) <= off < int(sym[len('a4_engv_'):], 16) + 16
               for sym in syms):
            print('cap    +%06X left out: its function is lifted' % off)
            continue
        want = CAPTURE_DISPLACED[off]
        have = struct.unpack_from('<HH', code, off)
        assert have == want, 'capture hook +%06X: displaced %04x %04x, expected %04x %04x' % ((off,) + have + want)
        blx_insert(code, off, syms[tramp])
        print('cap    +%06X -> %s +%06X (4-byte BLX detour)' % (off, tramp, syms[tramp] - LINK_BASE))

    if mesh:
        for off, sym in MESH_HOOKS.items():
            want = MESH_DISPLACED[off]
            have = struct.unpack_from('<HH', code, off)
            assert have == want, 'mesh hook +%06X: displaced %04x %04x, expected %04x %04x' % ((off,) + have + want)
            blx_insert(code, off, syms[sym])
            print('mesh   +%06X -> %s +%06X (4-byte BLX for the bl)' % (off, sym, syms[sym] - LINK_BASE))
    if steptime:
        for off, (sym, want) in STEP_HOOKS.items():
            have = struct.unpack_from('<HH', code, off)
            assert have == want, 'step hook +%06X: displaced %04x %04x, expected %04x %04x' % ((off,) + have + want)
            blx_insert(code, off, syms[sym])
            print('step   +%06X -> %s +%06X (4-byte BLX for the bl)' % (off, sym, syms[sym] - LINK_BASE))
    if '-DA4_HALFLOGIC' in os.environ.get('A4_EXTRA_CFLAGS', '').split() and gl:
        have = struct.unpack_from('<HH', code, 0x167A6)
        assert have == (0x6BA0, 0x19C0), 'half logic: +167A6 holds %04x %04x' % have
        blx_insert(code, 0x167A6, syms['a4_hstep_tramp'])
        print('HALF   +0167A6 -> a4_hstep_tramp +%06X (the step loop head)' % (syms['a4_hstep_tramp'] - LINK_BASE))
    if '-DA4_HALFMOTION' in os.environ.get('A4_EXTRA_CFLAGS', '').split() and gl:
        for off, (sym, want) in HALFMOTION_HOOKS.items():
            have = struct.unpack_from('<HH', code, off)
            assert have == want, 'half motion +%06X: holds %04x %04x, expected %04x %04x' % ((off,) + have + want)
            blx_insert(code, off, syms[sym])
            print('HMOT   +%06X -> %s' % (off, sym))
    if '-DA4_HALFPHYS' in os.environ.get('A4_EXTRA_CFLAGS', '').split() and gl:
        # round 42: the game's camera updates halved -- the whole step's (+14EEC) and slow motion's
        # (+1306C) bl +12DEC
        for off, want in ((0x14EEC, (0xF7FD, 0xFF7E)), (0x1306C, (0xF7FF, 0xFEBE))):
            have = struct.unpack_from('<HH', code, off)
            assert have == want, 'half phys: +%06X holds %04x %04x, expected %04x %04x' % ((off,) + have + want)
            blx_insert(code, off, syms['a4_h_12dec'])
            print('HPHY   +%06X -> a4_h_12dec +%06X' % (off, syms['a4_h_12dec'] - LINK_BASE))
    if '-DA4_ENG3' in os.environ.get('A4_EXTRA_CFLAGS', '').split() and gl:
        # round 50: the key handler +25E8 (bl at +167AE in the step loop) read only before a whole step
        have = struct.unpack_from('<HH', code, 0x167AE)
        assert have == (0xF7EB, 0xFF1B), 'engine keys: +167AE holds %04x %04x' % have
        blx_insert(code, 0x167AE, syms['a4_eng_keys'])
        print('ENG3   +0167AE -> a4_eng_keys +%06X (keys before a whole step)' % (syms['a4_eng_keys'] - LINK_BASE))
    if '-DA4_SPECTRAFFIC' in os.environ.get('A4_EXTRA_CFLAGS', '').split() and gl:
        # round 41: the traffic's step +FB12 (from +14574, once a step) recorded for the speculation
        have = struct.unpack_from('<HH', code, 0x14574)
        assert have == (0xF7FB, 0xFACD), 'spec traffic: +14574 holds %04x %04x' % have
        blx_insert(code, 0x14574, syms['a4_h_fb12'])
        print('TRAF   +014574 -> a4_h_fb12 +%06X (the traffic\'s step)' % (syms['a4_h_fb12'] - LINK_BASE))
    if '-DA4_DROPHOLD' in os.environ.get('A4_EXTRA_CFLAGS', '').split() and gl:
        # round 37: the framework's own post (+606's [r4+0x3C]->vtbl[0x34](this, 0)), held
        # with a held frame (a4_fwpost_tramp, a4_fw_hold)
        have = struct.unpack_from('<HH', code, 0x6B2)
        assert have == (0x2100, 0x4790), 'drop hold: +6B2 holds %04x %04x' % have
        blx_insert(code, 0x6B2, syms['a4_fwpost_tramp'])
        print('HOLD   +0006B2 -> a4_fwpost_tramp +%06X (the framework\'s post)' % (syms['a4_fwpost_tramp'] - LINK_BASE))
    if '-DA4_SPECCAMSAFE' in os.environ.get('A4_EXTRA_CFLAGS', '').split() and gl:
        # round 40: the camera scripts not advanced by the speculation (a4_tramp.s)
        for ph, target in (('a4_hc_5205a', 0x5205A), ('a4_hc_3d2a', 0x3D2A)):
            struct.pack_into('<I', code, syms[ph] - LINK_BASE, ((LINK_BASE + (target | 1)) - syms[ph]) & 0xFFFFFFFF)
        for off, want, tramp in ((0x556E, (0xF04C, 0xFD74), 'a4_cs_5205a_tramp'), (0x5912, (0xF04C, 0xFBA2), 'a4_cs_5205a_tramp'),
                                 (0x5578, (0xF7FE, 0xFBD7), 'a4_cs_3d2a_tramp'), (0x5926, (0xF7FE, 0xFA00), 'a4_cs_3d2a_tramp')):
            have = struct.unpack_from('<HH', code, off)
            assert have == want, 'spec cam safe: +%06X holds %04x %04x, expected %04x %04x' % ((off,) + have + want)
            blx_insert(code, off, syms[tramp])
            print('CAMS   +%06X -> %s +%06X' % (off, tramp, syms[tramp] - LINK_BASE))
        # round 41: the keyframe add +51FEC and the script init +5202C, at every call site (each
        # checked to be a BL to its target before it is replaced)
        for ph, target in (('a4_hc_51fec', 0x51FEC), ('a4_hc_5202c', 0x5202C)):
            struct.pack_into('<I', code, syms[ph] - LINK_BASE, ((LINK_BASE + (target | 1)) - syms[ph]) & 0xFFFFFFFF)
        def bl_target(off):
            h1, h2 = struct.unpack_from('<HH', code, off)
            if (h1 & 0xF800) != 0xF000 or (h2 & 0xF800) != 0xF800: return None
            o = ((h1 & 0x7FF) << 12) | ((h2 & 0x7FF) << 1)
            if o & 0x400000: o -= 0x800000
            return off + 4 + o
        for target, tramp, sites in ((0x51FEC, 'a4_cs_51fec_tramp', (0x3CE8, 0x3CF8, 0x3D08, 0x5662, 0x566E, 0x5682, 0x5696, 0x56A2,
                                                                     0x56B6, 0x56C2, 0x56CE, 0x5762, 0x576E, 0x5786, 0x579A)),
                                     (0x5202C, 'a4_cs_5202c_tramp', (0x3CCC, 0x3F14, 0x5618, 0x5620, 0x5211C))):
            for off in sites:
                assert bl_target(off) == target, 'spec cam safe: +%06X is not a BL to +%06X' % (off, target)
                blx_insert(code, off, syms[tramp])
            print('CAMS   %d sites -> %s +%06X (+%06X)' % (len(sites), tramp, syms[tramp] - LINK_BASE, target))
    if {'-DA4_SKYCEN', '-DA4_SKYSKIP'} & set(os.environ.get('A4_EXTRA_CFLAGS', '').split()) and gl:
        # round 54: the sky routine +35FDA's band loops join at +3616A (ldr r0, [sp, #0x70];
        # cmp r0, #0 -- both done by the tramp); a4_sky_tramp notes where the band ended
        have = struct.unpack_from('<HH', code, 0x3616A)
        assert have == (0x981C, 0x2800), 'skycen: +3616A holds %04x %04x' % have
        blx_insert(code, 0x3616A, syms['a4_sky_tramp'])
        print('SKY    +03616A -> a4_sky_tramp +%06X (the sky band\'s end)' % (syms['a4_sky_tramp'] - LINK_BASE))
    if '-DA4_ZSKIP' in os.environ.get('A4_EXTRA_CFLAGS', '').split() and gl:
        # round 51: the begin-frame's clear of the 1/w buffer (+31C70's bl +2DEF4 at +31CC0) through
        # a4_zclr_tramp, which skips it while the buffer is known clean (a4_gl.c a4_zclr_skip)
        h1, h2 = struct.unpack_from('<HH', code, 0x31CC0)
        o = ((h1 & 0x7FF) << 12) | ((h2 & 0x7FF) << 1)
        if o & 0x400000: o -= 0x800000
        assert (h1 & 0xF800) == 0xF000 and (h2 & 0xF800) == 0xF800 and 0x31CC0 + 4 + o == 0x2DEF4, \
            'zskip: +031CC0 is not a BL to +2DEF4 (%04x %04x)' % (h1, h2)
        struct.pack_into('<I', code, syms['a4_hc_2def4'] - LINK_BASE, ((LINK_BASE + (0x2DEF4 | 1)) - syms['a4_hc_2def4']) & 0xFFFFFFFF)
        blx_insert(code, 0x31CC0, syms['a4_zclr_tramp'])
        print('ZSKIP  +031CC0 -> a4_zclr_tramp +%06X (the 1/w buffer clear, skipped while clean)' % (syms['a4_zclr_tramp'] - LINK_BASE))
    if os.environ.get('A4_HALFSTEP'):
        # round 31, the faster-step work: the logic step halved, 21333 -> 10667 units
        # (85.33 -> 42.67 ms). +52B80's divide (t / 21333 for the real-time count m): its
        # magic +52B98 and factor +52B9C; the logic count n's period literal +15F28
        # (scale x period >> 8). /10667 by 0x312684D0 >> 43 checked exact for 0 <= t < 2^31.
        for off, old_w, new_w in ((0x52B98, 0x18938DE7, 0x312684D0), (0x52B9C, (-21333) & 0xFFFFFFFF, (-10667) & 0xFFFFFFFF),
                                  (0x15F28, 21333, 10667)):
            have = struct.unpack_from('<I', code, off)[0]
            assert have == old_w, 'half step: +%06X holds %08x, expected %08x' % (off, have, old_w)
            struct.pack_into('<I', code, off, new_w)
        print('STEP   halved: +52B98 +52B9C +15F28 (10667 units, 42.67 ms)')
    if os.environ.get('A4_THIRDSTEP'):
        # round 50, the engine at 3x: the game's logic step a third, 21333 -> 7111 units (85.33 ->
        # 28.44 ms), each a real step of the engine in C (renderer/a4_eng.c), not a half-step frame.
        # /7111 by 0x49BAA9B5 >> 43, checked exact for 0 <= t < 2^31 as +52B98's umull >> 43 reads it.
        assert not os.environ.get('A4_HALFSTEP'), 'A4_THIRDSTEP and A4_HALFSTEP both set'
        for off, old_w, new_w in ((0x52B98, 0x18938DE7, 0x49BAA9B5), (0x52B9C, (-21333) & 0xFFFFFFFF, (-7111) & 0xFFFFFFFF),
                                  (0x15F28, 21333, 7111)):
            have = struct.unpack_from('<I', code, off)[0]
            assert have == old_w, 'third step: +%06X holds %08x, expected %08x' % (off, have, old_w)
            struct.pack_into('<I', code, off, new_w)
        print('STEP   a third: +52B98 +52B9C +15F28 (7111 units, 28.44 ms)')
    if os.environ.get('A4_BENCH_UNLOCK'):
        # BENCH ONLY, never a phone build: +3A15C (is this city/item unlocked?) returns 1 at
        # once (movs r0,#1 ; bx lr), so a fresh career reaches every city -- the night/lights
        # tracks the bench race (LA) does not have (round 27).
        have = struct.unpack_from('<HH', code, 0x3A15C)
        assert have == (0xB510, 0x0004), 'bench unlock: +3A15C holds %04x %04x' % have
        struct.pack_into('<HH', code, 0x3A15C, 0x2001, 0x4770)
        print('BENCH  +03A15C -> return 1 (every city unlocked)')
    grow = len(code) - code_size
    out = bytearray(d[:code_off]) + code + d[code_off + code_size:]
    struct.pack_into('<I', out, 0x30, len(code))
    # grow .bss so the fps (and, with --gl, GL) scratch is backed by zero-mapped
    # writable data
    data_base, data_size, bss_size = u32(out, 0x50), u32(out, 0x34), u32(out, 0x44)
    if not clean:                                   # clean builds carry no scratch
        top = FPS_SCRATCH + FPS_SCRATCH_SIZE
        if gl: top = max(top, GL_SCRATCH + GL_SCRATCH_SIZE)
        if capture: top = max(top, PRIM_SCRATCH + PRIM_SCRATCH_SIZE)
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
        if 'a4_ws_iat' in syms:
            out = add_gl_import(out, syms['a4_ws_iat'] - LINK_BASE, WS_IMPORT_DLL, WS_IMPORT_ORDINALS)
        if 'a4_cone_iat' in syms:
            out = add_gl_import(out, syms['a4_cone_iat'] - LINK_BASE, CONE_IMPORT_DLL, CONE_IMPORT_ORDINALS)
        if 'a4_efs_iat' in syms and 'A4_TEL' in os.environ.get('A4_EXTRA_CFLAGS', ''):
            out = add_gl_import(out, syms['a4_efs_iat'] - LINK_BASE, EFS_IMPORT_DLL, EFS_IMPORT_ORDINALS)
    out = bytearray(e32crc.fix(bytes(out)))
    open(out_exe, 'wb').write(out)
    print('code %X -> %X (+%d), image %d bytes' % (code_size, len(code), grow, len(out)))

if __name__ == '__main__':
    flags = {'--measure-only', '--gl', '--clean', '--capture'}
    args = [a for a in sys.argv[1:] if a not in flags]
    main(args[0], args[1], measure_only='--measure-only' in sys.argv,
         gl='--gl' in sys.argv, clean='--clean' in sys.argv,
         capture='--capture' in sys.argv)
