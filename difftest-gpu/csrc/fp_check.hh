#pragma once

#include "difftest_gpu/types.hh"

namespace difftest {

bool is_nan_fp16(uint16_t x);
bool is_inf_fp16(uint16_t x);
bool is_nan_fp32(uint32_t x);
bool is_inf_fp32(uint32_t x);
bool is_nan_fp64(uint64_t x);
bool is_inf_fp64(uint64_t x);

void classify_fp(uint64_t x, NumericFormat fmt, bool &is_nan, bool &is_inf);

} // namespace difftest