#include <common/buffer.h>
#include <loader/e32img.h>

#include <cstdio>
#include <fstream>

using namespace eka2l1;

int main(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: e32dump <input.exe/dll> <output_code.bin>\n");
        return 1;
    }

    common::ro_std_file_stream stream(argv[1], true);
    if (!stream.valid()) {
        std::fprintf(stderr, "can't open %s\n", argv[1]);
        return 1;
    }

    auto img = loader::parse_e32img(&stream, false);
    if (!img) {
        std::fprintf(stderr, "parse_e32img failed\n");
        return 1;
    }

    std::printf("uid1=0x%X uid2=0x%X uid3=0x%X\n", static_cast<unsigned>(img->header.uid1), img->header.uid2, img->header.uid3);
    std::printf("cpu=%u flags=0x%X\n", static_cast<unsigned>(img->header.cpu), img->header.flags);
    std::printf("code_base=0x%X entry_point=0x%X\n", img->header.code_base, img->header.entry_point);
    std::printf("code_offset=0x%X code_size=0x%X text_size=0x%X\n", img->header.code_offset, img->header.code_size, img->header.text_size);
    std::printf("data.size()=%zu\n", img->data.size());

    if (img->header.code_offset + img->header.code_size > img->data.size()) {
        std::fprintf(stderr, "code range out of bounds of decompressed data!\n");
        return 1;
    }

    std::ofstream out(argv[2], std::ios::binary);
    out.write(img->data.data() + img->header.code_offset, img->header.code_size);
    out.close();

    std::printf("wrote %u bytes of code to %s (runtime base for this blob = code_base + entry-point-relative? use code_base as base for first byte)\n",
        img->header.code_size, argv[2]);

    return 0;
}
