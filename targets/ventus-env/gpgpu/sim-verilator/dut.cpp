#include "kernel.hpp"
#include "ventus_rtlsim.h"
#include <cassert>
#include <chrono>
#include <cstring>
#include <fmt/core.h>
#include <fstream>
#include <iostream>
#include <memory>
#include <spdlog/spdlog.h>

#include "verilated.h"
#ifdef ENABLE_COVERAGE
#include "verilated_cov.h"
#endif

#include <difftest_gpu/api.hh>
#include <difftest_gpu/dut.hh>
#include <difftest_gpu/isa_emulator.hh>
#include <difftest_gpu/util/thread_throttle.hh>

typedef struct {
    std::filesystem::path datafile;
    ventus_rtlsim_t* sim;
} kernel_load_data_callback_t;

namespace difftest {

class DUTImpl {
public:
    std::string coverage_dat_file;
    ventus_rtlsim_t* sim { nullptr };

    difftest::ThreadThrottle thread_throttle;

    DUTImpl() {
        const char* verilator_argv[] = {
            "+verilator+seed+10086",
        };

        //
        // simulation config
        //
        ventus_rtlsim_config_t sim_config;
        ventus_rtlsim_get_default_config(&sim_config);

        sim_config.log.console.level = "trace";
        sim_config.sim_time_max = ~0ull;
        sim_config.pmem.auto_alloc = true;
        sim_config.waveform.enable = false;
        sim_config.waveform.time_begin = 0; //135030 ;
        sim_config.waveform.time_end = -1; // 50000;
        sim_config.verilator.argc = sizeof(verilator_argv) / sizeof(verilator_argv[0]);
        sim_config.verilator.argv = verilator_argv;
        sim_config.snapshot.enable = false; // this may cause the emulator thread differs from the main() thread
        sim = ventus_rtlsim_init(&sim_config);
    }
};

DUT::DUT(const std::string& coverage_dat_file)
    : opaque_(new DUTImpl()) {
    auto impl = static_cast<DUTImpl*>(opaque_);
    impl->coverage_dat_file = coverage_dat_file;
    difftest::set_dut_callbacks(
        // read pmem callback
        [this](uint64_t paddr, uint8_t* out, size_t len) {
            this->pmem_read(ScopeIdentifier::global(), paddr, out, len);
        },
        // pause callback
        [this]() {
            auto impl = static_cast<DUTImpl*>(opaque_);
            impl->thread_throttle.pause();
        },
        // resume callback
        [this]() {
            auto impl = static_cast<DUTImpl*>(opaque_);
            impl->thread_throttle.resume();
        },
        // stop callback
        [this]() {
            auto impl = static_cast<DUTImpl*>(opaque_);
            impl->thread_throttle.terminate();
        }
    );
}
DUT::~DUT() { delete static_cast<DUTImpl*>(opaque_); }

void DUT::pmem_write(const ScopeIdentifier& scope, Word paddr, const uint8_t* buf, size_t len) {
    auto impl = static_cast<DUTImpl*>(opaque_);
    switch (scope.scope()) {
    case MemScope::Global:
    case MemScope::Warp: // the warp mem is physically in global memory, but partitioned logically per-warp
        ventus_rtlsim_pmemcpy_h2d(impl->sim, paddr, buf, len);
        break;
        // FIXME: support other scopes
    default:
        spdlog::error("DUT::pmem_write: unsupported scope {}", unsigned(scope.scope()));
        std::abort();
    }
}

void DUT::pmem_read(const ScopeIdentifier& scope, Word paddr, uint8_t* out, size_t len) {
    auto impl = static_cast<DUTImpl*>(opaque_);
    switch (scope.scope()) {
    case MemScope::Global:
    case MemScope::Warp: // the warp mem is physically in global memory, but partitioned logically per-warp
        ventus_rtlsim_pmemcpy_d2h(impl->sim, out, paddr, len);
        break;
    default:
        spdlog::error("DUT::pmem_read: unsupported scope {}", unsigned(scope.scope()));
        std::abort();
    }
}

static void kernel_load_data_callback(const metadata_t*) {
    // unused
}

void DUT::new_blocks(
    const unsigned kernel_size[3], unsigned num_warps_per_block,
    unsigned wavefront_size, unsigned sgpr_usage, unsigned vgpr_usage,
    Word metadata_base, Word lds_size, Word pds_size, Word pds_base,
    Word start_pc
) {
    auto impl = static_cast<DUTImpl*>(opaque_);
    metadata_t metadata = {};  // Zero-initialize all fields
    metadata.name = "fuzz_kernel";
    metadata.kernel_size[0] = kernel_size[0];
    metadata.kernel_size[1] = kernel_size[1];
    metadata.kernel_size[2] = kernel_size[2];
    metadata.wf_size = wavefront_size;
    metadata.wg_size = num_warps_per_block; // per workgroup
    metadata.metaDataBaseAddr = metadata_base;
    metadata.ldsSize = lds_size;
    metadata.pdsSize = pds_size;
    metadata.sgprUsage = sgpr_usage;
    metadata.vgprUsage = vgpr_usage;
    metadata.pdsBaseAddr = pds_base;
    metadata.startaddr = start_pc;

    std::cout << "new_blocks: kernel_size=[" << kernel_size[0] << ", " << kernel_size[1] << ", " << kernel_size[2]
              << "], num_warps_per_block=" << num_warps_per_block << ", wavefront_size=" << wavefront_size
              << ", sgpr_usage=" << sgpr_usage << ", vgpr_usage=" << vgpr_usage << "\n";

    metadata.data = new kernel_load_data_callback_t { .datafile = "", .sim = impl->sim };
    ventus_rtlsim_add_kernel__delay_data_loading(impl->sim, /*copy */ &metadata, kernel_load_data_callback, nullptr);

    difftest::new_warps_event(num_warps_per_block, kernel_size[0] * kernel_size[1] * kernel_size[2]);
}

void DUT::run() {
    auto impl = static_cast<DUTImpl*>(opaque_);

    // Prevent $stop from aborting - instead it will set gotFinish() flag
    Verilated::fatalOnError(false);
    Verilated::traceEverOn(true);
    ventus_rtlsim_icache_invalidate(impl->sim);

    //
    // Run simulation, each step stands for 1 simulation time-unit
    //
    const ventus_rtlsim_step_result_t* result;
    while (!Verilated::gotFinish()) {
        if (!impl->thread_throttle.poll())
            break;
        result = ventus_rtlsim_step(impl->sim);
        if (result->error || result->idle || result->time_exceed) {
            spdlog::info(
                "Simulation stopped: error={}, idle={}, time_exceed={}", result->error, result->idle,
                result->time_exceed
            );
            break;
        }
    }

    // Save coverage data (gotFinish() may be set by $stop or $finish)
    if (Verilated::gotFinish()) {
        spdlog::info("Verilog $stop/$finish detected");
    }
#ifdef ENABLE_COVERAGE
    if (impl->coverage_dat_file != "") {
        VerilatedCov::write(impl->coverage_dat_file.c_str());
        spdlog::info("Coverage saved to {}", impl->coverage_dat_file);
    }
#endif
}

} // namespace difftest