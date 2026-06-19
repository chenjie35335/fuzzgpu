#include "functional/socket.h"
#include "functional/cluster.h"

namespace vortex {

Socket::Socket(uint32_t socket_id,
               Cluster *cluster,
               const Arch &arch,
               const DCRS &dcrs)
  : socket_id_(socket_id)
  , cluster_(cluster)
  , cores_(arch.socket_size()) {
  auto cores_per_socket = cores_.size();
  // create cores
  for (uint32_t i = 0; i < cores_per_socket; ++i) {
    uint32_t core_id = socket_id * cores_per_socket + i;
    cores_.at(i) = std::make_shared<Core>(core_id, this, arch, dcrs);
  }
}

void Socket::reset() {
  // without platform reset, we explicitly reset all cores
  for(auto &core : cores_) {
    core->reset();
  }
}
void Socket::attach_ram(RAM* ram) {
  for (auto core : cores_) {
    core->attach_ram(ram);
  }
}
void Socket::barrier(uint32_t bar_id, uint32_t count, uint32_t core_id) {
  cluster_->barrier(bar_id, count, socket_id_ * cores_.size() + core_id);
}

void Socket::resume(uint32_t core_index) {
  cores_.at(core_index)->resume(-1);
}

} // namespace vortex