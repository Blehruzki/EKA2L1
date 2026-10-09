#pragma once

#include <mem/ptr.h>
#include <memory>

namespace eka2l1 {
    namespace kernel {
        class thread;
    }

    namespace common {
        class chunkyseri;
    }

    using thread_ptr = kernel::thread *;
}

/* Header only request status */
namespace eka2l1::epoc {
    // EKA2L1_KERNREQ=1: a completion writes the status word only, as EKA2's
    // DThread::RequestComplete does (sizeof(TInt)); the flags word, and its
    // ERequestPending bit, is left for the active scheduler to clear when it
    // dispatches. Off, the emulator clears the bit on every completion, which
    // a device never does -- and code that keeps a raw TRequestStatus in an
    // EKA1 one-word layout then works here and not on a phone.
    bool kernel_completion_keeps_flags();

    // Don't change the structure! Specifically no more fields and no vtable!
    struct request_status {
        static constexpr int pending_status = static_cast<int>(0x80000001);

        int status;
        int flags;

        void do_state(common::chunkyseri &seri);

        enum flag_type {
            active = 1,
            pending = 2
        };

        void set(const int sts, const bool is_eka1) {
            if (!is_eka1) {
                if (sts == pending_status) {
                    flags |= pending;
                } else if (!kernel_completion_keeps_flags()) {
                    flags &= ~pending;
                }
            }

            status = sts;
        }

        request_status(const int sts, const bool is_eka1)
            : flags(0) {
            set(sts, is_eka1);
        }
    };

    struct notify_info {
        eka2l1::ptr<epoc::request_status> sts = 0;
        eka2l1::kernel::thread *requester;
        bool is_eka1;

        explicit notify_info() = default;

        explicit notify_info(eka2l1::ptr<epoc::request_status> &sts, eka2l1::kernel::thread *requester)
            : sts(sts)
            , requester(requester) {
        }

        void complete(int err_code);
        void pending();
        void do_state(common::chunkyseri &seri);

        bool empty() const {
            return !sts;
        }

        const bool operator==(const notify_info &rhs) const {
            return (sts == rhs.sts) && (requester == rhs.requester);
        }
    };
}
