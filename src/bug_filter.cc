#include "isa/riscv/riscv_meta.hh"

#include "bug_filter.hh"

namespace fuzzgpu {

bool BugFilter::filter(const std::string &bug_name) {
  if (disabled_) {
    return false;
  }
  if (bug_map_.find(bug_name) == bug_map_.end()) {
    std::abort();
  } else {
    return bug_map_[bug_name];
  }
}

bool BugFilter::filter(GenWarp *gen_warp, const riscv_instr &instr) {
  if (disabled_) {
    return false;
  }

  //
  // Ventus
  //
#if defined(ARCH_VENTUS)
  // amo bug
  if (instr.meta()->category == riscv_instr_category_t::AMO) {
    return true;
  }

#endif

  switch (instr.meta()->instr_name) {
#if defined(ARCH_VENTUS)
  case riscv_instr_name_t::VFTTA_VV:

  // mulhsu bug
  case riscv_instr_name_t::VMULHSU_VV:
  case riscv_instr_name_t::VMULHSU_VX:
  case riscv_instr_name_t::MULHSU:

  // pds bug
  case riscv_instr_name_t::VLB_V:
  case riscv_instr_name_t::VLBU_V:
  case riscv_instr_name_t::VLH_V:
  case riscv_instr_name_t::VLHU_V:
  case riscv_instr_name_t::VLW_V:
  case riscv_instr_name_t::VSB_V:
  case riscv_instr_name_t::VSH_V:
  case riscv_instr_name_t::VSW_V:

  // b11
  case riscv_instr_name_t::VMSGT_VX:
  case riscv_instr_name_t::VMSGTU_VX:
  case riscv_instr_name_t::VMSLE_VX:
  case riscv_instr_name_t::VMSLEU_VX:

  // divide bug (microarch)
  case riscv_instr_name_t::DIV:
  case riscv_instr_name_t::DIVU:
  case riscv_instr_name_t::REM:
  case riscv_instr_name_t::REMU:
#endif

#if defined(ARCH_VORTEX)

  //
  // Vortex
  //
  case riscv_instr_name_t::VX_WMMA:
  case riscv_instr_name_t::CZERO_EQZ:
  case riscv_instr_name_t::CZERO_NEZ:
  case riscv_instr_name_t::SRAI:

#endif

    return true;
  default:
    return false;
  }
}

void BugFilter::register_bug(const std::string &bug_name, bool filtered) {
  bug_map_[bug_name] = filtered;
}

void BugFilter::register_bugs(const nlohmann::json &bug_filter_list) {
  // Register all known bugs with default filtering values
  //
  // Ventus
  //
  register_bug("amo_dec", default_filtering_);        // b2
  register_bug("var_thread_num", default_filtering_); // b18
  register_bug("csr_tid", default_filtering_);
  register_bug("vmv_x_s", default_filtering_);        // b5, known bug in Ventus
  register_bug("vmask", default_filtering_);
  register_bug("vec_nfield", default_filtering_);     // b17
  register_bug("vftta_vv_tmask", default_filtering_); // b19
  register_bug("inactive_warp_barrier", false);

  //
  // Vortex
  //
  register_bug("vx_wmma", default_filtering_);
  register_bug("wb_fp_rd_zero", default_filtering_);
  register_bug("wb_zero", default_filtering_);
  register_bug("jalr_align", default_filtering_);
  register_bug("global_bar", true);

  // Override with dynamically detected bugs from JSON filter list
  if (bug_filter_list.is_array()) {
    for (const auto &bug_name : bug_filter_list) {
      if (bug_name.is_string()) {
        const auto &name = bug_name.get<std::string>();
        // If the bug exists in the map, mark it as filtered (true)
        if (bug_map_.find(name) != bug_map_.end()) {
          bug_map_[name] = true;
        }
      }
    }
  }
}

} // namespace fuzzgpu
