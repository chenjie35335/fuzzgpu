#pragma once

#include "types.h"
#include <difftest_gpu/types.hh>

namespace difftest {

difftest::RegId make_reg_id(const vortex::RegOpd &r);
vortex::RegOpd make_reg_opd(const difftest::RegId &r);

VarBitset make_varbitset(const vortex::ThreadMask &tmask);
VarBitset make_varbitset(const vortex::WarpMask &wmask);

} // namespace difftest