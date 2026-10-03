/*
 * Copyright (c) 2019 EKA2L1 Team.
 * Copyright 2015 Dolphin Emulator Project.
 * 
 * This file is part of EKA2L1 project.
 * 
 * A portion of the code is borrowed from MainWindow.cpp in DolphinQt
 * source folder.
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

#include <common/arghandler.h>
#include <common/configure.h>
#include <common/cvt.h>
#include <common/log.h>
#include <common/random.h>
#include <common/thread.h>
#include <common/time.h>
#include <common/vecx.h>
#include <qt/cmdhandler.h>
#include <qt/displaywidget.h>
#include <qt/seh_handler.h>
#include <qt/state.h>
#include <qt/thread.h>
#include <qt/utils.h>

#include <drivers/graphics/emu_window.h>
#include <drivers/graphics/graphics.h>
#include <drivers/input/common.h>
#include <drivers/input/emu_controller.h>

#include <services/window/window.h>
#include <qt/custom_question_dialog.h>
#include <qt/dialog_driver.h>

#include <kernel/kernel.h>

#if EKA2L1_PLATFORM(WIN32)
#include <Windows.h>
#if defined(_MSC_VER)
#include <DbgHelp.h>
#pragma comment(lib, "dbghelp.lib")
#endif
#endif

#include <QApplication>
#include <QWindow>

#include <qt/mainwindow.h>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <new>
#include <typeinfo>

static eka2l1::drivers::input_event make_mouse_event_driver(const float x, const float y, const float z, const int button, const int action,
    const int mouse_id) {
    eka2l1::drivers::input_event evt;
    evt.type_ = eka2l1::drivers::input_event_type::touch;
    evt.mouse_.raw_screen_pos_ = false;
    evt.mouse_.pos_x_ = static_cast<int>(x);
    evt.mouse_.pos_y_ = static_cast<int>(y);
    evt.mouse_.pos_z_ = static_cast<int>(z);
    evt.mouse_.mouse_id = static_cast<std::uint32_t>(mouse_id);
    evt.mouse_.button_ = static_cast<eka2l1::drivers::mouse_button>(button);
    evt.mouse_.action_ = static_cast<eka2l1::drivers::mouse_action>(action);

    return evt;
}

/**
 * \brief Callback when a host mouse event is triggered
 * \param mouse_pos    position of mouse pointer          
 * \param button       0: left, 1: right, 2: other, -1: no button     
 * \param action       0: press, 1: repeat(move), 2: release      
 */
static void on_ui_window_mouse_evt(void *userdata, eka2l1::vec3 mouse_pos, int button, int action, int mouse_id) {
    float mouse_pos_x = static_cast<float>(mouse_pos.x), mouse_pos_y = static_cast<float>(mouse_pos.y),
        mouse_pos_z = static_cast<float>(mouse_pos.z);

    eka2l1::desktop::emulator *emu = reinterpret_cast<eka2l1::desktop::emulator *>(userdata);

    const std::lock_guard<std::mutex> guard(emu->lockdown);
    if (emu->ui_main && emu->ui_main->deliver_overlay_mouse_event(mouse_pos, button, action, mouse_id)) {
        return;
    }

    const float scale = emu->symsys->get_config()->ui_scale;
    auto mouse_evt = make_mouse_event_driver(mouse_pos_x / scale, mouse_pos_y / scale, mouse_pos_z / scale,
        button, action, emu->ui_main->map_mouse_id_to_touch_index(mouse_id, (action == 2)));

    if ((emu->symsys) && emu->winserv) {
        emu->winserv->queue_input_from_driver(mouse_evt);
    }
}

static eka2l1::drivers::input_event make_controller_event_driver(int jid, int button, bool is_press) {
    eka2l1::drivers::input_event evt;
    evt.type_ = eka2l1::drivers::input_event_type::button;
    evt.button_.button_ = button;
    evt.button_.controller_ = jid;
    evt.button_.state_ = is_press ? eka2l1::drivers::button_state::pressed : eka2l1::drivers::button_state::released;

    return evt;
}

