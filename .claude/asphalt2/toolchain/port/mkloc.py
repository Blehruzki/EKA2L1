"""Write the localisable registration resource (<app>.rsc) apparc reads next.

Resource 1 is the RSS signature apparc confirms before anything else; the
caption record follows it and expands to:
    8 reserved bytes, the short caption, 8 more reserved bytes, the long
    caption, a u16 icon count, the icon file's path, and a u16 view count.
"""
import struct, sys
import rscwrite as R

# The second word of the signature resource is the file's NAME offset in its top
# twenty bits, and its version in the rest. A .rss file's NAME statement sets
# that offset, and every resource id in the file is the offset plus an index.
#
# It must not be zero: CCoeEnv::AddResourceFileL reads RResourceFile::Offset()
# and panics CONE 15, ECoePanicResourceFileHasNullName, if it is. Nothing
# checks that the offset matches any particular name, so any non-zero value
# works as long as the ids we hand out agree with it -- which is why
# CAPTION_RES_ID carries it.
RES_OFFSET = 0x00010000
RES_VERSION = 1
SIGNATURE = struct.pack('<II', 4, RES_OFFSET | RES_VERSION)
CAPTION_RES_ID = RES_OFFSET | 2


def caption_runs(short_caption, long_caption, icon_path='', icon_count=0):
    runs = [('u', b'')]
    runs += R.string_runs(bytes(8), short_caption)
    runs += R.string_runs(bytes(8), long_caption)
    runs += R.string_runs(struct.pack('<H', icon_count), icon_path)
    runs += [('c', struct.pack('<H', 0) + bytes(1))]     # no views, one spare
    return runs


def build(uid3, short_caption, long_caption=None, icon_path='', icon_count=0):
    runs = caption_runs(short_caption, long_caption or short_caption, icon_path, icon_count)
    body = R.encode_runs(runs)
    return R.container(0, uid3, [SIGNATURE, body],
                       unicode_bits=0x02,      # resource 2 is compressed, the signature is not
                       largest=max(len(SIGNATURE), R.expanded_size(runs)))


def selftest(reference):
    """The shipped file's caption record is its 6th resource, at 0x5b..0xaa."""
    ref = open(reference, 'rb').read()[0x5b:0xab]
    mine = R.encode_runs(caption_runs('Asphalt2', 'Asphalt2',
                                      '\\resource\\apps\\Asphalt2_full.mbm', 1))
    ok = mine == ref
    print('mkloc selftest: %s (%d bytes)' % ('exact match' if ok else 'FAILED', len(ref)))
    if not ok:
        print(' ref ', ref.hex()); print(' mine', mine.hex())
    return ok


if __name__ == '__main__':
    sys.exit(0 if selftest(sys.argv[1]) else 1)
