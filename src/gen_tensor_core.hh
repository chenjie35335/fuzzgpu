#pragma once

namespace fuzzgpu {

#if defined(ARCH_VORTEX)

/// @brief Singleton helper for Vortex tensor-core register layout.
class vortex_tensor_config {
  ///< Matrix tiling steps in each dimension.
  uint32_t m_steps;
  uint32_t n_steps;
  uint32_t k_steps;

  ///< Number of result registers reserved by tensor-core operations.
  uint32_t NRB;

  ///< Global tensor configuration instance.
  static std::unique_ptr<vortex_tensor_config> g_vortex_tensor_config;

public:
  /// @brief Initialize tensor-core layout from target configuration.
  vortex_tensor_config();

  /// @brief Return first destination register used by tensor results.
  unsigned get_rd_base();
  /// @brief Return one-past-last destination register used by tensor results.
  unsigned get_rd_end();

  /// @brief Return the global tensor configuration.
  static vortex_tensor_config &instance();
};

#endif

} // namespace fuzzgpu