static eka2l1::drivers::input_event make_controller_event_driver(int jid, int button, float axisx, float axisy) {
    eka2l1::drivers::input_event evt;
    evt.type_ = eka2l1::drivers::input_event_type::button;
    evt.button_.button_ = button;
    evt.button_.controller_ = jid;
    evt.button_.axis_[0] = axisx;
    evt.button_.axis_[1] = axisy;
    evt.button_.state_ = eka2l1::drivers::button_state::joy;

    return evt;
}

static eka2l1::drivers::input_event make_key_event_driver(const int key, const eka2l1::drivers::key_state key_state) {
    eka2l1::drivers::input_event evt;
    evt.type_ = eka2l1::drivers::input_event_type::key;
    evt.key_.state_ = key_state;
    evt.key_.code_ = key;

    return evt;
}

static void on_ui_window_key_release(void *userdata, const int key) {
    eka2l1::desktop::emulator *emu = reinterpret_cast<eka2l1::desktop::emulator *>(userdata);
    auto key_evt = make_key_event_driver(key, eka2l1::drivers::key_state::released);

    const std::lock_guard<std::mutex> guard(emu->lockdown);
    if (emu->ui_main && emu->ui_main->deliver_key_event(static_cast<std::uint32_t>(key), false)) {
        return;
    }
    if (emu->winserv)
        emu->winserv->queue_input_from_driver(key_evt);
}

static void on_ui_window_key_press(void *userdata, const int key) {
    eka2l1::desktop::emulator *emu = reinterpret_cast<eka2l1::desktop::emulator *>(userdata);
    auto key_evt = make_key_event_driver(key, eka2l1::drivers::key_state::pressed);

    const std::lock_guard<std::mutex> guard(emu->lockdown);
    if (emu->ui_main && emu->ui_main->deliver_key_event(static_cast<std::uint32_t>(key), true)) {
        return;
    }
    if (emu->winserv)
        emu->winserv->queue_input_from_driver(key_evt);
}

namespace eka2l1::desktop {
    static constexpr const char *graphics_driver_thread_name = "Graphics thread";
    static constexpr const char *os_thread_name = "Symbian OS thread";

    static int graphics_driver_thread_initialization(emulator &state) {
        // Halloween decoration breath of the graphics
        eka2l1::common::set_thread_name(graphics_driver_thread_name);
        eka2l1::common::set_thread_priority(eka2l1::common::thread_priority_high);

        state.window->raw_mouse_event = on_ui_window_mouse_evt;
        state.window->button_pressed = on_ui_window_key_press;
        state.window->button_released = on_ui_window_key_release;

        state.window->init("Emulator display", eka2l1::vec2(800, 600), drivers::emu_window_flag_maximum_size);
        state.window->set_userdata(&state);

        // We got window and context ready (OpenGL, let makes stuff now)
        // TODO: Configurable
        state.graphics_driver = drivers::create_graphics_driver(drivers::graphic_api::opengl, state.window->get_window_system_info());
        state.graphics_driver->update_surface_size(state.window->window_fb_size());

        // Now we start set the hook
        state.window->resize_hook = [](void *userdata, const eka2l1::vec2 &size) {
            emulator *state = reinterpret_cast<emulator *>(userdata);
            state->graphics_driver->update_surface_size(size);
        };

        state.symsys->set_graphics_driver(state.graphics_driver.get());

        drivers::emu_window *window = state.window;

        switch (state.graphics_driver->get_current_api()) {
        case drivers::graphic_api::opengl: {
            state.graphics_driver->set_display_hook([window]() {
                window->swap_buffer();
                window->poll_events();
            });

            break;
        }

        default: {
            state.graphics_driver->set_display_hook([window]() {
                window->poll_events();
            });

            break;
        }
        }

        state.joystick_controller = drivers::new_emu_controller(eka2l1::drivers::controller_type::sdl2);
        state.joystick_controller->on_button_event = [&](int jid, int button, bool pressed) {
            const std::lock_guard<std::mutex> guard(state.lockdown);
            auto evt = make_controller_event_driver(jid, button, pressed);

            // If the handler accepts it
            if (state.ui_main->controller_event_handler(evt) && state.winserv) {
                state.winserv->queue_input_from_driver(evt);
            }
        };
        state.joystick_controller->on_joy_move = [&](int jid, int button, float axisx, float axisy) {
            const std::lock_guard<std::mutex> guard(state.lockdown);
            auto evt = make_controller_event_driver(jid, button, axisx, axisy);

            state.ui_main->controller_event_handler(evt);
        };

        // Signal that the initialization is done
        state.graphics_event.set();
        return 0;
    }

