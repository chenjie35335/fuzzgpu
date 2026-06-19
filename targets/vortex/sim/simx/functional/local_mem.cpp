#include "functional/local_mem.h"

namespace vortex {

LocalMem::LocalMem(const Config &config)
  : ram_(config.capacity) {
  uint32_t total_lines = config.capacity / config.line_size;
  line_bits_ = log2ceil(total_lines);
}

void LocalMem::reset() {
  //--
}

void LocalMem::read(void *data, uint64_t addr, uint32_t size) {
  auto l_addr = to_local_addr(addr);
  ram_.read(data, l_addr, size);
}

void LocalMem::write(const void *data, uint64_t addr, uint32_t size) {
  auto l_addr = to_local_addr(addr);
  ram_.write(data, l_addr, size);
}

} // namespace vortex