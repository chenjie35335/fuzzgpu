#pragma once

#include <deque>
#include <functional>
#include <iostream>

#include "rob.hh"
#include "types_internal.hh"

namespace difftest {

class ArchState {
public:
  Word pc;
  std::vector<std::vector<Word>> arf_int;
#if defined(ARCH_VORTEX)
  std::vector<std::vector<uint64_t>> arf_fp;
#elif defined(ARCH_VENTUS)
  std::vector<std::vector<uint64_t>> arf_vec;
#endif

  struct Calling {
    Word caller_pc;
  };
  std::deque<Calling> calling_stk;

  ArchState();

  void reset();

  void commit(const ROB::Entry &rob_entry);

  void dump_regs(std::ostream &out, const ArchState &ref) const;

  void dump_calling_stack(std::ostream &out) const;
};

} // namespace difftest