    static int graphics_driver_thread_deinitialization(emulator &state) {
        if (state.stage_two_inited)
            state.graphics_event.wait();

        state.joystick_controller->stop_polling();
        state.graphics_driver.reset();

        return 0;
    }

    void graphics_driver_thread(emulator &state) {
        int result = graphics_driver_thread_initialization(state);

        if (result != 0) {
            LOG_ERROR(FRONTEND_CMDLINE, "Graphics driver initialization failed with code {}", result);
            return;
        }

        state.joystick_controller->start_polling();

        // Keep running. User which want to change the graphics backend will have to restart EKA2L1.
        state.graphics_event.reset();
        state.graphics_driver->run();

        result = graphics_driver_thread_deinitialization(state);

        if (result != 0) {
            LOG_ERROR(FRONTEND_CMDLINE, "Graphics driver deinitialization failed with code {}", result);
            return;
        }
    }

    void os_thread(emulator &state) {
#if EKA2L1_PLATFORM(WIN32)
        HRESULT hr = S_OK;
        hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

        if (hr != S_OK) {
            LOG_CRITICAL(FRONTEND_CMDLINE, "Failed to initialize COM");
            return;
        }
#endif

        eka2l1::common::set_thread_name(os_thread_name);
        eka2l1::common::set_thread_priority(eka2l1::common::thread_priority_high);

        bool first_time = true;

        while (true) {
            if (state.should_emu_quit) {
                break;
            }

            const bool success = state.stage_two();
            state.init_event.set();

            if (first_time) {
                state.graphics_event.wait();
                first_time = false;
            }

            if (success) {
                break;
            }

            // Try wait for initialization from other parties to make this success.
            state.init_event.reset();
            state.init_event.wait();
        }

        // Register SEH handler for this thread
#if EKA2L1_PLATFORM(WIN32) && defined(_MSC_VER) && ENABLE_SEH_HANDLER
        _set_se_translator(seh_handler_translator_func);
#endif

        while (!state.should_emu_quit) {
#if ENABLE_SEH_HANDLER
            try {
#endif
                state.symsys->loop();
#if ENABLE_SEH_HANDLER
            } catch (std::exception &exc) {
                std::cout << "Main loop exited with exception: " << exc.what() << std::endl;
                // TODO： Come back and make this display in UI
                // state.debugger->queue_error(exc.what());
                state.should_emu_quit = true;
                break;
            }
#endif

            if (state.should_emu_pause && !state.should_emu_quit) {
                state.pause_event.wait();
                state.pause_event.reset();
            }
        }

        state.kill_event.wait();
        state.symsys.reset();
        state.graphics_event.set();

#if EKA2L1_PLATFORM(WIN32)
        CoUninitialize();
#endif
    }

    void kill_emulator(emulator &state) {
        state.should_emu_quit = true;
        state.should_emu_pause = false;

        state.pause_event.set();

        kernel_system *kern = state.symsys->get_kernel_system();

        if (kern) {
            kern->stop_cores_idling();
        }

        state.graphics_driver->abort();
        state.init_event.set();
        state.kill_event.set();
    }

