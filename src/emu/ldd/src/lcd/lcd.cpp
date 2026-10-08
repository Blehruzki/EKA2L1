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

#include <ldd/lcd/lcd.h>

#include <kernel/chunk.h>
#include <kernel/kernel.h>
#include <kernel/process.h>
#include <kernel/thread.h>

#include <common/log.h>
#include <dispatch/screen.h>
#include <services/window/screen.h>
#include <services/window/window.h>
#include <system/epoc.h>
#include <utils/err.h>

namespace eka2l1::ldd {
    static const std::string LCD_FACTORY_NAME = "Lcd";

    lcd_factory::lcd_factory(kernel_system *kern, system *sys)
        : factory(kern, sys) {
    }

    void lcd_factory::install() {
        obj_name = LCD_FACTORY_NAME;
    }

    std::unique_ptr<channel> lcd_factory::make_channel(epoc::version ver) {
        return std::make_unique<lcd_channel>(kern, sys_, ver);
    }

    // GenericLcd_Lcd.ldd's own sizes: 0x1A0 rectangles of 16 bytes, then the count.
    static constexpr std::uint32_t LCD_UPDATE_RECT_COUNT_OFFSET = 0x1A00;
    static constexpr std::uint32_t LCD_UPDATE_CHUNK_SIZE = 0x2000;

    lcd_channel::lcd_channel(kernel_system *kern, system *sys, epoc::version ver)
        : channel(kern, sys, ver)
        , update_chunk_(nullptr) {
    }

    lcd_channel::~lcd_channel() {
        if (update_chunk_) {
            kern->destroy(update_chunk_);
        }
    }

    static std::uint32_t peek_word(kernel::thread *r, const eka2l1::ptr<void> p) {
        const std::uint32_t *w = reinterpret_cast<const std::uint32_t *>(p.get(r->owning_process()));
        return w ? *w : 0xDEADDEAD;
    }

    static epoc::screen *focus_screen(system *sys) {
        window_server *winserv = reinterpret_cast<window_server *>(sys->get_kernel_system()->get_by_name<service::server>(
            eka2l1::get_winserv_name_by_epocver(sys->get_symbian_version_use())));
        return winserv ? winserv->get_current_focus_screen() : nullptr;
    }

    // The frame buffer the window server draws into, as HAL's
    // EDisplayMemoryAddress answers it on later firmwares.
    static std::uint32_t screen_buffer_address(system *sys) {
        epoc::screen *scr = focus_screen(sys);
        if (!scr || !scr->screen_buffer_chunk) {
            return 0;
        }
        return scr->screen_buffer_chunk->base(nullptr).ptr_address();
    }

    // What the panel does with the frame buffer on a flush: here, what the
    // emulator's own screen driver asks for after a frame (dispatch's
    // update_screen), which also paces the caller to the screen's refresh.
    static void present_screen(system *sys) {
        epoc::screen *scr = focus_screen(sys);
        if (!scr) {
            return;
        }
        eka2l1::rect whole;
        whole.top = { 0, 0 };
        whole.size = scr->size();
        dispatch::update_screen(sys, 0, scr->number, 1, &whole);
    }

    // One bit per TDisplayMode: the mode the frame buffer is laid out in, the
    // only one this panel can show.
    std::uint32_t lcd_channel::screen_mode_mask() const {
        epoc::screen *scr = focus_screen(sys_);
        const epoc::display_mode mode = scr ? scr->dsa_disp_mode : epoc::display_mode::color64k;
        return 1u << static_cast<std::uint32_t>(mode);
    }

