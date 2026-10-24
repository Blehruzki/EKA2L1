"""profreport.py <EKA2L1_PCPROF output> <emulator log> [top N] -- an exact profile by image.

Credits each block (by its first PC) to the loaded image holding it: RAM-loaded
images from the log's "runtime code" lines (the next image's base bounds each),
ROM images from the RM-409 z: drive's image headers. Prints the share per image,
then the top blocks as image+offset.
"""
import collections, glob, os, re, sys
sys.path.insert(0, '/home/user/EKA2L1/.claude/ngage-port/toolchain/port')
import romimg

prof, log = sys.argv[1], sys.argv[2]
top = int(sys.argv[3]) if len(sys.argv) > 3 else 40
rows = []
for l in open(prof):
    if l.startswith('#'):
        print(l.strip()); continue
    pc, n = l.split(); rows.append((int(pc, 16), int(n)))
total = sum(n for _, n in rows)

images = {}   # base -> (name, size or None)
for l in open(log, errors='replace'):
    m = re.search(r'codeseg\.cpp:\d+ \[Kernel\]: (\S+) \(UID3=0x[0-9A-Fa-f]+\) runtime code: 0x([0-9a-fA-F]+)', l)
    if m:
        images[int(m.group(2), 16)] = (m.group(1), None)
for f in glob.glob('/root/.local/share/EKA2L1/data/drives/z/rm-409/sys/bin/*'):
    try:
        d, h = romimg.load(f)
        images[h['code_address']] = (os.path.basename(f).lower(), h['code_size'])
    except Exception:
        pass
bases = sorted(images)

import bisect
def where(pc):
    i = bisect.bisect_right(bases, pc) - 1
    if i < 0: return ('?', pc)
    b = bases[i]; name, size = images[b]
    if size is not None and pc >= b + size: return ('?', pc)
    if size is None and i + 1 < len(bases) and pc >= bases[i + 1]: return ('?', pc)
    return (name, pc - b)

by_img = collections.Counter()
named = []
for pc, n in rows:
    img, off = where(pc)
    by_img[img] += n
    named.append((n, img, off))
print('\n%-40s %6s' % ('image', 'share'))
for img, n in by_img.most_common(15):
    print('%-40s %5.1f%%' % (img, 100.0 * n / total))
print('\ntop blocks')
for n, img, off in sorted(named, reverse=True)[:top]:
    print('%5.2f%%  %-32s +%X' % (100.0 * n / total, img, off))