    // Process-wide terminate handler. An exception escaping a non-main thread (graphics thread,
    // std::thread workers...) used to call std::terminate silently, losing buffered log output.
    // Log the exception identity and flush before aborting.
    static void log_unhandled_exception_and_abort() {
        const std::exception_ptr ep = std::current_exception();

        if (ep) {
            try {
                std::rethrow_exception(ep);
            } catch (const std::exception &exc) {
                LOG_CRITICAL(SYSTEM, "Unhandled exception, std::terminate called: type={}, what()={}", typeid(exc).name(), exc.what());
            } catch (...) {
                LOG_CRITICAL(SYSTEM, "Unhandled non-std::exception object thrown, std::terminate called");
            }
        } else {
            LOG_CRITICAL(SYSTEM, "std::terminate called with no active exception");
        }

        if (eka2l1::log::spd_logger) {
            eka2l1::log::spd_logger->flush();
        }

        std::abort();
    }

#if EKA2L1_PLATFORM(WIN32) && defined(_MSC_VER)
    // A C++ exception that finds no matching catch on its thread's stack never reaches
    // std::terminate on MSVC: the unwinder's search phase fails and the OS kills the process with
    // code 0xE06D7363, leaving no trace in the log. SetUnhandledExceptionFilter is the last hook
    // that runs in that case, so use it to name the thrown type before the process dies.
    namespace {
        constexpr DWORD MSVC_CPP_EXCEPTION_CODE = 0xE06D7363;

        // MSVC ThrowInfo layout. On x64 every link is a 32-bit RVA from the module base given in
        // ExceptionInformation[3]; on x86 they are absolute pointers (base is then 0).
        struct msvc_ptmd {
            int mdisp;
            int pdisp;
            int vdisp;
        };

        struct msvc_catchable_type {
            unsigned int properties;
            int p_type;
            msvc_ptmd this_displacement;
            int size_or_offset;
            int copy_function;
        };

        struct msvc_catchable_type_array {
            int count;
            int types[1];
        };

        struct msvc_throw_info {
            unsigned int attributes;
            int unwind;
            int forward_compat;
            int catchable_type_array;
        };

        struct msvc_type_descriptor {
            const void *vftable;
            void *spare;
            char name[1];
        };

        static const void *msvc_rva(const ULONG_PTR base, const int value) {
#ifdef _WIN64
            return reinterpret_cast<const void *>(base + static_cast<unsigned int>(value));
#else
            return reinterpret_cast<const void *>(static_cast<ULONG_PTR>(static_cast<unsigned int>(value)));
#endif
        }

        // No C++ objects with destructors here, so __try is legal. Returns nullptr on any fault.
        static const char *safe_call_what(const std::exception *exc) {
            const char *result = nullptr;
            __try {
                result = exc->what();
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                result = nullptr;
            }
            return result;
        }

