/*
 * Copyright (c) 2021 EKA2L1 Team
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

#include <common/log.h>

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <services/bluetooth/btmidman.h>
#include <services/bluetooth/protocols/base_inet.h>
#include <services/bluetooth/protocols/btlink/btlink_inet.h>
#include <services/bluetooth/protocols/btmidman_inet.h>
#include <services/bluetooth/protocols/common.h>
#include <services/internet/protocols/common.h>

#include <utils/err.h>

#include <kernel/thread.h>
#include <kernel/kernel.h>
#include <utils/err.h>

namespace eka2l1::epoc::bt {
    namespace {
        constexpr std::uint32_t K_UNDEFINED_PROTOCOL_OPTION_LEVEL = 0xFFFFFFFE;

        class btlink_control_inet_socket final : public btinet_socket {
        public:
            btlink_control_inet_socket(btlink_inet_protocol *protocol,
                std::unique_ptr<epoc::socket::socket> &inet_socket)
                : btinet_socket(protocol, inet_socket) {
            }

            bool set_option(const std::uint32_t option_id, const std::uint32_t option_family,
                std::uint8_t *buffer, const std::size_t avail_size) override {
                // Symbian 8.1 Bluetooth opens the raw link-manager control
                // socket with an undefined protocol level, then enables the
                // provider with option 1. There is no host-side data path to
                // configure; accepting it makes the existing HCI/LM controls
                // available to the client.
                if ((option_family == K_UNDEFINED_PROTOCOL_OPTION_LEVEL) && (option_id == 1)) {
                    return true;
                }

                return btinet_socket::set_option(option_id, option_family, buffer, avail_size);
            }
        };
    }

    std::unique_ptr<epoc::socket::socket> btlink_inet_protocol::make_socket(const std::uint32_t family_id,
        const std::uint32_t protocol_id, const socket::socket_type sock_type) {
        // Link-manager sockets do not carry user data. They are nevertheless
        // opened as raw sockets by Bluetooth clients to issue HCI/link-manager
        // ioctls (for example, to change the scan mode). Reuse the common
        // Bluetooth socket implementation without a backing TCP socket so
        // those control operations remain available.
        std::unique_ptr<epoc::socket::socket> empty_socket;
        return std::make_unique<btlink_control_inet_socket>(this, empty_socket);
    }

    btlink_inet_host_resolver::btlink_inet_host_resolver(btlink_inet_protocol *papa)
        : papa_(papa)
        , friend_name_entry_(nullptr)
        , current_friend_(0xFFFFFFFF)
        , need_name_(false)
        , in_completion_(false)
        , no_more_friends_(false)
        , in_async_searching_(false)
        , friend_querier_(reinterpret_cast<midman_inet*>(papa->get_midman()))
        , delay_emu_evt_(-1) {
    }

    btlink_inet_host_resolver::~btlink_inet_host_resolver() {
        if (!friend_retrieve_info_.empty()) {
            if (in_completion_) {
                ntimer *timing = friend_retrieve_info_.requester->get_kernel_object_owner()->get_ntimer();
                timing->unschedule_event(delay_emu_evt_, reinterpret_cast<std::uint64_t>(this));
            }

            friend_retrieve_info_.complete(epoc::error_cancel);
        }

        midman_inet *midman = reinterpret_cast<midman_inet*>(papa_->get_midman());

        if (midman->get_discovery_mode() > DISCOVERY_MODE_DIRECT_IP) {
            midman->unregister_stranger_call_observer(static_cast<inet_stranger_call_observer*>(this));
        }
    }

    std::u16string btlink_inet_host_resolver::host_name() const {
        return papa_->get_midman()->device_name();
    }

    bool btlink_inet_host_resolver::host_name(const std::u16string &name) {
        papa_->get_midman()->device_name(name);
        return true;
    }

    void btlink_inet_host_resolver::get_by_address(epoc::socket::saddress &addr, epoc::socket::name_entry *result, epoc::notify_info &info) {
        inquiry_socket_address &cast_addr = static_cast<inquiry_socket_address&>(addr);
        inquiry_info *info_addr_in = cast_addr.get_inquiry_info();

        if (bt_radio_off()) {
            LOG_WARN(SERVICE_BLUETOOTH, "Inquiry with the radio off (EKA2L1_BTOFF): KErrHardwareNotAvailable");
            info.complete(epoc::error_hardware_not_avail);
            return;
        }

        // EKA2L1_BTSTRICT=1: refuse an inquiry the way os/bt's
        // CBTHostResolver::GetByAddress does (btstack/linkmgr/hostresolver.cpp):
        // a cache-only request (KHostResCache, 0x20) for an all-zero address
        // completes KErrNotFound; one asking neither an inquiry nor a name,
        // KErrArgument; an inquiry on an IAC other than KGIAC or KLIAC,
        // KErrNotSupported. That source is later than any S60 3.x phone (its
        // struct layout is not theirs, see inquiry_info), so these checks are
        // its rules, not a phone's measured ones. Off, the action flags' name
        // bit is all that is read. Each request's raw address is logged.
        static const bool strict = [] {
            const char *v = std::getenv("EKA2L1_BTSTRICT");
            return v && v[0] == '1';
        }();
        if (strict) {
            const std::uint8_t act = info_addr_in->action_flags_;
            const std::uint32_t iac = info_addr_in->iac_;
            const std::uint8_t *raw = reinterpret_cast<const std::uint8_t *>(info_addr_in);
            LOG_INFO(SERVICE_BLUETOOTH, "Inquiry asked: family 0x{:X}, user bytes {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} | {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} | {:02X} {:02X} {:02X} {:02X} | {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X}",
                addr.family_, raw[0], raw[1], raw[2], raw[3], raw[4], raw[5], raw[6], raw[7], raw[8], raw[9], raw[10], raw[11],
                raw[12], raw[13], raw[14], raw[15], raw[16], raw[17], raw[18], raw[19], raw[20], raw[21], raw[22], raw[23]);
            bool zero_addr = true;
            for (std::size_t i = 0; i < sizeof(info_addr_in->addr_.addr_); i++) {
                if (info_addr_in->addr_.addr_[i]) {
                    zero_addr = false;
                }
            }
            int refuse = 0;
            if ((act & INQUIRY_ACTION_CACHE_ONLY) && zero_addr) {
                refuse = epoc::error_not_found;
            } else if (!(act & (INQUIRY_ACTION_INCLUDE_ADDR | INQUIRY_ACTION_INCLUDE_NAME))) {
                refuse = epoc::error_argument;
            } else if ((act & INQUIRY_ACTION_INCLUDE_ADDR) && (iac != INQUIRY_IAC_GENERAL) && (iac != INQUIRY_IAC_LIMITED)) {
                refuse = epoc::error_not_supported;
            }
            if (refuse) {
                LOG_WARN(SERVICE_BLUETOOTH, "Inquiry refused as a device would (action 0x{:X}, IAC 0x{:X}): {}", act, iac, refuse);
                info.complete(refuse);
                return;
            }
        }

        if (info_addr_in->action_flags_ & INQUIRY_ACTION_INCLUDE_NAME) {
            need_name_ = true;
        }

        if (in_completion_) {
            return;
        }

        midman_inet *midman = reinterpret_cast<midman_inet*>(papa_->get_midman());
        current_friend_ = 0;

        if (midman->get_discovery_mode() > DISCOVERY_MODE_DIRECT_IP) {
            searched_list_async_.clear();
            in_async_searching_ = false;
            in_completion_ = false;
            no_more_friends_ = false;

            midman->unregister_stranger_call_observer(static_cast<inet_stranger_call_observer*>(this));
            midman->begin_hearing_stranger_call(static_cast<inet_stranger_call_observer*>(this));
        }

        next(result, info);
    }

    void btlink_inet_host_resolver::get_by_name(epoc::socket::name_entry *supply_and_result, epoc::notify_info &info) {
        LOG_WARN(SERVICE_BLUETOOTH, "Get host by name stubbed to not found");
        info.complete(epoc::error_not_supported);
    }

    void btlink_inet_host_resolver::complete_background_find_device_delay_emulation() {
        if (friend_retrieve_info_.empty()) {
            return;
        }

        kernel_system *kern = friend_retrieve_info_.requester->get_kernel_object_owner();
        kern->lock();

        current_friend_ = 0xFFFFFFFF;

        friend_retrieve_info_.complete(epoc::error_eof);
        in_completion_ = false;

        kern->unlock();
    }
    
    void btlink_inet_host_resolver::on_stranger_call(epoc::socket::saddress &addr, std::uint32_t index_in_list) {        
        const std::lock_guard<std::mutex> guard(lock_);

        if (searched_list_async_.find(index_in_list) == searched_list_async_.end()) {
            searched_list_async_.emplace(index_in_list);
        } else {
            return;
        }

        if (no_more_friends_) {
            return;
        }

        if (!new_friends_wait_.empty() || friend_retrieve_info_.empty() || in_async_searching_) {
            new_friends_wait_.push(std::make_pair(addr, index_in_list));
            return;
        }

        in_async_searching_ = true;
        search_next_friend_and_report_to_client(addr, index_in_list);
    }

    void btlink_inet_host_resolver::report_search_end_to_client() {
        midman_inet *midman = reinterpret_cast<midman_inet*>(papa_->get_midman());
        midman->set_friend_info_cached();

        static const char *FIND_DEVICE_DELAY_EMU_NAME = "FindNextDeviceCompleteDelayEmulation";
        static const std::int64_t FIND_DEVICE_DELAY_EMU_DURATION = 2000000;

        ntimer *timing = friend_retrieve_info_.requester->get_kernel_object_owner()->get_ntimer();

        if (delay_emu_evt_ < 0) {
            delay_emu_evt_ = timing->get_register_event(FIND_DEVICE_DELAY_EMU_NAME);
        }
        
        if (delay_emu_evt_ < 0) {
            delay_emu_evt_ = timing->register_event(FIND_DEVICE_DELAY_EMU_NAME, [](std::uint64_t userdata, std::int64_t late) {
                reinterpret_cast<btlink_inet_host_resolver*>(userdata)->complete_background_find_device_delay_emulation();
            });
        }

        timing->schedule_event(FIND_DEVICE_DELAY_EMU_DURATION, delay_emu_evt_, reinterpret_cast<std::uint64_t>(this));            
        in_completion_ = true;
    }

    void btlink_inet_host_resolver::on_no_more_strangers() {
        no_more_friends_ = true;

        if (friend_retrieve_info_.empty()) {
            return;
        }

        kernel_system *kern = friend_retrieve_info_.requester->get_kernel_object_owner();
        kern->lock();

        report_search_end_to_client();

        kern->unlock();
    }

    void btlink_inet_host_resolver::search_next_friend_and_report_to_client(epoc::socket::saddress &addr, std::uint32_t index) {        
        midman_inet *midman = reinterpret_cast<midman_inet*>(papa_->get_midman());

        inquiry_socket_address &cast_addr = static_cast<inquiry_socket_address&>(friend_name_entry_->addr_);
        inquiry_info *info_addr_in = cast_addr.get_inquiry_info();

        // EKA2L1_BTSERVICE=<hex>: the major service classes a found device
        // advertises (CoD bits 13-23 as Symbian keeps them, 11 bits). Default
        // telephony alone. An S60 3.x camera phone with dial-up networking also
        // sets networking, capturing and object transfer (0x2D0), and an N-Gage
        // title that reads the low byte as a filter rejects it (round 173).
        static const std::uint16_t service = [] {
            const char *v = std::getenv("EKA2L1_BTSERVICE");
            return static_cast<std::uint16_t>(v ? std::strtoul(v, nullptr, 0) : static_cast<unsigned long>(MAJOR_SERVICE_TELEPHONY));
        }();
        info_addr_in->major_service_class_ = service;
        info_addr_in->major_device_class_ = MAJOR_DEVICE_PHONE;
        info_addr_in->minor_device_class_ = MINOR_DEVICE_PHONE_CECULLAR;
        cast_addr.family_ = BTADDR_PROTOCOL_FAMILY_ID;

        friend_querier_.get_device_address_async(addr, [this, addr, midman, index](device_address *dvc_addr_res) {
            const bool manual_direct_search = (midman->get_discovery_mode() == DISCOVERY_MODE_DIRECT_IP);

            if (dvc_addr_res == nullptr) {
                manual_direct_search ? current_friend_++ : 0;
                next_impl();
            } else {
                inquiry_socket_address &cast_addr = static_cast<inquiry_socket_address&>(friend_name_entry_->addr_);
                inquiry_info *info_addr_in = cast_addr.get_inquiry_info();
                info_addr_in->addr_ = *dvc_addr_res;

                midman->add_device_address_mapping(index, info_addr_in->addr_);

                friend_querier_.get_device_name_async(addr, [this, manual_direct_search](const char *result, const char result_length) {
                    kernel_system *kern = friend_retrieve_info_.requester->get_kernel_object_owner();
                    kern->lock();

                    if (result == nullptr) {
                        friend_name_entry_->name_.assign(nullptr, u"Unknown o_o");
                    } else {
                        std::u16string result_u16 = common::utf8_to_ucs2(std::string(result, result_length));
                        friend_name_entry_->name_.assign(nullptr, result_u16);
                    }
                    
                    manual_direct_search ? current_friend_++ : 0;

                    const std::lock_guard<std::mutex> guard(lock_);
                    in_async_searching_ = false;

                    friend_retrieve_info_.complete(epoc::error_none);

                    kern->unlock();
                });
            }
        });
    }

    void btlink_inet_host_resolver::next_impl() {
        if (in_completion_) {
            return;
        }

        midman_inet *midman = reinterpret_cast<midman_inet*>(papa_->get_midman());
        internet::sinet6_address addr;

        if (midman->get_discovery_mode() <= DISCOVERY_MODE_DIRECT_IP) {
            if (!midman->get_friend_address(current_friend_, addr)) {
                report_search_end_to_client();
                return;
            }

            search_next_friend_and_report_to_client(addr, current_friend_);
        } else {
            if (!new_friends_wait_.empty()) {
                std::pair<epoc::socket::saddress, std::uint32_t> search_friend_req = new_friends_wait_.front();
                new_friends_wait_.pop();

                search_next_friend_and_report_to_client(search_friend_req.first, search_friend_req.second);
                return;
            }

            if (no_more_friends_) {
                report_search_end_to_client();
                return;
            }
        }
    }

    static std::atomic<int> radio_off_state{ -1 };     // -1: not read from EKA2L1_BTOFF yet

    bool bt_radio_off() {
        int v = radio_off_state.load();
        if (v < 0) {
            const char *e = std::getenv("EKA2L1_BTOFF");
            int expected = -1;
            radio_off_state.compare_exchange_strong(expected, (e && e[0] == '1') ? 1 : 0);
            v = radio_off_state.load();
        }
        return v == 1;
    }

    void bt_set_radio_off(const bool off) {
        radio_off_state.store(off ? 1 : 0);
    }

    void btlink_inet_host_resolver::next(epoc::socket::name_entry *result, epoc::notify_info &info) {        
        if (bt_radio_off()) {
            info.complete(epoc::error_hardware_not_avail);
            return;
        }
        const std::unique_lock<std::mutex> guard(lock_);

        if (!friend_retrieve_info_.empty()) {
            info.complete(epoc::error_server_busy);
            return;
        }

        if (current_friend_ == 0xFFFFFFFF) {
            info.complete(epoc::error_not_ready);
            return;
        }

        friend_retrieve_info_ = info;
        friend_name_entry_ = result;

        next_impl();
    }
}
