/*
 * Copyright (c) 2026 EKA2L1 Team.
 *
 * This file is part of EKA2L1 project.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <cstdint>

// EKA2L1_CPUMODEL: a cycle model of an ARM1136 (the N95's OMAP2420 core) for the
// EKA2L1_CPUCAP speed cap. Off, the cap counts instructions. On, it counts cycles:
// each instruction a base CPI (interlocks and mispredicted branches folded in), plus
// the cycles ARM's ARM1136 pipeline spends beyond one issue on multiplies, long
// multiplies and load/store multiples (two registers a cycle), plus a level-one data
// cache: 32 KB, 4-way, 32-byte lines, round-robin replacement, read-allocate. A read
// that misses pays a line fill, a write that misses goes to the write buffer.
// The figures are the model's inputs:
// "base_cpi_x100:read_miss_cycles:write_miss_cycles[:mul_cycles:long_mul_cycles[:arm_cpi_x100]]",
// e.g. EKA2L1_CPUMODEL=280:30:2:4:5:160 with EKA2L1_CYCLETICK=330 (cycles a microsecond: the
// guest's clock then runs on modelled cycles) or EKA2L1_CPUCAP=330 (a wall-clock cap).
// base_cpi applies to Thumb-state code; arm_cpi (default: the same) to ARM-state code --
// on a phone image the two are different compilers' output with different stall rates.
namespace eka2l1::arm::cpumodel {
    struct state {
        bool on = false;
        std::uint32_t base_cpi_x100 = 100;
        std::uint32_t read_miss_cycles = 0;
        std::uint32_t write_miss_cycles = 0;
        std::uint32_t mul_cycles = 1;         // beyond issue: MUL/MLA (ARM1136: 2 issue cycles, result 4 later)
        std::uint32_t long_mul_cycles = 2;    // SMULL/UMULL/SMLAL/UMLAL (3 issue cycles, result 5 later)
        std::uint32_t arm_cpi_x100 = 100;     // ARM-state instructions' CPI (Thumb-state: base_cpi_x100)
        std::uint64_t arm_instrs = 0;         // ARM-state instructions executed (counted always)
        std::uint64_t extra = 0;      // cycles beyond one an instruction (before the base CPI)
        std::uint64_t reads = 0;
        std::uint64_t read_misses = 0;
        std::uint64_t write_misses = 0;
        std::uint32_t tag[256][4];
        std::uint8_t next[256];
    };

    extern state g;

    void init_from_env();
    void read_slow(const std::uint32_t address);
    void write_slow(const std::uint32_t address);
    // A run of contiguous bytes read or written (the loop accelerator's bulk copies).
    void range(const std::uint32_t address, const std::uint32_t bytes, const bool is_write);

    inline void read(const std::uint32_t address) {
        if (g.on)
            read_slow(address);
    }

    inline void write(const std::uint32_t address) {
        if (g.on)
            write_slow(address);
    }

    inline void cycles(const std::uint32_t n) {
        if (g.on)
            g.extra += n;
    }

    // a multiply: n = 1 for MUL/MLA/SMMUL, 2 for the long ones
    inline void multiply(const std::uint32_t n) {
        if (g.on)
            g.extra += n == 1 ? g.mul_cycles : g.long_mul_cycles;
    }

    // Load/store multiple of n registers: two a cycle, the first in the issue cycle.
    inline void multiple(const std::uint32_t n) {
        if (g.on && n > 2)
            g.extra += (n + 1) / 2 - 1;
    }
}