        // Fills thrown type name and (best effort) what(). Faults while walking are swallowed.
        static void decode_msvc_exception(const EXCEPTION_RECORD *rec, char *type_name, const size_t type_name_size,
            char *what_text, const size_t what_text_size) {
            type_name[0] = '\0';
            what_text[0] = '\0';

            __try {
                if (rec->NumberParameters < 3) {
                    return;
                }

                const void *object = reinterpret_cast<const void *>(rec->ExceptionInformation[1]);
                const msvc_throw_info *info = reinterpret_cast<const msvc_throw_info *>(rec->ExceptionInformation[2]);
#ifdef _WIN64
                const ULONG_PTR base = (rec->NumberParameters >= 4) ? rec->ExceptionInformation[3] : 0;
#else
                const ULONG_PTR base = 0;
#endif
                if (!info || !info->catchable_type_array) {
                    return;
                }

                const msvc_catchable_type_array *array = static_cast<const msvc_catchable_type_array *>(msvc_rva(base, info->catchable_type_array));

                for (int i = 0; i < array->count && i < 32; i++) {
                    const msvc_catchable_type *type = static_cast<const msvc_catchable_type *>(msvc_rva(base, array->types[i]));
                    const msvc_type_descriptor *desc = static_cast<const msvc_type_descriptor *>(msvc_rva(base, type->p_type));

                    if (i == 0) {
                        strncpy_s(type_name, type_name_size, desc->name, _TRUNCATE);
                    }

                    if (strcmp(desc->name, ".?AVexception@std@@") == 0) {
                        // Adjust the object pointer to its std::exception subobject.
                        const char *adjusted = static_cast<const char *>(object);
                        if (type->this_displacement.pdisp >= 0) {
                            const char *vbtable = *reinterpret_cast<const char *const *>(adjusted + type->this_displacement.pdisp);
                            adjusted += type->this_displacement.pdisp + *reinterpret_cast<const int *>(vbtable + type->this_displacement.vdisp);
                        }
                        adjusted += type->this_displacement.mdisp;

                        const char *what = safe_call_what(reinterpret_cast<const std::exception *>(adjusted));
                        if (what) {
                            strncpy_s(what_text, what_text_size, what, _TRUNCATE);
                        }
                        break;
                    }
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
        }

        // Releases the DbgHelp lock on every exit path. This matters: the stack logger runs from the
        // new-handler, so a nested allocation failure inside DbgHelp or the logger can unwind straight
        // through it. Leaking the lock there would deadlock every later crash report.
        struct critical_section_guard {
            CRITICAL_SECTION *section_;

            explicit critical_section_guard(CRITICAL_SECTION *section)
                : section_(section) {
                EnterCriticalSection(section_);
            }

            ~critical_section_guard() {
                LeaveCriticalSection(section_);
            }

            critical_section_guard(const critical_section_guard &) = delete;
            critical_section_guard &operator=(const critical_section_guard &) = delete;
        };

        // Logs a symbolized stack of the calling thread (module+RVA always, symbol and file:line when
        // DbgHelp can resolve them). DbgHelp is not thread safe, hence the lock.
        static void log_symbolized_stack(const char *tag, const int skip_frames) {
            static CRITICAL_SECTION lock;
            static INIT_ONCE lock_once = INIT_ONCE_STATIC_INIT;
            static bool sym_ready = false;

            InitOnceExecuteOnce(&lock_once, [](PINIT_ONCE, PVOID, PVOID *) -> BOOL {
                InitializeCriticalSection(&lock);
                return TRUE;
            }, nullptr, nullptr);

            critical_section_guard guard(&lock);

            if (!sym_ready) {
                SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
                SymInitialize(GetCurrentProcess(), nullptr, TRUE);
                sym_ready = true;
            }

            void *frames[24] = {};
            const USHORT count = CaptureStackBackTrace(0, 24, frames, nullptr);
            const HANDLE process = GetCurrentProcess();

            alignas(SYMBOL_INFO) char sym_buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME] = {};

            LOG_CRITICAL(SYSTEM, "[{}] symbolized stack, thread_id={}, {} frames (skipping {})", tag, GetCurrentThreadId(), count, skip_frames);

            for (USHORT i = static_cast<USHORT>(skip_frames); i < count; i++) {
                HMODULE module = nullptr;
                char module_path[MAX_PATH] = {};

                GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<LPCSTR>(frames[i]), &module);
                if (module) {
                    GetModuleFileNameA(module, module_path, MAX_PATH);
                }

                const char *module_name = module_path;
                for (const char *p = module_path; *p; p++) {
                    if (*p == '\\' || *p == '/') {
                        module_name = p + 1;
                    }
                }

                const DWORD64 address = reinterpret_cast<DWORD64>(frames[i]);

                SYMBOL_INFO *symbol = reinterpret_cast<SYMBOL_INFO *>(sym_buffer);
                symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
                symbol->MaxNameLen = MAX_SYM_NAME;

                DWORD64 displacement = 0;
                const char *sym_name = "<no symbol>";
                if (SymFromAddr(process, address, &displacement, symbol)) {
                    sym_name = symbol->Name;
                }

                IMAGEHLP_LINE64 line = {};
                line.SizeOfStruct = sizeof(line);
                DWORD line_disp = 0;
                const bool has_line = SymGetLineFromAddr64(process, address, &line_disp, &line) != FALSE;

                LOG_CRITICAL(SYSTEM, "  #{:02} {}+0x{:x} | {}+0x{:x} | {}:{}", i, module_name[0] ? module_name : "<unknown>",
                    reinterpret_cast<std::uintptr_t>(frames[i]) - reinterpret_cast<std::uintptr_t>(module), sym_name,
                    static_cast<unsigned long long>(displacement), has_line ? line.FileName : "<no line info>",
                    has_line ? line.LineNumber : 0);
            }

            if (eka2l1::log::spd_logger) {
                eka2l1::log::spd_logger->flush();
            }

        }

