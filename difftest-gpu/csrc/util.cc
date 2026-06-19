#include "util.hh"

namespace difftest {

bool is_null_reg(const RegId &rd) {
#if defined(ARCH_VENTUS)
  if (rd.type != RegType::Vec &&
      rd.idx == 0) // FP and INT are using the same index
    return true;
#else
  if (rd.type == RegType::Int && rd.idx == 0)
    return true;
#endif
  return false;
}

} // namespace difftest
