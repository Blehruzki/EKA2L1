import struct, sys

# Matches eka2l1::loader::parse_romimg (src/emu/loader/src/romimage.cpp), NOT just
# the rom_image_header struct layout - the real parser is conditional, not a flat
# sequential read. Round 6 fixed the header size from 84 to 128 bytes (still counted
# some fields as "alternatives" wrongly, landed close by accident). Round 7 found via
# a LIVE MEMORY DUMP that even 128 bytes was wrong - runtime code is offset -8 bytes
# from that extraction. Root cause, found by reading romimage.cpp directly: the real
# parser reads EITHER the 8-byte checksum_code/checksum_data pair (os_ver <= eka2)
# OR the 16-byte rom_vsec_info block (secure_id/vendor_id/cap1/cap2, os_ver > eka2) -
# never both sequentially like the naive struct-based reading assumed. This device
# (S60 3rd ed / Symbian OS 9.1, N95 RM-320) is past eka2, so it's the 16-byte
# sec_info block and the checksum fields are never read from the file at all
# (they're zero-initialized and stay zero). Real header size: 120 bytes (0x78), not
# 128. Ground-truth verified: live process memory dump matched this exactly at a
# constant 0-byte offset once corrected (was -8 bytes off before).
def parse(path):
    data = open(path, 'rb').read()
    off = 0
    def u32():
        nonlocal off
        v = struct.unpack_from('<I', data, off)[0]
        off += 4
        return v
    def i32():
        nonlocal off
        v = struct.unpack_from('<i', data, off)[0]
        off += 4
        return v
    def u16():
        nonlocal off
        v = struct.unpack_from('<H', data, off)[0]
        off += 2
        return v
    def u8():
        nonlocal off
        v = struct.unpack_from('<B', data, off)[0]
        off += 1
        return v

    h = {}
    h['uid1'] = u32(); h['uid2'] = u32(); h['uid3'] = u32(); h['uid_checksum'] = u32()
    h['entry_point'] = u32(); h['code_address'] = u32(); h['data_address'] = u32()
    h['code_size'] = i32(); h['text_size'] = i32(); h['data_size'] = i32(); h['bss_size'] = i32()
    h['heap_minimum_size'] = i32(); h['heap_maximum_size'] = i32(); h['stack_size'] = i32()
    h['dll_ref_table_address'] = u32()
    h['export_dir_count'] = i32()
    h['export_dir_address'] = u32()
    # Post-eka2 devices (everything from S60v3/Symbian 9.1 onward, including this N95
    # RM-320) read sec_info (16 bytes) here, NOT checksum_code/checksum_data (8 bytes).
    # checksum_code/checksum_data stay zero for these files - not a parsing bug, they
    # are genuinely never present in the file for this OS era.
    h['secure_id'] = u32(); h['vendor_id'] = u32(); h['cap1'] = u32(); h['cap2'] = u32()
    h['checksum_code'] = 0; h['checksum_data'] = 0
    h['major'] = u8(); h['minor'] = u8(); h['build'] = u16()
    h['flags_raw'] = u32()
    h['priority'] = u32()
    h['data_bss_linear_base_address'] = u32()
    h['next_extension_linear_address'] = u32()
    h['hardware_variant'] = u32()
    h['total_size'] = u32()
    h['module'] = u32()
    h['exception_des'] = u32()
    h['header_end_offset'] = off
    return h, data

def code_range(path):
    h, data = parse(path)
    return h['code_address'], h['code_address'] + h['code_size'], h

if __name__ == '__main__':
    path = sys.argv[1]
    h, data = parse(path)
    print(f"--- {path} (file size {len(data)}) ---")
    for k, v in h.items():
        print(f"  {k} = 0x{v:X}" if isinstance(v, int) and v >= 0 else f"  {k} = {v}")