        // Called by operator new at the point of allocation failure, on the allocating thread, with
        // the real stack intact. Must not return (operator new would retry forever).
        static void log_new_failure_and_throw() {
            static thread_local bool in_handler = false;
            static volatile LONG report_count = 0;

            if (!in_handler && InterlockedIncrement(&report_count) <= 8) {
                in_handler = true;
                log_symbolized_stack("new_handler: operator new failed", 2);
                in_handler = false;
            }

            throw std::bad_alloc();
        }

        // First-chance backup: sees every C++ throw before unwinding; logs only std::bad_alloc.
        static LONG CALLBACK log_first_chance_bad_alloc(EXCEPTION_POINTERS *pointers) {
            const EXCEPTION_RECORD *rec = pointers->ExceptionRecord;
            static thread_local bool in_hook = false;
            static volatile LONG report_count = 0;

            if (rec->ExceptionCode != MSVC_CPP_EXCEPTION_CODE || in_hook) {
                return EXCEPTION_CONTINUE_SEARCH;
            }

            char type_name[256];
            char what_text[512];
            decode_msvc_exception(rec, type_name, sizeof(type_name), what_text, sizeof(what_text));

            if (!strstr(type_name, "bad_alloc") || InterlockedIncrement(&report_count) > 8) {
                return EXCEPTION_CONTINUE_SEARCH;
            }

            in_hook = true;
            LOG_CRITICAL(SYSTEM, "[vectored] first-chance C++ exception: type={}, what()={}", type_name, what_text);
            log_symbolized_stack("vectored hook", 2);
            in_hook = false;

            return EXCEPTION_CONTINUE_SEARCH;
        }

        // Permanent, always-on first-chance hook for hardware faults only: access violation, heap
        // corruption and stack buffer overrun. Vectored handlers run before any __try or catch on the
        // faulting thread, so this still fires when an SEH-to-C++ translator (ENABLE_SEH_HANDLER) or a
        // JIT frame without unwind info stops SetUnhandledExceptionFilter from ever being reached.
        // Nothing in the emulator faults on purpose (no guard pages, no fault-driven memory emulation,
        // no other vectored handlers), so these codes are rare and always real. C++ exceptions are
        // deliberately not looked at. Purely diagnostic: always lets the exception continue unchanged.
        static LONG CALLBACK log_hardware_fault(EXCEPTION_POINTERS *pointers) {
            static thread_local bool in_handler = false;
            static volatile LONG report_count = 0;

            const EXCEPTION_RECORD *rec = pointers->ExceptionRecord;

            switch (rec->ExceptionCode) {
            case EXCEPTION_ACCESS_VIOLATION:
            case 0xC0000374: // STATUS_HEAP_CORRUPTION
            case 0xC0000409: // STATUS_STACK_BUFFER_OVERRUN
                break;

            default:
                return EXCEPTION_CONTINUE_SEARCH;
            }

            if (in_handler || InterlockedIncrement(&report_count) > 8) {
                return EXCEPTION_CONTINUE_SEARCH;
            }

            in_handler = true;

            HMODULE module = nullptr;
            char module_path[MAX_PATH] = {};

            GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCSTR>(rec->ExceptionAddress), &module);
            if (module) {
                GetModuleFileNameA(module, module_path, MAX_PATH);
            }

