#pragma once

#include <memory>
#include <vector>
#define MAGIC_ENUM_RANGE_MAX 1024
#include "spdlog/fmt/fmt.h"
#include <magic_enum/magic_enum.hpp>
#include <variant>

#include "config.hh"
namespace fuzzgpu {

enum class riscv_instr_format_t {
  J_FORMAT = 0,
  U_FORMAT,
  I_FORMAT,
  I_CSR_FORMAT,      // special I format for CSR*
  I_UIMM_CSR_FORMAT, // special I format for CSR*
  I_VV_FORMAT,
  I_VX_FORMAT,
  B_FORMAT,
  B_VV,
  R_FORMAT,
  R_RM_FORMAT,       // funct3 for RM
  R_ORDERING_FORMAT, // funct5 for memory ordering
  R_VVV,
  R_VIV,
  R_XXV,
  R_VXV,
  R_VXX,
  R_VXV_MOP_MEW_NF,
  R_V3XV_MOP_MEW_NF,
  R_VXX_MOP_MEW_NF,
  R_V3XX_MOP_MEW_NF,
  S_FORMAT,
  S_XV, // rs1 & vs1 (id(rs1)=id(vs1)), vs2
  S_VV,
  R4_RM_FORMAT,
  FMT_FMT_FORMAT, // vortex wmma
  // Compressed instruction format
  CI_FORMAT,
  CB_FORMAT,
  CJ_FORMAT,
  CR_FORMAT,
  CA_FORMAT,
  CL_FORMAT,
  CS_FORMAT,
  CSS_FORMAT,
  CIW_FORMAT,
  // Vector instruction format
  VSET_FORMAT,
  VA_FORMAT,
  VS2_FORMAT, // op vd,vs2
  VL_FORMAT,
  VS_FORMAT,
};

enum class riscv_instr_category_t {
  LOAD = 0,
  STORE,
  SHIFT,
  ARITHMETIC,
  LOGICAL,
  COMPARE,
  BRANCH,
  JUMP,
  SYNCH,
  SYSTEM,
  COUNTER,
  CSR,
  CHANGELEVEL,
  TRAP,
  INTERRUPT,
  // TODO import vector_category
  AMO, // (last one)
  THREAD_CTRL,
  THREAD_MERGE,
  REG_CTRL,
  TENSOR,
  PSEUDO,
  Count
};

enum class riscv_instr_group_t {
  RV32I = 0,
  RV64I,
  RV32M,
  RV64M,
  RV32A,
  RV64A,
  RV32F,
  RV32FC,
  RV64F,
  RV32D,
  RV32DC,
  RV64D,
  RV32C,
  RV64C,
  RV128I,
  RV128C,
  RV32V,
  RV32B,
  RV64V,
  RV64B,
  RV32X,
  RV64X,
  RVV,
  RVL,
  ZICOND,
  VORTEX
};

enum class riscv_instr_name_t {
  // RV32I instructions
  LUI = 0,
  AUIPC,
  JAL,
  JALR,
  BEQ,
  BNE,
  BLT,
  BGE,
  BLTU,
  BGEU,
  LB,
  LH,
  LW,
  LBU,
  LHU,
  SB,
  SH,
  SW,
  ADDI,
  SLTI,
  SLTIU,
  XORI,
  ORI,
  ANDI,
  SLLI,
  SRLI,
  SRAI,
  ADD,
  SUB,
  SLL,
  SLT,
  SLTU,
  XOR,
  SRL,
  SRA,
  OR,
  AND,
  FENCE,
  FENCE_I,
  ECALL,
  EBREAK,
  CSRRW,
  CSRRS,
  CSRRC,
  CSRRWI,
  CSRRSI,
  CSRRCI,
  // RV32B instructions
  ANDN,
  ORN,
  XNOR,
  GORC,
  SLO,
  SRO,
  ROL,
  ROR,
  SBCLR,
  SBSET,
  SBINV,
  SBEXT,
  GREV,
  SLOI,
  SROI,
  RORI,
  SBCLRI,
  SBSETI,
  SBINVI,
  SBEXTI,
  GORCI,
  GREVI,
  CMIX,
  CMOV,
  FSL,
  FSR,
  FSRI,
  CLZ,
  CTZ,
  PCNT,
  SEXT_B,
  SEXT_H,
  CRC32_B,
  CRC32_H,
  CRC32_W,
  CRC32C_B,
  CRC32C_H,
  CRC32C_W,
  CLMUL,
  CLMULR,
  CLMULH,
  MIN,
  MAX,
  MINU,
  MAXU,
  SHFL,
  UNSHFL,
  BDEP,
  BEXT,
  PACK,
  PACKU,
  BMATOR,
  BMATXOR,
  PACKH,
  BFP,
  SHFLI,
  UNSHFLI,
  // RV64B instructions
  ADDIWU,
  SLLIU_W,
  ADDWU,
  SUBWU,
  BMATFLIP,
  CRC32_D,
  CRC32C_D,
  ADDU_W,
  SUBU_W,
  SLOW,
  SROW,
  ROLW,
  RORW,
  SBCLRW,
  SBSETW,
  SBINVW,
  SBEXTW,
  GORCW,
  GREVW,
  SLOIW,
  SROIW,
  RORIW,
  SBCLRIW,
  SBSETIW,
  SBINVIW,
  GORCIW,
  GREVIW,
  FSLW,
  FSRW,
  FSRIW,
  CLZW,
  CTZW,
  PCNTW,
  CLMULW,
  CLMULRW,
  CLMULHW,
  SHFLW,
  UNSHFLW,
  BDEPW,
  BEXTW,
  PACKW,
  PACKUW,
  BFPW,
  // RV32M instructions
  MUL,
  MULH,
  MULHSU,
  MULHU,
  DIV,
  DIVU,
  REM,
  REMU,
  // RV64M instructions
  MULW,
  DIVW,
  DIVUW,
  REMW,
  REMUW,
  // RV32F instructions
  FLW,
  FSW,
  FMADD_S,
  FMSUB_S,
  FNMSUB_S,
  FNMADD_S,
  FADD_S,
  FSUB_S,
  FMUL_S,
  FDIV_S,
  FSQRT_S,
  FSGNJ_S,
  FSGNJN_S,
  FSGNJX_S,
  FMIN_S,
  FMAX_S,
  FCVT_W_S,
  FCVT_WU_S,
  FMV_X_W,
  FEQ_S,
  FLT_S,
  FLE_S,
  FCLASS_S,
  FCVT_S_W,
  FCVT_S_WU,
  FMV_W_X,
  // RV64F instructions
  FCVT_L_S,
  FCVT_LU_S,
  FCVT_S_L,
  FCVT_S_LU,
  // RV32D instructions
  FLD,
  FSD,
  FMADD_D,
  FMSUB_D,
  FNMSUB_D,
  FNMADD_D,
  FADD_D,
  FSUB_D,
  FMUL_D,
  FDIV_D,
  FSQRT_D,
  FSGNJ_D,
  FSGNJN_D,
  FSGNJX_D,
  FMIN_D,
  FMAX_D,
  FCVT_S_D,
  FCVT_D_S,
  FEQ_D,
  FLT_D,
  FLE_D,
  FCLASS_D,
  FCVT_W_D,
  FCVT_WU_D,
  FCVT_D_W,
  FCVT_D_WU,
  // RV64D instructions
  FCVT_L_D,
  FCVT_LU_D,
  FMV_X_D,
  FCVT_D_L,
  FCVT_D_LU,
  FMV_D_X,
  // RV64I instructions
  LWU,
  LD,
  SD,
  ADDIW,
  SLLIW,
  SRLIW,
  SRAIW,
  ADDW,
  SUBW,
  SLLW,
  SRLW,
  SRAW,
  // RV32C instructions
  C_LW,
  C_SW,
  C_LWSP,
  C_SWSP,
  C_ADDI4SPN,
  C_ADDI,
  C_LI,
  C_ADDI16SP,
  C_LUI,
  C_SRLI,
  C_SRAI,
  C_ANDI,
  C_SUB,
  C_XOR,
  C_OR,
  C_AND,
  C_BEQZ,
  C_BNEZ,
  C_SLLI,
  C_MV,
  C_EBREAK,
  C_ADD,
  C_NOP,
  C_J,
  C_JAL,
  C_JR,
  C_JALR,
  // RV64C instructions
  C_ADDIW,
  C_SUBW,
  C_ADDW,
  C_LD,
  C_SD,
  C_LDSP,
  C_SDSP,
  // RV128C instructions
  C_SRLI64,
  C_SRAI64,
  C_SLLI64,
  C_LQ,
  C_SQ,
  C_LQSP,
  C_SQSP,
  // RV32FC instructions
  C_FLW,
  C_FSW,
  C_FLWSP,
  C_FSWSP,
  // RV32DC instructions
  C_FLD,
  C_FSD,
  C_FLDSP,
  C_FSDSP,
  // RV32A instructions
  LR_W,
  SC_W,
  AMOSWAP_W,
  AMOADD_W,
  AMOAND_W,
  AMOOR_W,
  AMOXOR_W,
  AMOMIN_W,
  AMOMAX_W,
  AMOMINU_W,
  AMOMAXU_W,
  // RV64A instructions
  LR_D,
  SC_D,
  AMOSWAP_D,
  AMOADD_D,
  AMOAND_D,
  AMOOR_D,
  AMOXOR_D,
  AMOMIN_D,
  AMOMAX_D,
  AMOMINU_D,
  AMOMAXU_D,
  // Vector instructions
  VSETVL,
  VSETVLI,
  VSETIVLI,
  VADD,
  VSUB,
  VRSUB,
  VWADDU,
  VWSUBU,
  VWADD,
  VWSUB,
  VADC,
  VMADC,
  VSBC,
  VMSBC,
  VAND,
  VOR,
  VXOR,
  VSLL,
  VSRL,
  VSRA,
  VNSRL,
  VNSRA,
  VMSEQ,
  VMSNE,
  VMSLTU,
  VMSLT,
  VMSLEU,
  VMSLE,
  VMSGTU,
  VMSGT,
  VMINU,
  VMIN,
  VMAXU,
  VMAX,
  VMUL,
  VMULH,
  VMULHU,
  VMULHSU,
  VDIVU,
  VDIV,
  VREMU,
  VREM,
  VWMUL,
  VWMULU,
  VWMULSU,
  VMACC,
  VNMSAC,
  VMADD,
  VNMSUB,
  VWMACCU,
  VWMACC,
  VWMACCSU,
  VWMACCUS,
  // VQMACCU,
  // VQMACC,
  // VQMACCSU,
  // VQMACCUS,
  VMERGE,
  VMV,
  VSADDU,
  VSADD,
  VSSUBU,
  VSSUB,
  VAADDU,
  VAADD,
  VASUBU,
  VASUB,
  VSSRL,
  VSSRA,
  VNCLIPU,
  VNCLIP,
  // 14. Vector Floating-Point instructions
  VFADD,
  VFSUB,
  VFRSUB,
  VFMUL,
  VFDIV,
  VFRDIV,
  VFWMUL,
  VFMACC,
  VFNMACC,
  VFMSAC,
  VFNMSAC,
  VFMADD,
  VFNMADD,
  VFMSUB,
  VFNMSUB,
  VFWMACC,
  VFWNMACC,
  VFWMSAC,
  VFWNMSAC,
  VFSQRT_V,
  VFMIN,
  VFMAX,
  VFSGNJ,
  VFSGNJN,
  VFSGNJX,
  VMFEQ,
  VMFNE,
  VMFLT,
  VMFLE,
  VMFGT,
  VMFGE,
  VFCLASS_V,
  VFMERGE,
  VFMV,
  VFCVT_XU_F_V,
  VFCVT_X_F_V,
  VFCVT_F_XU_V,
  VFCVT_F_X_V,
  VFWCVT_XU_F_V,
  VFWCVT_X_F_V,
  VFWCVT_F_XU_V,
  VFWCVT_F_X_V,
  VFWCVT_F_F_V,
  VFNCVT_XU_F_W,
  VFNCVT_X_F_W,
  VFNCVT_F_XU_W,
  VFNCVT_F_X_W,
  VFNCVT_F_F_W,
  VFNCVT_ROD_F_F_W,
  // 15. Vector reduction instructions
  VREDSUM_VS,
  VREDMAXU_VS,
  VREDMAX_VS,
  VREDMINU_VS,
  VREDMIN_VS,
  VREDAND_VS,
  VREDOR_VS,
  VREDXOR_VS,
  VWREDSUMU_VS,
  VWREDSUM_VS,
  VFREDOSUM_VS,
  VFREDSUM_VS,
  VFREDMAX_VS,
  VFWREDOSUM_VS,
  VFWREDSUM_VS,
  // Vector mask instructions
  VMAND_MM,
  VMNAND_MM,
  VMANDNOT_MM,
  VMXOR_MM,
  VMOR_MM,
  VMNOR_MM,
  VMORNOT_MM,
  VMXNOR_MM,
  VPOPC_M,
  VFIRST_M,
  VMSBF_M,
  VMSIF_M,
  VMSOF_M,
  VIOTA_M,
  VID_V,
  // Vector permutation instructions
  VMV_X_S,
  VMV_S_X,
  VMV_V_V,
  VFMV_F_S,
  VFMV_S_F,
  VSLIDEUP,
  VSLIDEDOWN,
  VSLIDE1UP,
  VSLIDE1DOWN,
  VRGATHER,
  VCOMPRESS,
  VMV1R_V,
  VMV2R_V,
  VMV4R_V,
  VMV8R_V,
  // Vector load/store instructions
  VLE_V,
  VSE_V,
  VLSE_V,
  VSSE_V,
  VLXEI_V,
  VSXEI_V,
  VSUXEI_V,
  VLEFF_V,
  // Segmented load/store instruction
  VLSEGE_V,
  VSSEGE_V,
  VLSEGEFF_V,
  VLSSEGE_V,
  VSSSEGE_V,
  VLXSEGEI_V,
  VSXSEGEI_V,
  VSUXSEGEI_V,
  // Vector AMO instruction
  // EEW vector AMOs
  VAMOSWAPE_V,
  VAMOADDE_V,
  VAMOXORE_V,
  VAMOANDE_V,
  VAMOORE_V,
  VAMOMINE_V,
  VAMOMAXE_V,
  VAMOMINUE_V,
  VAMOMAXUE_V,
  // RVL
  VLE32_V,
  VLSE32_V,
  VLOXEI32_V,
  VSE8_V,
  VSE16_V,
  VSE32_V,
  VSSE32_V,
  VSOXEI32_V,

