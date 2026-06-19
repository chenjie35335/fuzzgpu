#pragma once

#include <difftest_gpu/types.hh>

namespace difftest {

/// @brief Opaque wrapper around the target ISA emulator implementation.
class ISAEmulator {
  ///< Emulator implementation pointer.
  void *opaque_;

public:
  /// @brief Construct the target emulator.
  ISAEmulator();
  ~ISAEmulator();
  ISAEmulator(const ISAEmulator &) = delete;

  /// @brief Return architecture properties reported by the emulator.
  Arch get_arch() const;

#ifdef ARCH_VORTEX
  /// @brief Bootstrap a Vortex program at @p boot_paddr.
  void bootstrap(uint64_t boot_paddr);
#endif
#ifdef ARCH_VENTUS
  /// @brief Bootstrap a Ventus program with kernel launch metadata.
  void bootstrap(uint64_t boot_vaddr, const uint32_t kernel_size[3],
                 unsigned num_wavefront_per_wavegroup, unsigned wavefront_size,
                 uint64_t sgpr_usage, uint64_t vgpr_usage);
  /// @brief Bootstrap a Ventus program with explicit metadata/LDS/PDS regions.
  void bootstrap(uint64_t boot_vaddr, const uint32_t kernel_size[3],
                 unsigned num_wavefront_per_wavegroup, unsigned wavefront_size,
                 uint64_t sgpr_usage, uint64_t vgpr_usage,
                 uint64_t metadata_base, uint64_t lds_size, uint64_t pds_size,
                 uint64_t pds_base);
#endif

  /// @brief Reset emulator state.
  void reset();

  /// @brief Return the current thread mask for @p warp_gid.
  ThreadMask get_thread_mask(WarpGID warp_gid) const;

  /// @brief Return whether all emulator work has terminated.
  bool terminated() const;

  /// @brief Return whether @p warp_gid can execute now.
  bool schedulable(WarpGID warp_gid);

  /// @brief Schedule to execute a warp
  void execute_warp(WarpGID warp_gid, difftest::Trace *trace_out);

  /// @brief Emulate one load/store instruction and return memory traces.
  std::vector<MemTrace> emulate_load_store(WarpGID warp_gid,
                                           const uint8_t *insn,
                                           size_t insn_bytes, bool &trap_taken);

  /// @brief Return memory scope for a virtual address in one warp.
  ScopeIdentifier get_mem_scope(WarpGID warp_gid, uint64_t vaddr);

  /// @brief Return PCs executed by the emulator.
  const std::unordered_map<uint64_t, bool> &pc_executed() const;
  /// @brief Return the total number of executed instructions.
  size_t instr_executed() const;

  /// @brief Write physical memory in the given scope.
  void pmem_write(const ScopeIdentifier &scope, Word paddr, const uint8_t *buf,
                  size_t len);
  /// @brief Read physical memory in the given scope.
  void pmem_read(const ScopeIdentifier &scope, Word paddr, uint8_t *out,
                 size_t len);
  /// @brief Return whether host-side code may write this memory scope.
  bool host_writeable(const ScopeIdentifier &scope) const;

  /// @brief Return whether a physical memory access is illegal.
  bool illegal_paddr(uint64_t paddr, size_t access_size, bool is_store) const;

  /// @brief Translate a virtual address for a warp memory access.
  uint64_t translate_vaddr(WarpGID warp_gid, uint64_t ea,
                           MemAccessType mem_access_type);

  /// @brief Write a register for active lanes.
  void reg_write(WarpGID warp_gid, const ThreadMask &tmask, const RegId &rd,
                 const std::vector<uint64_t> &rd_data);

  /// @brief Read one register value for one thread lane.
  uint64_t reg_read(WarpGID warp_gid, ThreadLID thread_lid, const RegId &rs);

  /// @brief Set the program counter for one warp.
  void pc_write(WarpGID warp_gid, uint64_t pc);

  /// @brief Append emulator statistics to @p stats.
  void get_statistics(std::unordered_map<std::string, size_t> &stats) const;

#ifdef ARCH_VORTEX
  /// @brief Send an MMIO command to the Vortex emulator.
  void send_command(uint32_t addr, uint32_t dat);
#endif

#ifdef ARCH_VENTUS
  /// @brief Set the RPC field of the top Ventus SIMT stack entry.
  void ventus_set_simt_stack_top_rpc(WarpGID warp_gid, uint64_t rpc);
  /// @brief Set the else-PC field of the top Ventus SIMT stack entry.
  void ventus_set_simt_stack_top_else_pc(WarpGID warp_gid, uint64_t else_pc);
#endif
};

} // namespace difftest
