#pragma once

#include "spdlog/fmt/fmt.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <sstream>
#include <unordered_set>
#include <vector>

#include "arch_state.hh"
#include "debugger.hh"
#include "per_warp.hh"
#include "term.hh"
#include <difftest_gpu/isa_emulator.hh>

namespace difftest {

class MemAxiomsChecker;

class Difftest {
  std::shared_ptr<ISAEmulator> isa_emu_;
  unsigned total_warps_, num_warps_per_block_, num_blocks_;
  std::vector<Trace> trace_buf_;
  std::unique_ptr<Debugger> debugger;
  std::vector<bool> suspicious; // indicate that it may ba a false positive

  std::vector<PerWarp> dut_per_warp_;
  std::vector<ArchState> ref_arch_state_;

  std::unordered_set<uint64_t> data_race_load_pc_;
  std::unique_ptr<MemAxiomsChecker> mem_axioms_checker_;

  // Instruction execution frequency statistics
  std::unordered_map<std::string, uint64_t> insn_stats_;

  bool stopped{false};

  void sync_from_dut_(WarpGID warp_gid, const ROB::Entry *dut_cmt_entry);
  bool commit_(WarpGID warp_gid);

public:
  static std::atomic<Difftest *> g_instance;
  static Difftest &instance() {
    return *g_instance.load(std::memory_order_acquire);
  }
  Difftest(const std::shared_ptr<ISAEmulator> &isa_emu);

  void
  decode_event(InstId iid, Word pc, WarpGID warp_gid, const ThreadMask &tmask,
               const std::optional<std::vector<Trace::RegWriteback>> &regs_,
               Opcode opcode, const std::optional<uint32_t> &insn_bits,
               bool dummy_wb);

  void writeback_reg_event(InstId iid, WarpGID warp_gid,
                           const ThreadMask &wb_tmask, Word pc,
                           const std::vector<Trace::RegWriteback> &regs,
                           const std::optional<uint32_t> &insn_bits);

  void reset_event();

  bool dut_terminated();

  void new_warps_event(unsigned num_warps_per_block, unsigned num_blocks);

  void add_data_race_load_pc(uint64_t pc) { data_race_load_pc_.insert(pc); }

  auto &mem_axioms_checker() { return *mem_axioms_checker_.get(); }

  ISAEmulator &isa_emulator() { return *isa_emu_; }

  void watchdog();
  void detect_deadlock();

  class Status {
    bool _failed{false};
    std::stringstream _msg;
    friend class Difftest;

  public:
    void operator&=(const Status &&src) {
      _failed |= src._failed;
      _msg << src._msg.str();
    }

    explicit operator bool() const { return !_failed; }

    std::string msg() const { return _msg.str(); }
  };

  template <typename... Args>
  Status check(bool expr, fmt::format_string<Args...> fmt,
               Args &&...fmt_args) const {

    Status ret;
    if (!expr) [[unlikely]] {
      ret._msg << TermColor::RED << "Error: " << TermColor::YELLOW
               << fmt::format(fmt, std::forward<Args>(fmt_args)...)
               << TermColor::RESET << std::endl;
      ret._failed = true;
    }
    return ret;
  }

  void report(const Status &ret, const std::string &event_name,
              std::optional<WarpGID> warp_gid, std::optional<Word> pc,
              const std::optional<std::string> &ref_pc_disasm = std::nullopt,
              bool dump_arf_regs = false,
              const std::optional<std::function<void()>>
                  &commit_dut_warp_callback = std::nullopt,
              std::optional<uint32_t> insn_bits = std::nullopt) const;

  auto num_total_warps() const { return total_warps_; }
  auto num_warps_per_block() const { return num_warps_per_block_; }
  auto num_blocks() const { return num_blocks_; }

  const auto &get_insn_stats() const { return insn_stats_; }

private:
  Status compare_regs_trace_(
      const std::vector<Trace::RegWriteback> &a, const std::string &a_name,
      const std::vector<Trace::RegWriteback> &b, const std::string &b_name,
      const std::function<Status(const Trace::RegWriteback &,
                                 const Trace::RegWriteback &)> &cmp);
};

} // namespace difftest