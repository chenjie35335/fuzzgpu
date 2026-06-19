#include "util.hh"

#include "isa/riscv/riscv_instr.hh"
#include "isa/riscv/riscv_meta.hh"

namespace fuzzgpu {

/**@brief
 * Emit the binary encoding of this instruction.
 */
void riscv_instr::emit(std::vector<uint8_t> &instr_buf) const {

  assert(assigned_operands_ == meta_->operand_mask &&
         "Not all operands are assigned");

  // helpers to get source and destination registers
  auto get_dest = [&]() -> uint32_t {
    assert(std::popcount(meta_->operand_mask &
                         (riscv_meta::HAS_RD | riscv_meta::HAS_FD |
                          riscv_meta::HAS_VD)) < 2);
    if (meta_->operand_mask & riscv_meta::HAS_RD) {
      return static_cast<uint32_t>(rd());
    }
    if (meta_->operand_mask & riscv_meta::HAS_FD) {
      return static_cast<uint32_t>(fd());
    }
    return uint32_t(-1);
  };
  auto get_src1 = [&]() -> uint32_t {
    assert(std::popcount(meta_->operand_mask &
                         (riscv_meta::HAS_RS1 | riscv_meta::HAS_FS1 |
                          riscv_meta::HAS_VS1)) < 2);
    if (meta_->operand_mask & riscv_meta::HAS_RS1) {
      return static_cast<uint32_t>(rs1());
    }
    if (meta_->operand_mask & riscv_meta::HAS_FS1) {
      return static_cast<uint32_t>(fs1());
    }
    return uint32_t(-1);
  };
  auto get_src2 = [&]() -> uint32_t {
    assert(std::popcount(meta_->operand_mask &
                         (riscv_meta::HAS_RS2 | riscv_meta::HAS_FS2 |
                          riscv_meta::HAS_VS2)) < 2);
    if (meta_->operand_mask & riscv_meta::HAS_RS2) {
      return static_cast<uint32_t>(rs2());
    }
    if (meta_->operand_mask & riscv_meta::HAS_FS2) {
      return static_cast<uint32_t>(fs2());
    }
    return uint32_t(-1);
  };
  auto get_src3 = [&]() -> uint32_t {
    assert(meta_->operand_mask & riscv_meta::HAS_FS3);
    return static_cast<uint32_t>(fs3());
  };
  auto get_imm = [&]() -> int64_t {
    if (meta_->operand_mask & riscv_meta::HAS_IMM) {
      return imm();
    }
    return -1;
  };

  auto get_vdest = [&]() -> uint32_t {
    assert(std::popcount(meta_->operand_mask &
                         (riscv_meta::HAS_RD | riscv_meta::HAS_FD |
                          riscv_meta::HAS_VD)) < 2);
    if (meta_->operand_mask & riscv_meta::HAS_VD) {
      return static_cast<uint32_t>(vd());
    }
    return uint32_t(-1);
  };
  auto get_vsrc1 = [&]() -> uint32_t {
    assert(std::popcount(meta_->operand_mask &
                         (riscv_meta::HAS_RS1 | riscv_meta::HAS_FS1 |
                          riscv_meta::HAS_VS1)) < 2);
    if (meta_->operand_mask & riscv_meta::HAS_VS1) {
      return static_cast<uint32_t>(vs1());
    }
    return uint32_t(-1);
  };
  auto get_vsrc2 = [&]() -> uint32_t {
    assert(std::popcount(meta_->operand_mask &
                         (riscv_meta::HAS_RS2 | riscv_meta::HAS_FS2 |
                          riscv_meta::HAS_VS2)) < 2);
    if (meta_->operand_mask & riscv_meta::HAS_VS2) {
      return static_cast<uint32_t>(vs2());
    }
    return uint32_t(-1);
  };
  auto get_vsrc3 = [&]() -> uint32_t {
    assert(std::popcount(meta_->operand_mask &
                         (riscv_meta::HAS_FS3 | riscv_meta::HAS_VS3)) < 2);
    if (meta_->operand_mask & riscv_meta::HAS_VS3) {
      return static_cast<uint32_t>(vs3());
    }
    return uint32_t(-1);
  };
  auto get_vm = [&]() -> bool {
    if (meta_->operand_mask & riscv_meta::HAS_VM) {
      return vm();
    }
    return false;
  };
  auto get_mop = [&]() -> uint32_t {
    if (meta_->operand_mask & riscv_meta::HAS_MOP) {
      return mop();
    }
    return 0;
  };
  auto get_mew = [&]() -> uint32_t {
    if (meta_->operand_mask & riscv_meta::HAS_MEW) {
      return mew();
      ;
    }
    return 0;
  };
  auto get_nf = [&]() -> uint32_t {
    if (meta_->operand_mask & riscv_meta::HAS_NF) {
      return nf();
    }
    return 0;
  };
  auto get_csr = [&]() -> uint32_t {
    if (meta_->operand_mask & riscv_meta::HAS_CSR)
      return static_cast<uint32_t>(csr());
    return uint32_t(-1);
  };
  auto get_tgt_addr_offset = [&]() -> int64_t {
    if (meta_->operand_mask & riscv_meta::HAS_TGT_ADDR) {
      return tgt_addr_offset();
    }
    return int64_t(-1);
  };

  // dispatcher
  uint32_t i32;
  switch (meta_->format) {
  case riscv_instr_format_t::J_FORMAT:
    assert(meta_->operand_mask & riscv_meta::HAS_TGT_ADDR);
    i32 = std::get<riscv_gen_cb_J>(meta_->gen_cb)(riscv_gen_J(), get_dest(),
                                                  tgt_addr_offset());
    break;

  case riscv_instr_format_t::U_FORMAT:
    i32 = std::get<riscv_gen_cb_U>(meta_->gen_cb)(
        riscv_gen_U(), get_dest(), static_cast<uint32_t>(get_imm()));
    break;

  case riscv_instr_format_t::I_FORMAT:
    i32 = std::get<riscv_gen_cb_I>(meta_->gen_cb)(riscv_gen_I(), get_dest(),
                                                  get_src1(), get_imm());
    break;

  case riscv_instr_format_t::I_CSR_FORMAT:
    i32 = std::get<riscv_gen_cb_I_CSR>(meta_->gen_cb)(
        riscv_gen_I_CSR(), get_dest(), get_src1(), csr());
    break;

  case riscv_instr_format_t::I_UIMM_CSR_FORMAT:
    i32 = std::get<riscv_gen_cb_I_UIMM_CSR>(meta_->gen_cb)(
        riscv_gen_I_UIMM_CSR(), get_dest(), static_cast<uint32_t>(get_imm()),
        get_csr());
    break;

  case riscv_instr_format_t::I_VV_FORMAT:
    i32 = std::get<riscv_gen_cb_I_VV>(meta_->gen_cb)(
        riscv_gen_I_VV(), get_vdest(), get_vsrc1(), get_imm());
    break;

  case riscv_instr_format_t::I_VX_FORMAT:
    i32 = std::get<riscv_gen_cb_I_VX>(meta_->gen_cb)(
        riscv_gen_I_VX(), get_vdest(), get_vsrc1(), get_imm());
    break;

  case riscv_instr_format_t::B_FORMAT:
    i32 = std::get<riscv_gen_cb_B>(meta_->gen_cb)(
        riscv_gen_B(), get_src1(), get_src2(), tgt_addr_offset());
    break;

  case riscv_instr_format_t::B_VV:
    i32 = std::get<riscv_gen_cb_B_VV>(meta_->gen_cb)(
        riscv_gen_B_VV(), get_vsrc1(), get_vsrc2(), get_tgt_addr_offset());
    break;

  case riscv_instr_format_t::R_FORMAT:
    i32 = std::get<riscv_gen_cb_R>(meta_->gen_cb)(riscv_gen_R(), get_dest(),
                                                  get_src1(), get_src2());
    break;

  case riscv_instr_format_t::R_RM_FORMAT:
    assert(meta_->operand_mask & riscv_meta::HAS_RM);
    i32 = std::get<riscv_gen_cb_R_RM>(meta_->gen_cb)(
        riscv_gen_R_RM(), get_dest(), static_cast<uint32_t>(rm()), get_src1(),
        get_src2());
    break;

  case riscv_instr_format_t::R_ORDERING_FORMAT:
    i32 = std::get<riscv_gen_cb_R_ORDERING>(meta_->gen_cb)(
        riscv_gen_R_ORDERING(), aq(), rl(), get_dest(), get_src1(), get_src2());
    break;

  case riscv_instr_format_t::R_VVV:
    i32 = std::get<riscv_gen_cb_R_VVV>(meta_->gen_cb)(
        riscv_gen_R_VVV(), get_vdest(), get_vsrc1(), get_vsrc2(), get_vm());
    break;

  case riscv_instr_format_t::R_VIV:
    i32 = std::get<riscv_gen_cb_R_VIV>(meta_->gen_cb)(
        riscv_gen_R_VIV(), get_vdest(), get_imm(), get_vsrc2(), get_vm());
    break;

  case riscv_instr_format_t::R_XXV:
    i32 = std::get<riscv_gen_cb_R_XXV>(meta_->gen_cb)(
        riscv_gen_R_XXV(), get_dest(), get_src1(), get_vsrc2(), get_vm());
    break;

  case riscv_instr_format_t::R_VXV:
    i32 = std::get<riscv_gen_cb_R_VXV>(meta_->gen_cb)(
        riscv_gen_R_VXV(), get_vdest(), get_src1(), get_vsrc2(), get_vm());
    break;
  case riscv_instr_format_t::R_VXX:
    i32 = std::get<riscv_gen_cb_R_VXX>(meta_->gen_cb)(
        riscv_gen_R_VXX(), get_vdest(), get_src1(), get_src2(), get_vm());
    break;

  case riscv_instr_format_t::R_VXV_MOP_MEW_NF:
    i32 = std::get<riscv_gen_cb_R_VXV_MOP_MEW_NF>(meta_->gen_cb)(
        riscv_gen_R_VXV_MOP_MEW_NF(), get_vdest(), get_src1(), get_vsrc2(),
        get_vm(), get_mop(), get_mew(), get_nf());
    break;
  case riscv_instr_format_t::R_V3XV_MOP_MEW_NF:
    i32 = std::get<riscv_gen_cb_R_V3XV_MOP_MEW_NF>(meta_->gen_cb)(
        riscv_gen_R_V3XV_MOP_MEW_NF(), get_vsrc3(), get_src1(), get_vsrc2(),
        get_vm(), get_mop(), get_mew(), get_nf());
    break;
  case riscv_instr_format_t::R_VXX_MOP_MEW_NF:
    i32 = std::get<riscv_gen_cb_R_VXX_MOP_MEW_NF>(meta_->gen_cb)(
        riscv_gen_R_VXX_MOP_MEW_NF(), get_vdest(), get_src1(), get_src2(),
        get_vm(), get_mop(), get_mew(), get_nf());
    break;
  case riscv_instr_format_t::R_V3XX_MOP_MEW_NF:
    i32 = std::get<riscv_gen_cb_R_V3XX_MOP_MEW_NF>(meta_->gen_cb)(
        riscv_gen_R_V3XX_MOP_MEW_NF(), get_vsrc3(), get_src1(), get_src2(),
        get_vm(), get_mop(), get_mew(), get_nf());
    break;

  case riscv_instr_format_t::S_FORMAT:
    i32 = std::get<riscv_gen_cb_S>(meta_->gen_cb)(riscv_gen_S(), get_src1(),
                                                  get_src2(), get_imm());
    break;

  case riscv_instr_format_t::S_VV:
    i32 = std::get<riscv_gen_cb_S_VV>(meta_->gen_cb)(
        riscv_gen_S_VV(), get_vsrc1(), get_vsrc2(), get_imm());
    break;
  case riscv_instr_format_t::S_XV:
    i32 = std::get<riscv_gen_cb_S_XV>(meta_->gen_cb)(
        riscv_gen_S_XV(), get_vsrc1(), get_vsrc2(), get_imm());
    break;

  case riscv_instr_format_t::R4_RM_FORMAT:
    assert(meta_->operand_mask & riscv_meta::HAS_RM);
    i32 = std::get<riscv_gen_cb_R4_RM>(meta_->gen_cb)(
        riscv_gen_R4_RM(), get_dest(), static_cast<uint32_t>(rm()), get_src1(),
        get_src2(), get_src3());
    break;

  case riscv_instr_format_t::FMT_FMT_FORMAT:
    i32 = std::get<riscv_gen_cb_FMT_FMT>(meta_->gen_cb)(
        riscv_gen_FMT_FMT(), static_cast<uint32_t>(fmt1()),
        static_cast<uint32_t>(fmt2()));
    break;

  default:
    spdlog::error("Unsupported instruction format: {}",
                  magic_enum::enum_name(meta_->format));
    std::abort();
  }

  instr_buf.push_back(i32 & 0xff);
  instr_buf.push_back((i32 >> 8) & 0xff);
  instr_buf.push_back((i32 >> 16) & 0xff);
  instr_buf.push_back((i32 >> 24) & 0xff);
}

} // namespace fuzzgpu
