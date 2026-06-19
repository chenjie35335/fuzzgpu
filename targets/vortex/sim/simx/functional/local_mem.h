
#pragma once

#include "types.h"
#include "mem.h"

#ifndef DIFFTEST_ISA_EMULATOR
#error Do not include the functional core when using non-difftest configuration
#endif

namespace vortex {

// compatible with the vortex::Emulator
class LocalMem {
  uint32_t line_bits_;
  RAM ram_;

  uint64_t to_local_addr(uint64_t addr) {
    // FIXME? it seems be a bug in the simx
    return bit_getw(addr, 0, line_bits_ - 1);
  }

public:
  struct Config {
    uint32_t capacity;
    uint32_t line_size;
  };
  LocalMem(const Config &config);

  void reset();

  void read(void *data, uint64_t addr, uint32_t size);

  void write(const void *data, uint64_t addr, uint32_t size);

public:
  //
  // perf counters (compatible with the vortex::Emulator)
  //
  struct PerfStats {
    uint64_t reads;
    uint64_t writes;
    uint64_t bank_stalls;

    PerfStats()
      : reads(0)
      , writes(0)
      , bank_stalls(0) {}

    PerfStats &operator+=(const PerfStats &rhs) {
      this->reads += rhs.reads;
      this->writes += rhs.writes;
      this->bank_stalls += rhs.bank_stalls;
      return *this;
    }
  };

private:
  PerfStats perf_;

public:
  const PerfStats &perf_stats() const {
    return perf_;
  }
};

} // namespace vortex