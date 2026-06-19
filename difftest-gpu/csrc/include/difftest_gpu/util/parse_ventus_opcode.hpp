#pragma once

#include "difftest_gpu/types.hh"
#include "difftest_gpu/util/riscv_encoding.hh"
#include "difftest_gpu/util/riscv_fp.hh"

namespace difftest {

#if defined(ARCH_VENTUS)
#define MATCH_INSN(_name)                                                      \
  ((insn_bits & riscv::mask_##_name) == riscv::match_##_name)

struct VentusOpcode {
  Opcode opcode{Opcode::Others};
  uint32_t csr_addr;
  bool applied_tmask{false}; // applied to rdst
  bool dummy_writeback{false};
};
static VentusOpcode parse_ventus_opcode(uint32_t insn_bits) {
  VentusOpcode ret;

  const auto opcode = insn_bits & 0x7f;
  const auto funct3 = (insn_bits >> 12) & 0x7;
  const auto funct7 = (insn_bits >> 25) & 0x7f;

  // test if it's a scalar/vector floating point instruction
  static constexpr uint32_t OPCODE_FP = 0b1010011;
  static constexpr uint32_t OPCODE_V = 0b1010111;
  static constexpr uint32_t OPCODE_STORE_FP = 0b0100111;
  static constexpr uint32_t OPCODE_LOAD_FP = 0b0000111;
  static constexpr uint32_t FUNCT3_OPIVV = 0b001;
  static constexpr uint32_t FUNCT3_OPFVV = 0b001;
  static constexpr uint32_t FUNCT3_OPFVF = 0b101;
  static constexpr uint32_t OPCODE_SYSTEM = 0b1110011;
  static constexpr uint32_t OPCODE_custom0 = 0b0001011;
  static constexpr uint32_t OPCODE_custom1 = 0b0101011;
  static constexpr uint32_t OPCODE_custom2 = 0b1011011;
  static constexpr uint32_t OPCODE_custom3 = 0b1111011;
  switch (opcode) {
  case OPCODE_SYSTEM:
    switch (funct3) {
    case 0b001:   // CSRRW
    case 0b101: { // CSRRWI
      const unsigned rd = (insn_bits >> 7) & 0x1f;
      if (!rd) {
        break;
      }
    }
      [[fallthrough]];
    case 0b010:
    case 0b011:
    case 0b110:
    case 0b111:
      ret.csr_addr = (insn_bits >> 20) & 0xfff;
      ret.opcode = Opcode::CtrlStatusRead;
      break;
    default:
      break;
    }
    break;

  case OPCODE_custom0:
  case OPCODE_custom1:
  case OPCODE_custom2:
  case OPCODE_custom3:
    ret.applied_tmask = true; // filtered later
    break;

  case OPCODE_STORE_FP:
  case OPCODE_LOAD_FP:
    switch (funct3) {
    case 0b001:
    case 0b010:
    case 0b011:
    case 0b100:
      break; // scalar FP
    default:
      ret.applied_tmask = true; // vector load/store & reserved
    }
    break;

  case OPCODE_V: {            // vector
    ret.applied_tmask = true; // filtered later
  } break;

  // FIXME add tmaskctrl
  default:;
  }

  if (
      // thread merge
      MATCH_INSN(VFMV_F_S) || MATCH_INSN(VFMV_S_F) || MATCH_INSN(VSETIVLI) ||

// single lane move
#if defined(ARCH_VENTUS)
// VMV.X.S and VMV.S.X are modified as broadcast in Ventus
#else
      MATCH_INSN(VMV_X_S) || MATCH_INSN(VMV_S_X) ||
#endif

      // thread ctrl
      MATCH_INSN(VSETVLI) || MATCH_INSN(VSETVL) || MATCH_INSN(SETRPC) ||
      MATCH_INSN(JOIN) || MATCH_INSN(BARRIER) || MATCH_INSN(ENDPRG) ||

      // prefix
      MATCH_INSN(REGEXT) || MATCH_INSN(REGEXTI)) {
    ret.applied_tmask = false;
  }

  if (MATCH_INSN(JAL)) {
    ret.opcode = Opcode::Call;
  } else if (MATCH_INSN(JALR)) {
    const unsigned rd = (insn_bits >> 7) & 0x1f;
    const unsigned rs1 = (insn_bits >> 15) & 0x1f;
    const bool is_call = (rd == 1), is_ret = (rs1 == 1); // ra
    if (is_call && is_ret) {
      ret.opcode = Opcode::RetCall;
    } else if (is_call) {
      ret.opcode = Opcode::Call;
    } else if (is_ret) {
      ret.opcode = Opcode::Ret;
    }
  }

  if (is_float_point(insn_bits)) {
    ret.opcode = Opcode::FloatingPoint;
  }

#if 0 // unused
  bool thread_merge=
      MATCH_INSN(VFMV_F_S) || MATCH_INSN(VFMV_S_F) ||
      MATCH_INSN(VFMV_V_F) || MATCH_INSN(VMERGE_VIM) ||
      MATCH_INSN(VMERGE_VVM) || MATCH_INSN(VMERGE_VXM);
#endif

  if (MATCH_INSN(VMAND_MM) || MATCH_INSN(VMANDN_MM) || MATCH_INSN(VMNAND_MM) ||
      MATCH_INSN(VMNOR_MM) || MATCH_INSN(VMOR_MM) || MATCH_INSN(VMORN_MM) ||
      MATCH_INSN(VMXNOR_MM) || MATCH_INSN(VMXOR_MM) ||

// since we have no thread-level barrier sync, and ventus issue join several times to
// pop the simt stack, which complicates the tmask tracking, we simply not
// mark join as tmask writer
#if 0
      MATCH_INSN(JOIN) ||
#endif

      MATCH_INSN(VBEQ) || MATCH_INSN(VBNE) || MATCH_INSN(VBLT) ||
      MATCH_INSN(VBLTU) || MATCH_INSN(VBGE) || MATCH_INSN(VBGEU) ||
      MATCH_INSN(BARRIER)) {
    ret.opcode = Opcode::TmaskWrite;
  }

  // no microarch writeback signal for these instructions
  if (
      // store
      MATCH_INSN(VSE32_V) || MATCH_INSN(VSOXEI32_V) || MATCH_INSN(VSSE32_V) ||
      MATCH_INSN(VSB12_V) || MATCH_INSN(VSB_V) || MATCH_INSN(VSH12_V) ||
      MATCH_INSN(VSH_V) || MATCH_INSN(VSW12_V) || MATCH_INSN(VSW_V) ||
      //MATCH_INSN(SC_W) || MATCH_INSN(SC_D) ||
      MATCH_INSN(FSD) ||
      MATCH_INSN(FSW) || MATCH_INSN(SB) || MATCH_INSN(SH) || MATCH_INSN(SW) ||
      MATCH_INSN(SD) ||

      // prefix
      MATCH_INSN(REGEXT) || MATCH_INSN(REGEXTI) ||

      // thread ctrl w/o rdst
      MATCH_INSN(ENDPRG) || MATCH_INSN(JOIN) || MATCH_INSN(VBEQ) ||
      MATCH_INSN(VBNE) || MATCH_INSN(VBLT) || MATCH_INSN(VBLTU) ||
      MATCH_INSN(VBGE) || MATCH_INSN(VBGEU) ||

      // branch
      MATCH_INSN(BEQ) || MATCH_INSN(BNE) || MATCH_INSN(BLT) ||
      MATCH_INSN(BLTU) || MATCH_INSN(BGE) || MATCH_INSN(BGEU)

      // fence has wb signal (to x0)
  ) {
    ret.dummy_writeback = true;
  }

  return ret;
}

static bool is_insn_prefix(uint32_t insn_bits) {
#if defined(ARCH_VENTUS)
  return MATCH_INSN(REGEXT) || MATCH_INSN(REGEXTI);
#elif defined(ARCH_VORTEX)
  return false;
#endif
}

#endif // defined(ARCH_VENTUS)

} // namespace difftest