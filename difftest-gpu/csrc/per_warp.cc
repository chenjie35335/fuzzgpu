#include <algorithm>
#include <iomanip>
#include <iostream>

#include "config.hh"
#include "difftest.hh"
#include "dpi.hh"
#include "per_warp.hh"
#include "term.hh"

namespace difftest {

PerWarp::PerWarp(Difftest &dft, WarpGID warp_gid)
  : dft_(dft)
  , warp_gid_(warp_gid)
  , rob(dft, warp_gid_) {}

void PerWarp::reset() {
  rob.reset();
  tmask_write_pending = 0;
  arch_state.reset();
  retire_records.clear();
  last_arch_event_cycles = uint64_t(-1);
}

void PerWarp::issue(InstId iid, Word pc, const ThreadMask &tmask,
                    const std::optional<std::vector<Trace::RegWriteback>> &regs,
                    Opcode opcode, const std::optional<uint32_t> &insn_bits,
                    bool dummy_wb) {
  if (opcode == Opcode::TmaskWrite) {
    spdlog::debug("warp {} Tmask write issued {:#x}, pending {}", warp_gid_, pc,
                  tmask_write_pending + 1);
    ++tmask_write_pending;
  }
  rob.issue(iid, pc, tmask, regs, opcode, insn_bits, dummy_wb);
}

void PerWarp::commit() {
  if (rob.oldest().opcode == Opcode::TmaskWrite) {
    spdlog::debug("warp {} Tmask write committed {:#x}, pending {}", warp_gid_,
                  rob.oldest().pc, tmask_write_pending - 1);
    if (tmask_write_pending)
      --tmask_write_pending;
  }
  arch_state.commit(rob.oldest());
#if defined(ARCH_VORTEX)
  ::dpi_grm_commit_callback(rob.oldest().iid);
#endif
}

void PerWarp::retire(uint64_t ts) {
  retire_records.emplace_back(rob.oldest().pc, ts);
  if (retire_records.size() > Config::instance().nr_retire_records) {
    retire_records.pop_front();
  }

  rob.commit();
}

} // namespace difftest