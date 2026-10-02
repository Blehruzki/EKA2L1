"""Write 16-bit mono PCM as an IMA ADPCM .wav (format tag 0x11).

Symbian's WAV controller decodes this, and it keeps a track at roughly the size
of the N-Gage original (4 bits per sample) instead of the 4x that plain PCM costs.

Block layout is the Microsoft one: int16 predictor, uint8 step index, uint8 zero,
then the nibbles, LOW nibble first -- the opposite order to the SWAV stream.
"""
import struct

STEP = [7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,
        107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,
        724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,
        3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,
        15289,16818,18500,20350,22385,24623,27086,29794,32767]
IDX = [-1,-1,-1,-1,2,4,6,8,-1,-1,-1,-1,2,4,6,8]


def _encode_block(samples, predictor, index):
    """Nibbles for one block, after its first sample became the block header."""
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
        predictor = max(-32768, min(32767, predictor - delta if nib & 8 else predictor + delta))
        index = max(0, min(88, index + IDX[nib]))
        if pending is None:
            pending = nib
        else:
            out.append(pending | (nib << 4))   # low nibble first
            pending = None
    if pending is not None:
        out.append(pending)
    return bytes(out), predictor, index


def build(pcm16, rate, block_align=256):
    samples = list(struct.unpack('<%dh' % (len(pcm16) // 2), pcm16))
    per_block = (block_align - 4) * 2 + 1
    data = bytearray()
    index = 0
    for start in range(0, len(samples), per_block):
        chunk = samples[start:start + per_block]
        if len(chunk) < per_block:                 # pad the last block with silence
            chunk = chunk + [chunk[-1]] * (per_block - len(chunk))
        predictor = chunk[0]
        start_index = index
        body, _, index = _encode_block(chunk[1:], predictor, start_index)
        block = struct.pack('<hBB', predictor, start_index, 0) + body
        data += block.ljust(block_align, b'\0')
    total = ((len(samples) + per_block - 1) // per_block) * per_block

    fmt = struct.pack('<HHIIHHH', 0x0011, 1, rate,
                      rate * block_align // per_block, block_align, 4, 2) \
        + struct.pack('<H', per_block)
    fact = b'fact' + struct.pack('<II', 4, total)
    chunks = (b'fmt ' + struct.pack('<I', len(fmt)) + fmt + fact
              + b'data' + struct.pack('<I', len(data)) + bytes(data))
    return b'RIFF' + struct.pack('<I', 4 + len(chunks)) + b'WAVE' + chunks