            const char *module_name = module_path;
            for (const char *p = module_path; *p; p++) {
                if (*p == '\\' || *p == '/') {
                    module_name = p + 1;
                }
            }

            LOG_CRITICAL(SYSTEM, "[hardware fault] code=0x{:08x}, thread_id={}, fault_ip={:p} ({}+0x{:x})",
                rec->ExceptionCode, GetCurrentThreadId(), rec->ExceptionAddress, module_name[0] ? module_name : "<no module, JIT or unmapped code>",
                reinterpret_cast<std::uintptr_t>(rec->ExceptionAddress) - reinterpret_cast<std::uintptr_t>(module));

            if (rec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && rec->NumberParameters >= 2) {
                const ULONG_PTR kind = rec->ExceptionInformation[0];
                LOG_CRITICAL(SYSTEM, "[hardware fault] access violation {} address 0x{:x}", (kind == 0) ? "reading" : ((kind == 1) ? "writing" : "executing"),
                    static_cast<unsigned long long>(rec->ExceptionInformation[1]));
            }

            // Captured from inside the handler: the faulting frame sits just past the ntdll dispatch frames.
            log_symbolized_stack("hardware fault", 1);

            in_handler = false;
            return EXCEPTION_CONTINUE_SEARCH;
        }

        static LONG WINAPI log_unhandled_seh_exception(EXCEPTION_POINTERS *pointers) {
            const EXCEPTION_RECORD *rec = pointers->ExceptionRecord;

            if (rec->ExceptionCode == MSVC_CPP_EXCEPTION_CODE) {
                char type_name[256];
                char what_text[512];
                decode_msvc_exception(rec, type_name, sizeof(type_name), what_text, sizeof(what_text));

                LOG_CRITICAL(SYSTEM, "Unhandled C++ exception (no catch on thread stack): type={}, what()={}, thread_id={}",
                    type_name[0] ? type_name : "<undecodable>", what_text[0] ? what_text : "<unavailable>", GetCurrentThreadId());

                void *frames[16] = {};
                const USHORT count = CaptureStackBackTrace(0, 16, frames, nullptr);

                for (USHORT i = 0; i < count; i++) {
                    HMODULE module = nullptr;
                    char module_path[MAX_PATH] = {};

                    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                        reinterpret_cast<LPCSTR>(frames[i]), &module);
                    if (module) {
                        GetModuleFileNameA(module, module_path, MAX_PATH);
                    }

                    const char *module_name = module_path;
                    for (const char *p = module_path; *p; p++) {
                        if (*p == '\\' || *p == '/') {
                            module_name = p + 1;
                        }
                    }

                    LOG_CRITICAL(SYSTEM, "  #{:02} {:p} {}+0x{:x}", i, frames[i], module_name[0] ? module_name : "<unknown>",
                        reinterpret_cast<std::uintptr_t>(frames[i]) - reinterpret_cast<std::uintptr_t>(module));
                }
            } else {
                LOG_CRITICAL(SYSTEM, "Unhandled exception: code=0x{:08x}, address={:p}, thread_id={}", rec->ExceptionCode,
                    rec->ExceptionAddress, GetCurrentThreadId());
            }

            if (eka2l1::log::spd_logger) {
                eka2l1::log::spd_logger->flush();
            }

            return (rec->ExceptionCode == MSVC_CPP_EXCEPTION_CODE) ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
        }
    }
