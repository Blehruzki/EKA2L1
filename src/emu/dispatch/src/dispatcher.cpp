/*
 * Copyright (c) 2020 EKA2L1 Team.
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

#include <chrono>
#include <common/gpumodel.h>
#include <config/config.h>
#include <dispatch/dispatcher.h>
#include <dispatch/libraries/register.h>
#include <dispatch/libraries/gles1/def.h>
#include <dispatch/libraries/gles2/def.h>
#include <dispatch/libraries/vg/consts.h>
#include <dispatch/register.h>
#include <dispatch/screen.h>
#include <kernel/kernel.h>
#include <kernel/process.h>
#include <services/window/window.h>
#include <utils/err.h>
#include <utils/event.h>

#include <common/armemitter.h>
#include <common/log.h>
#include <common/pchook.h>
#include <kernel/codeseg.h>
#include <services/window/screen.h>
#include <system/epoc.h>

#include <mem/mem.h>

#include <algorithm>

namespace eka2l1::dispatch {
    static std::uint32_t MAX_TRAMPOLINE_CHUNK_SIZE = 0x4000;

    // EKA2L1_DSBHOLD=1: a model in which a CDirectScreenBitmap holds the panel from its Create
    // to its Close (or destruction), so that while one is open the window server's composites
    // and EGL window surfaces are not shown. The N95's scdv opens a session of its display
    // driver at Create and ends it at Close (ROM scdv, vtable 8087ECD0); whether its panel then
    // hides everything else is NOT confirmed on any device -- this is for testing code that
    // must cope with a phone that does. Without it this emulator draws a post over the screen
    // once and lets the next redraw cover it. The emulator's own DSB stub (src/patch/scdv, the prebuilt scdv_general.dll)
    // calls nothing at Create's start or at Close, so the host watches those guest
    // addresses (common::g_pc_hook). The offsets are this DLL's, checked against its code and
    // its vtable at load: a rebuilt stub that moves them is refused with a log line.
    namespace {
        struct dsb_hold_state {
            eka2l1::window_server *winserv = nullptr;
        } g_dsb_hold;

        constexpr std::uint32_t DSB_STUB_CREATE = 0x2A60;
        constexpr std::uint32_t DSB_STUB_CLOSE = 0x2B98;
        constexpr std::uint32_t DSB_STUB_DTOR1 = 0x2BC0;
        constexpr std::uint32_t DSB_STUB_DTOR2 = 0x2BD4;
        constexpr std::uint32_t DSB_STUB_VTABLE = 0x5F18;

        void dsb_hold_hook(int which, const std::uint32_t *regs) {
            if (!g_dsb_hold.winserv) {
                return;
            }
            const std::uint32_t obj = regs[0];
            for (epoc::screen *scr = g_dsb_hold.winserv->get_screens(); scr; scr = scr->next) {
                if (which == 0) {
                    // Create(this, const TRect &, TSettingsFlags): the screen number in bits 28-30.
                    if (static_cast<std::uint32_t>(scr->number) == ((regs[2] >> 28) & 7)) {
                        const std::uint32_t prev = scr->dsb_hold_.exchange(obj);
                        LOG_INFO(HLE_DISPATCHER, "DSBHOLD: screen {} held by DSB 0x{:X} (Create, lr 0x{:X}, was 0x{:X})",
                            scr->number, obj, regs[14], prev);
                    }
                } else if (scr->dsb_hold_.load() == obj) {
                    scr->dsb_hold_.store(0);
                    LOG_INFO(HLE_DISPATCHER, "DSBHOLD: screen {} released by DSB 0x{:X} ({}, lr 0x{:X})", scr->number,
                        obj, (which == 1) ? "Close" : "destructor", regs[14]);
                }
            }
        }

        void dsb_hold_install(const std::string &name, kernel::process *attacher, codeseg_ptr seg) {
            std::string lower = name;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            if (!seg || (lower.find("scdv") == std::string::npos)) {
                return;
            }
            std::uint8_t *base = nullptr;
            const address run = seg->get_code_run_addr(attacher, &base);
            if (!base || (seg->get_text_size() < DSB_STUB_VTABLE + 0x20)) {
                return;
            }
            auto half = [&](std::uint32_t off) { return *reinterpret_cast<const std::uint16_t *>(base + off); };
            auto word = [&](std::uint32_t off) { return *reinterpret_cast<const std::uint32_t *>(base + off); };
            const bool layout_ok = (half(DSB_STUB_CLOSE) == 0x4770) && (half(DSB_STUB_CREATE) == 0xB5F0)
                && (word(DSB_STUB_VTABLE + 0x0C) == run + DSB_STUB_CREATE + 1)
                && (word(DSB_STUB_VTABLE + 0x1C) == run + DSB_STUB_CLOSE + 1)
                && (word(DSB_STUB_VTABLE + 0x00) == run + DSB_STUB_DTOR1 + 1)
                && (word(DSB_STUB_VTABLE + 0x04) == run + DSB_STUB_DTOR2 + 1);
            if (!layout_ok) {
                LOG_ERROR(HLE_DISPATCHER, "DSBHOLD: {} at 0x{:X} is not the DSB stub this switch knows; not watched", name, run);
                return;
            }
            // Each load of the stub (one a process that maps it) its own set of four; a
            // third load replaces the older set.
            std::uint32_t *set = common::g_pc_hook.addr;
            for (int k = 0; k < 8; k += 4) {
                if (set[k] == run + DSB_STUB_CREATE) {
                    return;
                }
            }
            static int next_set = 0;
            set += 4 * next_set;
            next_set ^= 1;
            set[0] = run + DSB_STUB_CREATE;
            set[1] = run + DSB_STUB_CLOSE;
            set[2] = run + DSB_STUB_DTOR1;
            set[3] = run + DSB_STUB_DTOR2;
            common::g_pc_hook.fn = dsb_hold_hook;
            LOG_INFO(HLE_DISPATCHER, "DSBHOLD: watching the DSB stub in {} at 0x{:X} (process {})", name, run,
                attacher ? attacher->name() : std::string("-"));
        }
    }

    dispatcher::dispatcher(kernel_system *kern, ntimer *timing)
        : trampoline_chunk_(nullptr)
        , static_data_chunk_(nullptr)
        , libmngr_(nullptr)
        , mem_(nullptr)
        , kern_(nullptr)
        , trampoline_allocated_(0)
        , static_data_allocated_(0)
        , winserv_(nullptr)
        , egl_controller_(std::make_unique<egl_controller>(nullptr))
        , graphics_string_added_(false)
        , has_mediums_pending_destroy_(false)
        , has_players_notify_deferred_(false)
        , has_streams_notify_deferred_(false)
        , process_exit_callback_handle_(0) {
        trampoline_chunk_ = kern->create<kernel::chunk>(kern->get_memory_system(), nullptr, "DispatcherTrampolines", 0,
            MAX_TRAMPOLINE_CHUNK_SIZE, MAX_TRAMPOLINE_CHUNK_SIZE, prot_read_write_exec, kernel::chunk_type::normal,
            kernel::chunk_access::rom, kernel::chunk_attrib::none);

        static_data_chunk_ = kern->create<kernel::chunk>(kern->get_memory_system(), nullptr, "DispatcherStaticData", 0,
            MAX_TRAMPOLINE_CHUNK_SIZE, MAX_TRAMPOLINE_CHUNK_SIZE, prot_read_write, kernel::chunk_type::normal,
            kernel::chunk_access::rom, kernel::chunk_attrib::none);

        winserv_ = reinterpret_cast<eka2l1::window_server *>(kern->get_by_name<service::server>(
            eka2l1::get_winserv_name_by_epocver(kern->get_epoc_version())));

        // Set global variables
        timing_ = timing;
        libmngr_ = kern->get_lib_manager();
        mem_ = kern->get_memory_system();
        kern_ = kern;

        post_transferer_.construct(timing_);

        process_exit_callback_handle_ = kern->register_process_exit_callback([this](kernel::process *pr) {
            on_process_exit(pr);
        });

        if (std::getenv("EKA2L1_DSBHOLD") && winserv_) {
            g_dsb_hold.winserv = winserv_;
            kern->register_codeseg_loaded_callback(dsb_hold_install);
        }
    }

    dispatcher::~dispatcher() {
        if (kern_ && process_exit_callback_handle_) {
            kern_->unregister_process_exit_callback(process_exit_callback_handle_);
        }
    }

    void dispatcher::on_process_exit(kernel::process *pr) {
        if (!pr) {
            return;
        }

        // Audio the process left playing must not outlive it: a killed or panicked app never
        // runs the guest destructor that would have stopped its player. Only orphan the
        // mediums here though. Destroying one stops the host audio stream, which waits out the
        // render callback in flight, and that callback takes the kernel lock to complete guest
        // notifications - while this may itself be running under the kernel lock (a host
        // requested kill holds it). Destroy them from flush_pending_teardown() instead.
        std::vector<std::unique_ptr<dsp_medium>> orphans;
        dsp_manager_.detach_objects_of_process(pr->unique_id(), orphans);

        if (orphans.empty()) {
            return;
        }

        const std::lock_guard<std::mutex> guard(mediums_pending_destroy_lock_);
        mediums_pending_destroy_.insert(mediums_pending_destroy_.end(),
            std::make_move_iterator(orphans.begin()), std::make_move_iterator(orphans.end()));

        has_mediums_pending_destroy_ = true;
    }

    void dispatcher::defer_player_notify(const std::uint32_t player_handle) {
        const std::lock_guard<std::mutex> guard(players_notify_deferred_lock_);

        if (std::find(players_notify_deferred_.begin(), players_notify_deferred_.end(), player_handle)
            == players_notify_deferred_.end()) {
            players_notify_deferred_.push_back(player_handle);
        }

        has_players_notify_deferred_ = true;
    }

    void dispatcher::defer_stream_buffer_notify(const std::uint32_t stream_handle) {
        const std::lock_guard<std::mutex> guard(streams_notify_deferred_lock_);

        if (std::find(streams_notify_deferred_.begin(), streams_notify_deferred_.end(), stream_handle)
            == streams_notify_deferred_.end()) {
            streams_notify_deferred_.push_back(stream_handle);
        }

        has_streams_notify_deferred_ = true;
    }

    void dispatcher::flush_pending_teardown() {
        if (has_mediums_pending_destroy_.exchange(false)) {
            std::vector<std::unique_ptr<dsp_medium>> to_destroy;
            {
                const std::lock_guard<std::mutex> guard(mediums_pending_destroy_lock_);
                to_destroy.swap(mediums_pending_destroy_);
            }

            // Destroyed outside of the lock: a medium destructor blocks until the audio backend
            // has drained its render callback.
            to_destroy.clear();
        }

        if (has_players_notify_deferred_.load(std::memory_order_relaxed)
            || has_streams_notify_deferred_.load(std::memory_order_relaxed)) {
            kern_->lock();
            complete_deferred_player_notifies_locked();
            complete_deferred_stream_notifies_locked();

            kern_->unlock();
        }
    }

    void dispatcher::complete_deferred_player_notifies_locked() {
        // Relaxed load first: this runs on every dispatch call, and the deferral is rare
        // enough that the read-modify-write below should not be on that path.
        if (!has_players_notify_deferred_.load(std::memory_order_relaxed)
            || !has_players_notify_deferred_.exchange(false)) {
            return;
        }

        std::vector<std::uint32_t> to_complete;
        {
            const std::lock_guard<std::mutex> guard(players_notify_deferred_lock_);
            to_complete.swap(players_notify_deferred_);
        }

        for (const std::uint32_t handle : to_complete) {
            // The player may have been destroyed (or its request cancelled) while the
            // notification was waiting here; both leave nothing to complete.
            dsp_epoc_player *player = dsp_manager_.get_object<dsp_epoc_player>(handle);

            if (!player || !player->impl_) {
                continue;
            }

            const std::lock_guard<std::mutex> guard(player->impl_->lock_);
            std::uint8_t *notify = player->impl_->get_notify_userdata(nullptr);

            if (!notify) {
                continue;
            }

            epoc::notify_info *info = reinterpret_cast<epoc::notify_info *>(notify);

            // The requester thread may be gone (app exit, player teardown): completing against
            // a destroyed thread dereferences a dangling pointer.
            if (!info->empty() && kern_->is_thread_alive(info->requester)) {
                info->complete(epoc::error_none);
            } else {
                info->sts = 0;
            }

            player->impl_->clear_notify_done();
        }
    }

    void dispatcher::complete_deferred_stream_notifies_locked() {
        // Relaxed load first: this runs on every dispatch call, and the deferral only happens
        // when the render thread lost the race for the kernel lock.
        if (!has_streams_notify_deferred_.load(std::memory_order_relaxed)
            || !has_streams_notify_deferred_.exchange(false)) {
            return;
        }

        std::vector<std::uint32_t> to_complete;
        {
            const std::lock_guard<std::mutex> guard(streams_notify_deferred_lock_);
            to_complete.swap(streams_notify_deferred_);
        }

        for (const std::uint32_t handle : to_complete) {
            // The stream may have been destroyed (or its request cancelled) while the
            // notification was waiting here; both leave nothing to complete.
            dsp_epoc_stream *stream = dsp_manager_.get_object<dsp_epoc_stream>(handle);

            if (!stream) {
                continue;
            }

            const std::lock_guard<std::mutex> guard(stream->lock_);

            // The requester thread may be gone (app exit, stream teardown): completing against
            // a destroyed thread dereferences a dangling pointer.
            if (!stream->copied_info_.empty() && kern_->is_thread_alive(stream->copied_info_.requester)) {
                stream->copied_info_.complete(epoc::error_none);
            } else {
                stream->copied_info_.sts = 0;
            }
        }
    }

    void dispatcher::set_graphics_driver(drivers::graphics_driver *driver) {
        if (!graphics_string_added_) {
            // Add static strings
            add_static_string(GLES_STATIC_STRING_KEY_VENDOR, GLES1_STATIC_STRING_VENDOR);
            add_static_string(GLES_STATIC_STRING_KEY_RENDERER, GLES1_STATIC_STRING_RENDERER);
            add_static_string(GLES_STATIC_STRING_KEY_EXTENSIONS, dispatch::get_es1_extensions(driver));
            add_static_string(GLES_STATIC_STRING_KEY_VERSION, GLES1_STATIC_STRING_VERSION);
            add_static_string(GLES_STATIC_STRING_KEY_VENDOR_ES2, GLES2_STATIC_STRING_VENDOR);
            add_static_string(GLES_STATIC_STRING_KEY_RENDERER_ES2, GLES2_STATIC_STRING_RENDERER);
            add_static_string(GLES_STATIC_STRING_KEY_EXTENSIONS_ES2, dispatch::get_es2_extensions(driver));
            add_static_string(GLES_STATIC_STRING_KEY_VERSION_ES2, GLES2_STATIC_STRING_VERSION);
            add_static_string(GLES_STATIC_STRING_SHADER_LANGUAGE_VERSION_ES2, GLES2_STATIC_STRING_SHADER_LANGUAGE_VERSION);
            add_static_string(EGL_VENDOR_EMU, EGL_STATIC_STRING_VENDOR);
            add_static_string(EGL_VERSION_EMU, EGL_STATIC_STRING_VERSION);
            add_static_string(EGL_EXTENSIONS_EMU, EGL_STATIC_STRING_EXTENSION);
            add_static_string(VG_VENDOR_EMU, EKA2L1_GNUVG_VENDOR);
            add_static_string(VG_RENDERER_EMU, EKA2L1_GNUVG_RENDERER);
            add_static_string(VG_VERSION_EMU, EKA2L2_GNUVG_VERSION);
            add_static_string(VG_EXTENSIONS_EMU, EKA2L2_GNUVG_EXTENSIONS);

            graphics_string_added_ = true;
        }

        egl_controller_->set_graphics_driver(driver);
    }

    void dispatcher::resolve(eka2l1::system *sys, const std::uint32_t function_ord) {
        // Runs under the kernel lock (see lib_manager::call_svc). Notifications the audio render
        // thread handed over are delivered before the dispatch call below, so that a guest that
        // cancels or re-arms its request in this very call sees the completion first, exactly as
        // it would have if the render thread had taken the lock itself.
        complete_deferred_player_notifies_locked();
        complete_deferred_stream_notifies_locked();

        auto dispatch_find_result = dispatch::dispatch_funcs.find(function_ord);

        if (dispatch_find_result == dispatch::dispatch_funcs.end()) {
            LOG_ERROR(HLE_DISPATCHER, "Can't find dispatch function {}", function_ord);
            return;
        }

        /*
        if (dispatch_find_result->second.second) {
            LOG_TRACE(HLE_DISPATCHER, "Calling {}", dispatch_find_result->second.second);
        } else {
            LOG_TRACE(HLE_DISPATCHER, "Calling 0x{:X}", function_ord);
        }*/

        // EKA2L1_GPUMODEL: what the phone's GL driver and GPU would have cost this call
        // (a GLES/EGL ordinal, or the guest's direct-screen post), paid where it was made
        double model_us = 0;
        if (common::gpumodel::enabled()) {
            if (function_ord >= 0x1100 && function_ord < 0x1200) {
                arm::core *cpu = sys->get_cpu();
                std::uint32_t args[8];
                for (int i = 0; i < 4; i++)
                    args[i] = cpu->get_reg(i);
                kernel::process *pr = sys->get_kernel_system()->crr_process();
                const std::uint32_t sp = cpu->get_sp();
                for (int i = 0; i < 4; i++) {
                    const std::uint32_t *w = pr ? reinterpret_cast<const std::uint32_t *>(pr->get_ptr_on_addr_space(sp + i * 4)) : nullptr;
                    args[4 + i] = w ? *w : 0;
                }
                model_us = common::gpumodel::gl_call_us(function_ord, args);
            } else if (function_ord == 1) {
                model_us = common::gpumodel::screen_post_us();
            }
        }
        const auto model_t0 = std::chrono::steady_clock::now();

        dispatch_find_result->second.first(sys, sys->get_kernel_system()->crr_process(), sys->get_cpu());

        if (model_us > 0)
            common::gpumodel::pay_us(model_us, std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - model_t0).count());
    }

    void dispatcher::shutdown(drivers::graphics_driver *driver) {
        post_transferer_.destroy(driver);

        video_player_container_.clear();
        cameras_.clear();
        dsp_manager_.shutdown();

        has_mediums_pending_destroy_ = false;
        {
            const std::lock_guard<std::mutex> guard(mediums_pending_destroy_lock_);
            mediums_pending_destroy_.clear();
        }

        // The players those handles refer to are gone with the manager above.
        has_players_notify_deferred_ = false;
        {
            const std::lock_guard<std::mutex> guard(players_notify_deferred_lock_);
            players_notify_deferred_.clear();
        }

        has_streams_notify_deferred_ = false;
        {
            const std::lock_guard<std::mutex> guard(streams_notify_deferred_lock_);
            streams_notify_deferred_.clear();
        }

        egl_controller_ = std::make_unique<egl_controller>(driver);
    }

    void dispatcher::update_all_screens(eka2l1::system *sys) {
        epoc::screen *scr = winserv_->get_screens();

        while (scr != nullptr) {
            eka2l1::rect up_rect;
            up_rect.top = { 0, 0 };
            up_rect.size = scr->size();

            dispatch::update_screen(sys, 0, scr->number, 1, &up_rect);
            scr = scr->next;
        }
    }

    address dispatcher::add_static_string(const std::uint32_t key, const std::string &value) {
        if (static_string_addrs_.find(key) != static_string_addrs_.end()) {
            return 0;
        }

        char *base = reinterpret_cast<char*>(static_data_chunk_->host_base());
        address base_virt = static_data_chunk_->base(nullptr).ptr_address();

        std::memcpy(base + static_data_allocated_, value.data(), value.length());
        base[static_data_allocated_ + value.length()] = '\0';

        address return_val = base_virt + static_data_allocated_;
        static_string_addrs_.emplace(key, return_val);

        static_data_allocated_ += static_cast<std::uint32_t>(value.length() + 1);
        return return_val;
    }

    address dispatcher::retrieve_static_string(const std::uint32_t key) {
        auto ite = static_string_addrs_.find(key);
        if (ite == static_string_addrs_.end()) {
            return 0;
        }

        return ite->second;
    }

    bool dispatcher::patch_libraries(const std::u16string &path, const patch_info *patches,
        const std::size_t patch_count) {
        codeseg_ptr seg = libmngr_->load(path);

        if (!seg) {
            return false;
        }

        seg->set_patched();
        seg->set_entry_point_disabled();

        for (std::size_t i = 0; i < patch_count; i++) {
            const address orgaddr = seg->lookup_no_relocate(patches[i].ordinal_number_);
            if (!orgaddr) {
                continue;
            }

            const address entryentry = trampoline_chunk_->base(nullptr).ptr_address() + trampoline_allocated_;

            if (seg->is_rom()) {
                void *ptr = mem_->get_real_pointer(orgaddr & ~1);

                if (orgaddr & 1) {
                    std::memcpy(ptr, hle::THUMB_TRAMPOLINE_ASM, sizeof(hle::THUMB_TRAMPOLINE_ASM));

                    std::uint32_t offset_do_write = sizeof(hle::THUMB_TRAMPOLINE_ASM);

                    if (((orgaddr & ~1) & 3) == 0) {
                        offset_do_write -= 2;
                    }

                    ptr = reinterpret_cast<std::uint8_t *>(ptr) + offset_do_write;
                } else {
                    std::memcpy(ptr, hle::ARM_TRAMPOLINE_ASM, sizeof(hle::ARM_TRAMPOLINE_ASM));
                    ptr = reinterpret_cast<std::uint8_t *>(ptr) + sizeof(hle::ARM_TRAMPOLINE_ASM);
                }

                *reinterpret_cast<std::uint32_t *>(ptr) = entryentry;
            }

            std::uint32_t *start_base = reinterpret_cast<std::uint32_t *>(reinterpret_cast<std::uint8_t *>(
                                                                              trampoline_chunk_->host_base())
                + trampoline_allocated_);

            start_base[0] = 0xEFC10001;
            start_base[1] = 0xE12FFF1E;     // BX LR
            start_base[2] = patches[i].dispatch_number_;

            // TODO!!! Export table is fixed as a whole, not as an individual, this is bad for HLEing only some functions!
            seg->set_export(patches[i].ordinal_number_, entryentry);
            
            // Check if symbols exist for this libraries
            auto ite = dispatch::dispatch_funcs.find(patches[i].dispatch_number_);
            if ((ite != dispatch::dispatch_funcs.end()) && (ite->second.second != nullptr)) {
                symbol_lookup_.emplace(ite->second.second, entryentry);
            }

            trampoline_allocated_ += 12;
        }

        return true;
    }

    address dispatcher::lookup_dispatcher_function_by_symbol(const char *symbol) {
        if (!symbol) {
            return 0;
        }

        auto ite = symbol_lookup_.find(symbol);
        if (ite != symbol_lookup_.end()) {
            return ite->second;
        }

        // An extension entry point reached through eglGetProcAddress has no library
        // ordinal, so patch_libraries() never built its guest trampoline. Build one on
        // first lookup for any symbol the dispatcher does register.
        for (const auto &dispatch_func : dispatch::dispatch_funcs) {
            if (!dispatch_func.second.second || symbol != std::string(dispatch_func.second.second)) {
                continue;
            }

            const address entry = trampoline_chunk_->base(nullptr).ptr_address() + trampoline_allocated_;
            std::uint32_t *start_base = reinterpret_cast<std::uint32_t *>(reinterpret_cast<std::uint8_t *>(
                trampoline_chunk_->host_base()) + trampoline_allocated_);

            start_base[0] = 0xEFC10001;
            start_base[1] = 0xE12FFF1E; // BX LR
            start_base[2] = dispatch_func.first;

            symbol_lookup_.emplace(symbol, entry);
            trampoline_allocated_ += 12;

            return entry;
        }

        return 0;
    }
}

namespace eka2l1::epoc {
    void dispatcher_do_resolve(eka2l1::system *sys, const std::uint32_t ordinal) {
        dispatch::dispatcher *dispatcher = sys->get_dispatcher();
        dispatcher->resolve(sys, ordinal);
    }

    void dispatcher_do_event_add(eka2l1::system *sys, epoc::raw_event &evt) {
        dispatch::dispatcher *dispatcher = sys->get_dispatcher();

        switch (evt.type_) {
        case epoc::raw_event_type_redraw:
            dispatcher->update_all_screens(sys);
            break;

        // This affects Window Service's processing state on real phone. Ignore.
        case epoc::raw_event_type_active:
        case epoc::raw_event_type_inactive:
            break;

        default:
            LOG_WARN(HLE_DISPATCHER, "Unhandled raw event {}", static_cast<int>(evt.type_));
            break;
        }
    }
}
