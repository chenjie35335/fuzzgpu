#pragma once

#include "difftest_gpu/types.hh"
#include "difftest_gpu/util/riscv_encoding.hh"

namespace difftest {

namespace riscv {
[[maybe_unused]] static constexpr uint32_t OPCODE_FP = 0b1010011;
[[maybe_unused]] static constexpr uint32_t OPCODE_V = 0b1010111;
[[maybe_unused]] static constexpr uint32_t FUNCT3_OPFVV = 0b001;
[[maybe_unused]] static constexpr uint32_t FUNCT3_OPFVF = 0b101;
[[maybe_unused]] static constexpr uint32_t OPCODE_custom0 = 0b0001011;
} // namespace riscv

/**
 * @brief Not including float-point load/store
 */
[[maybe_unused]] static bool is_float_point(uint64_t insn_bits) {
  // test if it's a scalar/vector floating point instruction
  const auto opcode = insn_bits & 0x7f;
  const auto funct3 = (insn_bits >> 12) & 0x7;
  switch (opcode) {
  case riscv::OPCODE_FP:
    return true;

#if defined(ARCH_VENTUS)
  case riscv::OPCODE_custom0: {
    const auto funct6 = (insn_bits >> 26) & 0x3f;
    if ((funct3 == 0b110 || funct3 == 0b100) &&
        (funct6 == 0b000010 || funct6 == 0b000011)) { // vfexp vftta
      return true;
    }
  } break;
#elif defined(ARCH_VORTEX)
  case riscv::OPCODE_custom0: { // custom0
    const auto funct7 = (insn_bits >> 25) & 0x7f;
    if (funct7 == 2) { // vx_wmma
      return true;
    }
  } break;
#endif

  case riscv::OPCODE_V: { // vector
    switch (funct3) {
    case riscv::FUNCT3_OPFVV:
    case riscv::FUNCT3_OPFVF:
      return true;
    default:;
    }
  } break;
  default:;
  }
  return false;
}

[[maybe_unused]] static NumericFormat get_rd_float_point_format(uint64_t insn_bits,
                                               unsigned vsew) {
  const auto opcode = insn_bits & 0x7f;
  if (opcode == riscv::OPCODE_FP) {

#define get_rd_float_point_format_INSN_MATCH(_name)                            \
  ((insn_bits & riscv::mask_##_name) == riscv::match_##_name)

    // TODO: Zfh, etc.
    if (get_rd_float_point_format_INSN_MATCH(FCLASS_S) ||
        get_rd_float_point_format_INSN_MATCH(FCLASS_D) ||
        get_rd_float_point_format_INSN_MATCH(FMV_X_W) ||
        get_rd_float_point_format_INSN_MATCH(FMV_X_D) ||
        get_rd_float_point_format_INSN_MATCH(FEQ_S) ||
        get_rd_float_point_format_INSN_MATCH(FEQ_D) ||
        get_rd_float_point_format_INSN_MATCH(FLT_S) ||
        get_rd_float_point_format_INSN_MATCH(FLT_D) ||
        get_rd_float_point_format_INSN_MATCH(FLE_S) ||
        get_rd_float_point_format_INSN_MATCH(FLE_D) ||
        get_rd_float_point_format_INSN_MATCH(FCVT_W_S) ||
        get_rd_float_point_format_INSN_MATCH(FCVT_WU_S) ||
        get_rd_float_point_format_INSN_MATCH(FCVT_L_S) ||
        get_rd_float_point_format_INSN_MATCH(FCVT_LU_S) ||
        get_rd_float_point_format_INSN_MATCH(FCVT_L_D) ||
        get_rd_float_point_format_INSN_MATCH(FCVT_LU_D) ||
        get_rd_float_point_format_INSN_MATCH(FCVT_W_D) ||
        get_rd_float_point_format_INSN_MATCH(FCVT_WU_D)) {
      return NumericFormat::INT_FROM_FP;
    }

    const auto fmt = (insn_bits >> 25) & 0x3;
    switch (fmt) {
    case 0b00:
      return NumericFormat::FP_S;
    case 0b01:
      return NumericFormat::FP_D;
    case 0b10:
      return NumericFormat::FP_H;
    case 0b11:
      return NumericFormat::FP_Q;
    default:;
    }

    std::abort();

  } else if (opcode == riscv::OPCODE_V) {
    const auto funct3 = (insn_bits >> 12) & 0x7;
    if (funct3 == riscv::FUNCT3_OPFVV || funct3 == riscv::FUNCT3_OPFVF) {
      switch (vsew) {
      case 16:
        return NumericFormat::FP_H;
      case 32:
        return NumericFormat::FP_S;
      case 64:
        return NumericFormat::FP_D;
      default:
        std::abort();
      }
    }
  }

  return NumericFormat::INT;
}

} // namespace difftest