  VMFLE_VF,
  VSRA_VV,
  VMADD_VX,
  VNMSAC_VV,
  VMSGTU_VX,
  VDIV_VX,
  VFNMADD_VF,
  VMSNE_VV,
  VMSLTU_VX,
  VFDIV_VV,
  VFMAX_VV,
  VSRL_VI,
  VLHU12_V,
  VLW12_V,
  VMFLT_VF,
  VRSUB_VX,
  VSW12_V,
  VAND_VV,
  VDIVU_VV,
  VDIVU_VX,
  VFMADD_VV,
  VMSLEU_VX,
  VFNMSUB_VV,
  VFSGNJ_VV,
  VMFLE_VV,
  VMSGT_VI,
  VADD_VV,
  VFSGNJX_VV,
  VFDIV_VF,
  VFMAX_VF,
  VMFEQ_VV,
  VFNMSUB_VF,
  VLBU12_V,
  VMSGTU_VI,
  VMULH_VV,
  VSH_V,
  VMFLT_VV,
  VXOR_VX,
  VFMSUB_VV,
  VNMSUB_VV,
  VMANDN_MM,
  VMFEQ_VF,
  VLHU_V,
  VMV_V_X,
  VSB12_V,
  VSH12_V,
  VFMV_V_F,
  VFNMADD_VV,
  VMSLEU_VI,
  VXOR_VI,
  VFSUB_VF,
  VOR_VV,
  VSUB_VV,
  VMULHU_VV,
  VMSEQ_VV,
  VLW_V,
  VFMADD_VF,
  VFMIN_VV,
  VMAXU_VX,
  VSRA_VX,
  VMAX_VX,
  VMSLEU_VV,
  VLB_V,
  VMULHSU_VV,
  VOR_VX,
  VMERGE_VIM,
  VOR_VI,
  VREMU_VX,
  VNMSAC_VX,
  VMFNE_VF,
  VLH12_V,
  VFMIN_VF,
  VLBU_V,
  VMSEQ_VI,
  VMSLTU_VV,
  VSRA_VI,
  VMIN_VV,
  VRSUB_VI,
  VSLL_VX,
  VMSLE_VX,
  VFNMSAC_VV,
  VMSLE_VV,
  VMUL_VX,
  VMERGE_VVM,
  VADD_VI,
  VMAXU_VV,
  VADD_VX,
  VSW_V,
  VSRL_VX,
  VREMU_VV,
  VMV_V_I,
  VSRL_VV,
  VFMSAC_VF,
  VFMSAC_VV,
  VMSLT_VV,
  VSLL_VI,
  VREM_VX,
  VFSGNJX_VF,
  VMORN_MM,
  VMSGT_VX,
  VFRSUB_VF,
  VFRDIV_VF,
  VFNMSAC_VF,
  VMSNE_VI,
  VFMACC_VV,
  VNMSUB_VX,
  VFMUL_VV,
  VMINU_VX,
  VMINU_VV,
  VSUB_VX,
  VLH_V,
  VMULHU_VX,
  VMIN_VX,
  VFADD_VV,
  VLB12_V,
  VFSGNJN_VF,
  VFNMACC_VV,
  VXOR_VV,
  VMUL_VV,
  VMSNE_VX,
  VSLL_VV,
  VMSEQ_VX,
  VMERGE_VXM,
  VFNMACC_VF,
  VFMUL_VF,
  VMFGE_VF,
  VFSGNJN_VV,
  VFSGNJ_VF,
  VMFGT_VF,
  VMAX_VV,
  VMACC_VV,
  VFMACC_VF,
  VFCVT_RTZ_XU_F_V,
  VAND_VX,
  VFSUB_VV,
  VFADD_VF,
  VMSLE_VI,
  VMSLT_VX,
  VMULHSU_VX,
  VFMSUB_VF,
  VMACC_VX,
  VDIV_VV,
  VFCVT_RTZ_X_F_V,
  VMULH_VX,
  VMADD_VV,
  VAND_VI,
  VMFNE_VV,
  VREM_VV,
  VSB_V,

