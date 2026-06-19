#pragma once

#include "arch_state.hh"
#include "rob.hh"
#include "types_internal.hh"

#include <cassert>
#include <deque>
#include <functional>
#include <vector>

namespace difftest {

class Difftest;

class PerWarp {
  Difftest &dft_;
  WarpGID warp_gid_;

public:
  InstId first_iid;
  ROB rob;
  size_t tmask_write_pending;
  ArchState arch_state;
  struct RetireRecord {
    Word pc;
    uint64_t cycles;
  };
  std::deque<RetireRecord> retire_records;
  uint64_t last_arch_event_cycles;

#if defined(ARCH_VENTUS)
  uint64_t prev_pc{uint64_t(-1)};
#endif

  PerWarp(Difftest &dft, WarpGID warp_gid);

  WarpGID warp_gid() { return warp_gid_; }

  void issue(InstId iid, Word pc, const ThreadMask &tmask,
             const std::optional<std::vector<Trace::RegWriteback>> &regs,
             Opcode opcode, const std::optional<uint32_t> &insn_bits,
             bool dummy_wb);

  void reset();

  void commit();

  void retire(uint64_t ts);

  void on_arch_event(uint64_t ts) { last_arch_event_cycles = ts; }
};

} // namespace difftest