    std::int32_t lcd_channel::do_control(kernel::thread *r, const std::uint32_t n, const eka2l1::ptr<void> arg1,
        const eka2l1::ptr<void> arg2) {
        std::uint32_t *out = reinterpret_cast<std::uint32_t *>(arg1.get(r->owning_process()));

        // Read off scdv.dll (the S60 3.0 screen driver, N80 RM-92 firmware) by
        // what it does with each answer, against the 3.2 driver's matching code,
        // and from 0x2001 on off the same ROM's GenericLcd_Lcd.ldd handlers.
        switch (n) {
        case lcd_control_frame_buffer_address:
            // Stored where 3.2's driver keeps HAL EDisplayMemoryAddress, read
            // just before EDisplayXPixels and EDisplayYPixels in the same routine.
            if (out) {
                *out = screen_buffer_address(sys_);
            }
            LOG_TRACE(LDD_LCD, "Lcd frame buffer address 0x{:X}", out ? *out : 0);
            return epoc::error_none;

        case lcd_control_open_update: {
            // The screen driver opens its update session with this. In: the
            // display mode it will draw in. Out, 8 words: the update list's
            // address, the current mode, the mask of modes the panel takes (the
            // driver tests a bit per TDisplayMode), a flag (0), then a word for
            // each of the four orientations (only the panel's own one, 0, is
            // there). Answers as the ROM driver's handler does; a mode the panel
            // cannot show is KErrNotSupported.
            const std::uint32_t *mode_ptr = reinterpret_cast<const std::uint32_t *>(arg2.get(r->owning_process()));
            const std::uint32_t mode = mode_ptr ? *mode_ptr : 0;
            const std::uint32_t mask = screen_mode_mask();

            LOG_TRACE(LDD_LCD, "Lcd open update, display mode {} (panel mask 0x{:X})", mode, mask);

            if (!out) {
                return epoc::error_argument;
            }
            if ((mode >= 32) || !(mask & (1u << mode))) {
                return epoc::error_not_supported;
            }

            if (!update_chunk_) {
                update_chunk_ = kern->create<kernel::chunk>(kern->get_memory_system(), nullptr, "LcdUpdateList", 0,
                    LCD_UPDATE_CHUNK_SIZE, LCD_UPDATE_CHUNK_SIZE, prot_read_write, kernel::chunk_type::normal,
                    kernel::chunk_access::kernel_mapping, kernel::chunk_attrib::none);
                if (!update_chunk_) {
                    return epoc::error_no_memory;
                }
            }

            std::uint8_t *list = reinterpret_cast<std::uint8_t *>(update_chunk_->host_base());
            std::fill(list, list + LCD_UPDATE_CHUNK_SIZE, 0);

            out[0] = update_chunk_->base(nullptr).ptr_address();
            out[1] = mode;
            out[2] = mask;
            out[3] = 0;
            out[4] = 1;
            out[5] = 0;
            out[6] = 0;
            out[7] = 0;

            return epoc::error_none;
        }

        case lcd_control_flush_rects:
            // The driver asks this when its update list is not empty. The frame
            // buffer is shown as a whole here, so the list is only emptied.
            if (update_chunk_) {
                *reinterpret_cast<std::uint32_t *>(reinterpret_cast<std::uint8_t *>(update_chunk_->host_base()) + LCD_UPDATE_RECT_COUNT_OFFSET) = 0;
            }
            present_screen(sys_);
            return epoc::error_none;

        case lcd_control_display_mode: {
            epoc::screen *scr = focus_screen(sys_);
            if (out) {
                *out = static_cast<std::uint32_t>(scr ? scr->dsa_disp_mode : epoc::display_mode::color64k);
            }
            return epoc::error_none;
        }

        case lcd_control_mode_supported: {
            // a1 holds the mode, a2 takes the answer.
            std::uint32_t *answer = reinterpret_cast<std::uint32_t *>(arg2.get(r->owning_process()));
            const std::uint32_t mode = out ? *out : 0;
            if (answer) {
                *answer = (mode < 32) && (screen_mode_mask() & (1u << mode)) ? 1 : 0;
            }
            return epoc::error_none;
        }

        case lcd_control_orientation:
            // 1 and 3 make the driver swap width and height: 0 is the panel as is.
            if (out) {
                *out = 0;
            }
            return epoc::error_none;

        case lcd_control_scale:
            // The driver divides the screen's size by this.
            if (out) {
                *out = 1;
            }
            return epoc::error_none;

        default:
            break;
        }

        LOG_TRACE(LDD_LCD, "Unhandled Lcd control 0x{:X}, a1 0x{:X} (word 0x{:X}), a2 0x{:X}", n, arg1.ptr_address(),
            peek_word(r, arg1), arg2.ptr_address());
        return epoc::error_none;
    }

    std::int32_t lcd_channel::do_request(epoc::notify_info info, const std::uint32_t n,
        const eka2l1::ptr<void> arg1, const eka2l1::ptr<void> arg2,
        const bool is_supervisor) {
        kernel::thread *r = info.requester;
        // 0 flushes and tells when the panel has taken the frame; 1 and 2 also
        // carry an update. The frame goes up now, so each is done at once.
        LOG_TRACE(LDD_LCD, "Lcd request {}, a1 0x{:X} (word 0x{:X}), a2 0x{:X} (words 0x{:X} 0x{:X} 0x{:X})", n,
            arg1.ptr_address(), peek_word(r, arg1), arg2.ptr_address(), peek_word(r, arg2),
            peek_word(r, eka2l1::ptr<void>(arg2.ptr_address() + 4)), peek_word(r, eka2l1::ptr<void>(arg2.ptr_address() + 8)));
        present_screen(sys_);
        info.complete(epoc::error_none);
        return epoc::error_none;
    }
}
