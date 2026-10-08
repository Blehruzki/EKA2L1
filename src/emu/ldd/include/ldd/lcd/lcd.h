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

#include <kernel/ldd.h>

namespace eka2l1::kernel {
    class chunk;
}

namespace eka2l1::ldd {
    enum lcd_control_op {
        lcd_control_frame_buffer_address = 0x1001,
        lcd_control_open_update = 0x2001,
        lcd_control_flush_rects = 0x2003,
        lcd_control_display_mode = 0x2005,
        lcd_control_mode_supported = 0x2006,
        lcd_control_orientation = 0x2008,
        lcd_control_scale = 0x200B
    };

    // The LCD driver S60 3.0 (Symbian 9.1) firmwares load as GenericLcd_Lcd.ldd.
    // Its client is the screen driver (scdv.dll): a channel opened by name, then
    // RBusLogicalChannel controls and requests carrying Nokia's own function
    // codes (0x10xx, 0x20xx). Later firmwares reach the screen through HAL
    // instead, which is why nothing else in the emulator needed it.
    class lcd_channel : public channel {
        // The update list 0x2001 hands out: 0x1A0 rectangles of four words the
        // screen driver appends to, and their count at 0x1A00.
        kernel::chunk *update_chunk_;

        std::uint32_t screen_mode_mask() const;

    public:
        explicit lcd_channel(kernel_system *kern, system *sys, epoc::version ver);
        ~lcd_channel() override;

        std::int32_t do_control(kernel::thread *r, const std::uint32_t n, const eka2l1::ptr<void> arg1,
            const eka2l1::ptr<void> arg2) override;

        std::int32_t do_request(epoc::notify_info info, const std::uint32_t n,
            const eka2l1::ptr<void> arg1, const eka2l1::ptr<void> arg2,
            const bool is_supervisor) override;
    };

    class lcd_factory : public factory {
    public:
        explicit lcd_factory(kernel_system *kern, system *sys);
        ~lcd_factory() override {}

        void install() override;
        std::unique_ptr<channel> make_channel(epoc::version ver) override;
    };
}
