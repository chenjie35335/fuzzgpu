#include <cassert>
#include <cstring>
#include <memory>
#include <unordered_map>

#include "arch.h"
#include "dcrs.h"
#include "emulator.h"
#include "functional/id.hh"
#include "functional/processor_impl.h"
#include "functional/types_compat.h"
#include "instr_trace.h"

#include <difftest_gpu/isa_emulator.hh>

#define MAX_MEM_ADDR 0xffffffff // see ../emulator.cpp mmu_.attach()

namespace difftest {

class ISAEmulatorOpaque {
  vortex::ProcessorImpl proc_;
  const vortex::Arch &arch_;
  vortex::RAM dev_mem_;
  std::vector<std::shared_ptr<vortex::Core>> cores_;
  std::unordered_map<uint64_t, bool> pc_set;
  size_t instr_count{0};

  friend class ISAEmulator;

public:
  ISAEmulatorOpaque()
    : proc_(NUM_CORES /* per cluster */,
            NUM_WARPS /* per core */,
            NUM_THREADS /* per core */)
    , arch_(proc_.arch())
    , dev_mem_(0, MEM_PAGE_SIZE)
    , cores_(arch_.num_clusters() * vortex::NUM_SOCKETS * arch_.socket_size()) {

    proc_.attach_ram(&dev_mem_);

    // build the direct mapping from global core_id
    for (unsigned cluster_id = 0; cluster_id < arch_.num_clusters(); ++cluster_id) {
      for (unsigned socket_id = 0; socket_id < vortex::NUM_SOCKETS; ++socket_id) {
        // note that core_id in vortex is global, while warp_id and thread_id are local to core
        // to avoid confusion, we use *_lid/*_gid to indicate local/global id
        for (unsigned core_lid = 0; core_lid < arch_.socket_size(); ++core_lid) {
          unsigned core_gid = cluster_id * vortex::NUM_SOCKETS * arch_.socket_size() + socket_id * arch_.socket_size() + core_lid;
          cores_[core_gid] = proc_.clusters().at(cluster_id)->sockets().at(socket_id)->cores().at(core_lid);
        }
      }
    }
  }
};

#define DECL_IMPL() auto impl = reinterpret_cast<ISAEmulatorOpaque *>(opaque_)

ISAEmulator::ISAEmulator()
  : opaque_(new ISAEmulatorOpaque()) {
}

ISAEmulator::~ISAEmulator() {
  delete reinterpret_cast<ISAEmulatorOpaque *>(opaque_);
}

Arch ISAEmulator::get_arch() const {
  DECL_IMPL();
  assert(LMEM_BASE_ADDR > USER_BASE_ADDR);
  return Arch(
      "vortex",
      XLEN,
      false,
      USER_BASE_ADDR,
      STARTUP_ADDR,
      std::make_pair(0,
                     MAX_MEM_ADDR),
      std::make_pair(USER_BASE_ADDR, LMEM_BASE_ADDR - 1),
      MEM_PAGE_SIZE,
      vortex::MAX_NUM_REGS,
      vortex::MAX_NUM_REGS,
      impl->arch_.num_cores() * impl->arch_.num_clusters(),
      impl->arch_.num_warps(),
      vortex::NUM_SOCKETS * impl->arch_.socket_size(),
      impl->arch_.num_clusters(),
      impl->arch_.num_cores() * impl->arch_.num_clusters() * impl->arch_.num_warps(),
      impl->arch_.num_threads(),

      false,
      Arch::vortex |
          Arch::rv32i |
#if XLEN == 64
          Arch::rv64i |
#endif
#if 0 // RTL didn't support yet
          Arch::rv32a | Arch::rv64a |
#endif
#ifdef EXT_M_ENABLE
          Arch::rv32m |
#if XLEN == 64
          Arch::rv64m |
#endif
#endif
#ifdef EXT_F_ENABLE
          Arch::rv32f |
#if XLEN == 64
          Arch::rv64f |
#endif
#endif
#ifdef EXT_D_ENABLE
          Arch::rv32d |
#if XLEN == 64
          Arch::rv64d |
#endif
#endif
#ifdef EXT_ZICOND_ENABLE
          Arch::zicond |
#endif
#ifdef EXT_TCU_ENABLE
          Arch::vortex_tcu |
#endif
#ifdef GBAR_ENABLE
          Arch::vortex_gbar |
#endif
          0,
      VLEN,
      false,
      NUM_BARRIERS,
      {
          std::make_pair(uint64_t(IO_COUT_ADDR), (uint64_t(IO_COUT_ADDR) + IO_COUT_SIZE)),
      });
}

void ISAEmulator::bootstrap(uint64_t boot_paddr) {
  DECL_IMPL();
  impl->proc_.dcrs().write(VX_DCR_BASE_STARTUP_ADDR0, boot_paddr & 0xffffffff);
  impl->proc_.dcrs().write(VX_DCR_BASE_STARTUP_ADDR1, boot_paddr >> 32);
  impl->proc_.dcrs().write(VX_DCR_BASE_MPM_CLASS, 0);
  impl->proc_.reset();
}

void ISAEmulator::reset() {
  DECL_IMPL();
  impl->proc_.reset();
  impl->pc_set.clear();
  impl->instr_count = 0;
}

ThreadMask ISAEmulator::get_thread_mask(WarpGID warp_gid) const {
  DECL_IMPL();
  const auto core_gid = vortex::warp_gid_2_core_gid(warp_gid);
  const auto warp_lid = vortex::warp_gid_2_lid(warp_gid);
  return (ThreadMask)make_varbitset(impl->cores_.at(core_gid)->emulator().get_thread_mask(warp_lid));
}

bool ISAEmulator::terminated() const {
  DECL_IMPL();
  bool running = false;
  for (const auto &core : impl->cores_) {
    running |= core->emulator().running();
  }
  return !running;
}

bool ISAEmulator::schedulable(WarpGID warp_gid) {
  DECL_IMPL();
  const auto core_gid = vortex::warp_gid_2_core_gid(warp_gid);
  const auto warp_lid = vortex::warp_gid_2_lid(warp_gid);
  return impl->cores_.at(core_gid)->emulator().schedulable(warp_lid);
}

/// @brief Schedule to execute a warp
/// @param core_gid
/// @param warp_lid
/// @param trace_out
void ISAEmulator::execute_warp(WarpGID warp_gid, difftest::Trace *trace_out) {
  DECL_IMPL();
  const auto core_gid = vortex::warp_gid_2_core_gid(warp_gid);
  const auto warp_lid = vortex::warp_gid_2_lid(warp_gid);
  impl->cores_[core_gid]->execute_warp_per_uop(warp_lid, trace_out);
  impl->pc_set[trace_out->pc] = true;
  impl->instr_count++;
}

const std::unordered_map<uint64_t, bool> &ISAEmulator::pc_executed() const {
  DECL_IMPL();
  return impl->pc_set;
}
size_t ISAEmulator::instr_executed() const {
  DECL_IMPL();
  return impl->instr_count;
}

void ISAEmulator::pmem_write(const ScopeIdentifier &scope, Word paddr, const uint8_t *buf,
                             size_t len) {

  DECL_IMPL();

  switch (scope.scope()) {
  case MemScope::Global:
    impl->dev_mem_.write(buf, paddr, len);
#ifdef VM_ENABLE
    // TODO: only flush the dirty TLB entry
    emu.tlb_shot_down();
#endif
    break;

  case MemScope::Workgroup:
  case MemScope::Warp:
    std::abort();
    break;

  default:
    assert(0);
  }
}

void ISAEmulator::pmem_read(const ScopeIdentifier &scope, Word paddr, uint8_t *out, size_t len) {
  DECL_IMPL();
  switch (scope.scope()) {
  case MemScope::Global:
    impl->dev_mem_.read(out, paddr, len);
    break;

  case MemScope::Workgroup: {
    const auto core_gid = vortex::warp_gid_2_core_gid(scope.warp_gid());
    impl->cores_[core_gid]->local_mem()->read(out, paddr, len);
  } break;

  case MemScope::Warp:
    std::abort();

  default:
    assert(0);
  }
}

bool ISAEmulator::host_writeable(const ScopeIdentifier &scope) const {
  switch (scope.scope()) {
  case MemScope::Global:
    return true;
  default:
    return false;
  }
}

bool ISAEmulator::illegal_paddr(uint64_t paddr, size_t access_size,
                                bool) const {
  return (paddr + access_size - 1) > MAX_MEM_ADDR;
}

uint64_t ISAEmulator::translate_vaddr(WarpGID, uint64_t ea,
                                      MemAccessType) {
  return ea;
}

std::vector<MemTrace> ISAEmulator::emulate_load_store(WarpGID, const uint8_t *, size_t, bool &) {
  std::abort(); // it's implemented in fuzzgpu emulate_load_store_(), we may unify them later.
}

ScopeIdentifier ISAEmulator::get_mem_scope(WarpGID warp_gid, uint64_t vaddr) {
  if (vaddr >= LMEM_BASE_ADDR &&
      vaddr < LMEM_BASE_ADDR + (1ULL << LMEM_LOG_SIZE)) {
    return ScopeIdentifier(ScopeIdentifier::workgroup(), warp_gid);
  } else {
    return ScopeIdentifier(ScopeIdentifier::global());
  }
}

static vortex::ThreadMask make_vortex_threadmask(const ThreadMask &tmask) {
  vortex::ThreadMask vmask(tmask.size());
  for (size_t i = 0; i < tmask.size(); ++i) {
    if (tmask.test(i)) {
      vmask.set(i);
    }
  }
  return vmask;
}

void ISAEmulator::reg_write(WarpGID warp_gid, const ThreadMask &tmask, const RegId &rd,
                            const std::vector<uint64_t> &rd_data) {
  DECL_IMPL();
  const auto core_gid = vortex::warp_gid_2_core_gid(warp_gid);
  const auto warp_lid = vortex::warp_gid_2_lid(warp_gid);
  impl->cores_[core_gid]->emulator().set_reg(warp_lid, make_vortex_threadmask(tmask), make_reg_opd(rd), rd_data);
}

uint64_t ISAEmulator::reg_read(WarpGID warp_gid, ThreadLID thread_lid, const RegId &rs) {
  DECL_IMPL();
  const auto core_gid = vortex::warp_gid_2_core_gid(warp_gid);
  const auto warp_lid = vortex::warp_gid_2_lid(warp_gid);
  return impl->cores_[core_gid]->emulator().get_reg(warp_lid, thread_lid, make_reg_opd(rs));
}

void ISAEmulator::pc_write(WarpGID warp_gid, uint64_t pc) {
  DECL_IMPL();
  const auto core_gid = vortex::warp_gid_2_core_gid(warp_gid);
  const auto warp_lid = vortex::warp_gid_2_lid(warp_gid);
  impl->cores_.at(core_gid)->emulator().set_pc(warp_lid, pc);
}

void ISAEmulator::send_command(uint32_t addr, uint32_t dat) {
  DECL_IMPL();
  std::cerr << "**** DCR write: addr=0x" << std::hex << addr
            << ", dat=0x" << dat << std::dec << std::endl;
  impl->proc_.dcrs().write(addr, dat);
}

void ISAEmulator::get_statistics(std::unordered_map<std::string, size_t> &stats) const {
    DECL_IMPL();
    
    // Aggregate statistics from all cores
    uint64_t total_divergent_branches = 0;
    uint64_t total_branches = 0;
    uint64_t max_simt_stack_depth = 0;
    uint64_t total_barrier_success = 0;
    uint64_t total_global_barrier_success = 0;
    
    // Collect all barrier counts for distribution statistics
    std::vector<uint32_t> all_local_barrier_counts;
    std::vector<uint32_t> all_global_barrier_counts;
    
    for (const auto &core : impl->cores_) {
        total_divergent_branches += core->emulator().divergent_branches();
        total_branches += core->emulator().total_branches();
        auto depth = core->emulator().max_simt_stack_depth();
        if (depth > max_simt_stack_depth) {
            max_simt_stack_depth = depth;
        }
        total_barrier_success += core->emulator().barrier_success_count();
        
        // Collect local barrier counts
        const auto &local_counts = core->emulator().get_local_barrier_counts();
        all_local_barrier_counts.insert(all_local_barrier_counts.end(), 
                                         local_counts.begin(), local_counts.end());
        
        // Collect global barrier counts
        const auto &global_counts = core->emulator().get_global_barrier_counts();
        all_global_barrier_counts.insert(all_global_barrier_counts.end(),
                                          global_counts.begin(), global_counts.end());
    }
    
    // Aggregate global barrier statistics from all clusters
    for (const auto &cluster : impl->proc_.clusters()) {
        total_global_barrier_success += cluster->global_barrier_success_count();
    }
    
    stats["thread_divergent_branches"] = total_divergent_branches;
    stats["thread_total_branches"] = total_branches;
    stats["max_simt_stack_depth"] = max_simt_stack_depth;
    stats["local_barrier_success_count"] = total_barrier_success;
    stats["global_barrier_success_count"] = total_global_barrier_success;
    
    // Compute and add local barrier distribution statistics (box plot metrics)
    if (!all_local_barrier_counts.empty()) {
        std::sort(all_local_barrier_counts.begin(), all_local_barrier_counts.end());
        size_t n = all_local_barrier_counts.size();
        
        // Basic stats
        stats["local_barrier_count_n"] = n;
        stats["local_barrier_count_min"] = all_local_barrier_counts.front();
        stats["local_barrier_count_max"] = all_local_barrier_counts.back();
        
        uint64_t sum = 0;
        for (const auto &c : all_local_barrier_counts) sum += c;
        stats["local_barrier_count_sum"] = sum;
        
        // Percentiles (stored as fixed-point * 1000 for precision)
        auto get_percentile = [&](const std::vector<uint32_t> &data, double p) -> size_t {
            size_t idx = static_cast<size_t>(p * (data.size() - 1));
            return data[idx];
        };
        
        stats["local_barrier_count_median"] = get_percentile(all_local_barrier_counts, 0.5);
        stats["local_barrier_count_q1"] = get_percentile(all_local_barrier_counts, 0.25);
        stats["local_barrier_count_q3"] = get_percentile(all_local_barrier_counts, 0.75);
        
        // IQR and fences (as integers)
        size_t q1 = stats["local_barrier_count_q1"];
        size_t q3 = stats["local_barrier_count_q3"];
        size_t iqr = q3 - q1;
        stats["local_barrier_count_iqr"] = iqr;
        stats["local_barrier_count_lower_fence"] = (q1 > iqr * 3 / 2) ? (q1 - iqr * 3 / 2) : 0;
        stats["local_barrier_count_upper_fence"] = q3 + iqr * 3 / 2;
    }
    
    // Compute and add global barrier distribution statistics (box plot metrics)
    if (!all_global_barrier_counts.empty()) {
        std::sort(all_global_barrier_counts.begin(), all_global_barrier_counts.end());
        size_t n = all_global_barrier_counts.size();
        
        // Basic stats
        stats["global_barrier_count_n"] = n;
        stats["global_barrier_count_min"] = all_global_barrier_counts.front();
        stats["global_barrier_count_max"] = all_global_barrier_counts.back();
        
        uint64_t sum = 0;
        for (const auto &c : all_global_barrier_counts) sum += c;
        stats["global_barrier_count_sum"] = sum;
        
        // Percentiles
        auto get_percentile = [&](const std::vector<uint32_t> &data, double p) -> size_t {
            size_t idx = static_cast<size_t>(p * (data.size() - 1));
            return data[idx];
        };
        
        stats["global_barrier_count_median"] = get_percentile(all_global_barrier_counts, 0.5);
        stats["global_barrier_count_q1"] = get_percentile(all_global_barrier_counts, 0.25);
        stats["global_barrier_count_q3"] = get_percentile(all_global_barrier_counts, 0.75);
        
        // IQR and fences (as integers)
        size_t q1 = stats["global_barrier_count_q1"];
        size_t q3 = stats["global_barrier_count_q3"];
        size_t iqr = q3 - q1;
        stats["global_barrier_count_iqr"] = iqr;
        stats["global_barrier_count_lower_fence"] = (q1 > iqr * 3 / 2) ? (q1 - iqr * 3 / 2) : 0;
        stats["global_barrier_count_upper_fence"] = q3 + iqr * 3 / 2;
    }
}

} // namespace difftest
