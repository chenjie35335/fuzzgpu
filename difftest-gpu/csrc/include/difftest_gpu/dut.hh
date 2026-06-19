#pragma once

#include <difftest_gpu/types.hh>

namespace difftest {

/// @brief Opaque wrapper around the RTL DUT shared library.
class __attribute__((visibility("default"))) DUT {
  ///< DUT implementation pointer.
  void *opaque_;

public:
  /// @brief Open the DUT and optional coverage output file.
  DUT(const std::string &coverage_dat_file);
  ~DUT();

  /// @brief Write DUT physical memory in the given scope.
  void pmem_write(const ScopeIdentifier &scope, Word paddr, const uint8_t *buf,
                  size_t len);
  /// @brief Read DUT physical memory in the given scope.
  void pmem_read(const ScopeIdentifier &scope, Word paddr, uint8_t *out,
                 size_t len);

#if defined(ARCH_VENTUS)
  /// @brief Launch new Ventus blocks with kernel metadata.
  void new_blocks(const unsigned kernel_size[3], unsigned num_warps_per_block,
                  unsigned wavefront_size, unsigned sgpr_usage,
                  unsigned vgpr_usage, Word metadata_base = 0,
                  Word lds_size = 0x1000, Word pds_size = 0x1000,
                  Word pds_base = 0x78000000, Word start_pc = 0x80000000);
#elif defined(ARCH_VORTEX)
  /// @brief Launch new Vortex blocks.
  void new_blocks(unsigned num_blocks, unsigned num_warps_per_block);
#endif

  /// @brief Run the DUT until it yields or terminates.
  void run();
};

} // namespace difftest
