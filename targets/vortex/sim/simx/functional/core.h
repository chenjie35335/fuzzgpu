#pragma once

#include "arch.h"
#include "emulator.h"

#include "functional/local_mem.h"
#include "functional/stubs.h"

#include <cstdint>

#ifndef DIFFTEST_ISA_EMULATOR
#error Do not include the functional core when using non-difftest configuration
#endif

#include <difftest_gpu/types.hh>

namespace vortex {

class Socket;

// compatible with the vortex::Emulator
class Core {
  uint32_t core_id_;
  Socket *socket_;
  Arch arch_;
#ifdef EXT_TCU_ENABLE
  TensorUnit::Ptr tensor_unit_;
#endif
  Emulator emulator_;
  std::shared_ptr<LocalMem> local_mem_;
  std::vector<std::shared_ptr<MemCoalescer>> mem_coalescers_;

  PoolAllocator<instr_trace_t, 64> trace_pool_;

  void execute_warp_impl(unsigned warp_id, difftest::Trace *trace_out,
                         bool per_uop);

public:
  Core(unsigned core_id, Socket *socket, const Arch &arch,
       const DCRS &dcrs);

  uint32_t id() const {
    return core_id_;
  }

  Socket *socket() const {
    return socket_;
  }

  Emulator &emulator() {
    return emulator_;
  }

  void reset();

  const std::shared_ptr<LocalMem> &local_mem() const {
    return local_mem_;
  }

  const std::shared_ptr<MemCoalescer> &mem_coalescer(uint32_t idx) const {
    return mem_coalescers_.at(idx);
  }

  auto &trace_pool() {
    return trace_pool_;
  }

  void attach_ram(RAM *ram);

  bool barrier(uint32_t bar_id, uint32_t count, uint32_t wid);

  void resume(uint32_t wid);

  void execute_warp(unsigned warp_id, difftest::Trace *trace_out);

  void execute_warp_per_uop(unsigned warp_id, difftest::Trace *trace_out);

#ifdef EXT_TCU_ENABLE
  TensorUnit::Ptr &tensor_unit() {
    return tensor_unit_;
  }
#endif

public:
  //
  // perf counters (compatible with the vortex::Emulator)
  //
  struct PerfStats {
    uint64_t cycles;
    uint64_t instrs;
    uint64_t sched_idle;
    uint64_t sched_stalls;
    uint64_t ibuf_stalls;
    uint64_t scrb_stalls;
    uint64_t opds_stalls;
    uint64_t scrb_alu;
    uint64_t scrb_fpu;
    uint64_t scrb_lsu;
    uint64_t scrb_sfu;
    uint64_t scrb_csrs;
    uint64_t scrb_wctl;
#ifdef EXT_V_ENABLE
    uint64_t vinstrs;
    uint64_t scrb_vpu;
#endif
#ifdef EXT_TCU_ENABLE
    uint64_t scrb_tcu;
#endif
    uint64_t ifetches;
    uint64_t loads;
    uint64_t stores;
    uint64_t ifetch_latency;
    uint64_t load_latency;

    PerfStats()
      : cycles(0)
      , instrs(0)
      , sched_idle(0)
      , sched_stalls(0)
      , ibuf_stalls(0)
      , scrb_stalls(0)
      , opds_stalls(0)
      , scrb_alu(0)
      , scrb_fpu(0)
      , scrb_lsu(0)
      , scrb_sfu(0)
      , scrb_csrs(0)
      , scrb_wctl(0)
#ifdef EXT_V_ENABLE
      , vinstrs(0)
      , scrb_vpu(0)
#endif
#ifdef EXT_TCU_ENABLE
      , scrb_tcu(0)
#endif
      , ifetches(0)
      , loads(0)
      , stores(0)
      , ifetch_latency(0)
      , load_latency(0) {
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
