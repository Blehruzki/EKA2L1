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

#include <cpu/dyncom/cpumodel.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace eka2l1::arm::cpumodel {
    state g;

    void init_from_env() {
        static bool done = false;
        if (done)
            return;
        done = true;
        const char *w = std::getenv("EKA2L1_CPUMODEL");
        if (!w || !*w)
            return;
        unsigned cpi = 100, rm = 0, wm = 0, mu = 1, lmu = 2;
        if (std::sscanf(w, "%u:%u:%u:%u:%u", &cpi, &rm, &wm, &mu, &lmu) < 1)
            return;
        g.mul_cycles = mu;
        g.long_mul_cycles = lmu;
        std::memset(g.tag, 0xFF, sizeof(g.tag));
        std::memset(g.next, 0, sizeof(g.next));
        g.base_cpi_x100 = cpi ? cpi : 100;
        g.read_miss_cycles = rm;
        g.write_miss_cycles = wm;
        g.on = true;
        std::fprintf(stderr, "EKA2L1_CPUMODEL: base CPI %u.%02u, read miss %u cycles, write miss %u cycles, multiply +%u, "
                             "long multiply +%u (32 KB 4-way 32-byte-line data cache)\n", cpi / 100, cpi % 100, rm, wm, mu, lmu);
    }

    static inline bool lookup(const std::uint32_t line, std::uint32_t &set) {
        set = line & 255u;
        const std::uint32_t tag = line >> 8;
        const std::uint32_t *t = g.tag[set];
        return t[0] == tag || t[1] == tag || t[2] == tag || t[3] == tag;
    }

    void read_slow(const std::uint32_t address) {
        const std::uint32_t line = address >> 5;
        std::uint32_t set;
        g.reads++;
        if (lookup(line, set))
            return;
        g.read_misses++;
        g.extra += g.read_miss_cycles;
        g.tag[set][g.next[set] & 3u] = line >> 8;
        g.next[set]++;
    }

    void write_slow(const std::uint32_t address) {
        std::uint32_t set;
        if (lookup(address >> 5, set))
            return;
        g.write_misses++;
        g.extra += g.write_miss_cycles;      // no allocation on a write miss: the write buffer
    }

    void range(const std::uint32_t address, const std::uint32_t bytes, const bool is_write) {
        if (!g.on || !bytes)
            return;
        const std::uint32_t first = address >> 5, last = (address + bytes - 1) >> 5;
        for (std::uint32_t l = first; l <= last; l++) {
            if (is_write)
                write_slow(l << 5);
            else
                read_slow(l << 5);
        }
    }
}
