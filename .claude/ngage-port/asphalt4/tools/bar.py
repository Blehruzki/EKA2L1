# bar.py <archive.bar> [ls | get <name> <out> | png <name> <out.png> | dark]
# Asphalt 4's .bar archives, read without the emulator (round 88, the black bicycles):
#   u32 directory size, u32 data size; then name\0 + a big-endian word per entry (bit 31: compressed; the rest the
#   offset after the directory); a compressed entry is a big-endian size and a zlib stream.
# An .rle texture inside: u16 type (& 0x3F: 0 4-bit, 1 4-bit index + 4-bit run, 2 6-bit index + 2-bit run, 3 8-bit;
#   0x40 an alpha map follows the data, 0x80 mirrored), u16 width, u16 height, u16 (unread by the loader), u32 data size,
#   then 16/16/64/256 palette halfwords (R4G4B4, three extra low bits in the top nibble; 0xFF0F the colour key), then
#   the data. The game's loader is +362EC; it makes a pure-black entry 0x0001 so it stays opaque.
# dark: each texture whose opaque texels are 90% or more pure black.
import sys, struct, zlib, collections

KEY = 0xFF0F


def entries(b):
    dsz, tot = struct.unpack_from('<II', b, 0)
    o, ents = 8, []
    while o < 8 + dsz:
        e = b.index(b'\0', o)
        ents.append((b[o:e], struct.unpack_from('>I', b, e + 1)[0]))
        o = e + 5
    base = 8 + dsz
    offs = sorted(set(v & 0x7FFFFFFF for _, v in ents)) + [tot]
    for n, v in ents:
        off = v & 0x7FFFFFFF
        blob = b[base + off:base + offs[offs.index(off) + 1]]
        yield n, (lambda blob=blob, v=v: zlib.decompress(blob[4:]) if v & 0x80000000 else blob)


def texture(d):
    typ, w, h, f4, size = struct.unpack_from('<HHHHI', d, 0)
    t = typ & 0x3F
    npal = {0: 16, 1: 16, 2: 64, 3: 256}[t]
    pal = struct.unpack_from('<%dH' % npal, d, 12)
    data = d[12 + 2 * npal:12 + 2 * npal + size]
    idx = []
    for x in data:
        if t == 3: idx.append(x)
        elif t == 0: idx += [x & 15, x >> 4]
        elif t == 1: idx += [x & 15] * ((x >> 4) + 1)
        else: idx += [x & 63] * ((x >> 6) + 1)
    return t, w, h, pal, idx[:w * h]


def rgb(p):
    if p == KEY: return (255, 0, 255)
    r = ((p >> 8) & 15) << 1 | ((p >> 14) & 1)
    g = ((p >> 4) & 15) << 2 | ((p >> 13) & 1) << 1
    b = (p & 15) << 1 | ((p >> 12) & 1)
    return (r * 255 // 31, g * 255 // 63, b * 255 // 31)


def main():
    b = open(sys.argv[1], 'rb').read()
    cmd = sys.argv[2] if len(sys.argv) > 2 else 'ls'
    for n, get in entries(b):
        if cmd == 'ls':
            print(n.decode('latin1'))
        elif cmd in ('get', 'png') and n.lower() == sys.argv[3].encode().lower():
            d = get()
            if cmd == 'get':
                open(sys.argv[4], 'wb').write(d)
            else:
                from PIL import Image
                t, w, h, pal, idx = texture(d)
                im = Image.new('RGB', (w, h))
                im.putdata([rgb(pal[i]) for i in idx])
                im.save(sys.argv[4])
            return
        elif cmd == 'dark' and n.lower().endswith(b'.rle'):
            try:
                t, w, h, pal, idx = texture(get())
            except (KeyError, struct.error, zlib.error):
                continue
            c = collections.Counter(idx)
            op = sum(k for i, k in c.items() if pal[i] != KEY)
            blk = sum(k for i, k in c.items() if pal[i] == 0)
            if op and blk * 10 >= op * 9:
                print('%-45s type %d %dx%d opaque %d black %d' % (n.decode('latin1'), t, w, h, op, blk))


if __name__ == '__main__':
    main()
