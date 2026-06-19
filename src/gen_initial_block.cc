#include "bug_filter.hh"
#include "fuzzer_state.hh"
#include "gen_warp.hh"
#include "interleaved_state.hh"
#include "isa/riscv/riscv_amo_meta.hh"
#include "isa/riscv/riscv_floating_point_meta.hh"
#include "isa/riscv/riscv_instr.hh"
#include "isa/riscv/riscv_meta.hh"
#include "isa/riscv/riscv_meta_list.hh"
#include "isa/riscv/vortex/csr.hh"
#include "types.hh"

#include "generator.hh"

namespace fuzzgpu {

GenExitReason Generator::gen_initial_block_(std::shared_ptr<GenWarp> &gen_warp,
                                            InterleavedState &istate) {
  auto filter_init_int_reg = [&](riscv_reg_t reg) -> bool {
    return reg == istate.sgpr_baseaddr_reg ||
           gen_warp->is_int_reg_excluded(reg);
  };

  if (!istate.inited_dat_blk_baseaddr_regs) {
    const auto data_alignment = Config::instance().arch.word_bits / 8;
#if defined(ARCH_VENTUS)
    // initialize the data block baseaddr regs
    istate.sgpr_baseaddr_reg = gen_warp->pick_rd({riscv_reg_t::ZERO});
    istate.vgpr_baseaddr_reg = gen_warp->pick_vd({});

    // alocate random data block for the regs
    const auto xlen_bytes = Config::instance().arch.word_bits / 8;
    const size_t sgpr_dat_blk_sz =
        (gen_warp->num_int_regs() + gen_warp->num_fp_regs()) * xlen_bytes;
    auto sgpr_dat_blk_mem_blk = state_.mem_alloc.allocate(
        MemBlkType::WRITABLE, Config::instance().arch.gmem_start_addr, 0,
        UINT64_MAX, sgpr_dat_blk_sz, data_alignment);
    assert(sgpr_dat_blk_mem_blk.valid());
    state_.mem_alloc.shrink(sgpr_dat_blk_mem_blk, sgpr_dat_blk_sz);

    // li t0, sgpr_dat_blk_mem_blk.begin
    riscv_meta::emit_imm_setter(
        istate.sgpr_baseaddr_reg, sgpr_dat_blk_mem_blk.begin, *gen_warp,
        [&](riscv_instr *instr) { gen_warp->scratchpad.emit(instr); }, {});

    const size_t vgpr_dat_blk_sz_per_thread =
        gen_warp->num_vec_regs() * xlen_bytes;
    const size_t vgpr_dat_blk_sz =
        vgpr_dat_blk_sz_per_thread * gen_warp->num_active_threads();
    auto vgpr_dat_blk_mem_blk = state_.mem_alloc.allocate(
        MemBlkType::WRITABLE, Config::instance().arch.gmem_start_addr, 0,
        UINT64_MAX, vgpr_dat_blk_sz, data_alignment);
    assert(vgpr_dat_blk_mem_blk.valid());
    state_.mem_alloc.shrink(vgpr_dat_blk_mem_blk, vgpr_dat_blk_sz);

    // vid.v v1,
    // li t1, vgpr_dat_blk_sz_per_thread
    // vmul.vx v1, v1, t1
    // li t1, vgpr_dat_blk_mem_blk.begin
    // vmv.v.i basereg, t1
    // vadd.vv basereg, basereg, v1

    const auto tmp_v0 = gen_warp->pick_vd({istate.vgpr_baseaddr_reg});
    const auto tmp_t0 =
        gen_warp->pick_rd({riscv_reg_t::ZERO, istate.sgpr_baseaddr_reg});
    riscv_instr vid_v(
        riscv_meta_list::instance().instr(riscv_instr_name_t::VID_V));
    vid_v.set_vd(tmp_v0);
    vid_v.set_vm(true);
    gen_warp->scratchpad.emit(&vid_v);

    riscv_meta::emit_imm_setter(
        tmp_t0, vgpr_dat_blk_sz_per_thread, *gen_warp,
        [&](riscv_instr *instr) { gen_warp->scratchpad.emit(instr); }, {});

    riscv_instr vmul_vx(
        riscv_meta_list::instance().instr(riscv_instr_name_t::VMUL_VX));
    vmul_vx.set_vd(tmp_v0);
    vmul_vx.set_vs2(tmp_v0);
    vmul_vx.set_rs1(tmp_t0);
    vmul_vx.set_vm(true);
    gen_warp->scratchpad.emit(&vmul_vx);

    riscv_meta::emit_imm_setter(
        tmp_t0, vgpr_dat_blk_mem_blk.begin, *gen_warp,
        [&](riscv_instr *instr) { gen_warp->scratchpad.emit(instr); }, {});

    riscv_instr vmv_vx(
        riscv_meta_list::instance().instr(riscv_instr_name_t::VMV_V_X));
    vmv_vx.set_vd(istate.vgpr_baseaddr_reg);
    vmv_vx.set_rs1(tmp_t0);
    gen_warp->scratchpad.emit(&vmv_vx);

    riscv_instr vadd_vv(
        riscv_meta_list::instance().instr(riscv_instr_name_t::VADD_VV));
    vadd_vv.set_vd(istate.vgpr_baseaddr_reg);
    vadd_vv.set_vs1(istate.vgpr_baseaddr_reg);
    vadd_vv.set_vs2(tmp_v0);
    vadd_vv.set_vm(true);
    gen_warp->scratchpad.emit(&vadd_vv);

#elif defined(ARCH_VORTEX)
    // initialize the data block baseaddr regs
    istate.sgpr_baseaddr_reg = gen_warp->pick_rd({riscv_reg_t::ZERO});

    const auto xlen_bytes = Config::instance().arch.word_bits / 8;
    const auto sgpr_dat_blk_sz_per_thread =
        (gen_warp->num_int_regs() + gen_warp->num_fp_regs()) * xlen_bytes;
    const auto sgpr_dat_blk_sz = sgpr_dat_blk_sz_per_thread *
                                 Config::instance().arch.nr_threads_per_warp;

    auto blk = state_.mem_alloc.allocate(
        MemBlkType::WRITABLE, Config::instance().arch.gmem_start_addr, 0,
        UINT64_MAX, sgpr_dat_blk_sz, data_alignment);
    assert(blk.valid());
    state_.mem_alloc.shrink(blk, sgpr_dat_blk_sz);

    // csrr t0, VX_CSR_THREAD_ID
    // li t1, sgpr_dat_blk_sz_per_thread
    // mul t0, t0, t1
    // li basereg, sgpr_dat_blk_mem_blk.begin
    // add basereg, basereg, t0

    const auto t0 =
        gen_warp->pick_rd({riscv_reg_t::ZERO, istate.sgpr_baseaddr_reg});
    const auto t1 =
        gen_warp->pick_rd({riscv_reg_t::ZERO, istate.sgpr_baseaddr_reg, t0});

    riscv_instr csrr_tid(
        riscv_meta_list::instance().instr(riscv_instr_name_t::CSRRS));
    csrr_tid.set_rd(t0);
    csrr_tid.set_csr(VX_CSR_THREAD_ID);
    csrr_tid.set_rs1(riscv_reg_t::ZERO);
    gen_warp->scratchpad.emit(&csrr_tid);

    riscv_meta::emit_imm_setter(
        t1, sgpr_dat_blk_sz_per_thread, *gen_warp,
        [&](riscv_instr *instr) { gen_warp->scratchpad.emit(instr); }, {});

    riscv_instr mul(riscv_meta_list::instance().instr(riscv_instr_name_t::MUL));
    mul.set_rd(t0);
    mul.set_rs1(t0);
    mul.set_rs2(t1);
    gen_warp->scratchpad.emit(&mul);

    riscv_meta::emit_imm_setter(
        istate.sgpr_baseaddr_reg, blk.begin, *gen_warp,
        [&](riscv_instr *instr) { gen_warp->scratchpad.emit(instr); }, {});

    riscv_instr add(riscv_meta_list::instance().instr(riscv_instr_name_t::ADD));
    add.set_rd(istate.sgpr_baseaddr_reg);
    add.set_rs1(istate.sgpr_baseaddr_reg);
    add.set_rs2(t0);
    gen_warp->scratchpad.emit(&add);
#endif

    istate.inited_dat_blk_baseaddr_regs = true;

    istate.terminating_warp |=
        state_.get_num_instrs() > Config::instance().max_num_instrs;
    return GenExitReason::Step;

  } else if (istate.inited_int_regs < gen_warp->num_int_regs()) {
    // initialize integer registers
    if (filter_init_int_reg(riscv_reg_t(istate.inited_int_regs))) {
      istate.inited_int_regs++;
      return GenExitReason::Step;
    }
    if (state_.get_num_instrs() > Config::instance().max_num_instrs) {
      istate.terminating_warp = true;
      return GenExitReason::Step;
    }
    const auto rd = riscv_reg_t(istate.inited_int_regs);

    // ld/lw rd, offset(sgpr_baseaddr_reg)
    if (Config::instance().arch.word_bits == 64) {
      istate.meta = riscv_meta_list::instance().instr(riscv_instr_name_t::LD);
    } else {
      istate.meta = riscv_meta_list::instance().instr(riscv_instr_name_t::LW);
    }
    istate.instr = riscv_instr(istate.meta);
    istate.instr.set_rd(rd);
    istate.instr.set_rs1(istate.sgpr_baseaddr_reg);
    istate.instr.set_imm(int64_t(istate.inited_int_regs) *
                         (Config::instance().arch.word_bits / 8));
    istate.inited_int_regs++;
    [[maybe_unused]] auto ret =
        gen_load_store_addr_(gen_warp, istate.instr, istate, false);
    assert(ret);
    return GenExitReason::MemAddrGen;

  } else if (istate.inited_fp_regs < gen_warp->num_fp_regs()) {
    if (state_.get_num_instrs() > Config::instance().max_num_instrs) {
      istate.terminating_warp = true;
      return GenExitReason::Step;
    }

    // initialize floating-point registers
    const auto fd = riscv_fpr_t(istate.inited_fp_regs);

    if (BugFilter::instance().filter("wb_zero") && fd == riscv_fpr_t::FT0) {
      istate.inited_fp_regs++;
      return GenExitReason::Step;
    }

    // fld/flw fd, offset(sgpr_baseaddr_reg)
    if (Config::instance().arch.word_bits == 64) {
      istate.meta = riscv_meta_list::instance().instr(riscv_instr_name_t::FLD);
    } else {
      istate.meta = riscv_meta_list::instance().instr(riscv_instr_name_t::FLW);
    }
    istate.instr = riscv_instr(istate.meta);
    istate.instr.set_fd(fd);
    istate.instr.set_rs1(istate.sgpr_baseaddr_reg);
    istate.instr.set_imm(
        int64_t(gen_warp->num_int_regs() + istate.inited_fp_regs) *
        (Config::instance().arch.word_bits / 8));
    istate.inited_fp_regs++;
    [[maybe_unused]] auto ret =
        gen_load_store_addr_(gen_warp, istate.instr, istate, false);
    assert(ret);
    return GenExitReason::MemAddrGen;

  } else if (istate.inited_vec_regs < gen_warp->num_vec_regs()) {
    if (state_.get_num_instrs() > Config::instance().max_num_instrs) {
      istate.terminating_warp = true;
      return GenExitReason::Step;
    }

    // initialize vector registers
    if (riscv_vreg_t(istate.inited_vec_regs) == istate.vgpr_baseaddr_reg) {
      istate.inited_vec_regs++;
      return GenExitReason::Step;
    }

#if defined(ARCH_VENTUS)
    // vlw.v vd, offset(vgpr_baseaddr_reg)
    const auto vd = riscv_vreg_t(istate.inited_vec_regs);

    istate.meta =
        riscv_meta_list::instance().instr(riscv_instr_name_t::VLW12_V);
    istate.instr = riscv_instr(istate.meta);
    istate.instr.set_vd(vd);
    istate.instr.set_vs1(istate.vgpr_baseaddr_reg);
    istate.instr.set_imm(int64_t(istate.inited_vec_regs) *
                         (Config::instance().arch.word_bits / 8));
#else
    std::abort();
#endif

    istate.inited_vec_regs++;
    [[maybe_unused]] auto ret =
        gen_load_store_addr_(gen_warp, istate.instr, istate, false);
    assert(ret);
    return GenExitReason::MemAddrGen;
  }
  return GenExitReason::EndOfInit;
}

}