  // Supervisor instruction
  DRET,
  MRET,
  URET,
  SRET,
  WFI,
  SFENCE_VMA,
  // Zicond instructions
  CZERO_EQZ,
  CZERO_NEZ,

  // Pseudo instructions
  NOP,

  // Custom instructions
  // TODO add a way to import custom instructions here from
  // isa/custom/riscv_custom_instr_enum.py
  // You can add other instructions here
  // vortex instructions
  VX_SPLIT,
  VX_SPLIT_N,
  VX_JOIN,
  VX_PRED,
  VX_PRED_N,
  VX_TMC,
  VX_WSPAWN,
  VX_BAR,
  VX_WMMA,

  // Ventus insns
  VBEQ,
  VADD12_VI,
  VBGEU,
  VFEXP_V,
  VBLT,
  VBLTU,
  VFTTA_VV,
  VSUB12_VI,
  VBGE,
  VBNE,
  JOIN,
  BARRIER,
  ENDPRG,
  REGEXT,
  REGEXTI,
  SETRPC,

  Count
};

enum class riscv_reg_t {
  ZERO = 0,
  RA,
  SP,
  GP,
  TP,
  T0,
  T1,
  T2,
  S0,
  S1,
  A0,
  A1,
  A2,
  A3,
  A4,
  A5,
  A6,
  A7,
  S2,
  S3,
  S4,
  S5,
  S6,
  S7,
  S8,
  S9,
  S10,
  S11,
  T3,
  T4,
  T5,
  T6,
  Count
};

enum class riscv_fpr_t {
  FT0 = 0,
  FT1,
  FT2,
  FT3,
  FT4,
  FT5,
  FT6,
  FT7,
  FS0,
  FS1,
  FA0,
  FA1,
  FA2,
  FA3,
  FA4,
  FA5,
  FA6,
  FA7,
  FS2,
  FS3,
  FS4,
  FS5,
  FS6,
  FS7,
  FS8,
  FS9,
  FS10,
  FS11,
  FT8,
  FT9,
  FT10,
  FT11,
  Count
};

enum class riscv_vreg_t {
  V0 = 0,
  V1,
  V2,
  V3,
  V4,
  V5,
  V6,
  V7,
  V8,
  V9,
  V10,
  V11,
  V12,
  V13,
  V14,
  V15,
  V16,
  V17,
  V18,
  V19,
  V20,
  V21,
  V22,
  V23,
  V24,
  V25,
  V26,
  V27,
  V28,
  V29,
  V30,
  V31,
  Count
};

//  instruction's rm field
enum class riscv_rm_t {
  RNE = 0b000, // Round to Nearest, ties to Even
  RTZ = 0b001, // Round towards Zero
  RDN = 0b010, // Round Down (towards −∞)
  RUP = 0b011, // Round Up (towards +∞)
  RMM = 0b100, // Round to Nearest, ties to Max Magnitude
  DYN = 0b111, // Dynamic rounding mode
};

enum class riscv_gen_R {};
enum class riscv_gen_R_RM {};
enum class riscv_gen_R_ORDERING {};
enum class riscv_gen_R_VVV {};
enum class riscv_gen_R_VIV {};
enum class riscv_gen_R_XXV {};
enum class riscv_gen_R_VXV {};
enum class riscv_gen_R_VXX {};
enum class riscv_gen_R_VXV_MOP_MEW_NF {};
enum class riscv_gen_R_V3XV_MOP_MEW_NF {};
enum class riscv_gen_R_VXX_MOP_MEW_NF {};
enum class riscv_gen_R_V3XX_MOP_MEW_NF {};
enum class riscv_gen_I {};
enum class riscv_gen_I_RM {};
enum class riscv_gen_I_CSR {};
enum class riscv_gen_I_UIMM_CSR {};
enum class riscv_gen_I_VV {};
enum class riscv_gen_I_VX {};
enum class riscv_gen_S {};
enum class riscv_gen_S_VV {};
enum class riscv_gen_S_XV {};
enum class riscv_gen_B {};
enum class riscv_gen_B_VV {};
enum class riscv_gen_U {};
enum class riscv_gen_J {};
enum class riscv_gen_R4_RM {};
enum class riscv_gen_FMT_FMT {};

using riscv_gen_cb_R = std::function<uint32_t(riscv_gen_R, uint32_t rd,
                                              uint32_t rs1, uint32_t rs2)>;

using riscv_gen_cb_R = std::function<uint32_t(riscv_gen_R, uint32_t rd,
                                              uint32_t rs1, uint32_t rs2)>;

using riscv_gen_cb_R_VVV = std::function<uint32_t(
    riscv_gen_R_VVV, uint32_t vd, uint32_t vs1, uint32_t vs2, bool vm)>;

using riscv_gen_cb_R_VIV = std::function<uint32_t(
    riscv_gen_R_VIV, uint32_t vd, int64_t imm, uint32_t vs2, bool vm)>;

using riscv_gen_cb_R_VXV = std::function<uint32_t(
    riscv_gen_R_VXV, uint32_t vd, uint32_t rs1, uint32_t vs2, bool vm)>;

using riscv_gen_cb_R_VXX = std::function<uint32_t(
    riscv_gen_R_VXX, uint32_t vd, uint32_t rs1, uint32_t rs2, bool vm)>;

using riscv_gen_cb_R_VXV_MOP_MEW_NF = std::function<uint32_t(
    riscv_gen_R_VXV_MOP_MEW_NF, uint32_t vd, uint32_t rs1, uint32_t vs2,
    bool vm, uint32_t mop, uint32_t mew, uint32_t nf)>;

using riscv_gen_cb_R_V3XV_MOP_MEW_NF = std::function<uint32_t(
    riscv_gen_R_V3XV_MOP_MEW_NF, uint32_t vs3, uint32_t rs1, uint32_t vs2,
    bool vm, uint32_t mop, uint32_t mew, uint32_t nf)>;

using riscv_gen_cb_R_VXX_MOP_MEW_NF = std::function<uint32_t(
    riscv_gen_R_VXX_MOP_MEW_NF, uint32_t vd, uint32_t rs1, uint32_t rs2,
    bool vm, uint32_t mop, uint32_t mew, uint32_t nf)>;

using riscv_gen_cb_R_V3XX_MOP_MEW_NF = std::function<uint32_t(
    riscv_gen_R_V3XX_MOP_MEW_NF, uint32_t vs3, uint32_t rs1, uint32_t vs2,
    bool vm, uint32_t mop, uint32_t mew, uint32_t nf)>;

using riscv_gen_cb_R_XXV = std::function<uint32_t(
    riscv_gen_R_XXV, uint32_t rd, uint32_t rs1, uint32_t vs2, bool vm)>;

// funct3 as the rm
using riscv_gen_cb_R_RM = std::function<uint32_t(
    riscv_gen_R_RM, uint32_t rd, uint32_t rm, uint32_t rs1, uint32_t rs2)>;

// funct5 for memory ordering
using riscv_gen_cb_R_ORDERING =
    std::function<uint32_t(riscv_gen_R_ORDERING, bool aq, bool rl, uint32_t rd,
                           uint32_t rs1, uint32_t rs2)>;

using riscv_gen_cb_I = std::function<uint32_t(riscv_gen_I, uint32_t rd,
                                              uint32_t rs1, int64_t imm)>;

using riscv_gen_cb_I_RM = std::function<uint32_t(
    riscv_gen_I_RM, uint32_t rd, uint32_t rm, uint32_t rs1, int64_t imm)>;

// csr is the imm of I-type
using riscv_gen_cb_I_CSR = std::function<uint32_t(riscv_gen_I_CSR, uint32_t rd,
                                                  uint32_t rs1, uint32_t csr)>;

using riscv_gen_cb_I_VV = std::function<uint32_t(riscv_gen_I_VV, uint32_t vd,
                                                 uint32_t vs1, int64_t imm)>;

using riscv_gen_cb_I_VX = std::function<uint32_t(riscv_gen_I_VX, uint32_t vd,
                                                 uint32_t rs1, int64_t imm)>;

// uimm is the rs1 of I-type
// csr is the imm of I-type
using riscv_gen_cb_I_UIMM_CSR = std::function<uint32_t(
    riscv_gen_I_UIMM_CSR, uint32_t rd, uint32_t uimm, uint32_t csr)>;

using riscv_gen_cb_S = std::function<uint32_t(riscv_gen_S, uint32_t rs1,
                                              uint32_t rs2, int64_t imm)>;
using riscv_gen_cb_B = std::function<uint32_t(riscv_gen_B, uint32_t rs1,
                                              uint32_t rs2, int64_t imm)>;

using riscv_gen_cb_S_VV = std::function<uint32_t(riscv_gen_S_VV, uint32_t vs1,
                                                 uint32_t vs2, int64_t imm)>;
using riscv_gen_cb_S_XV = std::function<uint32_t(riscv_gen_S_XV, uint32_t rs1,
                                                 uint32_t vs2, int64_t imm)>;

using riscv_gen_cb_B_VV = std::function<uint32_t(riscv_gen_B_VV, uint32_t vs1,
                                                 uint32_t vs2, int64_t imm)>;
using riscv_gen_cb_U =
    std::function<uint32_t(riscv_gen_U, uint32_t rd, uint32_t uimm)>;
using riscv_gen_cb_J =
    std::function<uint32_t(riscv_gen_J, uint32_t rd, int64_t imm)>;

using riscv_gen_cb_R4_RM =
    std::function<uint32_t(riscv_gen_R4_RM, uint32_t rd, uint32_t rm,
                           uint32_t rs1, uint32_t rs2, uint32_t rs3)>;

using riscv_gen_cb_FMT_FMT =
    std::function<uint32_t(riscv_gen_FMT_FMT, uint32_t fmt1, uint32_t fmt2)>;

using riscv_gen =
    std::variant<riscv_gen_cb_R, riscv_gen_cb_R_RM, riscv_gen_cb_R_ORDERING,
                 riscv_gen_cb_R_VVV, riscv_gen_cb_R_VIV, riscv_gen_cb_R_VXV,
                 riscv_gen_cb_R_VXX, riscv_gen_cb_R_VXV_MOP_MEW_NF,
                 riscv_gen_cb_R_V3XV_MOP_MEW_NF, riscv_gen_cb_R_VXX_MOP_MEW_NF,
                 riscv_gen_cb_R_V3XX_MOP_MEW_NF, riscv_gen_cb_R_XXV,
                 riscv_gen_cb_I, riscv_gen_cb_I_RM, riscv_gen_cb_I_CSR,
                 riscv_gen_cb_I_UIMM_CSR, riscv_gen_cb_I_VV, riscv_gen_cb_I_VX,
                 riscv_gen_cb_S, riscv_gen_cb_S_VV, riscv_gen_cb_S_XV,
                 riscv_gen_cb_B, riscv_gen_cb_B_VV, riscv_gen_cb_U,
                 riscv_gen_cb_J, riscv_gen_cb_R4_RM, riscv_gen_cb_FMT_FMT>;

class riscv_meta;

/// @brief Registry of all instruction metadata enabled for one architecture.
class riscv_meta_list {
  ///< Instruction metadata grouped by instruction category.
  std::vector<std::vector<std::shared_ptr<riscv_meta>>>
      instr_per_category_list_;
  ///< Instruction metadata indexed by riscv_instr_name_t.
  std::vector<std::shared_ptr<riscv_meta>> instr_;

