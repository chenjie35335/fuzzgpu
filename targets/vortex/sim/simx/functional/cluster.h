
#pragma once

#include <vector>

#include "functional/core.h"
#include "functional/socket.h"
#include "functional/stubs.h"
#include "types.h"

#ifndef DIFFTEST_ISA_EMULATOR
#error Do not include the functional core when using non-difftest configuration
#endif

namespace vortex {

class ProcessorImpl;

///@brief
/// A cluster with the signle socket
/// compatible with the vortex::Emulator
class Cluster {
  ProcessorImpl *processor_;
  std::vector<std::shared_ptr<Socket>> sockets_;
  std::vector<CoreMask> barriers_;
  uint32_t cores_per_socket_;
  uint64_t global_barrier_success_count_;

public:
  Cluster(uint32_t cluster_id,
          ProcessorImpl *processor,
          const Arch &arch,
          const DCRS &dcrs);

  ProcessorImpl *processor() const {
    return processor_;
  }

  const auto &sockets() const {
    return sockets_;
  }

  void attach_ram(RAM *ram);

  void reset();

  void barrier(uint32_t bar_id, uint32_t count, uint32_t core_id);

  uint64_t global_barrier_success_count() const { return global_barrier_success_count_; }

public:
  //
  // perf counters (compatible with the vortex::Emulator)
  //
  struct PerfStats {
    CacheSim::PerfStats l2cache;
  };

private:
  PerfStats perf_;

public:
  PerfStats perf_stats() const { return perf_; }
};

} // namespace vortex