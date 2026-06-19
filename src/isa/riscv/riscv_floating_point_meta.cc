#include "fuzzer_state.hh"
#include "isa/riscv/riscv_instr.hh"

#include "bug_filter.hh"
#include "isa/riscv/riscv_floating_point_meta.hh"

namespace fuzzgpu {

void riscv_floating_point_meta::set_rand_mode_() {
  if (format == riscv_instr_format_t::I_FORMAT) {
    operand_mask = (HAS_FD | HAS_IMM | HAS_RS1);

  } else if (format == riscv_instr_format_t::S_FORMAT) {
    operand_mask = (HAS_FS2 | HAS_IMM | HAS_RS1);

  } else if (format == riscv_instr_format_t::R_FORMAT) {
    if (category == riscv_instr_category_t::COMPARE) {
      operand_mask = (HAS_RD | HAS_FS1 | HAS_FS2);

    } else {
      switch (instr_name) {
      case riscv_instr_name_t::FCLASS_S:
      case riscv_instr_name_t::FCLASS_D:
        operand_mask = (HAS_RD | HAS_FS1);
        break;

      case riscv_instr_name_t::FMV_X_W:
      case riscv_instr_name_t::FMV_X_D:
        operand_mask = (HAS_RD | HAS_FS1);
        break;

      case riscv_instr_name_t::FMV_W_X:
      case riscv_instr_name_t::FMV_D_X:
        operand_mask = (HAS_FD | HAS_RS1);
        break;

      case riscv_instr_name_t::FEQ_S:
      case riscv_instr_name_t::FEQ_D:
      case riscv_instr_name_t::FLT_S:
      case riscv_instr_name_t::FLT_D:
      case riscv_instr_name_t::FLE_S:
      case riscv_instr_name_t::FLE_D:
        operand_mask = (HAS_RD | HAS_FS1 | HAS_FS2);
        break;

      default:
        operand_mask = (HAS_FD | HAS_FS1 | HAS_FS2);
      }
    }

  } else if (format == riscv_instr_format_t::R_RM_FORMAT) {

    switch (instr_name) {
    case riscv_instr_name_t::FCVT_W_S:
    case riscv_instr_name_t::FCVT_WU_S:
    case riscv_instr_name_t::FCVT_L_S:
    case riscv_instr_name_t::FCVT_LU_S:
    case riscv_instr_name_t::FCVT_L_D:
    case riscv_instr_name_t::FCVT_LU_D:
    case riscv_instr_name_t::FCVT_W_D:
    case riscv_instr_name_t::FCVT_WU_D:
      operand_mask = (HAS_RD | HAS_RM | HAS_FS1);
      break;

    case riscv_instr_name_t::FCVT_S_W:
    case riscv_instr_name_t::FCVT_S_WU:
    case riscv_instr_name_t::FCVT_S_L:
    case riscv_instr_name_t::FCVT_D_L:
    case riscv_instr_name_t::FCVT_S_LU:
    case riscv_instr_name_t::FCVT_D_W:
    case riscv_instr_name_t::FCVT_D_LU:
    case riscv_instr_name_t::FCVT_D_WU:
      operand_mask = (HAS_FD | HAS_RM | HAS_RS1);
      break;

    case riscv_instr_name_t::FSQRT_S:
    case riscv_instr_name_t::FSQRT_D:

    case riscv_instr_name_t::FCVT_S_D:
    case riscv_instr_name_t::FCVT_D_S:
      operand_mask = (HAS_FD | HAS_RM | HAS_FS1);
      break;

    default:
      operand_mask = (HAS_FD | HAS_RM | HAS_FS1 | HAS_FS2);
    }

  } else if (format == riscv_instr_format_t::R4_RM_FORMAT) {
    operand_mask = (HAS_FD | HAS_RM | HAS_FS1 | HAS_FS2 | HAS_FS3);
  }
}

bool riscv_floating_point_meta::randomize(
    FuzzerState &fuzzer_state, GenWarp &warp, riscv_instr *out,
    const std::vector<riscv_reg_t> &exclude_regs,
    const std::vector<riscv_fpr_t> &exclude_fprs, bool force_mutate,
    uint64_t operand_filter) {

  riscv_meta::check_operand_mask();
  bool ret = riscv_meta::randomize(fuzzer_state, warp, out, exclude_regs,
                                   exclude_fprs, force_mutate, operand_filter);

  // If operand_filter is set, only randomize operands that match the filter
  const uint64_t effective_mask = operand_filter ? (operand_mask & operand_filter) : operand_mask;

  if (effective_mask & HAS_FS1) {
    out->set_fs1((riscv_fpr_t)warp.pick_fs(force_mutate));
  }
  if (effective_mask & HAS_FS2) {
    out->set_fs2((riscv_fpr_t)warp.pick_fs(force_mutate));
  }
  if (effective_mask & HAS_FS3) {
    out->set_fs3((riscv_fpr_t)warp.pick_fs(force_mutate));
  }
  if (effective_mask & HAS_FD) {
    out->set_fd((riscv_fpr_t)warp.pick_fd(exclude_fprs, force_mutate));
    if (BugFilter::instance().filter("wb_zero") &&
        out->fd() == riscv_fpr_t::FT0) {
      return false;
    }
  }
  if (effective_mask & HAS_RM) {
    out->set_rm(static_cast<riscv_rm_t>(std::uniform_int_distribution<unsigned>(
        riscv_floating_point_meta::min_supported_instr_rm,
        riscv_floating_point_meta::max_supported_instr_rm)(fuzzer_state.rng)));
  }
  return ret;
}

} // namespace fuzzgpu