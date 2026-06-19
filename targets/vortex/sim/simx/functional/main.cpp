#include <cassert>

#include "arch.h"
#include "functional/processor_impl.h"

using namespace vortex;

void cluster_barrier_callback(uint32_t, uint32_t,
                              uint32_t, void *) {
  assert(0);
}
int main() {

  // create processor
  ProcessorImpl processor(4, 64, 32);

  return 0;
}