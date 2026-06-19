#include <iostream>
#include <difftest_gpu/isa_emulator.hh>
#include <difftest_gpu/types.hh>

int main() {
    difftest::ISAEmulator emulator;
    auto arch = emulator.get_arch();

    std::cout << "arch_name=" << arch.name << std::endl;
    std::cout << "word_bits=" << arch.word_bits << std::endl;
    std::cout << "nr_threads_per_warp=" << arch.nr_threads_per_warp << std::endl;
#ifdef ARCH_VORTEX
    std::cout << "vortex_nr_warps_per_core=" << arch.vortex_nr_warps_per_core << std::endl;
    std::cout << "vortex_nr_cores_per_cluster=" << arch.vortex_nr_cores_per_cluster << std::endl;
    std::cout << "vortex_nr_total_clusters=" << arch.vortex_nr_total_clusters << std::endl;
    std::cout << "vortex_nr_total_warps=" << arch.vortex_nr_total_warps << std::endl;
    std::cout << "num_barriers=" << arch.num_barriers << std::endl;
#else
#endif

    return 0;
}