  // Regular integer instruction
  void DEFINE_INSTR(riscv_instr_name_t instr_name, riscv_instr_format_t format,
                    riscv_instr_category_t category, riscv_instr_group_t group,
                    const riscv_gen &gen_cb);

  // Floating point instruction
  void DEFINE_FP_INSTR(riscv_instr_name_t instr_n,
                       riscv_instr_format_t instr_format,
                       riscv_instr_category_t instr_category,
                       riscv_instr_group_t instr_group,
                       const riscv_gen &gen_cb);
  // Vector instruction
  void DEFINE_VEC_INSTR(riscv_instr_name_t instr_n,
                        riscv_instr_format_t instr_format,
                        riscv_instr_category_t instr_category,
                        riscv_instr_group_t instr_group,
                        const riscv_gen &gen_cb);

  // A-extension instruction
  void DEFINE_AMO_INSTR(riscv_instr_name_t instr_n,
                        riscv_instr_format_t instr_format,
                        riscv_instr_category_t instr_category,
                        riscv_instr_group_t instr_group,
                        const riscv_gen &gen_cb);

  void register_rv32a();
  void register_rv32d();
  void register_rv32f();
  void register_rv32i();
  void register_rv32m();
  void register_rv64a();
  void register_rv64d();
  void register_rv64f();
  void register_rv64i();
  void register_rv64m();
  void register_zicond();
  void register_vortex();
  void register_rvv();
  void register_ventus();

