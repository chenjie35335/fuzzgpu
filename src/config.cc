#include "config.hh"

namespace fuzzgpu {

Config *Config::g_instance_;

void Config::dump() const {
  spdlog::info("Arch name: {}", arch.name);
  spdlog::info("Num threads per warp: {}", arch.nr_threads_per_warp);
#if defined(ARCH_VORTEX)
  spdlog::info("Vortex max. warps per core (arch): {}",
               arch.vortex_nr_warps_per_core);
#elif defined(ARCH_VENTUS)
  spdlog::info("Ventus max. warps per block (arch): {}",
               arch.ventus_nr_warp_per_block);
#endif
  spdlog::info("Global memory size: {} bytes", arch.gmem_size);

  spdlog::info("Compiler name: {}", compiler_name);
  spdlog::info("Compilers path: {}", compilers_path);

  spdlog::info("Num gen blocks: {}", num_genblocks);
  spdlog::info("Num gen warps per block: {}", num_genwarps_per_block);
  spdlog::info("Num total gen warps: {}", num_genwarps);

  spdlog::info("Max instructions per bb: {}", max_num_fuzzing_instrs_per_bb);
  spdlog::info("Min instructions per bb: {}", min_num_fuzzing_instrs_per_bb);
  spdlog::info("Max total instructions: {}", max_num_instrs);
  spdlog::info("Min tmask flips: {}", min_tmask_flips);

  spdlog::info("Force LDS probability: {}", force_lds_prob);
  spdlog::info("Branch taken probability: {}", branch_taken_prob);
  spdlog::info("Branch major taken probability: {}", branch_major_taken_prob);
  spdlog::info("Branch divergent probability: {}", branch_divergent_prob);

  spdlog::info("Instr pool node size: {}", instr_pool_node_size);
  spdlog::info("Max alloc tries: {}", max_alloc_tries);
  spdlog::info("Max resample times: {}", max_resample_times);
}

} // namespace fuzzgpu
