#include <random>

#include "isa/riscv/riscv_instr.hh"

#include "generator.hh"

namespace fuzzgpu {

void Generator::gen_refresh_(const std::shared_ptr<GenWarp> &gen_warp,
                             const std::shared_ptr<riscv_meta> &meta) {

  if (meta->is_vector()) {
    //
    // Floating-point registers
    //
    if (!gen_warp->no_available_vec_reg())
      return;
    spdlog::debug("Refreshing vector register");
    std::vector<unsigned> nodet_regs;
    for (unsigned reg = 0; reg < riscv_meta::gpr_encoding_space; ++reg) {
      if (gen_warp->is_nodet_vec_reg(riscv_vreg_t(reg))) {
        nodet_regs.push_back(reg);
      }
    }
    const size_t max_vec_src_operands =
        3; // max number of source regs for the ISAs
    const auto nr_to_refresh =
        std::min(max_vec_src_operands, nodet_regs.size());
    topk_shuffle(nodet_regs, nr_to_refresh, state_.rng);

    for (size_t i = 0; i < nr_to_refresh; ++i) {
      // make a random vector register determinstic
      const auto vreg = riscv_vreg_t(nodet_regs[i]);
      bool fallthrough = true;
      if (state_.uniform_dist(state_.rng) > 0.5) {
        riscv_instr vmv_vx(
            riscv_meta_list::instance().instr(riscv_instr_name_t::VMV_V_X));
        vmv_vx.set_vd(vreg);
        riscv_reg_t xreg = gen_warp->pick_rs(
            true, true); // use a random deterministic source reg (always exists
                         // because of the null reg)
        if (xreg != riscv_reg_t::ZERO) {
          vmv_vx.set_rs1(xreg);
          gen_warp->scratchpad.emit(&vmv_vx);
          fallthrough = false;
        }
      }
      if (fallthrough) {
        riscv_instr vmv_vi(
            riscv_meta_list::instance().instr(riscv_instr_name_t::VMV_V_I));
        vmv_vi.set_vd(vreg);
        vmv_vi.set_imm(
            std::uniform_int_distribution<int64_t>(-16, 15)(state_.rng));
        gen_warp->scratchpad.emit(&vmv_vi);
      }
    }

    assert(!gen_warp->no_available_vec_reg());
    state_.stat.num_refresh_vector_reg++;

  } else if (gen_warp->no_available_int_reg()) {
    //
    // Integer registers
    //
    spdlog::debug("Refreshing integer register");

    std::vector<unsigned> nodet_regs;
    for (unsigned reg = 0; reg < riscv_meta::gpr_encoding_space; ++reg) {
      if (gen_warp->is_nodet_int_reg(riscv_reg_t(reg))) {
        nodet_regs.push_back(reg);
      }
    }
    const size_t max_int_src_operands =
        3; // max number of source regs for the ISAs
    const auto nr_to_refresh =
        std::min(max_int_src_operands, nodet_regs.size());
    topk_shuffle(nodet_regs, nr_to_refresh, state_.rng);

    for (size_t i = 0; i < nr_to_refresh; ++i) {
      // make a random integer register determinstic
      const auto reg = riscv_reg_t(nodet_regs[i]);

      // ventus has bugy vmv.x.v, so no commucate with vector regs

      std::shared_ptr<riscv_meta> rand_meta;
      do {
        // randomly sample the integer arithmetic instruction
        rand_meta = state_.pick_instr();
      } while ((rand_meta->category != riscv_instr_category_t::SHIFT &&
                rand_meta->category != riscv_instr_category_t::ARITHMETIC &&
                rand_meta->category != riscv_instr_category_t::LOGICAL &&
                rand_meta->category != riscv_instr_category_t::COMPARE) ||
               (rand_meta->group != riscv_instr_group_t::RV32I &&
                rand_meta->group != riscv_instr_group_t::RV64I) ||
               (rand_meta->format != riscv_instr_format_t::I_FORMAT));

      // mix with the randsrc
      riscv_instr instr(rand_meta);
      instr.set_rd(reg);
      instr.set_rs1(riscv_reg_t::ZERO);
      auto imm12 = state_.imm12_dist(state_.rng);
      if (rand_meta->instr_name == riscv_instr_name_t::SLLI ||
          rand_meta->instr_name == riscv_instr_name_t::SRLI ||
          rand_meta->instr_name == riscv_instr_name_t::SRAI) {
        // shamt
        imm12 &= (Config::instance().arch.word_bits - 1);
      } else if (rand_meta->instr_name == riscv_instr_name_t::SLLIW ||
                 rand_meta->instr_name == riscv_instr_name_t::SRLIW ||
                 rand_meta->instr_name == riscv_instr_name_t::SRAIW) {
        // shamt
        imm12 &= 31;
      }
      instr.set_imm(imm12); // use a random immediate
      gen_warp->scratchpad.emit(&instr);
    }

    assert(!gen_warp->no_available_int_reg());
    state_.stat.num_refresh_int_reg++;
  }
}

} // namespace fuzzgpu