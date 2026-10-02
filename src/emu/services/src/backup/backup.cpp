/*
 * Copyright (c) 2022 EKA2L1 Team
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

#include <services/backup/backup.h>
#include <system/epoc.h>
#include <utils/err.h>

namespace eka2l1 {
    backup_old_server::backup_old_server(eka2l1::system *sys, const char *server_name)
        : service::typical_server(sys, server_name) {
    }

    void backup_old_server::connect(service::ipc_context &context) {
        create_session<backup_old_session>(&context);
        context.complete(epoc::error_none);
    }

    backup_old_session::backup_old_session(service::typical_server *serv, const kernel::uid ss_id,
        epoc::version client_version)
        : service::typical_session(serv, ss_id, client_version) {
    }

    void backup_old_session::fetch(service::ipc_context *ctx) {
        switch (ctx->msg->function) {
        case 0x1F:
            ctx->complete(epoc::error_none);
            break;

        case 0x20:
            // EBakOpCodeBackupOperationEventReady. The real server leaves this pending when an
            // observer is present (and the client always announces one first). EKA2L1 never
            // performs a backup, so it is simply never completed. Completing it with KErrNone
            // instead makes the client re-subscribe forever. The kernel completes it when the
            // session closes.
            if (!backup_observer_present_) {
                ctx->complete(epoc::error_none);
            }
            break;

        case 0x22:
            // EBakOpCodeSetBackupOperationObserverIsPresent (Send, not SendReceive)
            backup_observer_present_ = (ctx->get_argument_value<std::uint32_t>(0).value_or(0) != 0);
            ctx->complete(epoc::error_none);
            break;

        default:
            ctx->complete_unhandled("BackupServer");
            break;
        }
    }
}
