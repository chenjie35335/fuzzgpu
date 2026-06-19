#include <assert.h>
#include <cstring>
#include <iostream>
#include <math.h>
#include <stdlib.h>
#include <unistd.h>
#include <util.h>

#include "disasm.h"
#undef set_pc
#undef FLEN

#include "cluster.h"
#include "core.h"
#include "dcrs.h"
#include "emulator.h"
#include "instr.h"
#include "instr_trace.h"
#include "local_mem.h"
#include "processor_impl.h"
#include "socket.h"

#include "spdlog/spdlog.h"

using namespace vortex;

// The followings are simply copied from the Emulator::step(), with separated two-phase interfaces:
//   (1) schedule
//   (2) fetch & decode & execute
//
// This separation does not require to alter the timing of the cmodel
// For example, we can run the cmodel in a separated thread, conditionally blocking its internal steps in Emulator::step()
// using semaphore

bool Emulator::schedulable(uint32_t warp_id) {
  // these codes are copied from the Emulator::step()

  // process pending wspawn
  if (wspawn_.valid && active_warps_.count() == 1) {
    DP(3, "*** activate " << (wspawn_.num_warps - 1) << " warps at pc: " << std::hex << wspawn_.nextPC << std::dec);
    for (uint32_t i = 1; i < wspawn_.num_warps; ++i) {
      auto &_warp = warps_.at(i);
      _warp.PC = wspawn_.nextPC;
      _warp.tmask.set(0);
      active_warps_.set(i);
    }
    wspawn_.valid = false;
    stalled_warps_.reset(0);
  }

  // find next ready warp
  bool warp_active = active_warps_.test(warp_id);
  bool warp_stalled = stalled_warps_.test(warp_id);
  return (warp_active && !warp_stalled);
}

static void set_trace_disasm(instr_trace_t *trace, uint32_t instr_code) {
  static std::unique_ptr<disassembler_t> disasm;
  static std::unique_ptr<isa_parser_t> isa_parser;
  if (!disasm) {
    std::stringstream isa_str;
    isa_str << "rv" << XLEN << "i";
#ifdef EXT_M_ENABLE
    isa_str << "m";
#endif
#ifdef EXT_F_ENABLE
    isa_str << "f";
#endif
#ifdef EXT_D_ENABLE
    isa_str << "d";
#endif
#ifdef EXT_ZICOND_ENABLE
// isa_str << "zicond";
// FIXME
#endif
    std::cout << "ISA = " << isa_str.str() << std::endl;
    const auto isa = isa_str.str();
    isa_parser = std::make_unique<isa_parser_t>(isa.c_str(), "MSU");
    disasm = std::make_unique<disassembler_t>(isa_parser.get());
  }
  trace->disasm = disasm->disassemble(insn_t(instr_code));
}

instr_trace_t *Emulator::execute_warp(uint32_t scheduled_warp) {
  // these code are copied from the Emulator::step()

  // get scheduled warp
  auto &warp = warps_.at(scheduled_warp);
  assert(warp.tmask.any());

  // the functional model should have no ibuffer
  assert(warp.ibuffer.empty());
  uint64_t uuid = 0;
#ifndef NDEBUG
  {
    // generate unique universal instruction ID
    uint32_t instr_uuid = warp.uuid++;
    uint32_t g_wid = core_->id() * arch_.num_warps() + scheduled_warp;
    uuid = (uint64_t(g_wid) << 32) | instr_uuid;
  }
#endif

  // Fetch
  auto instr_code = this->fetch(scheduled_warp, uuid);

  // decode
  assert(warp.ibuffer.empty());
  this->decode(instr_code, scheduled_warp, uuid);

  // Execute
  instr_trace_t *ret;
  while (!warp.ibuffer.empty()) {
    ret = this->execute(*warp.ibuffer.front(), scheduled_warp);
    warp.ibuffer.pop_front();
    if (!warp.ibuffer.empty()) {
      // we have a micro-instruction in the ibuffer
      // adjust PC back to original (incremented in execute())
      warp.PC -= 4;
    }
  }

  ret->insn_bits = instr_code;
  set_trace_disasm(ret, instr_code);
  return ret;
}

