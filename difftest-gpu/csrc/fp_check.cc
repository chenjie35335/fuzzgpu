
#include "fp_check.hh"

namespace difftest {

bool is_nan_fp16(uint16_t x) {
  uint16_t exp_mask = 0x7C00;  // 0111110000000000
  uint16_t frac_mask = 0x03FF; // 0000001111111111

  uint16_t exp = x & exp_mask;
  uint16_t frac = x & frac_mask;

  return (exp == exp_mask) && (frac != 0);
}

bool is_inf_fp16(uint16_t x) {
  uint16_t exp_mask = 0x7C00;
  uint16_t frac_mask = 0x03FF;

  uint16_t exp = x & exp_mask;
  uint16_t frac = x & frac_mask;

  return (exp == exp_mask) && (frac == 0);
}

bool is_nan_fp32(uint32_t x) {
  uint32_t exp_mask = 0x7F800000;  // 01111111100000000000000000000000
  uint32_t frac_mask = 0x007FFFFF; // 00000000011111111111111111111111

  uint32_t exp = x & exp_mask;
  uint32_t frac = x & frac_mask;

  return (exp == exp_mask) && (frac != 0);
}

bool is_inf_fp32(uint32_t x) {
  uint32_t exp_mask = 0x7F800000;
  uint32_t frac_mask = 0x007FFFFF;

  uint32_t exp = x & exp_mask;
  uint32_t frac = x & frac_mask;

  return (exp == exp_mask) && (frac == 0);
}

bool is_nan_fp64(uint64_t x) {
  uint64_t exp_mask = 0x7FF0000000000000ull; // 011111111111
  uint64_t frac_mask = 0x000FFFFFFFFFFFFFULL;
  uint64_t exp = x & exp_mask;
  uint64_t frac = x & frac_mask;
  return (exp == exp_mask) && (frac != 0);
}

bool is_inf_fp64(uint64_t x) {
  uint64_t exp_mask = 0x7FF0000000000000ull;
  uint64_t frac_mask = 0x000FFFFFFFFFFFFFULL;
  uint64_t exp = x & exp_mask;
  uint64_t frac = x & frac_mask;
  return (exp == exp_mask) && (frac == 0);
}

void classify_fp(uint64_t x, NumericFormat fmt, bool &is_nan, bool &is_inf) {
  switch (fmt) {
  case NumericFormat::FP_H:
    is_nan = is_nan_fp16(static_cast<uint16_t>(x));
    is_inf = is_inf_fp16(static_cast<uint16_t>(x));
    break;
  case NumericFormat::FP_S:
    is_nan = is_nan_fp32(static_cast<uint32_t>(x));
    is_inf = is_inf_fp32(static_cast<uint32_t>(x));
    break;
  case NumericFormat::FP_D:
    is_nan = is_nan_fp64(x);
    is_inf = is_inf_fp64(x);
    break;
  default:
    std::abort();
  }

}

} // namespace difftest