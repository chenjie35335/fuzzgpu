#pragma once

#include <difftest_gpu/types.hh>

namespace vortex {

using difftest::WarpGID;

using WarpLID = unsigned; // local to the core
using CoreGID = unsigned;

WarpLID warp_gid_2_lid(WarpGID warp_gid);
CoreGID warp_gid_2_core_gid(WarpGID warp_gid);

};