#endif

    int emulator_entry(QApplication &application, emulator &state, const int argc, const char **argv) {
        std::set_terminate(log_unhandled_exception_and_abort);
#if EKA2L1_PLATFORM(WIN32) && defined(_MSC_VER)
        SetUnhandledExceptionFilter(log_unhandled_seh_exception);
        // Always on: see log_hardware_fault. Hardware faults only, so it is silent in normal operation.
        AddVectoredExceptionHandler(1, log_hardware_fault);
#if defined(EKA2L1_FIRST_CHANCE_EXCEPTION_HOOK)
        // Opt-in diagnostic only: a first-chance hook runs on every C++ throw in the process.
        AddVectoredExceptionHandler(1, log_first_chance_bad_alloc);
#endif
        std::set_new_handler(log_new_failure_and_throw);
#endif

        state.stage_one();

        // Instantiate UI and High-level interface threads
        std::thread os_thread_obj(os_thread, std::ref(state));
        state.init_event.wait();

        eka2l1::common::arg_parser parser(argc, argv);

        parser.add("--help, -h", "Display helps menu", help_option_handler);
        parser.add("--listapp", "List all installed applications", list_app_option_handler);
        parser.add("--listdevices", "List all installed devices", list_devices_option_handler);
        parser.add("--app, -a, --run", "Run an app with given name or UID, or the absolute virtual path to executable.\n"
                                        "\t\t\t  See list of apps with --listapp.\n"
                                        "\t\t\t  Extra command line arguments can be passed to the application.\n"
                                        "\n"
                                        "\t\t\t  Some example:\n"
                                        "\t\t\t    eka2l1 --run C:\\sys\\bin\\BitmapTest.exe \"--hi --arg 5\"\n"
                                        "\t\t\t    eka2l1 --run Bounce\n"
                                        "\t\t\t    eka2l1 --run 0x200412ED\n",
            app_specifier_option_handler);
        parser.add("--device, -dvc", "Set a device to be ran, through the given firmware code. This device will also be saved in the configuration as the current device.\n"
                               "\t\t\t Example: --device RH-29",
            device_set_option_handler);
        parser.add("--install, -i", "Install a SIS.", app_install_option_handler);
        parser.add("--remove, -r", "Remove an package.", package_remove_option_handler);
        parser.add("--fullscreen, -f", "Display the emulator in fullscreen.", fullscreen_option_handler);
        parser.add("--mount, -m", "Load a folder/zip as a Game Card ROM.", mount_card_option_handler);
        parser.add("--keybindprofile, -kbp", "Set a keybind profile to associate with the emulator launch. Don't include any file extension here.\n"
                                              "\t Example: eka2l1 --kbp controller_for_octopus",
            keybind_profile_option_handler);
        parser.add("--mmcid, --cid, -cid", "Set the MMC-ID for the mounted card", set_mmcid_option_handler);
        parser.add("--runng, --appng, -rng, -ang", "Run a single N-Gage game inside the E drive", run_ngage_game_option_handler);

#if ENABLE_PYTHON_SCRIPTING
        parser.add("--gendocs", "Generate Python documentation", python_docgen_option_handler);
#endif

        if (argc > 1) {
            std::string err;
            state.should_emu_quit = !parser.parse(&state, &err);

            if (state.should_emu_quit) {
                // Notify the OS thread that is still sleeping, waiting for
                // graphics sema to be freed.
                state.graphics_event.set();
                state.kill_event.set();

                std::cout << err << std::endl;
                os_thread_obj.join();

                return -1;
            }
        }

        state.ui_main = new main_window(application, nullptr, state);
        state.ui_main->setWindowTitle(get_emulator_window_title());
        state.ui_main->load_and_show();

        eka2l1::drivers::ui::main_window_instance = state.ui_main;
        state.window = state.ui_main->render_window();

        std::thread graphics_thread_obj(graphics_driver_thread, std::ref(state));

        if (state.app_launch_from_command_line) {
            if (!state.launched_app_name_.empty()) {
                state.ui_main->set_discord_presence_current_playing(state.launched_app_name_);
            }
            state.ui_main->setup_and_switch_to_game_mode();
        }

        const int exec_code = application.exec();
        kill_emulator(state);

        // Wait for OS thread to die
        os_thread_obj.join();
        graphics_thread_obj.join();

        delete state.ui_main;
        return exec_code;
    }
}
