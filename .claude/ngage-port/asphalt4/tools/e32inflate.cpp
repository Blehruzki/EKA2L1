// e32inflate <in> <out>: an EKA2 image with deflate compression, written out uncompressed (header kept, compression type cleared)
#include <common/flate.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <vector>
int main(int argc, char **argv) {
    FILE *f = fopen(argv[1], "rb"); std::vector<uint8_t> d; int ch;
    while ((ch = fgetc(f)) != EOF) d.push_back((uint8_t)ch); fclose(f);
    uint32_t comp, code_off, hdr_len, usize;
    memcpy(&comp, &d[0x1c], 4); memcpy(&code_off, &d[0x64], 4);
    hdr_len = code_off; memcpy(&usize, &d[hdr_len - 4 >= 0x7c ? 0x7c : 0x7c], 4);   // iUncompressedSize at 0x7c (J-format header)
    std::vector<uint8_t> out(hdr_len + usize);
    memcpy(out.data(), d.data(), hdr_len);
    eka2l1::flate::bit_input in(d.data() + hdr_len, (int)((d.size() - hdr_len) * 8));
    eka2l1::flate::inflater inf(in); inf.init();
    int got = inf.read(out.data() + hdr_len, usize);
    uint32_t zero = 0; memcpy(&out[0x1c], &zero, 4);
    f = fopen(argv[2], "wb"); fwrite(out.data(), 1, out.size(), f); fclose(f);
    printf("%s: header %u, uncompressed %u, inflated %d\n", argv[1], hdr_len, usize, got);
    return 0;
}
