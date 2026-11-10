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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <cstdint>

namespace eka2l1::common {
    // A host hook on up to eight guest code addresses, checked by the dyncom core at
    // every block entry: when the entered PC (Thumb bit cleared) is addr[k], fn(k & 3, regs)
    // runs with the guest's r0-r15 before the block executes -- two sets of four, for a
    // routine loaded at two addresses (a DLL in two processes). Unset (fn null) it costs
    // one load per block. Installed by host code that knows a guest routine's address,
    // e.g. EKA2L1_DSBHOLD's watch of the emulator's own CDirectScreenBitmap stub
    // (dispatch/src/dispatcher.cpp). Dyncom only.
    struct pc_hook {
        std::uint32_t addr[8];
        void (*fn)(int which, const std::uint32_t *regs);
    };

    extern pc_hook g_pc_hook;
}
