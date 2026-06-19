#pragma once

#include "types.h"

#ifndef DIFFTEST_ISA_EMULATOR
#error Do not include the functional core when using non-difftest configuration
#endif

namespace vortex {

// compatible with the vortex::Emulator
class CacheSim {

public:
  //
  // perf counters (compatible with the vortex::Emulator)
  //
  struct Config {
    bool bypass;        // cache bypass
    uint8_t C;          // log2 cache size
    uint8_t L;          // log2 line size
    uint8_t W;          // log2 word size
    uint8_t A;          // log2 associativity
    uint8_t B;          // log2 number of banks
    uint8_t addr_width; // word address bits
    uint8_t num_inputs; // number of inputs
    uint8_t mem_ports;  // memory ports
    bool write_back;    // is write-back
    bool write_reponse; // enable write response
    uint16_t mshr_size; // MSHR buffer size
    uint8_t latency;    // pipeline latency
  };

  struct PerfStats {
    uint64_t reads;
    uint64_t writes;
    uint64_t read_misses;
    uint64_t write_misses;
    uint64_t evictions;
    uint64_t bank_stalls;
    uint64_t mshr_stalls;
    uint64_t mem_latency;

    PerfStats()
      : reads(0)
      , writes(0)
      , read_misses(0)
      , write_misses(0)
      , evictions(0)
      , bank_stalls(0)
      , mshr_stalls(0)
      , mem_latency(0) {}

    PerfStats &operator+=(const PerfStats &rhs) {
      this->reads += rhs.reads;
      this->writes += rhs.writes;
      this->read_misses += rhs.read_misses;
      this->write_misses += rhs.write_misses;
      this->evictions += rhs.evictions;
      this->bank_stalls += rhs.bank_stalls;
      this->mshr_stalls += rhs.mshr_stalls;
      this->mem_latency += rhs.mem_latency;
      return *this;
    }
  };
};

// compatible with the vortex::Emulator
class MemCoalescer {

public:
  //
  // perf counters (compatible with the vortex::Emulator)
  //
  struct PerfStats {
    uint64_t misses;

    PerfStats()
      : misses(0) {}

    PerfStats &operator+=(const PerfStats &rhs) {
      this->misses += rhs.misses;
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

// compatible with the vortex::Emulator
class MemSim {

public:
  //
  // perf counters (compatible with the vortex::Emulator)
  //
  struct PerfStats {
    uint64_t bank_stalls;

    PerfStats()
      : bank_stalls(0) {}

    PerfStats &operator+=(const PerfStats &rhs) {
      this->bank_stalls += rhs.bank_stalls;
      return *this;
    }
  };
};

} // namespace vortex