"""HUD patch set for asphalt2_full.exe (S60v3 Asphalt 2) -- three-layer dashboard.

Stock: every HUD element is anchored bottom-right from its own sprite size; the
nitro slice is the only value-driven element (copy HEIGHT clipped to
slice_h * level / 2304); a tachometer needle is drawn as a line.

Result after patching, matching the original art's empty-fills-up behaviour:
  layer 1  silver band  (empty bar + empty squares)  static, (BAR_X, BAR_Y)
  layer 2  gold bar     clipped by NITRO             (BAR_X, BAR_Y)
  layer 3  gold squares clipped by REVS              (REV_X, REV_Y)

 1 0x202cc  layer-1 sprite  -> absolute (BAR_X, BAR_Y)
 2 0x204a2  speed readout   -> absolute (SPD_X, SPD_Y)
 3 0x20462  needle line     -> removed
 4 0x20340  nitro slice     -> absolute (BAR_X, BAR_Y)
 5 0x20370  nitro fraction over sprite WIDTH, not height
 6 0x20386  slice blit args: copyW = clipped value, copyH = full height
 7 0x203a4  slice blit 0x1f54e (bottom-up) -> 0x1f458 (top-down)
 8 0x20414  code cave in the dead needle trig block: rev width -> sp+0xec,
            constant destination for the square strip -> sp+0xe8
 9 0x204ca  the 5x5 'larrow' draw becomes the rev square strip: copyW read from
            sp+0xec, destination from sp+0xe8
"""
import struct, sys
from thumb import *

CODE_OFF, CODE_BASE = 156, 0x8000
def fo(va): return va - CODE_BASE + CODE_OFF
def b_imm(frm,to): return 0xE000|(((to-(frm+4))>>1)&0x7ff)

STOCK = {
 0x202cc:'ff204130801a58435b1a',
 0x204a2:'571a23374133be461f1a0022511e012001ab07c31937800228180097c06b7346624643a9', 0x20462:'fff7dcf8', 0x20340:'9a1a511a0831f0235943f022d21b8918', 0x20370:'3f9a',
 0x20386:'02ab8446002007c33f99002200910192', 0x203a4:'fff7d3f8',
 0x20414:'1ff054f8', 0x204cc:'0521', 0x204d0:'0522',
 0x204dc:'c14b40980092', 0x204e6:'c0180523',
 0x202f2:'589aff23106b4133c28881889f1af0235f4340695b1aff188446002001ab07c3780000220092409a0b008018f0216246fff7baf8',
}
UNIT_W, UNIT_H, UNIT_STRIDE = 17, 8, 34
DIGIT_W = 8            # digits.RLE is 80x7 -- ten 8-wide glyphs (speed_digit.RLE was 6)
# Upper clamp on the rev strip's copy width.  The last red square ends at x=174, so
# nothing is lost, and the strip can never reach the speed readout at x>=188 and
# paint over it.
REV_MAX = 176
import os
REV_SHIFT = int(os.environ.get("REV_SHIFT",2))
REV_BASE  = int(os.environ.get("REV_BASE",66))

