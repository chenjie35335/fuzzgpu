
#include "assert.hh"
#include "difftest.hh"

#include "rob.hh"

namespace difftest {

void ROB::issue(InstId iid, Word pc, const ThreadMask &tmask,
                const std::optional<std::vector<Trace::RegWriteback>> &regs_,
                Opcode opcode, const std::optional<uint32_t> &insn_bits,
                bool dummy_wb) {
  if (allocated_iid_ == InstId(-1)) {
    head_ = iid;
  }
  auto ret = dft_.check(!iid_valid(iid), "Duplicate Inst ID: {}", iid);
  if (!ret) {
    dft_.report(ret, "Issue", warp_gid_, pc);
    std::terminate();
  }
  allocated_iid_ = iid;

  std::optional<std::vector<Trace::RegWriteback>> final_regs;
  if (dummy_wb) {
    final_regs = std::vector<Trace::RegWriteback>{};
  } else {
    final_regs = regs_;
  }
  iid_to_index_[iid] = q.size();
  q.emplace_back(iid, pc, tmask, final_regs, opcode, insn_bits, dummy_wb);
}

void ROB::writeback_reg(InstId id, const ThreadMask &wb_tmask,
                        const std::vector<Trace::RegWriteback> &regs_) {
  auto &entry = get(id);
  assert(!entry.dummy_wb && "bugy dummy_wb meta or writeback event listener");
  if (!entry.regs.has_value()) {
    entry.regs = regs_;
  } else {
    for (size_t irg = 0; irg < regs_.size(); ++irg) {
      for (unsigned t = 0; t < wb_tmask.size(); ++t) {
        if (wb_tmask.test(t)) {
          entry.regs.value()[irg].rd_data[t] = regs_[irg].rd_data[t];
        }
      }
    }
  }
  entry.wb_tmask |= wb_tmask; // not gated by rd_wen
}

const ROB::Entry *ROB::poll_commit() const {
  // try to commit
  if (q.empty())
    return nullptr;
  if (oldest().wb_tmask != oldest().tmask && !oldest().dummy_wb)
    return nullptr;

  return &oldest();
}

void ROB::commit() {
  assert(!q.empty());
  iid_to_index_.erase(q.front().iid);
  q.pop_front();
  for (auto &[_, index] : iid_to_index_) {
    --index;
  }
  head_ = q.empty() ? 0 : q.front().iid;
}

void ROB::reset() {
  allocated_iid_ = InstId(-1);
  q.clear();
  iid_to_index_.clear();
  head_ = 0;
}

} // namespace difftest
