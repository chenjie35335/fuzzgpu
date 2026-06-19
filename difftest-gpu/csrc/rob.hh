#pragma once

#include <cassert>
#include <deque>
#include <unordered_map>

#include "types_internal.hh"

namespace difftest {

class Difftest;

/** ROB is for the out-of-order *write-back* per warp, caused by the scoreboard
schedule that supports continuous issuing.
* Detailed explanation:
Once an instruction is issued into its corresponding Load/Store Unit or FU
(e.g.,, TensorCore), they hang in different latency. For instance, If it issues
a long delayed load (cache miss) first, followed by a short delayed ALU
instruction without hazard, the latter is likely to run and write back to the
register first. This looks like 'the first instruction issued has not been
written back yet, while the later instructions are written back first' - the
out-order writing back.
 */
class ROB {
  Difftest &dft_;
  WarpGID warp_gid_;
  InstId head_;
  InstId allocated_iid_;
  std::unordered_map<InstId, size_t> iid_to_index_;

public:
  struct Entry {
    InstId iid;
    Word pc;
    ThreadMask tmask;
    ThreadMask next_tmask;
    Opcode opcode;
    std::optional<std::vector<Trace::RegWriteback>> regs;
    std::optional<uint32_t> insn_bits;
    ThreadMask wb_tmask; // if the instr has been written-back (not required to
                         // write reg, which is determined by regs.empty())
    bool dummy_wb{
        false}; // for instructions without reg write-back, e.g., store, barrier

    Entry(InstId iid_, Word pc_, const ThreadMask &tmask_,
          const std::optional<std::vector<Trace::RegWriteback>> &regs_,
          Opcode opcode_, const std::optional<uint32_t> &insn_bits,
          bool dummy_wb_)
      : iid(iid_)
      , pc(pc_)
      , tmask(tmask_)
      , next_tmask(tmask_.size())
      , opcode(opcode_)
      , regs(regs_)
      , insn_bits(insn_bits)
      , wb_tmask(tmask_.size())
      , dummy_wb(dummy_wb_) {}
  };

  std::deque<Entry> q;

public:
  ROB(Difftest &dft, WarpGID warp_gid)
    : dft_(dft)
    , warp_gid_(warp_gid) {}

  void issue(InstId iid, Word pc, const ThreadMask &tmask,
             const std::optional<std::vector<Trace::RegWriteback>> &regs_,
             Opcode opcode, const std::optional<uint32_t> &insn_bits,
             bool dummy_wb);

  bool iid_valid(InstId iid) const {
    return iid_to_index_.find(iid) != iid_to_index_.end();
  }

  inline auto &get(InstId iid) {
    assert(iid_valid(iid));
    return q[iid_to_index_.at(iid)];
  }

  bool empty() const { return q.empty(); }
  void writeback_reg(InstId iid, const ThreadMask &wb_tmask,
                     const std::vector<Trace::RegWriteback> &regs_);
  const Entry *poll_commit() const;
  const Entry &oldest() const { return q.front(); }
  const Entry &latest() const { return q.back(); }
  void commit();
  void reset();
};

} // namespace difftest
