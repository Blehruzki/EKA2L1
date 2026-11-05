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

// EKA2L1_GPUMODEL: what a phone's GL driver and GPU cost the game thread. The
// emulator's GLES is host code the guest never pays for; on an N95 the PowerVR MBX
// driver runs every call on the ARM, a texture upload is twiddled by the CPU, and a
// read of the frame (glReadPixels, glFinish, eglSwapBuffers) waits for the tile
// renderer to finish the frame and copies it back. Each call is stretched to its cost
// where the guest made it: on the wall clock under EKA2L1_CPUCAP (scaled by
// EKA2L1_TIMESCALE), or on the cycle tick (EKA2L1_CYCLETICK) as cycles the guest spent.
// Comma-separated, any left out keep their
// default: call_us, draw_us, vert_ns, bind_us, fill_us, tri_ns, readback_ns_per_byte,
// texture_ns_per_byte, post_us (the display update of each direct-screen post).
namespace eka2l1::common::gpumodel {
    struct params {
        double call_us = 0, draw_us = 0, vert_ns = 0, bind_us = 0, fill_us = 0, tri_ns = 0;
        double readback_ns_per_byte = 0, texture_ns_per_byte = 0, post_us = 0;
    };

    bool enabled();
    const params &get();
    // the modelled cost, in microseconds, of one GLES/EGL call (ordinal 0x1100..0x11FF)
    // with its first four arguments and the four on the stack
    double gl_call_us(const std::uint32_t ordinal, const std::uint32_t *args);
    // of a direct-screen post (dispatcher function 1, update_screen, called by the guest)
    double screen_post_us();
    // charge a call that cost the host host_ns as costing the guest us
    void pay_us(const double us, const std::uint64_t host_ns);
}
