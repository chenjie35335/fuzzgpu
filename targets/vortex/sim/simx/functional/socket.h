
#pragma once

#include "functional/core.h"
#include "functional/stubs.h"
#include <cstdint>
#include <vector>

#ifndef DIFFTEST_ISA_EMULATOR
#error Do not include the functional core when using non-difftest configuration
#endif

namespace vortex {
class Cluster;

///@brief
/// A socket with the signle core
/// compatible with the vortex::Emulator
class Socket {
  uint32_t socket_id_;
  Cluster *cluster_;
  std::vector<std::shared_ptr<Core>> cores_;

public:
  Socket(uint32_t socket_id,
         Cluster *cluster,
         const Arch &arch,
         const DCRS &dcrs);

  Cluster *cluster() const {
    return cluster_;
  }

  const auto &cores() const {
    return cores_;
  }
  
  void reset();

  void attach_ram(RAM *ram);

  void barrier(uint32_t bar_id, uint32_t count, uint32_t core_id);

  void resume(uint32_t core_id);

public:
  struct PerfStats {
    CacheSim::PerfStats icache;
    CacheSim::PerfStats dcache;
  };

private:
  PerfStats perf_;

public:
  PerfStats perf_stats() const {
    return perf_;
  }
};

} // namespace vortex