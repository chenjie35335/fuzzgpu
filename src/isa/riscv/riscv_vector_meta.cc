#include "bug_filter.hh"
#include "fuzzer_state.hh"
#include "isa/riscv/riscv_instr.hh"

#include "isa/riscv/riscv_vector_meta.hh"

namespace fuzzgpu {

void riscv_vector_meta::set_rand_mode_() {
#include "riscv_vector_meta.inc"
#include "ventus/ventus_meta.inc"
}

void riscv_vector_meta::set_imm_len_() {
  switch (format) {
  case riscv_instr_format_t::R_VIV:
    imm_len = 5;
    break;
  case riscv_instr_format_t::I_VV_FORMAT:
    imm_len = 12;
    break;
  case riscv_instr_format_t::S_VV:
    imm_len = 12;
    break;
  case riscv_instr_format_t::S_XV:
    imm_len = 11; // imm[11] is fixed as opcode
    break;
  case riscv_instr_format_t::B_VV:
    imm_align_bits = 1;
    imm_len = 13;
    break;
  default:
    break;
  }
  switch (instr_name) {
  case riscv_instr_name_t::VLW_V:
  case riscv_instr_name_t::VLH_V:
  case riscv_instr_name_t::VLB_V:
  case riscv_instr_name_t::VLHU_V:
  case riscv_instr_name_t::VLBU_V:
  case riscv_instr_name_t::VSW_V:
  case riscv_instr_name_t::VSH_V:
  case riscv_instr_name_t::VSB_V:
    imm_len = 11; // imm[11] is fixed as opcode
    break;
  default:
    break;
  }

  if (imm_len && (category == riscv_instr_category_t::LOAD ||
                  category == riscv_instr_category_t::STORE ||
                  category == riscv_instr_category_t::AMO)) {
    imm_align_bits = unsigned(std::log2(mem_access_size()));
  }
}

bool riscv_vector_meta::randomize(FuzzerState &fuzzer_state, GenWarp &warp,
                                  riscv_instr *out,
                                  const std::vector<riscv_reg_t> &exclude_regs,
                                  const std::vector<riscv_fpr_t> &exclude_fprs,
                                  bool force_mutate,
                                  uint64_t operand_filter) {
  riscv_meta::check_operand_mask();
  bool ret = riscv_meta::randomize(fuzzer_state, warp, out, exclude_regs,
                                   exclude_fprs, force_mutate, operand_filter);

  // If operand_filter is set, only randomize operands that match the filter
  const uint64_t effective_mask = operand_filter ? (operand_mask & operand_filter) : operand_mask;

  bool cti = (category == riscv_instr_category_t::BRANCH ||
              category == riscv_instr_category_t::LOAD ||
              category == riscv_instr_category_t::STORE ||
              category == riscv_instr_category_t::AMO);

  if (effective_mask & HAS_VM) {
    out->set_vm(true); // disable mask since ventus does not fully implement it
  }

  if (effective_mask & HAS_VS1) {
    out->set_vs1(warp.pick_vs(cti, force_mutate));
  }

  if (effective_mask & HAS_VS2) {
    out->set_vs2(warp.pick_vs(cti, force_mutate));
  }

  if (effective_mask & HAS_VD) {
    std::vector<riscv_vreg_t> exclude_vds;
    if ((operand_mask & HAS_VM) && (!out->vm() || vm_hardcoded_0())) {
      exclude_vds.push_back(riscv_vreg_t::V0); // v0 is the mask register
    }
    out->set_vd(warp.pick_vd(exclude_vds, force_mutate));
  }
  if (effective_mask & HAS_MOP) {
    out->set_mop(fuzzer_state.bits_sampler.gen_bits(2));
  }
  if (effective_mask & HAS_MEW) {
    out->set_mew(fuzzer_state.bits_sampler.gen_bits(1));
  }
  if (effective_mask & HAS_NF) {
    if (nf_range == 2) {
      if (!BugFilter::instance().filter("vec_nfield")) {
        out->set_nf(fuzzer_state.bits_sampler.gen_bits(3));
      } else {
        out->set_nf(0); // skip the found bug
      }
    } else {
      assert(nf_range == 0 || nf_range == 1);
      out->set_nf(fuzzer_state.bits_sampler.gen_bits(2) | uint32_t(nf_range)
                                                              << 2);
    }
  }

  if (effective_mask & HAS_VS3) {
    out->set_vs3(warp.pick_vs(cti, force_mutate));
    if (cti) {
      unsigned resample_count = 0;
      do {
        if (riscv_vector_meta::foreach_vector_nfield_(
                *out, [&](unsigned reg_offset) {
                  if (!warp.is_pickable_vec_reg(
                          riscv_vreg_t(unsigned(out->vs3()) + reg_offset), cti)) {
                    out->set_vs3(warp.pick_vs(cti, force_mutate));
                    return false; // resample
                  }
                  return true;
                }))
          break;
      } while (++resample_count < Config::instance().max_resample_times);
      if (resample_count == Config::instance().max_resample_times) {
        spdlog::warn(
            "resample max times reached when picking vs3 for instruction {}",
            static_cast<unsigned>(instr_name));
        return false;
      }
    }
  }
  return ret;
}

bool riscv_vector_meta::foreach_vector_nfield_(
    const riscv_instr &instr,
    const std::function<bool(unsigned reg_offset)> &f) {
  unsigned nf = 1;
  if (instr.meta()->operand_mask & riscv_meta::HAS_NF) {
    nf = instr.nf() + 1;
  }
#if defined(ARCH_VORTEX) || defined(ARCH_VENTUS)
  const auto veew = 32;  // ventus only supports e32 for now
  const auto vsew = 32;  // ventus only supports VSEW=32
  const auto vflmul = 1; // ventus only supports LMUL=1
#else
#error
#endif
  // TODO: vsm.v
  const unsigned vemul = ((float)veew / vsew * vflmul);
  for (unsigned fn = 0; fn < nf; ++fn) {
    if (!f(fn * vemul))
      return false;
  }
  return true;
}

} // namespace fuzzgpu
