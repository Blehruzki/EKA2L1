"""Write the application resource file (<app>.rsc).

It carries two things that are read by different people. apparc reads the
caption record, which expands to: 8 reserved bytes, the short caption, 8 more
reserved bytes, the long caption, a u16 icon count, the icon file's path, and a
u16 view count. The UI framework reads EIK_APP_INFO, and it finds it by
position -- the third resource in the file, after the signature and the
document name -- which is why those two come first even though nothing here
uses them. A file without it panics CONE 14, ECoePanicNoResourceFileForId,
before an application with any screen furniture can start.

A shipped S60v3 resource file has six resources; this has the four that are
read, and the caption moves from second to fourth with them.
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

# EIK_APP_INFO is six resource links -- hotkeys, menu bar, toolbar, toolband,
# status pane, command buttons -- and a word the compiler adds. All zero asks
# avkon for the defaults, which is what an application with no menus of its own
# wants. The shipped file's is 28 bytes of zeros; so is this.
APP_INFO = bytes(28)

CAPTION_RES_ID = RES_OFFSET | 4


def caption_runs(short_caption, long_caption, icon_path='', icon_count=0):
    runs = [('u', b'')]
    runs += R.string_runs(bytes(8), short_caption)
    runs += R.string_runs(bytes(8), long_caption)
    runs += R.string_runs(struct.pack('<H', icon_count), icon_path)
    runs += [('c', struct.pack('<H', 0) + bytes(1))]     # no views, one spare
    return runs


def build(uid3, short_caption, long_caption=None, icon_path='', icon_count=0):
    caption = caption_runs(short_caption, long_caption or short_caption, icon_path, icon_count)
    # One compressed run, the way the shipped file writes its own: nothing
    # here reads the document name, so it only has to be well formed.
    document = [('u', short_caption[:8].encode('ascii'))]
    return R.container(0, uid3,
                       [SIGNATURE, R.encode_runs(document), APP_INFO, R.encode_runs(caption)],
                       unicode_bits=0x0A,      # 2 and 4 are compressed; 1 and 3 are raw
                       largest=max(len(SIGNATURE), R.expanded_size(document),
                                   len(APP_INFO), R.expanded_size(caption)))


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
