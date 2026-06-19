#include "functional/cluster.h"

namespace vortex {

Cluster::Cluster(uint32_t cluster_id,
                 ProcessorImpl *processor,
                 const Arch &arch,
                 const DCRS &dcrs)
  : processor_(processor)
  , sockets_(NUM_SOCKETS)
  , barriers_(arch.num_barriers(), 0)
  , cores_per_socket_(arch.socket_size())
  , global_barrier_success_count_(0) {
  uint32_t sockets_per_cluster = sockets_.size();

  // create sockets

  for (uint32_t i = 0; i < sockets_per_cluster; ++i) {
    uint32_t socket_id = cluster_id * sockets_per_cluster + i;
    sockets_.at(i) = std::make_shared<Socket>(socket_id, this, arch, dcrs);
  }
}

void Cluster::reset() {
  // without platform reset, we explicitly reset all sockets
  for(auto &socket : sockets_) {
    socket->reset();
  }
  for (auto &barrier : barriers_) {
    barrier.reset();
  }
  global_barrier_success_count_ = 0;
}

void Cluster::attach_ram(RAM *ram) {
  for (auto &socket : sockets_) {
    socket->attach_ram(ram);
  }
}

void Cluster::barrier(uint32_t bar_id, uint32_t count, uint32_t core_id) {
  auto &barrier = barriers_.at(bar_id);

  auto sockets_per_cluster = sockets_.size();
  auto cores_per_socket = cores_per_socket_;

  uint32_t cores_per_cluster = sockets_per_cluster * cores_per_socket;
  uint32_t local_core_id = core_id % cores_per_cluster;
  barrier.set(local_core_id);

  DP(3, "*** Suspend core #" << core_id << " at barrier #" << bar_id);

  if (barrier.count() == (size_t)count) {
    // resume all suspended cores
    for (uint32_t s = 0; s < sockets_per_cluster; ++s) {
      for (uint32_t c = 0; c < cores_per_socket; ++c) {
        uint32_t i = s * cores_per_socket + c;
        if (barrier.test(i)) {
          DP(3, "*** Resume core #" << i << " at barrier #" << bar_id);
          sockets_.at(s)->resume(c);
        }
      }
    }
    barrier.reset();
    ++global_barrier_success_count_;
  }
}

} // namespace vortex