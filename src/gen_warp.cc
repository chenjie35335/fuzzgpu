#include "config.hh"
#include "exchange_file.hh"
#include "isa/riscv/riscv_instr.hh"
#include "isa/riscv/riscv_meta.hh"
#include "isa/riscv/riscv_vector_meta.hh"
#include "isa/riscv/ventus/csr.hh"
#include "isa/riscv/vortex/csr.hh"

#include "gen_warp.hh"

namespace fuzzgpu {

GenWarp::GenWarp(FuzzerState &fuzzer_state,
                 const std::string &trace_file_prefix, unsigned num_int_regs,
                 unsigned num_fp_regs, unsigned vgpr_usage, unsigned vlmul,
                 unsigned num_active_threads)
  : fuzzer_state_(fuzzer_state)
  , num_active_threads_(num_active_threads)
  , num_int_regs_(num_int_regs)
  , num_fp_regs_(num_fp_regs)
  , num_vec_regs_(vgpr_usage)
  , int_reg_sampler_(RegisterSampler{fuzzer_state_.rng, num_int_regs,
                                     /*null_reg*/ 0})
  , fp_reg_sampler_(RegisterSampler{fuzzer_state_.rng, num_fp_regs,
                                    /*null_reg*/ unsigned(-1)})
  , vec_reg_sampler_(RegisterSampler{fuzzer_state_.rng, vgpr_usage,
                                     /*null_reg*/ unsigned(-1),
                                     riscv_vector_meta::max_nfields})
  , vlmul_(vlmul)
  , trace_file_prefix_(trace_file_prefix)
  , tmask_(Config::instance().arch.nr_threads_per_warp, false)
  , scratchpad(this, fuzzer_state_.isa_emu)
  , emu_state(Config::instance().arch)
  , tmask_one_(Config::instance().arch.nr_threads_per_warp, false)
  , tmask_all_(Config::instance().arch.nr_threads_per_warp, true) {
  tmask_one_.set(0);
  reset();
}

void GenWarp::reset() {
  int_reg_sampler_.reset();
  fp_reg_sampler_.reset();
  vec_reg_sampler_.reset();

  while (!simt_stack.empty()) {
    simt_stack.pop();
  }

  bb_snapshot.reset();
  nr_gen_bb = 0;
  nr_gen_instrs = 0;
  tstart_ = 0;
}

void GenWarp::exclude_int_regs(riscv_reg_t reg) {
  exclude_int_regs_.push_back(reg);
}

bool GenWarp::is_int_reg_excluded(riscv_reg_t reg) const {
  return std::find(exclude_int_regs_.begin(), exclude_int_regs_.end(), reg) !=
         exclude_int_regs_.end();
}

void GenWarp::mask_int_reg(riscv_reg_t reg) {
  spdlog::debug("Masking int reg {}", unsigned(reg));
  int_reg_sampler_.mask_reg(GPREncode(reg), get_tmask_for_scala_reg_());
}

const difftest::ThreadMask &
GenWarp::get_tmask_for_clearing_nodet_(ClearNodetMode mode) const {
  switch (mode) {
  case ClearNodetMode::ForcedFirstThread: {
    return tmask_one_;
  }
  case ClearNodetMode::ForcedAllThread: {
    return tmask_all_;
  }
  case ClearNodetMode::FollowTmask:
    return tmask_;
  default:
    std::abort();
  }
}

const difftest::ThreadMask &GenWarp::get_tmask_for_scala_reg_() const {
#if defined(ARCH_VENTUS)
  return tmask_one_;
#elif defined(ARCH_VORTEX)
  return tmask_;
#endif
}

// for integer registers

riscv_reg_t GenWarp::pick_rs(bool cti, bool force_mutate) {
  return riscv_reg_t(int_reg_sampler_.sample_rs_enc(
      cti, get_tmask_for_scala_reg_(), force_mutate));
}

riscv_reg_t GenWarp::pick_rd(const std::vector<riscv_reg_t> &exclude,
                             bool force_mutate) {
  riscv_reg_t ret;
  do {
    ret = riscv_reg_t(int_reg_sampler_.sample_rd_enc(force_mutate));
  } while (std::find(exclude.begin(), exclude.end(), ret) != exclude.end() ||
           std::find(exclude_int_regs_.begin(), exclude_int_regs_.end(), ret) !=
               exclude_int_regs_.end());
  return ret;
}

void GenWarp::touch_rd(riscv_reg_t reg, ClearNodetMode clear_nodet) {
#if defined(ARCH_VENTUS)
  clear_nodet = ClearNodetMode::ForcedFirstThread; // single-thread only
#endif
  int_reg_sampler_.touch_rd_enc(GPREncode(reg),
                                get_tmask_for_clearing_nodet_(clear_nodet));
}

// for floating-point registers

riscv_fpr_t GenWarp::pick_fs(bool force_mutate) {
  return riscv_fpr_t(fp_reg_sampler_.sample_rs_enc(
      false, get_tmask_for_scala_reg_(), force_mutate));
}

riscv_fpr_t GenWarp::pick_fd(const std::vector<riscv_fpr_t> &exclude,
                             bool force_mutate) {
  riscv_fpr_t ret;
  do {
    ret = riscv_fpr_t(fp_reg_sampler_.sample_rd_enc(force_mutate));
  } while (std::find(exclude.begin(), exclude.end(), ret) != exclude.end());
  return ret;
}

void GenWarp::touch_fd(riscv_fpr_t fpr, ClearNodetMode clear_nodet) {
#if defined(ARCH_VENTUS)
  clear_nodet = ClearNodetMode::ForcedFirstThread; // single-thread only
#endif
  fp_reg_sampler_.touch_rd_enc(GPREncode(fpr),
                               get_tmask_for_clearing_nodet_(clear_nodet));
}

// for vector registers

riscv_vreg_t GenWarp::pick_vs(bool cti, bool force_mutate, bool ret_null) {
  const auto ret = vec_reg_sampler_.sample_rs_enc(cti, tmask_, force_mutate);
  if (ret == unsigned(-1)) {
    if (ret_null)
      return riscv_vreg_t(unsigned(-1));
    spdlog::error("No available vector register to pick");
    std::terminate();
  }
  return riscv_vreg_t(ret * vlmul_);
}

riscv_vreg_t GenWarp::pick_vd(const std::vector<riscv_vreg_t> &exclude,
                              bool force_mutate) {
  riscv_vreg_t ret;
  do {
    ret = riscv_vreg_t(vec_reg_sampler_.sample_rd_enc(force_mutate) * vlmul_);
  } while (std::find(exclude.begin(), exclude.end(), ret) != exclude.end());
  return ret;
}

void GenWarp::touch_vd(riscv_vreg_t vreg, ClearNodetMode clear_nodet) {
  vec_reg_sampler_.touch_rd_enc(GPREncode(vreg) / vlmul_,
                                get_tmask_for_clearing_nodet_(clear_nodet));
}

void GenWarp::switch_basic_block(
    const MemoryBlock &pc_mem_blk,
    const GenWarp::BasicBlockSnapshot &bb_snapshot_,
    const difftest::ThreadMask &utmask_) {
  assert(pc_mem_blk.end == bb_snapshot_.mem_blk.end &&
         pc_mem_blk.begin >= bb_snapshot_.mem_blk.begin &&
         "pc not in the memory block");
  scratchpad.seek(pc_mem_blk);
  bb_snapshot = bb_snapshot_;
  *tmask_proxy() = utmask_;

  spdlog::debug("*****************************");
  spdlog::debug(
      "*** Switched to new basic block at {:#x}, size {}, num_target_instrs "
      "{} ****",
      pc_mem_blk.begin, pc_mem_blk.size(), bb_snapshot.num_target_instrs);
  spdlog::debug("*****************************");
}

#ifndef DISABLE_CHANGE_DETECTION
void GenWarp::on_property_changed_(const difftest::ThreadMask &tmask,
                                   difftest::ThreadMask &old_tmask) {
  if (spdlog::get_level() <= spdlog::level::trace) {
    if (old_tmask == tmask)
      return;

    spdlog::debug("tmask changed: old={} new={}", old_tmask, tmask);

    old_tmask = tmask;
  }
}
#endif

GenWarp::PropertyProxy<difftest::ThreadMask> GenWarp::tmask_proxy() {
  return PropertyProxy(tmask_);
}

void GenWarp::SIMIStack::Stat::dump(unsigned ident) {
  std::string ident_ch;
  for (unsigned i = 0; i < ident; ++i)
    ident_ch.push_back(' ');
  spdlog::info("{}Max SIMT stack depth: {}", ident_ch, max_depth);
}

void GenWarp::flush_trace() {
  if (trace_stream_) {
    trace_stream_.flush();
  }
}

void GenWarp::set_warp_gid(difftest::WarpGID rt_warp_gid) {
  rt_warp_gid_ = rt_warp_gid;

  // open trace file to write
  if (!trace_file_prefix_.empty()) {
    if (trace_stream_) {
      trace_stream_.close();
    }
    const std::string trace_file =
        fmt::format("{}_warp_gid_{}.trace", trace_file_prefix_, rt_warp_gid_);
    trace_stream_.open(trace_file, std::ios::binary | std::ios::app);
  }
}

/*** @brief Emulate the warps until they reach the given PC breakpoint
 * @clobbers trace_buf_
 * @param pc The PC to emulate to
 * @param rt_warp_gids The RT warp GIDs to emulate
 */
void GenWarp::emulate_until(uint64_t pc, bool is_next_pc) {
  if (!Config::instance().emu_guide) {
    return;
  }
  spdlog::debug("Emulating to PC {} {:#x}", is_next_pc ? "next" : "", pc);

  if ((!is_next_pc || emu_state.next_pc != pc) &&
      (is_next_pc || emu_state.pc != pc)) {
    do {
      [[maybe_unused]] const auto scheduable =
          fuzzer_state_.isa_emu.schedulable(rt_warp_gid_);
      assert(scheduable);
      fuzzer_state_.isa_emu.execute_warp(rt_warp_gid_, &emu_state);

      if (emu_state.trap_taken) {
        spdlog::error(
            "Warp GID {} trapped at PC {:#x} during ISA emulation, aborting "
            "fuzzing.",
            rt_warp_gid_, emu_state.pc);
        std::abort();
      }
      
      if (trace_stream_) {
        TraceFragmentHeader trace;
        trace.magic = TraceFragmentHeader::MAGIC_TRUTH;
        trace.pc = this->emu_state.pc;
        trace.tmask = this->emu_state.tmask.to_ulong();
        trace.applied_tmask = this->emu_state.applied_tmask;
        trace.nr_rd = emu_state.regs.size();
        trace.opcode = this->emu_state.opcode;
        trace.csr_addr = this->emu_state.csr_addr;
        if (emu_state.regs.empty()) {
          trace.nr_rd_data = 0;
        } else {
          trace.nr_rd_data =
              emu_state.regs[0].rd_data.size() * emu_state.regs.size();
        }
        trace_stream_.write(reinterpret_cast<const char *>(&trace),
                            sizeof(trace));

        for (const auto &r : this->emu_state.regs) {
          trace_stream_.write(reinterpret_cast<const char *>(&r.rd),
                              sizeof(r.rd));
        }
        for (size_t irg = 0; irg < emu_state.regs.size(); ++irg) {
          foreach_thread_lid(this, tlid) {
            trace_stream_.write(reinterpret_cast<const char *>(
                                    &this->emu_state.regs[irg].rd_data[tlid]),
                                sizeof(uint64_t));
          }
        }
      }

      spdlog::trace("Emulated warp GID {} PC {:#x} Op {} next PC {:#x}",
                    rt_warp_gid_, emu_state.pc,
                    magic_enum::enum_name(emu_state.opcode), emu_state.next_pc);

    } while ((!is_next_pc || emu_state.next_pc != pc) &&
             (is_next_pc || emu_state.pc != pc));

    foreach_thread_lid(this, tlid) {
      assert(tmask_.test(tlid) == emu_state.tmask.test(tlid) &&
             "isa emulator doesn't match the generated thread mask."
             "maybe due to tmask write before reaching the target PC!");
    }
    // sync the gen warp's thread mask
    *tmask_proxy() = emu_state.next_tmask;
  }
}

void GenWarp::gen_init_threads() {
#if defined(ARCH_VORTEX)
  const auto &k_shuffled =
      fuzzer_state_.shuffle_thread_lid(num_active_threads_);
  tmask_proxy()->reset();
  for (size_t iks = 0; iks < num_active_threads_; ++iks) {
    tmask_proxy()->set(k_shuffled[iks]);
  }

  difftest::ThreadMask thread_mask(Config::instance().arch.nr_threads_per_warp,
                                   false);
  for (size_t iks = 0; iks < num_active_threads_; ++iks) {
    thread_mask.set(k_shuffled[iks]);
  }
  auto t0 = pick_rd({riscv_reg_t::ZERO}, false);
  riscv_meta::emit_imm_setter(
      t0, thread_mask.to_ulong(), *this,
      [&](riscv_instr *instr) { scratchpad.emit(instr); }, {});
  riscv_instr tmc_instr(
      riscv_meta_list::instance().instr(riscv_instr_name_t::VX_TMC));
  tmc_instr.set_rs1(t0);
  scratchpad.emit(&tmc_instr);

#elif defined(ARCH_VENTUS)
  // ventus's tmask is left-aligned & contiguous
  tmask_proxy()->reset();
  for (unsigned i = 0; i < num_active_threads_; ++i) {
    tmask_proxy()->set(i);
  }

  const auto t0 = pick_rd({riscv_reg_t::ZERO});
  riscv_instr addi_t0(
      riscv_meta_list::instance().instr(riscv_instr_name_t::ADDI));
  addi_t0.set_rd(t0);
  addi_t0.set_rs1(riscv_reg_t::ZERO);
  addi_t0.set_imm(num_active_threads_);
  scratchpad.emit(&addi_t0);

  riscv_instr setvli_zero_t0(
      riscv_meta_list::instance().instr(riscv_instr_name_t::VSETVLI));
  setvli_zero_t0.set_rd(riscv_reg_t::ZERO);
  setvli_zero_t0.set_rs1(t0);
  setvli_zero_t0.set_imm(riscv_meta::encode_vtype(
      /*vsew=*/Config::instance().arch.word_bits, /*vlmul=*/1,
      /*ta=*/true, /*ma=*/true));
  scratchpad.emit(&setvli_zero_t0);
#endif

#if defined(ARCH_VENTUS)
  // enable SSTATUS_FS
  const auto t1 = pick_rd({riscv_reg_t::ZERO});
  riscv_meta::emit_imm_setter(
      t1, SSTATUS_FS, *this,
      [&](riscv_instr *instr) { scratchpad.emit(instr); }, {});
  riscv_instr csrrs_sstatus(
      riscv_meta_list::instance().instr(riscv_instr_name_t::CSRRS));
  csrrs_sstatus.set_rd(riscv_reg_t::ZERO);
  csrrs_sstatus.set_csr(CSR_SSTATUS);
  csrrs_sstatus.set_rs1(t1);
  scratchpad.emit(&csrrs_sstatus);
#endif
}

bool GenWarp::has_unsolved_barrier() const {
  if (!Config::instance().barrier_plan) {
    return false;
  }
  return barrier_instr_idx < nr_pending_barriers;
}

} // namespace fuzzgpu