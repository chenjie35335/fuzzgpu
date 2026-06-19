#include "spdlog/spdlog.h"
#include <chrono>
#include <ctime>
#include <unordered_set>

#include "api_internal.hh"
#include "assert.hh"
#include "config.hh"
#include "debugger.hh"
#include "fp_check.hh"
#include "mem_axioms_checker.hh"
#include "term.hh"
#include "util.hh"

#include <difftest_gpu/util/parse_ventus_opcode.hpp>

#include "difftest.hh"

namespace difftest {

Difftest::Difftest(const std::shared_ptr<ISAEmulator> &isa_emu)
  : isa_emu_(isa_emu)
  , debugger(std::make_unique<Debugger>(dut_per_warp_))
  , mem_axioms_checker_(std::make_unique<MemAxiomsChecker>(*this)) {}

void Difftest::reset_event() {
  isa_emu_->reset();
  dut_per_warp_.clear();
  ref_arch_state_.clear();
  trace_buf_.clear();
  suspicious.clear();
  total_warps_ = 0;
}

void Difftest::new_warps_event(unsigned num_warps_per_block,
                               unsigned num_blocks) {
  const auto num_warps = num_warps_per_block * num_blocks;
#if defined(ARCH_VENTUS)
  const auto max_wgid = total_warps_ + num_warps;
#elif defined(ARCH_VORTEX)
  const auto max_wgid = Config::instance().arch.vortex_nr_total_warps;
  if (total_warps_)
    std::abort();
#endif
  for (unsigned wgid = total_warps_; wgid < max_wgid; ++wgid) {
    dut_per_warp_.emplace_back(*this, wgid);
    dut_per_warp_.back().reset();
    ref_arch_state_.emplace_back();
    ref_arch_state_.back().reset();
    trace_buf_.emplace_back(Config::instance().arch);
    suspicious.push_back(false);
  }
  total_warps_ = max_wgid;
  num_warps_per_block_ = num_warps_per_block;
  num_blocks_ = num_blocks;
}

/**
 * @brief called on a decode event
 * @param regs The rd_data fileds are don't-cares
 */
void Difftest::decode_event(
    InstId iid, Word pc, WarpGID warp_gid, const ThreadMask &tmask,
    const std::optional<std::vector<Trace::RegWriteback>> &regs, Opcode opcode,
    const std::optional<uint32_t> &insn_bits, bool dummy_wb) {
  if (stopped)
    return;
  assert_log(warp_gid < dut_per_warp_.size(),
             "Decode Event: Warp GID {} > the number of warps {}", warp_gid,
             dut_per_warp_.size());

  spdlog::trace("DECODE w{}: #{} pc: {:#x} tmask: {}", warp_gid, iid, pc,
                tmask);

  dut_per_warp_[warp_gid].issue(iid, pc, tmask, regs, opcode, insn_bits,
                                dummy_wb);
  dut_per_warp_[warp_gid].on_arch_event(rtl_clk_cycles);
}

static bool reg_compatible(const RegId &a, const RegId &b) {
#if defined(ARCH_VENTUS)
  if (a.idx != b.idx || a.sew != b.sew)
    return false;
  // shared int & fp reg, with regext
  switch (a.type) {
  case RegType::Float:
  case RegType::Int:
    return (b.type == RegType::Float || b.type == RegType::Int);
  case RegType::Vec:
    return a.type == b.type;
  default:
    std::abort();
  }
#else
  return a == b;
#endif
}
struct RegIdCompatible {
  bool operator()(const RegId &a, const RegId &b) const {
    return reg_compatible(a, b);
  }
};

Difftest::Status Difftest::compare_regs_trace_(
    const std::vector<Trace::RegWriteback> &a, const std::string &a_name,
    const std::vector<Trace::RegWriteback> &b, const std::string &b_name,
    const std::function<Status(const Trace::RegWriteback &,
                               const Trace::RegWriteback &)> &cmp) {
  Difftest::Status ret;
  // build the index
  std::unordered_map<RegId, const Trace::RegWriteback *, RegIdHash,
                     RegIdCompatible>
      a_rd_map, b_rd_map;
  for (const auto &regwb : a) {
    if (is_null_reg(regwb.rd))
      continue;
    a_rd_map.insert({regwb.rd, &regwb});
  }
  for (const auto &regwb : b) {
    if (is_null_reg(regwb.rd))
      continue;
    b_rd_map.insert({regwb.rd, &regwb});
  }

  // a queries b
  for (const auto &[rd, regwb_a] : a_rd_map) {
    auto it = b_rd_map.find(rd);
    if (it == b_rd_map.end()) {
      fmt::memory_buffer b_lst;
      for (const auto &[rd, _] : b_rd_map) {
        fmt::format_to(std::back_insert_iterator(b_lst), "{} ", rd);
      }
      return check(false, "{} rd {} not in {} [{}]", a_name, rd, b_name,
                   fmt::to_string(b_lst));
    }
    const auto &regwb_b = it->second;
    ret &= cmp(*regwb_a, *regwb_b);
  }

  // b queries a
  for (const auto &[rd, regwb_b] : b_rd_map) {
    auto it = a_rd_map.find(rd);
    if (it == a_rd_map.end()) {
      fmt::memory_buffer a_lst;
      for (const auto &[rd, _] : a_rd_map) {
        fmt::format_to(std::back_insert_iterator(a_lst), "{} ", rd);
      }
      return check(false, "{} rd {} not in {} [{}]", b_name, rd, a_name,
                   fmt::to_string(a_lst));
    }
  }
  return ret;
}

void Difftest::writeback_reg_event(InstId iid, WarpGID warp_gid,
                                   const ThreadMask &wb_tmask, Word pc,
                                   const std::vector<Trace::RegWriteback> &regs,
                                   const std::optional<uint32_t> &insn_bits) {
  if (stopped)
    return;

  assert_log(warp_gid < dut_per_warp_.size(),
             "Writeback Reg Event: Warp ID {} > the number of warps {}",
             warp_gid, dut_per_warp_.size());

  spdlog::trace("WRITEBACK REG w{}: #{} pc: {:#x} wb_tmask: {} "
                "num rd: {} ",
                warp_gid, iid, pc, wb_tmask, regs.size());
  if (spdlog::should_log(spdlog::level::trace)) {
    for (const auto &regwb : regs) {
      for (unsigned t = 0; t < wb_tmask.size() && t < regwb.rd_data.size();
           ++t) {
        if (wb_tmask.test(t)) {
          spdlog::trace("  {} <= {:#x}", regwb.rd, regwb.rd_data[t]);
        }
      }
    }
    if (regs.empty()) {
      spdlog::trace("  <none>");
    }
  }

  // **** Writeback stage ****

  auto &dut_warp = dut_per_warp_[warp_gid];
  auto &ref_state = ref_arch_state_[warp_gid];

  dut_warp.on_arch_event(rtl_clk_cycles);

  // Checking basic info with the DUT ROB entry
  // Compare the pc
  Difftest::Status ret;
  ret &= check(dut_warp.rob.iid_valid(iid),
               "DUT ROB does not have iid #{} in warp {}", iid, warp_gid);
  if (!ret) {
    report(ret, "WriteBack", warp_gid, pc, std::nullopt, false, std::nullopt,
           insn_bits);
    stop_emulator_thread();
    return;
  }
  auto &rob_entry = dut_warp.rob.get(iid);
  ret &=
      check(rob_entry.pc == pc, "PC {:#x} != decoded {:#x}", pc, rob_entry.pc);

  // Compare the rd
  if (rob_entry.regs.has_value()) {
    ret &= compare_regs_trace_(
        *rob_entry.regs, "DUT", regs, "REF",
        [&](const Trace::RegWriteback &a, const Trace::RegWriteback &b) {
          return Difftest::Status();
        });
  }

  // Compare the insn bits
  if (rob_entry.insn_bits.has_value()) {
    assert(insn_bits.has_value());
    ret &= check(insn_bits.value() == rob_entry.insn_bits.value(),
                 "Insns bits {:#x} != decoded {:#x}", insn_bits.value(),
                 rob_entry.insn_bits.value());
  }

  ret &= check((rob_entry.tmask & wb_tmask) == wb_tmask,
               "Writeback tmask {} not subset of Decode tmask {}", wb_tmask,
               rob_entry.tmask);

  if (!ret) {
    report(ret, "WriteBack", warp_gid, pc, std::nullopt, false, std::nullopt,
           insn_bits);
    stop_emulator_thread();
    return;
  }

  // store results to the DUT ROB
  dut_warp.rob.writeback_reg(iid, wb_tmask, regs);

  // **** Commit stage ****

  // Prioritize commitiing a warp that has pending TMC, since WSPAWN depends on
  // TMC
  // Note: no dummy_wb is allowed
  bool solved_dep = true;
  for (WarpGID warp_gid = 0; warp_gid < total_warps_; ++warp_gid) {
    if (dut_per_warp_[warp_gid].tmask_write_pending) {
      if (!commit_(warp_gid)) {
        stopped = true;
        return;
      }
      solved_dep &= !dut_per_warp_[warp_gid].tmask_write_pending;
    }
  }

  // wait until all the pending tmask write has been committed
  if (solved_dep) {
    for (WarpGID warp_gid = 0; warp_gid < total_warps_; ++warp_gid) {
      if (!dut_per_warp_[warp_gid].tmask_write_pending)
        if (!commit_(warp_gid)) {
          stopped = true;
          return;
        }
    }
  }
}

void Difftest::sync_from_dut_(WarpGID warp_gid,
                              const ROB::Entry *dut_cmt_entry) {
  assert_log(warp_gid < trace_buf_.size(),
             "sync_from_dut_: Warp ID {} >= trace_buf_ size {}", warp_gid,
             trace_buf_.size());

  trace_buf_[warp_gid].regs.resize(
      dut_cmt_entry->regs->size(),
      Trace::RegWriteback(dut_cmt_entry->wb_tmask.size()));
  for (size_t irg = 0; irg < dut_cmt_entry->regs->size(); ++irg) {
    auto &regs = trace_buf_[warp_gid].regs[irg];
    regs.rd = dut_cmt_entry->regs->at(irg).rd;

    const auto &dut_rd_data = dut_cmt_entry->regs->at(irg).rd_data;
    bool single_thread = false;
    for (unsigned tid = 0;
         !single_thread && tid < dut_cmt_entry->wb_tmask.size(); ++tid) {
      if (trace_buf_[warp_gid].applied_tmask) {
        if (!dut_cmt_entry->wb_tmask.test(tid))
          continue;
      } else {
        single_thread = true;
      }

      // Bounds check before accessing rd_data
      if (tid >= regs.rd_data.size() || tid >= dut_rd_data.size())
        continue;

      // copy from the DUT
      spdlog::trace(
          "SYNC GPR: PC = {:#x}, {}: {:#x} <- from DUT {}: {:#x} PC = {:#x}",
          trace_buf_[warp_gid].pc, regs.rd, regs.rd_data[tid],
          dut_cmt_entry->regs->at(irg).rd, dut_rd_data[tid], dut_cmt_entry->pc);
      regs.rd_data[tid] = dut_rd_data[tid];
    }

    // write to emulator
    isa_emu_->reg_write(warp_gid, dut_cmt_entry->wb_tmask, regs.rd,
                        regs.rd_data);
  }
  suspicious[warp_gid] = true;
}

bool Difftest::commit_(WarpGID warp_gid) {

  assert_log(warp_gid < trace_buf_.size(),
             "Commit: Warp ID {} >= trace_buf_ size {}", warp_gid,
             trace_buf_.size());
  assert_log(warp_gid < dut_per_warp_.size(),
             "Commit: Warp ID {} >= dut_per_warp_ size {}", warp_gid,
             dut_per_warp_.size());

  auto &dut_warp = dut_per_warp_[warp_gid];

  // drain the DUT ROB if possible
  while (const auto *dut_cmt_entry = dut_warp.rob.poll_commit()) {

    assert(dut_cmt_entry->regs.has_value());
    fmt::memory_buffer rd_list;
    fmt::format_to(std::back_insert_iterator(rd_list), "rd: [");
    for (const auto &regwb : *dut_cmt_entry->regs) {
      fmt::format_to(std::back_insert_iterator(rd_list), "{}, ", regwb.rd);
    }
    fmt::format_to(std::back_insert_iterator(rd_list), "]");

    spdlog::trace("COMMIT w{}: #{} DUT: pc: {:#x} tmask: {} {} REF: pc: {:#x}",
                  warp_gid, dut_cmt_entry->iid, dut_cmt_entry->pc,
                  dut_cmt_entry->tmask, fmt::to_string(rd_list),
                  trace_buf_[warp_gid].pc);

    // advance the reference cmodel

    Difftest::Status ret;
    ret &= check(
        isa_emu_->schedulable(warp_gid),
        "Try to commit an inactivate (or stalled by a barrier/wspawn) warp {}",
        warp_gid);
    if (!ret) {
      report(ret, "Commit", warp_gid, dut_cmt_entry->pc);
      stop_emulator_thread();
      return false;
    }

#if defined(ARCH_VENTUS)
    assert(dut_cmt_entry->insn_bits.has_value());
    const auto is_join =
        (riscv::mask_JOIN & *dut_cmt_entry->insn_bits) == riscv::match_JOIN;
    if (is_join) {
      if (dut_warp.prev_pc == dut_cmt_entry->pc) {
        // ventus will fire a join several times to read the stack
        dut_warp.rob.commit();
        continue;
      }
    }
    dut_warp.prev_pc = dut_cmt_entry->pc;
#endif

    isa_emu_->execute_warp(warp_gid, &trace_buf_[warp_gid]);

#if defined(ARCH_VENTUS)
    if (is_insn_prefix(trace_buf_[warp_gid].insn_bits)) {
      // DUT not triggers any DECODE event for prefix instructions,
      // thus we must advance the ISA emulator
      continue;
    }
#endif

    ret &= check(!trace_buf_[warp_gid].trap_taken,
                 "Trap taken in DUT at PC {:#x}", dut_cmt_entry->pc);

    // check the instr fetch
    ret &= check(dut_cmt_entry->pc == trace_buf_[warp_gid].pc,
                 "Insn Fetch error: Warp {} PC {:#x} != REF {:#x}", warp_gid,
                 dut_cmt_entry->pc, trace_buf_[warp_gid].pc);
    if (!ret) {
      report(ret, "Commit", warp_gid, dut_cmt_entry->pc,
             trace_buf_[warp_gid].disasm);
      stop_emulator_thread();
      return false;
    }

    // Instruction execution frequency statistics
    const auto &disasm = trace_buf_[warp_gid].disasm;
    if (!disasm.empty()) {
      // Extract instruction name from disasm (e.g., "add" from "add x1, x2,
      // x3")
      size_t space_pos = disasm.find(' ');
      std::string insn_name = (space_pos != std::string::npos)
                                  ? disasm.substr(0, space_pos)
                                  : disasm;
      // Validate instruction name before insertion
      if (!insn_name.empty() && insn_name.size() < 64) {
        insn_stats_[insn_name]++;
      }
    }

    bool dut_warp_committed = false;
    if (!dut_cmt_entry->dummy_wb) {
      // for the instructions where DUT not triggers any writeback event,
      // we must advance the ISA emulator

      auto get_non_null_count =
          [](const std::vector<Trace::RegWriteback> &regs) {
            size_t cnt = 0;
            for (const auto &r : regs) {
              if (!is_null_reg(r.rd))
                ++cnt;
            }
            return cnt;
          };
      const bool ref_rd_wen = !!get_non_null_count(trace_buf_[warp_gid].regs);
      const bool dut_rd_wen = !!get_non_null_count(*dut_cmt_entry->regs);

      ret &= check(ref_rd_wen == dut_rd_wen, "rd WEN {} != ref {}", dut_rd_wen,
                   ref_rd_wen);

      // sync architectural states from the DUT if needed
      if (trace_buf_[warp_gid].opcode == Opcode::CtrlStatusRead && ref_rd_wen &&
          Config::instance().trust_csr(trace_buf_[warp_gid].csr_addr)) {
        sync_from_dut_(warp_gid, dut_cmt_entry);
      }

      ret &= check(trace_buf_[warp_gid].pc == dut_cmt_entry->pc,
                   "PC {:#x} != ref {:#x}", dut_cmt_entry->pc,
                   trace_buf_[warp_gid].pc);
      if (trace_buf_[warp_gid].applied_tmask) {
        ret &= check(trace_buf_[warp_gid].tmask == dut_cmt_entry->tmask,
                     "tmask {} != ref {}", dut_cmt_entry->tmask,
                     trace_buf_[warp_gid].tmask);
      }
      ret &= check(trace_buf_[warp_gid].opcode == dut_cmt_entry->opcode,
                   "opcode {} != ref {}", dut_cmt_entry->opcode,
                   trace_buf_[warp_gid].opcode);

      ret &= compare_regs_trace_(
          trace_buf_[warp_gid].regs, "REF", *dut_cmt_entry->regs, "DUT",
          [&](const Trace::RegWriteback &ref, const Trace::RegWriteback &dut) {
            Difftest::Status ret;

            bool single_thread = false;
            for (unsigned t = 0;
                 !single_thread &&
                 t < std::min(dut_cmt_entry->wb_tmask.size(),
                              trace_buf_[warp_gid].tmask.size());
                 ++t) {
              if (trace_buf_[warp_gid].applied_tmask) {
                if (!dut_cmt_entry->wb_tmask.test(t))
                  continue;
              } else {
                single_thread = true;
              }

              auto a = ref.rd_data[t], b = dut.rd_data[t];
              bool a_is_nan = false, a_is_inf = false;
              bool b_is_nan = false, b_is_inf = false;
              bool ok = false;
              switch (trace_buf_[warp_gid].num_format) {
              case NumericFormat::FP_D:
              case NumericFormat::FP_S:
              case NumericFormat::FP_H:
              case NumericFormat::FP_Q: {
                // classify the numbers
                classify_fp(a, trace_buf_[warp_gid].num_format, a_is_nan,
                            a_is_inf);
                classify_fp(b, trace_buf_[warp_gid].num_format, b_is_nan,
                            b_is_inf);
                if ((a_is_nan && b_is_nan) || (a_is_inf && b_is_inf)) {
                  ok = true;
                }

                unsigned sign_bit;
                uint64_t abs_mask;
                switch (trace_buf_[warp_gid].num_format) {
                case NumericFormat::FP_S:
                  sign_bit = 31;
                  abs_mask = 0x7FFFFFFF;
                  break;
                case NumericFormat::FP_D:
                  sign_bit = 63;
                  abs_mask = 0x7FFFFFFFFFFFFFFF;
                  break;
                case NumericFormat::FP_H:
                  sign_bit = 15;
                  abs_mask = 0x7FFF;
                  break;
                // TODO: support FP_Q
                default:
                  std::abort();
                }

                if (!ok) {
                  // compute the ULP distance
                  uint64_t ulp;
                  const bool sign_a = (a >> sign_bit) & 1;
                  const bool sign_b = (b >> sign_bit) & 1;
                  if (sign_a != sign_b) {
                    // the sum of distance to zero
                    ulp = (a & abs_mask) + (b & abs_mask);
                  } else {
                    ulp = a > b ? a - b : b - a;
                  }
                  ok = ulp <= Config::instance().fp_ulp_tolerance;
                }

                // check boxing
                const bool boxing_ok =
                    sign_bit == 63
                        ? true
                        : (a >> (sign_bit + 1)) == (b >> (sign_bit + 1));
                ret &= check(
                    boxing_ok,
                    "FP Boxing error: thread {} {}: {:#x} != ref {:#x} fmt {}",
                    t, dut.rd, b, a, (unsigned)trace_buf_[warp_gid].num_format);
                ok &= boxing_ok;

                if (ok && (a != b)) {
                  // preventing error propagation
                  sync_from_dut_(warp_gid, dut_cmt_entry);
                }
                break;
              }

              case NumericFormat::INT_FROM_FP: {
                const auto abs_delta = a > b ? a - b : b - a;
                ok = abs_delta <= Config::instance().fp2int_tolerance;

                if (ok && (a != b)) {
                  // preventing error propagation
                  sync_from_dut_(warp_gid, dut_cmt_entry);
                }
              } break;

              default:
                ok = (a == b);
              }
              ret &= check(ok, "thread {} {}: {:#x} != ref {:#x}", t, dut.rd, b,
                           a);
            }

            return ret;
          });

      ret &= check(!trace_buf_[warp_gid].trap_taken, "Trap taken != ref");

      if (!ret) {
        bool data_race = false;
        if (data_race_load_pc_.find(trace_buf_[warp_gid].pc) !=
            data_race_load_pc_.end()) {
          data_race = true;
          spdlog::warn("Skip data race load instruction at PC {:#x}",
                       trace_buf_[warp_gid].pc);
          sync_from_dut_(warp_gid, dut_cmt_entry);

        } else if (!Config::instance().crash_on_error) {
          // sync the DUT state to the emulator to continue the testing
          sync_from_dut_(warp_gid, dut_cmt_entry);
        }

        // ref architectural state (for debugging only)
        auto ref_tmask = trace_buf_[warp_gid].tmask;
        if (!trace_buf_[warp_gid].applied_tmask) {
          ref_tmask.reset();
          ref_tmask.set(0);
        }
        ROB::Entry ref_ent(
            0, trace_buf_[warp_gid].pc, ref_tmask, trace_buf_[warp_gid].regs,
            trace_buf_[warp_gid].opcode, trace_buf_[warp_gid].insn_bits,
            trace_buf_[warp_gid].dummy_wb);
        ref_ent.wb_tmask = ref_tmask;
        ref_arch_state_[warp_gid].commit(ref_ent);

        if (!data_race) {
          report(ret, "Commit", warp_gid, dut_cmt_entry->pc,
                 trace_buf_[warp_gid].disasm, true,
                 // commit DUT warp to obtain the output ARF values
                 [&]() {
                   dut_warp.commit();
                   dut_warp_committed = true;
                 });

          if (Config::instance().crash_on_error ||
              Config::instance().crash_on_pc.find(dut_cmt_entry->pc) !=
                  Config::instance().crash_on_pc.end()) {
            bool term = true;
            if (Config::instance().debug_crash) {
              pause_emulator_thread();
              term = !debugger->repl();
              resume_emulator_thread();
            }
            if (term) {
              stop_emulator_thread();
              return false;
            }
          }
        }
      }

    } // !dummy_wb

    if (Config::instance().breakpoints.find(dut_cmt_entry->pc) !=
        Config::instance().breakpoints.end()) {
      pause_emulator_thread();
      spdlog::info("Hit breakpoint at PC {:#x}, warp GID {}", dut_cmt_entry->pc,
                   warp_gid);
      bool term = !debugger->repl();
      resume_emulator_thread();
      if (term) {
        stop_emulator_thread();
        return false;
      }
    }

    if (!dut_warp_committed) {
      dut_warp.commit();
    }

    dut_warp.retire(rtl_clk_cycles);

    if (!isa_emu_->schedulable(warp_gid)) {
      // this warp is inactivate (or stalled by a barrier/wspawn), stop here
      // not commit the following dummy_wb instructions, until an architectural
      // wb event happens
      break;
    }

  } // while (dut_warp.rob.poll_commit())

  return true;
}

void Difftest::watchdog() {
  spdlog::trace("Cycles {}: Watchdog", rtl_clk_cycles);

  bool failed = false;
  for (unsigned wgid = 0; wgid < total_warps_; ++wgid) {
    if (!isa_emu_->schedulable(wgid)) {
      continue;
    }

    auto &dut_warp = dut_per_warp_[wgid];
    if (dut_warp.last_arch_event_cycles == uint64_t(-1))
      continue; // no history

    const auto et = rtl_clk_cycles - dut_warp.last_arch_event_cycles;
    Difftest::Status ret =
        check(et < Config::instance().watch_dog_timeout,
              "{}DUT watch dog timeout on warp {}, time since "
              "the last arch event {}{}",
              TermColor::RED, dut_warp.warp_gid(), et, TermColor::RESET);

    if (!ret) {
      report(ret, "Watchdog", wgid, dut_per_warp_[wgid].arch_state.pc);
      spdlog::error("Current RTL cycles = {}", rtl_clk_cycles);
      failed = true;
    }
  }

  if (failed) {
    stop_emulator_thread();
    return;
  }
}

void Difftest::detect_deadlock() {
  spdlog::trace("Cycles {}: Deadlock Detection", rtl_clk_cycles);

  if (isa_emu_->terminated())
    return;

  bool has_progress = false;
  for (difftest::WarpGID warp_gid = 0; warp_gid < total_warps_; ++warp_gid) {
    if (isa_emu_->schedulable(warp_gid)) {
      has_progress = true;
      break;
    }
  }

  if (!has_progress) {
    Difftest::Status ret =
        check(false, "Deadlock detected! None of the warps is "
                     "schedulable, this may be caused by buggy kernel");
    report(ret, "Deadlock Detection", 0, 0);
    stop_emulator_thread();
    return;
  }
}

/**
 * @return false if an error occurred
 */
bool Difftest::dut_terminated() {
  Status ret;
  for (auto &dut_warp : dut_per_warp_) {
    bool is_empty = dut_warp.rob.empty();
    fmt::memory_buffer rob_lst;
    if (!is_empty) {
      size_t count = std::min(size_t(10), dut_warp.rob.q.size());
      for (size_t i = 0; i < count; ++i) {
        const auto &entry = dut_warp.rob.q[i];
        fmt::format_to(std::back_insert_iterator(rob_lst),
                       "  [{}] iid={} pc={:#x} tmask={} wb_tmask={} opcode={} "
                       "dummy_wb={}{}\n",
                       i, entry.iid, entry.pc, entry.tmask, entry.wb_tmask,
                       entry.opcode, entry.dummy_wb,
                       entry.insn_bits.has_value()
                           ? fmt::format(" insn_bits={:#x}", *entry.insn_bits)
                           : "");
      }
    }

    ret &= check(is_empty,
                 "@ DUT Warp GID {} terminated but some warps still have "
                 "uncommitted instructions: {}",
                 dut_warp.warp_gid(), fmt::to_string(rob_lst));
  }
  spdlog::info("DUT terminated!");
  ret &= mem_axioms_checker_->check_axioms();
  mem_axioms_checker_->dump();
  if (!ret) {
    report(ret, "Terminated", 0, 0);
    stop_emulator_thread();
  } else {
    spdlog::info("Memory model check passed!");
  }
  return !!ret;
}

void Difftest::report(
    const Status &ret, const std::string &event_name,
    std::optional<WarpGID> warp_gid, std::optional<Word> pc,
    const std::optional<std::string> &ref_pc_disasm, bool dump_arf_regs,
    const std::optional<std::function<void()>> &commit_dut_warp_callback,
    std::optional<uint32_t> insn_bits) const {

  auto banner = [&]() {
    if (warp_gid.has_value() && *warp_gid < dut_per_warp_.size() &&
        suspicious[*warp_gid]) {
      spdlog::info("{} (SUSPICIOUS) {}", TermColor::YELLOW, TermColor::RESET);
    }
    // Get Unix timestamp (seconds since 1970-01-01)
    auto now = std::chrono::system_clock::now();
    auto timestamp =
        std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch())
            .count();

#if defined(ARCH_VORTEX)
    if (pc.has_value() && !insn_bits.has_value()) {
      // workaround for the vortex that has no insn bits
      uint32_t fetched_insn = 0;
      if (read_dut_pmem(*pc, reinterpret_cast<uint8_t *>(&fetched_insn),
                        sizeof(fetched_insn))) {
        insn_bits = fetched_insn;
      }
    }
#endif

    spdlog::info(
        "{} @Warp #{} {} : PC = {} [Timestamp: {}s]{}{}", TermColor::YELLOW,
        warp_gid.has_value() ? fmt::format("{}", *warp_gid) : "(unknown)",
        event_name, pc.has_value() ? fmt::format("{:#x}", *pc) : "(unknown)",
        timestamp,
        insn_bits.has_value() ? fmt::format(" insn bits = {:#x}", *insn_bits)
                              : "",
        TermColor::RESET);

    if (warp_gid.has_value() && *warp_gid < dut_per_warp_.size() &&
        ref_pc_disasm.has_value()) {
      spdlog::info("  REF disasm: {}", *ref_pc_disasm);
      if (dump_arf_regs) {
        const auto &arch = dut_per_warp_.at(*warp_gid).arch_state;
        const std::string &disasm = *ref_pc_disasm;

        static const char *xpr_name[] = {
            "x0",   "x1",   "x2",   "x3",   "x4",   "x5",   "x6",   "x7",
            "x8",   "x9",   "x10",  "x11",  "x12",  "x13",  "x14",  "x15",
            "x16",  "x17",  "x18",  "x19",  "x20",  "x21",  "x22",  "x23",
            "x24",  "x25",  "x26",  "x27",  "x28",  "x29",  "x30",  "x31",
            "x32",  "x33",  "x34",  "x35",  "x36",  "x37",  "x38",  "x39",
            "x40",  "x41",  "x42",  "x43",  "x44",  "x45",  "x46",  "x47",
            "x48",  "x49",  "x50",  "x51",  "x52",  "x53",  "x54",  "x55",
            "x56",  "x57",  "x58",  "x59",  "x60",  "x61",  "x62",  "x63",
            "x64",  "x65",  "x66",  "x67",  "x68",  "x69",  "x70",  "x71",
            "x72",  "x73",  "x74",  "x75",  "x76",  "x77",  "x78",  "x79",
            "x80",  "x81",  "x82",  "x83",  "x84",  "x85",  "x86",  "x87",
            "x88",  "x89",  "x90",  "x91",  "x92",  "x93",  "x94",  "x95",
            "x96",  "x97",  "x98",  "x99",  "x100", "x101", "x102", "x103",
            "x104", "x105", "x106", "x107", "x108", "x109", "x110", "x111",
            "x112", "x113", "x114", "x115", "x116", "x117", "x118", "x119",
            "x120", "x121", "x122", "x123", "x124", "x125", "x126", "x127",
            "x128", "x129", "x130", "x131", "x132", "x133", "x134", "x135",
            "x136", "x137", "x138", "x139", "x140", "x141", "x142", "x143",
            "x144", "x145", "x146", "x147", "x148", "x149", "x150", "x151",
            "x152", "x153", "x154", "x155", "x156", "x157", "x158", "x159",
            "x160", "x161", "x162", "x163", "x164", "x165", "x166", "x167",
            "x168", "x169", "x170", "x171", "x172", "x173", "x174", "x175",
            "x176", "x177", "x178", "x179", "x180", "x181", "x182", "x183",
            "x184", "x185", "x186", "x187", "x188", "x189", "x190", "x191",
            "x192", "x193", "x194", "x195", "x196", "x197", "x198", "x199",
            "x200", "x201", "x202", "x203", "x204", "x205", "x206", "x207",
            "x208", "x209", "x210", "x211", "x212", "x213", "x214", "x215",
            "x216", "x217", "x218", "x219", "x220", "x221", "x222", "x223",
            "x224", "x225", "x226", "x227", "x228", "x229", "x230", "x231",
            "x232", "x233", "x234", "x235", "x236", "x237", "x238", "x239",
            "x240", "x241", "x242", "x243", "x244", "x245", "x246", "x247",
            "x248", "x249", "x250", "x251", "x252", "x253", "x254", "x255"};

        static const char *fpr_name[] = {
            "ft0", "ft1", "ft2",  "ft3",  "ft4", "ft5", "ft6",  "ft7",
            "fs0", "fs1", "fa0",  "fa1",  "fa2", "fa3", "fa4",  "fa5",
            "fa6", "fa7", "fs2",  "fs3",  "fs4", "fs5", "fs6",  "fs7",
            "fs8", "fs9", "fs10", "fs11", "ft8", "ft9", "ft10", "ft11"};

        static const char *vr_name[] = {
            "v0",   "v1",   "v2",   "v3",   "v4",   "v5",   "v6",   "v7",
            "v8",   "v9",   "v10",  "v11",  "v12",  "v13",  "v14",  "v15",
            "v16",  "v17",  "v18",  "v19",  "v20",  "v21",  "v22",  "v23",
            "v24",  "v25",  "v26",  "v27",  "v28",  "v29",  "v30",  "v31",
            "v32",  "v33",  "v34",  "v35",  "v36",  "v37",  "v38",  "v39",
            "v40",  "v41",  "v42",  "v43",  "v44",  "v45",  "v46",  "v47",
            "v48",  "v49",  "v50",  "v51",  "v52",  "v53",  "v54",  "v55",
            "v56",  "v57",  "v58",  "v59",  "v60",  "v61",  "v62",  "v63",
            "v64",  "v65",  "v66",  "v67",  "v68",  "v69",  "v70",  "v71",
            "v72",  "v73",  "v74",  "v75",  "v76",  "v77",  "v78",  "v79",
            "v80",  "v81",  "v82",  "v83",  "v84",  "v85",  "v86",  "v87",
            "v88",  "v89",  "v90",  "v91",  "v92",  "v93",  "v94",  "v95",
            "v96",  "v97",  "v98",  "v99",  "v100", "v101", "v102", "v103",
            "v104", "v105", "v106", "v107", "v108", "v109", "v110", "v111",
            "v112", "v113", "v114", "v115", "v116", "v117", "v118", "v119",
            "v120", "v121", "v122", "v123", "v124", "v125", "v126", "v127",
            "v128", "v129", "v130", "v131", "v132", "v133", "v134", "v135",
            "v136", "v137", "v138", "v139", "v140", "v141", "v142", "v143",
            "v144", "v145", "v146", "v147", "v148", "v149", "v150", "v151",
            "v152", "v153", "v154", "v155", "v156", "v157", "v158", "v159",
            "v160", "v161", "v162", "v163", "v164", "v165", "v166", "v167",
            "v168", "v169", "v170", "v171", "v172", "v173", "v174", "v175",
            "v176", "v177", "v178", "v179", "v180", "v181", "v182", "v183",
            "v184", "v185", "v186", "v187", "v188", "v189", "v190", "v191",
            "v192", "v193", "v194", "v195", "v196", "v197", "v198", "v199",
            "v200", "v201", "v202", "v203", "v204", "v205", "v206", "v207",
            "v208", "v209", "v210", "v211", "v212", "v213", "v214", "v215",
            "v216", "v217", "v218", "v219", "v220", "v221", "v222", "v223",
            "v224", "v225", "v226", "v227", "v228", "v229", "v230", "v231",
            "v232", "v233", "v234", "v235", "v236", "v237", "v238", "v239",
            "v240", "v241", "v242", "v243", "v244", "v245", "v246", "v247",
            "v248", "v249", "v250", "v251", "v252", "v253", "v254", "v255"};

        // Build register name to index mappings
        static std::unordered_map<std::string, int> xpr_map, fpr_map, vr_map;
        static bool maps_initialized = false;
        if (!maps_initialized) {
          for (size_t i = 0; i < sizeof(xpr_name) / sizeof(xpr_name[0]); ++i) {
            xpr_map[xpr_name[i]] = i;
          }
          for (size_t i = 0; i < sizeof(fpr_name) / sizeof(fpr_name[0]); ++i) {
            fpr_map[fpr_name[i]] = i;
          }
          for (size_t i = 0; i < sizeof(vr_name) / sizeof(vr_name[0]); ++i) {
            vr_map[vr_name[i]] = i;
          }
          maps_initialized = true;
        }

        // Helper to get register value by name
        // Returns: <register_name, is_vector, values>
        auto get_reg_value = [&](const std::string &reg_name)
            -> std::optional<
                std::tuple<std::string, bool, std::vector<uint64_t>>> {
          // Try integer register
          auto it_xpr = xpr_map.find(reg_name);
          if (it_xpr != xpr_map.end()) {
            int reg_idx = it_xpr->second;
            if (reg_idx >= 0 && reg_idx < (int)arch.arf_int.size() &&
                !arch.arf_int[reg_idx].empty()) {
#if defined(ARCH_VORTEX)
              // For Vortex, return all thread values
              return std::make_tuple(reg_name, true,
                  std::vector<uint64_t>(arch.arf_int[reg_idx].begin(),
                                        arch.arf_int[reg_idx].end()));
#elif defined(ARCH_VENTUS)
              return std::make_tuple(
                  reg_name, false,
                  std::vector<uint64_t>{arch.arf_int[reg_idx][0]});
#endif
            }
            return std::nullopt;
          }

          // Try float register
          auto it_fpr = fpr_map.find(reg_name);
          if (it_fpr != fpr_map.end()) {
            int reg_idx = it_fpr->second;
#if defined(ARCH_VORTEX)
            if (reg_idx >= 0 && reg_idx < (int)arch.arf_fp.size() &&
                !arch.arf_fp[reg_idx].empty()) {
              // For Vortex, return all thread values
              return std::make_tuple(reg_name, true,
                  std::vector<uint64_t>(arch.arf_fp[reg_idx].begin(),
                                        arch.arf_fp[reg_idx].end()));
            }
#elif defined(ARCH_VENTUS)
            // For Ventus, float regs are stored in arf_int[reg_idx+32]
            int actual_idx = reg_idx + 32;
            if (actual_idx >= 0 && actual_idx < (int)arch.arf_int.size() &&
                !arch.arf_int[actual_idx].empty()) {
              return std::make_tuple(
                  reg_name, false,
                  std::vector<uint64_t>{arch.arf_int[actual_idx][0]});
            }
#endif
            return std::nullopt;
          }

          // Try vector register
          auto it_vr = vr_map.find(reg_name);
          if (it_vr != vr_map.end()) {
            int reg_idx = it_vr->second;
#if defined(ARCH_VENTUS)
            if (reg_idx >= 0 && reg_idx < (int)arch.arf_vec.size() &&
                !arch.arf_vec[reg_idx].empty()) {
              // Return all lanes for vector register
              return std::make_tuple(reg_name, true,
                  std::vector<uint64_t>(arch.arf_vec[reg_idx].begin(),
                                        arch.arf_vec[reg_idx].end()));
            }
#endif
            return std::nullopt;
          }

          return std::nullopt;
        };

        // Find all registers in the disassembly string
        std::vector<std::string> found_regs;
        size_t pos = 0;
        while (pos < disasm.length()) {
          // Skip non-alphabetic characters
          if (!std::isalpha(disasm[pos])) {
            pos++;
            continue;
          }

          // Extract potential register name (alphanumeric only)
          size_t end = pos;
          while (end < disasm.length() && std::isalnum(disasm[end])) {
            end++;
          }

          std::string token = disasm.substr(pos, end - pos);

          // Check if it's a known register
          if (xpr_map.count(token) || fpr_map.count(token) ||
              vr_map.count(token)) {
            found_regs.push_back(token);
          }

          pos = end;
        }

        // Print output and input registers
        if (!found_regs.empty()) {
          // First, get input registers (remaining registers after the first
          // one)
          std::string in_str;
          if (found_regs.size() > 1) {
            fmt::memory_buffer in_buf;
            fmt::format_to(std::back_insert_iterator(in_buf), "  ARF In: ");
            bool first = true;
            for (size_t i = 1; i < found_regs.size(); i++) {
              auto in_reg = get_reg_value(found_regs[i]);
              if (in_reg.has_value()) {
                auto [name, is_vec, values] = *in_reg;
                if (!first) {
                  fmt::format_to(std::back_insert_iterator(in_buf), ", ");
                }
                if (is_vec && values.size() > 1) {
                  // Vector register with multiple lanes
                  fmt::format_to(std::back_insert_iterator(in_buf), "{} = [",
                                 name);
                  for (size_t j = 0; j < values.size(); j++) {
                    if (j > 0)
                      fmt::format_to(std::back_insert_iterator(in_buf), ", ");
                    fmt::format_to(std::back_insert_iterator(in_buf), "{:#x}",
                                   values[j]);
                  }
                  fmt::format_to(std::back_insert_iterator(in_buf), "]");
                } else {
                  // Scalar register
                  fmt::format_to(std::back_insert_iterator(in_buf),
                                 "{} = {:#x}", name, values[0]);
                }
                first = false;
              }
            }
            if (!first) {
              in_str = fmt::to_string(in_buf);
            }
          }

          // Then, call commit_dut_warp_callback if provided
          // This seperates the output and input register values when they share
          // the same index
          if (commit_dut_warp_callback.has_value()) {
            (*commit_dut_warp_callback)();
          }

          // Finally, get output registers (first register is typically the
          // output)
          auto out_reg = get_reg_value(found_regs[0]);
          if (out_reg.has_value()) {
            auto [name, is_vec, values] = *out_reg;
            if (is_vec && values.size() > 1) {
              // Vector register with multiple lanes
              fmt::memory_buffer vec_buf;
              fmt::format_to(std::back_insert_iterator(vec_buf),
                             "  ARF Out: {} = [", name);
              for (size_t i = 0; i < values.size(); i++) {
                if (i > 0)
                  fmt::format_to(std::back_insert_iterator(vec_buf), ", ");
                fmt::format_to(std::back_insert_iterator(vec_buf), "{:#x}",
                               values[i]);
              }
              fmt::format_to(std::back_insert_iterator(vec_buf), "]");
              spdlog::info("{}", fmt::to_string(vec_buf));
            } else {
              // Scalar register
              spdlog::info("  ARF Out: {} = {:#x}", name, values[0]);
            }
          }

          // Print input registers
          if (!in_str.empty()) {
            spdlog::info("{}", in_str);
          }
        }
      } // dump_arf_regs
    }
  };

