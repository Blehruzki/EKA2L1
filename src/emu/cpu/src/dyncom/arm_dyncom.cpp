/*
 * Copyright (c) 2021 EKA2L1 Team
 * 
 * This file is part of EKA2L1 project
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
#include <cstdio>
#include <cpu/dyncom/arm_dyncom.h>
#include <cpu/dyncom/cpumodel.h>
#include <common/time.h>
#include <cpu/dyncom/arm_dyncom_interpreter.h>
#include <cpu/dyncom/arm_dyncom_trans.h>

#include <common/log.h>

namespace eka2l1::arm {
    dyncom_core::dyncom_core(arm::exclusive_monitor *monitor, const std::size_t page_bits)
        : monitor_(monitor)
        , state_(nullptr)
        , ticks_executed_(0)
        , mem_cache_(page_bits) {
        state_ = std::make_unique<ARMul_State>(this, USER32MODE);
        cpumodel::init_from_env();
        // Cache the data TLB on the state so the inline memory accessors can hit
        // it without needing the full dyncom_core definition in armstate.h.
        state_->mem_cache_ = &mem_cache_;
    }

    dyncom_core::~dyncom_core() {
    }

    void dyncom_core::run(const std::uint32_t instruction_count) {
        ticks_executed_ = 0;
        state_->NumInstrsToExecute = instruction_count;

        const auto start = std::chrono::steady_clock::now();
        const std::uint64_t extra0 = cpumodel::g.extra;
        InterpreterMainLoop(state_.get(), ticks_executed_);
        // EKA2L1_CPUMODEL: the cap and the cycle tick (EKA2L1_CYCLETICK, then cycles a
        // microsecond) count modelled ARM1136 cycles instead of instructions
        const std::uint64_t units = cpumodel::g.on
            ? (static_cast<std::uint64_t>(ticks_executed_) * cpumodel::g.base_cpi_x100) / 100 + (cpumodel::g.extra - extra0)
            : ticks_executed_;
        common::add_cpu_cycles(units);
        common::cpu_cap_throttle(units,
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count());
        if (cpumodel::g.on) {
            // every ~5 s: the model's counters since the last line, for calibrating it
            static std::uint64_t instr = 0, last_instr = 0, last_extra = 0, last_reads = 0, last_rm = 0, last_wm = 0;
            static auto last_t = std::chrono::steady_clock::now();
            instr += ticks_executed_;
            const auto now = std::chrono::steady_clock::now();
            if (now - last_t > std::chrono::seconds(5)) {
                const auto &m = cpumodel::g;
                std::fprintf(stderr, "EKA2L1_CPUMODEL: %llu instr, %llu reads (%llu miss), %llu write misses, %llu extra cycles\n",
                    (unsigned long long)(instr - last_instr), (unsigned long long)(m.reads - last_reads), (unsigned long long)(m.read_misses - last_rm),
                    (unsigned long long)(m.write_misses - last_wm), (unsigned long long)(m.extra - last_extra));
                last_instr = instr; last_extra = m.extra; last_reads = m.reads; last_rm = m.read_misses; last_wm = m.write_misses; last_t = now;
            }
        }
    }

    void dyncom_core::stop() {
        state_->NumInstrsToExecute = 0;
    }

    void dyncom_core::step() {
        ticks_executed_ = 0;
        state_->NumInstrsToExecute = 1;

        InterpreterMainLoop(state_.get(), ticks_executed_);
        common::add_cpu_cycles(ticks_executed_);
    }

    std::uint32_t dyncom_core::get_reg(size_t idx) {
        return state_->Reg[idx];
    }

    std::uint32_t dyncom_core::get_sp() {
        return state_->Reg[13];
    }

    uint32_t dyncom_core::get_pc() {
        return state_->Reg[15];
    }

    uint32_t dyncom_core::get_vfp(size_t idx) {
        return state_->ExtReg[idx];
    }

    void dyncom_core::set_reg(size_t idx, uint32_t val) {
        state_->Reg[idx] = val;
    }

    void dyncom_core::set_pc(uint32_t val) {
        state_->Reg[15] = val;
    }

    void dyncom_core::set_sp(uint32_t val) {
        state_->Reg[13] = val;
    }

    void dyncom_core::set_lr(uint32_t val) {
        state_->Reg[14] = val;
    }

    void dyncom_core::set_vfp(size_t idx, uint32_t val) {
        state_->ExtReg[idx] = val;
    }

    uint32_t dyncom_core::get_cpsr() {
        return state_->Cpsr;
    }

    uint32_t dyncom_core::get_fpscr() {
        return state_->VFP[1];
    }

    uint32_t dyncom_core::get_lr() {
        return state_->Reg[14];
    }

    void dyncom_core::set_cpsr(uint32_t val) {
        state_->Cpsr = val;
    }

    void dyncom_core::set_fpscr(uint32_t val) {
        state_->VFP[1] = val;
    }

    bool dyncom_core::is_thumb_mode() {
        return get_cpsr() & 0x20;
    }

    void dyncom_core::save_context(thread_context &ctx) {
        ctx.cpsr = state_->Cpsr;

        for (uint8_t i = 0; i < 16; i++) {
            ctx.cpu_registers[i] = get_reg(i);
        }

        for (std::uint8_t i = 0; i < 64; i++) {
            ctx.fpu_registers[i] = state_->ExtReg[i];
        }

        ctx.fpscr = state_->VFP[1];
        ctx.uprw = state_->CP15[CP15_THREAD_UPRW];
    }

    void dyncom_core::load_context(const thread_context &ctx) {
        // When the scheduler drives asids (primary core), translated blocks are
        // tagged per address space and stay valid across thread/process switches,
        // so there is no need to wipe the cache here. Only the fallback-embedded
        // interpreter, which never receives an asid, still clears eagerly.
        if (!asid_instruction_cache_) {
            clear_instruction_cache();
        }

        for (uint8_t i = 0; i < 16; i++) {
            state_->Reg[i] = ctx.cpu_registers[i];
        }

        for (std::uint8_t i = 0; i < 64; i++) {
            state_->ExtReg[i] = ctx.fpu_registers[i];
        }

        set_cpsr(ctx.cpsr);
        state_->CP15[CP15_THREAD_UPRW] = ctx.uprw;

        state_->VFP[1] = ctx.fpscr;
    }

    void dyncom_core::set_tlb_page(address vaddr, std::uint8_t *ptr, prot protection) {
        mem_cache_.add(vaddr, ptr, protection);
    }

    void dyncom_core::dirty_tlb_page(address addr) {
        mem_cache_.make_dirty(addr);
    }

    void dyncom_core::flush_tlb() {
        mem_cache_.flush();
    }

    void dyncom_core::clear_instruction_cache() {
        state_->instruction_cache.clear();
        state_->trans_cache_buf_top = 0;
        state_->flush_block_l1_cache();
    }

    void dyncom_core::imb_range(address addr, std::size_t size) {
        clear_instruction_cache();
    }

    void dyncom_core::set_asid(const std::uint32_t asid) {
        state_->instruction_cache_asid = asid;
        asid_instruction_cache_ = true;
    }

    std::uint32_t dyncom_core::get_num_instruction_executed() {
        return ticks_executed_;
    }
}