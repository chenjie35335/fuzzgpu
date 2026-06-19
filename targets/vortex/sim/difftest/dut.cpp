#include "processor.h"
#include <VX_config.h>
#include <VX_types.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mem.h>
#include <unistd.h>
#include <util.h>

#ifdef ENABLE_COVERAGE // VM_COVERAGE
#include "verilated_cov.h"
#endif

#include <difftest_gpu/api.hh>
#include <difftest_gpu/dut.hh>
#include <difftest_gpu/isa_emulator.hh>
#include <difftest_gpu/util/thread_throttle.hh>

#include "processor.h"

namespace difftest {
class DUTImpl {
public:
  std::string coverage_dat_file;
  vortex::RAM ram;
  vortex::Processor processor;
  bool inited{false};

  DUTImpl()
    : ram(0, MEM_PAGE_SIZE) {
    processor.attach_ram(&ram);
  }
};

DUT::DUT(const std::string &coverage_dat_file)
  : opaque_(new DUTImpl()) {
  auto impl = static_cast<DUTImpl *>(opaque_);
  impl->coverage_dat_file = coverage_dat_file;
  set_dut_callbacks(
      // read pmem callback
      [this](uint64_t paddr, uint8_t *out, size_t len) {
        this->pmem_read(difftest::ScopeIdentifier(difftest::ScopeIdentifier::global()), paddr, out, len);
      },
      [this]() {
        auto impl = static_cast<DUTImpl *>(opaque_);
        // pause callback
        impl->processor.get_thread_throttle().pause();
      },
      [this]() {
        // resume callback
        auto impl = static_cast<DUTImpl *>(opaque_);
        impl->processor.get_thread_throttle().resume();
      },
      [this]() {
        // stop callback
        auto impl = static_cast<DUTImpl *>(opaque_);
        impl->processor.get_thread_throttle().terminate();
      });
}
DUT::~DUT() {
  delete static_cast<DUTImpl *>(opaque_);
}

void DUT::pmem_write(const ScopeIdentifier &scope, Word paddr, const uint8_t *buf,
                     size_t len) {
  auto impl = static_cast<DUTImpl *>(opaque_);
  switch (scope.scope()) {
  case MemScope::Global:
    impl->ram.enable_acl(false);
    impl->ram.write(buf, paddr, len);
    impl->ram.enable_acl(true);
    break;
  default:
    std::cerr << "DUT::pmem_write: unsupported scope "
              << unsigned(scope.scope()) << std::endl;
    std::abort();
  }
}

void DUT::pmem_read(const ScopeIdentifier &scope, Word paddr, uint8_t *out,
                    size_t len) {
  auto impl = static_cast<DUTImpl *>(opaque_);
  switch (scope.scope()) {
  case MemScope::Global:
    impl->ram.enable_acl(false);
    impl->ram.read(out, paddr, len);
    impl->ram.enable_acl(true);
    break;
  default:
    std::cerr << "DUT::pmem_read: unsupported scope "
              << unsigned(scope.scope()) << std::endl;
    std::abort();
  }
}

void DUT::new_blocks(unsigned num_blocks, unsigned num_warps_per_block) {
  auto impl = static_cast<DUTImpl *>(opaque_);

  if (!impl->inited) {
    impl->inited = true;
    auto isa_emu = &difftest::get_isa_emulator();
    // setup base DCRs
    const uint64_t startup_addr(STARTUP_ADDR);
    impl->processor.dcr_write(VX_DCR_BASE_STARTUP_ADDR0, startup_addr & 0xffffffff);
    isa_emu->send_command(VX_DCR_BASE_STARTUP_ADDR0, startup_addr & 0xffffffff);
#if (XLEN == 64)
    impl->processor.dcr_write(VX_DCR_BASE_STARTUP_ADDR1, startup_addr >> 32);
    isa_emu->send_command(VX_DCR_BASE_STARTUP_ADDR1, startup_addr >> 32);
#endif
    impl->processor.dcr_write(VX_DCR_BASE_MPM_CLASS, 0);
    isa_emu->send_command(VX_DCR_BASE_MPM_CLASS, 0);
  }

  difftest::new_warps_event(num_warps_per_block, num_blocks);
}

void DUT::run() {
  auto impl = static_cast<DUTImpl *>(opaque_);
  impl->processor.run();

#ifdef ENABLE_COVERAGE
  if (impl->coverage_dat_file != "") {
    VerilatedCov::write(impl->coverage_dat_file.c_str());
    std::cout << "Coverage saved to " << impl->coverage_dat_file << std::endl;
  }
#endif
}

} // namespace difftest