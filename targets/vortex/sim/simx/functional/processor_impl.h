
#pragma once

#include "arch.h"
#include "dcrs.h"
#include "functional/cluster.h"
#include "functional/stubs.h"
#include "types.h"

#ifndef DIFFTEST_ISA_EMULATOR
#error Do not include the functional core when using non-difftest configuration
#endif

namespace vortex {

// compatible with the vortex::Emulator
class ProcessorImpl {
  Arch arch_;
  DCRS dcrs_;
  std::vector<std::shared_ptr<Cluster>> clusters_;

public:
  ProcessorImpl(unsigned nr_core, unsigned nr_warps, unsigned nr_threads);
                
  void reset();

  void attach_ram(RAM* ram);

  DCRS &dcrs() { return dcrs_; }
  const Arch &arch() { return arch_; }

  const auto &clusters() const {
    return clusters_;
  }

public:
  //
  // perf counters (compatible with the vortex::Emulator)
  //
  struct PerfStats {
    CacheSim::PerfStats l3cache;
    MemSim::PerfStats memsim;
    uint64_t mem_reads;
    uint64_t mem_writes;
    uint64_t mem_latency;
  };

private:
  ProcessorImpl::PerfStats perf_;

public:
  PerfStats perf_stats() const {
    return perf_;
  }
};

} // namespace vortex