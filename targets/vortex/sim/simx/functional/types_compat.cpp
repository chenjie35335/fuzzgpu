#include <unordered_map>
#include <iostream>

#include "spdlog/spdlog.h"

#include "functional/types_compat.h"

namespace difftest {
static const std::unordered_map<const vortex::RegType, const difftest::RegType> vortex_to_reg_type = {
    std::make_pair(vortex::RegType::Integer, difftest::RegType::Int),
    std::make_pair(vortex::RegType::Float, difftest::RegType::Float),
#ifdef EXT_V_ENABLE
    std::make_pair(vortex::RegType::Vector, difftest::RegType::RegVec),
#endif
};

static auto flipped(const std::unordered_map<const vortex::RegType, const difftest::RegType> &vortex_to_reg_type) {
  std::unordered_map<difftest::RegType, vortex::RegType> ret;
  for (const auto &p : vortex_to_reg_type) {
    ret[p.second] = p.first;
  }
  return ret;
}
static std::unordered_map<difftest::RegType, vortex::RegType> reg_type_to_vortex = flipped(vortex_to_reg_type);

difftest::RegId make_reg_id(const vortex::RegOpd &r) {
  RegId ret;
  auto it = vortex_to_reg_type.find(r.type);
  if (it == vortex_to_reg_type.end()) {
    std::cerr << "Invalid reg type " << r.type << std::endl;
    std::abort();
  }
  ret.type = it->second;
  ret.idx = r.idx;
  ret.sew = XLEN; // vortex RTL does not have vec for now
  return ret;
}

vortex::RegOpd make_reg_opd(const difftest::RegId &r) {
  vortex::RegOpd ret;
  auto it = reg_type_to_vortex.find(r.type);
  if (it == reg_type_to_vortex.end()) {
    spdlog::error("Invalid reg type {}", unsigned(r.type));
    std::abort();
  }
  ret.type = it->second;
  ret.idx = r.idx;
  return ret;
}

VarBitset make_varbitset(const vortex::ThreadMask &tmask) {
  VarBitset vb(tmask.size());
  for (size_t i = 0; i < tmask.size(); ++i) {
    if (tmask.test(i)) {
      vb.set(i);
    }
  }
  return vb;
}

VarBitset make_varbitset(const vortex::WarpMask &wmask) {
  VarBitset vb(wmask.size());
  for (size_t i = 0; i < wmask.size(); ++i) {
    if (wmask.test(i)) {
      vb.set(i);
    }
  }
  return vb;
}

} // namespace difftest