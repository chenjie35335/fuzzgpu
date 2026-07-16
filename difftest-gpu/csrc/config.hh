#pragma once

#include "types_internal.hh"
#include <unordered_set>

namespace difftest {

class Config {
  static Config *g_instance_;

public:
  Arch arch;
  uint64_t watch_dog_timeout{30000}; // in cycles
  uint64_t check_watch_dog_cycles{10000};

  uint64_t check_deadlock_cycles{50000};

  size_t nr_retire_records{3};

  bool dump_regs{false}, dump_all_regs{false}, dump_call_stack{false}, dump_retired_insn{false};
  bool crash_on_error{false};
  std::unordered_set<uint64_t> crash_on_pc;
  bool debug_crash{false};
  std::unordered_set<uint64_t> breakpoints;

  //
  // Architecture-dependent configs
  //
  uint64_t fp_ulp_tolerance, fp2int_tolerance;
  std::function<bool(uint32_t)> trust_csr;

  Config(const Arch &arch);

  static void create(const Arch &arch) {
    g_instance_ = new Config(arch);
  }
  static Config &instance() {
    return *g_instance_;
  }
};

} // namespace difftest
