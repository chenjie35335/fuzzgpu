#include "functional/id.hh"
#include "constants.h"

namespace vortex {
WarpLID warp_gid_2_lid(WarpGID warp_gid) {
  return warp_gid % NUM_WARPS;
}

CoreGID warp_gid_2_core_gid(WarpGID warp_gid) {
  return warp_gid / NUM_WARPS;
}

} // namespace vortex