instr_trace_t *Emulator::execute_warp_per_uop(uint32_t scheduled_warp) {
  // these code are copied from the Emulator::step()

  // get scheduled warp
  auto &warp = warps_.at(scheduled_warp);
  assert(warp.tmask.any());

  uint64_t uuid = 0;

  uint32_t instr_code;
  if (warp.ibuffer.empty()) {
#ifndef NDEBUG
    {
      // generate unique universal instruction ID
      uint32_t instr_uuid = warp.uuid++;
      uint32_t g_wid = core_->id() * arch_.num_warps() + scheduled_warp;
      uuid = (uint64_t(g_wid) << 32) | instr_uuid;
    }
#endif

    // Fetch and decode a new macro instruction.
    instr_code = this->fetch(scheduled_warp, uuid);
    this->decode(instr_code, scheduled_warp, uuid);
  } else {
    // Continue a pending micro-instruction from the previous macro.
    uuid = warp.ibuffer.front()->getUUID();
    instr_code = this->fetch(scheduled_warp, uuid);
  }

  // Execute one instruction or micro-instruction.
  auto ret = this->execute(*warp.ibuffer.front(), scheduled_warp);
  warp.ibuffer.pop_front();
  if (!warp.ibuffer.empty()) {
    // keep PC at the macro instruction while pending micro-instructions remain
    warp.PC -= 4;
  }

  ret->insn_bits = instr_code;
  set_trace_disasm(ret, instr_code);
  return ret;
}

void Emulator::set_reg(uint32_t warp_id, ThreadMask tmask, RegOpd reg, const std::vector<uint64_t> &value) {

  switch (reg.type) {
  case RegType::Integer:
    for (unsigned tid = 0; tid < arch_.num_threads(); ++tid) {
      if (!tmask.test(tid))
        continue;
      warps_[warp_id].ireg_file[reg.idx][tid] = value[tid];
    }
    break;
  case RegType::Float:
    for (unsigned tid = 0; tid < arch_.num_threads(); ++tid) {
      if (!tmask.test(tid))
        continue;
      warps_[warp_id].freg_file[reg.idx][tid] = value[tid];
    }
    break;
  default:
    std::abort();
  }
}

uint64_t Emulator::get_reg(uint32_t warp_id, uint32_t tid, RegOpd reg) {
  switch (reg.type) {
  case RegType::Integer:
    return warps_[warp_id].ireg_file[reg.idx][tid];
  case RegType::Float:
    return warps_[warp_id].freg_file[reg.idx][tid];
  default:
    std::abort();
  }
}

void Emulator::set_pc(uint32_t warp_lid, uint64_t pc) {
  warps_.at(warp_lid).PC = pc;
}

// Compute barrier statistics for box plot visualization
BarrierStats Emulator::compute_barrier_stats(std::vector<uint32_t> counts) const {
  BarrierStats stats;
  
  if (counts.empty()) {
    return stats;
  }
  
  // Sort the data for percentile calculations
  std::sort(counts.begin(), counts.end());
  
  size_t n = counts.size();
  stats.count = n;
  
  // Compute min and max
  stats.min = counts.front();
  stats.max = counts.back();
  
  // Compute sum and mean
  stats.sum = 0;
  for (const auto& c : counts) {
    stats.sum += c;
  }
  stats.mean = static_cast<double>(stats.sum) / n;
  
  // Compute median (Q2)
  if (n % 2 == 0) {
    stats.median = (counts[n/2 - 1] + counts[n/2]) / 2.0;
  } else {
    stats.median = counts[n/2];
  }
  
  // Compute Q1 (25th percentile) and Q3 (75th percentile)
  // Using the inclusive method (Tukey's hinges)
  if (n == 1) {
    stats.q1 = stats.median;
    stats.q3 = stats.median;
  } else if (n == 2) {
    stats.q1 = counts[0];
    stats.q3 = counts[1];
  } else {
    // Q1: median of lower half
    size_t lower_end = n / 2;
    if (lower_end % 2 == 0) {
      stats.q1 = (counts[lower_end/2 - 1] + counts[lower_end/2]) / 2.0;
    } else {
      stats.q1 = counts[lower_end/2];
    }
    
    // Q3: median of upper half
    size_t upper_start = (n + 1) / 2;
    size_t upper_count = n - upper_start;
    if (upper_count % 2 == 0) {
      stats.q3 = (counts[upper_start + upper_count/2 - 1] + counts[upper_start + upper_count/2]) / 2.0;
    } else {
      stats.q3 = counts[upper_start + upper_count/2];
    }
  }
  
  // Compute IQR and fences
  stats.iqr = stats.q3 - stats.q1;
  stats.lower_fence = stats.q1 - 1.5 * stats.iqr;
  stats.upper_fence = stats.q3 + 1.5 * stats.iqr;
  
  // Compute standard deviation
  if (n > 1) {
    double sum_sq_diff = 0;
    for (const auto& c : counts) {
      double diff = c - stats.mean;
      sum_sq_diff += diff * diff;
    }
    stats.std_dev = std::sqrt(sum_sq_diff / (n - 1));  // Sample standard deviation
  } else {
    stats.std_dev = 0;
  }
  
  return stats;
}

BarrierStats Emulator::get_local_barrier_stats() const {
  return compute_barrier_stats(local_barrier_counts_);
}

BarrierStats Emulator::get_global_barrier_stats() const {
  return compute_barrier_stats(global_barrier_counts_);
}
