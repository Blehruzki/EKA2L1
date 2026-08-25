/*
 * Copyright (c) 2018 EKA2L1 Team
 * 
 * This file is part of EKA2L1 project
 * (see bentokun.github.com/EKA2L1).
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

#include <common/log.h>
#include <utils/err.h>

#include <kernel/kernel.h>
#include <kernel/server.h>
#include <kernel/timing.h>

#include <config/config.h>

namespace eka2l1::service {
    // Fail-fast timeout for messages sent to !BackupServer that never get a
    // message_complete from the guest's own dispatch. See
    // investigation/ngage-backup-hang (rounds 13-25) in the EKA2L1 project for the
    // full history: the guest's compiled switch dispatch has a genuine, in-range
    // no-op case for a specific backup-server opcode, so message_complete never
    // fires and the sender blocks forever on WaitForAnyRequest (which has no
    // timeout on real Symbian either - this is not fixable by finding a "missing
    // timeout" on the guest side). The real client (bafl.dll) already retries this
    // exact call up to 10 times, correctly bounded - but that bound only helps
    // against a callee that fails fast; it never gets a chance to retry if the
    // first attempt never returns at all. This timeout exists purely to make the
    // first attempt fail fast, so bafl.dll's own already-correct retry/give-up
    // logic can do the rest.
    //
    // Deliberately scoped to !BackupServer only, and to a single message at a
    // time, using a long (1 second of virtual time) delay - long enough that any
    // genuine completion (which rounds 13/23 showed happens within microseconds of
    // guest execution) would have already happened, so this should never race
    // against or misfire on a message that was always going to complete normally
    // (round 22 found that racing on the *event* of a re-arm, rather than genuine
    // elapsed time, is unsound - this uses elapsed time instead, specifically to
    // avoid that failure mode). Re-resolves the message by id at callback time
    // (rather than holding a raw pointer across the delay) and re-checks its
    // session/server/status before acting, since the message slot could in
    // principle have been reused for something else in the interim.
    //
    // Completion value: KErrCancel (-3), not KErrNone or a generic error code.
    // Rounds 50/55/56/57/58 traced this all the way through - completing with
    // KErrNone makes bafl.dll's own retry loop treat this as "try again"
    // (matching its literal gate check, an infinite non-progressing poll);
    // completing with a generic error (round 26 originally used
    // KErrNotSupported) lets that retry loop exit cleanly, but the value then
    // propagates into a real guest Leave that a backup-observer object's own
    // RunError()-equivalent handling doesn't survive a second time, escalating
    // to a genuine guest panic (E32USER-CBase/47) that leaves !ecomserver dead
    // and, downstream, leaves CdlServer spinning forever trying to talk to it
    // (rounds 51-54, 56). Round 57 found that same observer object's own
    // compiled code contains an explicit `!= KErrCancel` guard on its state
    // field - i.e. the guest binary already expects KErrCancel to mean "this
    // outstanding request was cancelled, not a real failure" and short-circuits
    // cleanly on it. Round 58 confirmed empirically, across repeated runs and
    // full thread-state dumps, that completing with KErrCancel avoids the panic
    // *and* leaves both !ecomserver and CdlServer in a normal idle/waiting
    // state instead of stuck - not just a quieter symptom, a genuinely resolved
    // one.
    static void arm_backup_server_msg_timeout(kernel_system *kern, ipc_msg_ptr msg) {
        static int timeout_evt_type = -1;

        if (timeout_evt_type == -1) {
            timeout_evt_type = kern->get_ntimer()->get_register_event("BackupServerMsgTimeout");
            if (timeout_evt_type == -1) {
                timeout_evt_type = kern->get_ntimer()->register_event("BackupServerMsgTimeout",
                    [kern](std::uint64_t userdata, std::uint64_t cycles_late) {
                        ipc_msg_ptr m = kern->get_msg(static_cast<int>(userdata));
                        if (!m) {
                            return;
                        }

                        if (m->msg_status != ipc_message_status::accepted) {
                            // Already completed normally (or never was this message
                            // at all anymore) - nothing to do.
                            return;
                        }

                        if (!m->msg_session || !m->msg_session->get_server()
                            || (m->msg_session->get_server()->name() != "!BackupServer")) {
                            return;
                        }

                        if (m->own_thr && (m->own_thr->current_state() != kernel::thread_state::stop)) {
                            epoc::request_status *final_sts = m->request_sts.get(m->own_thr->owning_process());
                            if (final_sts) {
                                m->msg_status = ipc_message_status::completed;
                                final_sts->set(epoc::error_cancel, kern->is_eka1());
                                m->own_thr->signal_request();

                                if (m->thread_handle_low) {
                                    kern->close(m->thread_handle_low);
                                }

                                kern->call_ipc_complete_callbacks(m, epoc::error_cancel);
                                LOG_WARN(KERNEL, "!BackupServer message (function={}) never completed by guest dispatch after 1s - failing fast with KErrCancel", m->function);
                            }
                        }

                        m->unref();
                    });
            }
        }

        kern->get_ntimer()->schedule_event(1000000, timeout_evt_type, static_cast<std::uint64_t>(msg->id));
    }

    server::~server() {
    }

    // Create a server with name
    server::server(kernel_system *kern, system *sys, kernel::thread *owner, const std::string name, bool hle, bool unhandle_callback_enable, const service::share_mode shmode)
        : kernel_obj(kern, name, nullptr, kernel::access_type::global_access)
        , sys(sys)
        , hle(hle)
        , owner_thread(owner)
        , unhandle_callback_enable(unhandle_callback_enable)
        , shmode_(shmode) {
        obj_type = kernel::object_type::server;

        if (owner_thread)
            owner_thread->increase_access_count();

        REGISTER_IPC(server, connect, -1, "Server::Connect");
        REGISTER_IPC(server, disconnect, -2, "Server::Disconnect");
    }

    void server::receive(ipc_msg_ptr &msg) {
        msg = nullptr;

        if (!delivered_msgs.empty()) {
            common::double_linked_queue_element *deliver_first = delivered_msgs.first();
            msg = E_LOFF(deliver_first, ipc_msg, delivered_msg_link);

            deliver_first->deque();
        }
    }

    bool server::ready() {
        return request_status && request_data;
    }

    int server::deliver(ipc_msg_ptr msg) {
        // Is ready
        if (ready()) {
            accept(msg, true);
        } else {
            msg->msg_status = ipc_message_status::delivered;
            delivered_msgs.push(&msg->delivered_msg_link);
        }

        return 0;
    }

    void server::register_ipc_func(uint32_t ordinal, ipc_func func) {
        ipc_funcs.emplace(ordinal, func);
    }

    void server::detach(session *svse) {
        auto ite = std::find(sessions.begin(), sessions.end(), svse);
        if (ite != sessions.end()) {
            sessions.erase(ite);
        }
    }

    int server::destroy() {
        if (owner_thread)
            owner_thread->decrease_access_count();

        for (std::size_t i = 0; i < sessions.size(); i++) {
            sessions[i]->detatch(epoc::error_server_terminated);

            if (!is_hle()) {
                // HLE may still need server pointer for deinit
                sessions[i]->svr = nullptr;
            }
        }

        sessions.clear();
        return 0;
    }

    void server::accept(ipc_msg_ptr msg, const bool notify_owner) {
        msg->msg_status = ipc_message_status::accepted;

        if (name() == "!BackupServer") {
            arm_backup_server_msg_timeout(kern, msg);
        }

        // On some platforms where IPCv1 is still relevant, odd pointer is used to indicates new IPCv2
        if (kern->is_ipc_old() || (kern->is_eka1() && !(request_data.ptr_address() & 1))) {
            message1 *dat_hle = request_data.cast<message1>().get(request_own_thread->owning_process());

            dat_hle->ipc_msg_handle = msg->id;
            dat_hle->function = msg->function;
            dat_hle->session_ptr = msg->session_ptr_lle;
            dat_hle->client_thread_handle = kern->open_handle_with_thread(request_own_thread,
                msg->own_thr, kernel::owner_type::thread);

            std::copy(msg->args.args, msg->args.args + 4, dat_hle->args);
            msg->thread_handle_low = dat_hle->client_thread_handle;
        } else {
            request_data = eka2l1::ptr<message2>(request_data.ptr_address() & ~1);
            message2 *dat_hle = request_data.get(request_own_thread->owning_process());

            dat_hle->ipc_msg_handle = msg->id;
            dat_hle->flags = msg->args.flag;
            dat_hle->function = msg->function;
            dat_hle->session_ptr = msg->session_ptr_lle;

            std::copy(msg->args.args, msg->args.args + 4, dat_hle->args);
            msg->thread_handle_low = 0;
        }

        (request_status.get(request_own_thread->owning_process()))->set(0, kern->is_eka1()); // KErrNone

        if (notify_owner) {
            request_own_thread->signal_request();
        }

        request_own_thread->decrease_access_count();

        request_own_thread = nullptr;
        request_status = 0;
        request_data = 0;
    }

    void server::receive_async_lle(eka2l1::ptr<epoc::request_status> msg_request_status,
        eka2l1::ptr<message2> data) {
        ipc_msg_ptr pending_msg = nullptr;
        receive(pending_msg);

        request_status = msg_request_status;
        request_own_thread = kern->crr_thread();
        request_data = data;

        request_own_thread->increase_access_count();

        if (!pending_msg) {
            return;
        }

        accept(pending_msg, true);
    }

    void server::cancel_async_lle() {
        if (!request_own_thread) {
            return;
        }

        if (!request_status) {
            request_own_thread->signal_request();
            return;
        }

        (request_status.get(request_own_thread->owning_process()))->set(-3, kern->is_eka1()); // KErrCancel
        request_own_thread->signal_request();
        request_own_thread->decrease_access_count();

        request_own_thread = nullptr;
        request_status = 0;
        request_data = 0;
    }

    // IPC process related functions belong in context.cpp
}
