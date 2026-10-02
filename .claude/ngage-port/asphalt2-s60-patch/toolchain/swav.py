"""SWAV (N-Gage SDK v3.1 stream) reader.

Layout: 'SWAV' u16 bom u16 ver u32 filesize u16 headersize u16 chunkcount,
then one 'DATA' chunk (u32 size covering itself), then an 8-byte audio
header: u8 format(2) u8 flags u16 sampleRate u16 ? u16 ?, then the samples.
Samples are 4-bit IMA ADPCM, mono, high nibble first, state starting at
predictor 0 / index 0 and running unbroken to the end of the file.
"""
import struct

STEP = [7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,
        107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,
        724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,
        3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,
        15289,16818,18500,20350,22385,24623,27086,29794,32767]
IDX = [-1,-1,-1,-1,2,4,6,8,-1,-1,-1,-1,2,4,6,8]

HEADER_LEN = 32


def parse(data):
    magic, bom, ver, size, hdr, chunks = struct.unpack_from('<4sHHIHH', data, 0)
    if magic != b'SWAV':
        raise ValueError('not a SWAV file')
    tag, csize = struct.unpack_from('<4sI', data, hdr)
    if tag != b'DATA':
        raise ValueError('expected a DATA chunk, got %r' % tag)
    fmt, flags, rate = struct.unpack_from('<BBH', data, hdr + 8)
    return {'rate': rate, 'format': fmt, 'flags': flags,
            'payload': data[HEADER_LEN:hdr + 8 + csize]}


def decode(payload, predictor=0, index=0):
    out = bytearray()
    for b in payload:
        for nib in (b >> 4, b & 0xf):
            step = STEP[index]
            diff = step >> 3
            if nib & 1: diff += step >> 2
            if nib & 2: diff += step >> 1
            if nib & 4: diff += step
            if nib & 8: diff = -diff
            predictor = max(-32768, min(32767, predictor + diff))
            index = max(0, min(88, index + IDX[nib]))
            out += struct.pack('<h', predictor)
    return bytes(out)


def encode(pcm16, predictor=0, index=0):
    """Inverse of decode(): 16-bit mono PCM to the same nibble stream."""
    samples = struct.unpack('<%dh' % (len(pcm16) // 2), pcm16)
    out = bytearray()
    pending = None
    for s in samples:
        step = STEP[index]
        diff = s - predictor
        nib = 0
        if diff < 0:
            nib = 8
            diff = -diff
        delta = step >> 3
        if diff >= step:
            nib |= 4; diff -= step; delta += step
        if diff >= step >> 1:
            nib |= 2; diff -= step >> 1; delta += step >> 1
        if diff >= step >> 2:
            nib |= 1; delta += step >> 2
        predictor = max(-32768, min(32767, predictor + (-delta if nib & 8 else delta)))
        index = max(0, min(88, index + IDX[nib]))
        if pending is None:
            pending = nib
        else:
            out.append((pending << 4) | nib)
            pending = None
    if pending is not None:
        out.append(pending << 4)
    return bytes(out)


def wav(pcm16, rate):
    return (b'RIFF' + struct.pack('<I', 36 + len(pcm16)) + b'WAVEfmt '
            + struct.pack('<IHHIIHH', 16, 1, 1, rate, rate * 2, 2, 16)
            + b'data' + struct.pack('<I', len(pcm16)) + pcm16)