def patch(img, bar=(0,274), spd=(22,280), rev=(0,293), rev_h=5, strip_w=240, unit=(214,304)):
    img=bytearray(img)
    for va,h in STOCK.items():
        o=fo(va); w=bytes.fromhex(h)
        assert bytes(img[o:o+len(w)])==w, 'site 0x%05x changed'%va
    bx,by=bar; sx,sy=spd; rx,ry=rev
    def put(va,d): o=fo(va); img[o:o+len(d)]=d

    put(0x202cc, asm(movs(0,by//2), lsls(0,0,1), nop())); put(0x202d4, asm(movs(3,bx)))
    hi=min(sy,200); lo=sy-hi
    #    ...and, while the font pointer is in r0, force its glyph width to 8: the
    #    speed sheet now carries the wider digits.RLE art.  0x147c6 reads the width
    #    from the font object for both the glyph source x and the right-align maths.
    put(0x204a2, asm(movs(7,sx), mov_lr_r7(), movs(7,hi), adds_i(7,lo),
                     movs(2,0), subs_i1(1,2), movs(0,1), add_sp(3,4), stmia(3,(0,1,2)),
                     str_sp(7,0), lsls(0,0,10), adds_r(0,5,0), ldr_r(0,0,0x3c),
                     movs(2,DIGIT_W), str_r(2,0,0), mov_r_lr(3), mov_r2_ip(), add_sp(1,0x10c)))
    put(0x20462, asm(nop(), nop()))
    put(0x20340, asm(movs(1,by//2), lsls(1,1,1), movs(3,0xf0), muls(1,3), adds_i(1,bx), nop(), nop(), nop()))
    put(0x20370, asm(ldr_sp(2,0xf8)))
    put(0x20386, asm(mov_ip_r0(), movs(0,0), str_sp(0,0), str_sp(0,4), str_sp(2,8),
                     ldr_sp(1,0xfc), str_sp(1,0xc), nop()))
    put(0x203a4, bl(0x203a4,0x1f458))

    # 8. cave: rev width -> sp+0xec ; constant strip destination -> sp+0xe8
    ry_hw = [movs(3,ry//2), lsls(3,3,1)] + ([adds_i(3,1)] if ry%2 else [])
    cave  = [ldr_sp(3,0xec), movs(2,220), lsls(2,2,2), subs_r(3,3,2),
             asrs(3,3,REV_SHIFT), adds_i(3,REV_BASE),
             movs(2,0), cmp_r(3,2), 0, mov_r(3,2),
             movs(2,REV_MAX), cmp_r(3,2), 0, mov_r(3,2),
             str_sp(3,0xec), ldr_sp(0,0x100)] + ry_hw + \
            [movs(2,0xf0), muls(3,2), adds_i(3,rx), lsls(3,3,1), 0x18C0, str_sp(0,0xe8), 0]
    base=0x20414
    a_bge=base+2*8; a_ble=base+2*12; a_b=base+2*(len(cave)-1)
    cave[8]  = bge(a_bge, a_bge+4)
    cave[12] = ble(a_ble, a_ble+4)
    cave[-1] = b_imm(a_b, 0x20466)
    assert a_b+2 <= 0x20466, 'cave overflows into live code'
    put(base, asm(*cave))

    # 9. larrow draw -> rev square strip
    put(0x204cc, asm(ldr_sp(1,0xec)))          # copyW = rev width
    put(0x204d0, asm(movs(2,rev_h)))           # copyH
    put(0x204dc, asm(str_sp(2,0), ldr_sp(0,0xe8), nop()))
    put(0x204e6, asm(nop(), movs(3,strip_w)))  # src stride

    # 10. the blanked tachometer-sweep draw becomes the km/h | mph unit label.
    #     kmhmph.RLE holds both labels side by side (KmH at x0, MpH at x17), so the
    #     The units flag is read here, not stashed by the cave: the cave runs later in
    #     the frame and the game reuses sp+0xe4 every frame before this point.  The flag
    #     is a bool, so srcX = flag * UNIT_W picks the half without a branch.
    #     [sp+4] (srcY) is already 0 from the bar draw.
    ux, uy = unit
    blk = [ldr_sp(0,0x190), movs(2,0x1d), lsls(2,2,10), adds_r(0,0,2), ldrb_r(0,0,0x1d),
           movs(2,UNIT_W), muls(0,2), str_sp(0,0), str_sp(2,8),
           movs(0,UNIT_H), str_sp(0,0xc),
           ldr_sp(0,0x100), movs(1,0xf0), movs(3,uy//2), lsls(3,3,1), muls(3,1),
           adds_i(3,ux), lsls(3,3,1), 0x18C0,
           ldr_sp(2,0x160), ldr_r(2,2,0x30), ldr_r(2,2,0x14), movs(3,UNIT_STRIDE)]
    code = asm(*blk) + bl(0x202f2+2*len(blk), 0x1f49a)
    code += asm(*([nop()]*((0x20326-0x202f2-len(code))//2)))
    assert len(code) == 0x20326-0x202f2, len(code)
    put(0x202f2, code)
    return bytes(img)

if __name__=='__main__':
    BY=int(os.environ.get('BAR_Y',294))          # 320 - 26 -> flush with the screen bottom
    SY=int(os.environ.get('SPD_Y',BY+6))
    RY=int(os.environ.get('REV_Y',BY+19))        # square row sits at band rel 19
    SX=int(os.environ.get('SPD_X',188)); UX=int(os.environ.get('UNIT_X',214))
    UY=int(os.environ.get('UNIT_Y',304))
    img=bytearray(patch(open('n73_full.img','rb').read(),
                        bar=(0,BY), spd=(SX,SY), rev=(0,RY), unit=(UX,UY)))
    # the payload is written back uncompressed, so the header must say so...
    struct.pack_into('<I',img,0x1C,0)
    # ...and the header carries a CRC32 of itself, which that edit invalidates.
    # The emulator never checks it; a real device does.
    import e32crc
    out=e32crc.fix(bytes(img))
    assert e32crc.stored(out)==e32crc.compute(out)
    open('asphalt2_full_patched.exe','wb').write(out)
    print('9 patches applied (three-layer dashboard)')