  if (warp_gid.has_value() && *warp_gid < dut_per_warp_.size()) {
    std::stringstream out;
    const auto &dut_warp = dut_per_warp_[*warp_gid];
    const auto &ref_warp = ref_arch_state_[*warp_gid];
    if (Config::instance().dump_regs) {
      out << "*** DUT GPRS ***" << std::endl;
      dut_warp.arch_state.dump_regs(out, ref_warp);

      out << "*** REF GPRS ***" << std::endl;
      ref_warp.dump_regs(out, dut_warp.arch_state);
    }

    if (Config::instance().dump_call_stack) {
      out << "*** DUT Calling Stack ***" << std::endl;
      dut_warp.arch_state.dump_calling_stack(out);
    }

    if (Config::instance().dump_retired_insn) {
      out << "*** DUT Retired Instructions ***" << std::endl;
      for (auto it = dut_warp.retire_records.rbegin();
           it != dut_warp.retire_records.rend(); ++it) {
        out << fmt::format(" Cycles #{} PC {:#x}", it->cycles, it->pc)
            << std::endl;
      }
    }

    banner();
    spdlog::info("{}", out.str());
  }

  if (!ret) {
    if (Config::instance().dump_regs) {
      banner(); // repeat the banner for better visibility
    }
    spdlog::error("{}FAILED: {}{}", TermColor::RED, ret.msg(),
                  TermColor::RESET);
  }
}

std::atomic<Difftest *> Difftest::g_instance;

} // namespace difftest