  ///< Singleton instance installed by create().
  static riscv_meta_list *g_instance;

public:
  /// @brief Build metadata registry for @p arch.
  riscv_meta_list(const ArchConfig &arch);

  /// @brief Create and install the global metadata registry.
  template <typename... Args>
  static void create(Args &&...args) {
    assert(!g_instance);
    g_instance = new riscv_meta_list(std::forward<Args>(args)...);
  }

  /// @brief Return the global metadata registry.
  static riscv_meta_list &instance() { return *g_instance; }

  /// @brief Return metadata entries belonging to @p category.
  const std::vector<std::shared_ptr<riscv_meta>> &
  instr_per_category(riscv_instr_category_t category) const {
    assert(size_t(category) < instr_per_category_list_.size());
    return instr_per_category_list_.at(size_t(category));
  }

  /// @brief Return metadata for instruction name @p instr_n.
  const std::shared_ptr<riscv_meta> &instr(riscv_instr_name_t instr_n) const {
    assert(size_t(instr_n) < instr_.size());
    return instr_.at(size_t(instr_n));
  }

  /// @brief Return the opposite conditional branch metadata.
  std::shared_ptr<riscv_meta>
  opposite_branch_meta(riscv_instr_name_t branch_instr_name) const;
};

} // namespace fuzzgpu
