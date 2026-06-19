#include "functional/processor_impl.h"

namespace vortex {

ProcessorImpl::ProcessorImpl(unsigned nr_cores, unsigned nr_warps, unsigned nr_threads)
  : arch_(nr_threads, nr_warps, nr_cores)
  , clusters_(arch_.num_clusters()) {

    // create clusters
  for (uint32_t i = 0; i < arch_.num_clusters(); ++i) {
    clusters_.at(i) = std::make_shared<Cluster>(i, this, arch_, dcrs_);
  }
}

void ProcessorImpl::attach_ram(RAM* ram) {
  for (auto &cluster : clusters_) {
    cluster->attach_ram(ram);
  }
}

void ProcessorImpl::reset() {
  // without platform reset, we explicitly reset all clusters
  for(auto &Cluster : clusters_) {
    Cluster->reset();
  }
}

} // namespace vortex