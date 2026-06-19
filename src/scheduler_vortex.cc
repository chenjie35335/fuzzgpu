#ifdef ARCH_VORTEX

#include "isa/riscv/riscv_amo_meta.hh"
#include "isa/riscv/riscv_instr.hh"
#include "isa/riscv/riscv_meta.hh"
#include "isa/riscv/riscv_meta_list.hh"
#include "isa/riscv/vortex/csr.hh"

#include "scheduler_vortex.hh"

namespace fuzzgpu {

VortexScheduler::VortexScheduler(FuzzerState &state,
                                 const std::string &trace_file_prefix_,
                                 unsigned vlmul, unsigned num_active_threads)
  : state_(state)
  , genwarp_ids_per_core_(Config::instance().arch.vortex_nr_total_cores)
  , gen_cores_per_cluster_(Config::instance().arch.vortex_nr_total_clusters)
  , vortex_nr_parallel_warps_per_core(Config::instance().arch.vortex_nr_warps_per_core) {

  leader_gen_warp_ = std::make_shared<GenWarp>(
      state_, trace_file_prefix_, 32, 32, 0, vlmul, num_active_threads);
  leader_gen_warp_->tmask_proxy()->reset();
  leader_gen_warp_->tmask_proxy()->set(0); // only the first thread is active

  // Allocate the leader block (LB) BEFORE the warps' memory blocks
  // which acts as a software-defined CTA scheduler (vortex works only this
  // way).
  // All the cores will execute the following code at startup:
  //  csrr t0, VX_CSR_CORE_ID
  //  imm_setter t1, max_wspawn_list_core_entry_sz
  //  mul t0, t0, t1
  //  auipc t1, 0
  //  add t1, t0, t1
  //  jalr zero, t1, 12
  //
  //  wspawn list:
  //  foreach core entry:
  //     foreach grid entry `g` per core:
  //       imm_setter t0, vortex_nr_parallel_warps_per_core
  //       imm_setter t1, entry_addr(g)
  //       wspawn t0, t1
  //       jal tra, t1
  //     tmc zero
  //     ...
  //     (garbage paddings)
  //
  // grid code:
  //  csrr t0, VX_CSR_WARP_ID
  //  imm_setter t1, max_warp_entry_sz
  //  mul t0, t0, t1
  //  auipc t1, 0
  //  add t1, t0, t1
  //  jalr zero, t1, 12
  //
  // grid list:
  // foreacg warp 'w' per grid:
  //    imm_setter t0, entry_addr(w)
  //    jmp t0
  //    ...
  //     (garbage paddings)
  //  tmc zero
  nr_grids_ = std::ceil(double(Config::instance().num_genwarps) /
                        vortex_nr_parallel_warps_per_core);
  max_nr_grids_per_core_ = std::ceil(
      double(nr_grids_) / Config::instance().arch.vortex_nr_total_cores);
  max_wspawn_list_grid_entry_sz_ =
      riscv_meta::ilen * (2 * riscv_meta::imm_setter_max_instrs() + 2);
  max_wspawn_list_core_entry_sz_ =
      max_wspawn_list_grid_entry_sz_ * max_nr_grids_per_core_ +
      riscv_meta::ilen;
  auto lb_sz =
      riscv_meta::ilen * (5 + riscv_amo_meta::imm_setter_max_instrs()) +
      max_wspawn_list_core_entry_sz_ *
          Config::instance().arch.vortex_nr_total_cores;

  assert(Config::instance().arch.startup_addr % riscv_meta::instr_alignment ==
             0 &&
         "startup addr must be aligned");
  lb_mem_blk_ = state_.mem_alloc.allocate(
      MemBlkType::CODE, Config::instance().arch.startup_addr, lb_sz);
  state_.mem_alloc.shrink(lb_mem_blk_, lb_sz);

  assert(lb_mem_blk_.valid());
  state_.isa_emu.bootstrap(lb_mem_blk_.begin);

  // the first warp acts as the leader warp, it will be executed by all the
  // cores after resetting
  leader_gen_warp_->scratchpad.seek(lb_mem_blk_);
  {
    spdlog::debug("emit leader warp");
    auto t0 = leader_gen_warp_->pick_rd({riscv_reg_t::ZERO});
    auto t1 = leader_gen_warp_->pick_rd({riscv_reg_t::ZERO, t0});
    riscv_instr csrr_t1_VX_CSR_CORE_ID(
        riscv_meta_list::instance().instr(riscv_instr_name_t::CSRRS));
    csrr_t1_VX_CSR_CORE_ID.set_rd(t0);
    csrr_t1_VX_CSR_CORE_ID.set_csr(VX_CSR_CORE_ID);
    csrr_t1_VX_CSR_CORE_ID.set_rs1(riscv_reg_t::ZERO);
    leader_gen_warp_->scratchpad.emit(&csrr_t1_VX_CSR_CORE_ID);

    riscv_meta::emit_imm_setter(
        t1, max_wspawn_list_core_entry_sz_, *leader_gen_warp_,
        [&](riscv_instr *instr) { leader_gen_warp_->scratchpad.emit(instr); },
        {t0});

    riscv_instr mul_t0(
        riscv_meta_list::instance().instr(riscv_instr_name_t::MUL));
    mul_t0.set_rd(t0);
    mul_t0.set_rs1(t0);
    mul_t0.set_rs2(t1);
    leader_gen_warp_->scratchpad.emit(&mul_t0);

    riscv_instr auipc_t1(
        riscv_meta_list::instance().instr(riscv_instr_name_t::AUIPC));
    auipc_t1.set_rd(t1);
    auipc_t1.set_imm(0);
    leader_gen_warp_->scratchpad.emit(&auipc_t1);

    riscv_instr add_t1(
        riscv_meta_list::instance().instr(riscv_instr_name_t::ADD));
    add_t1.set_rd(t1);
    add_t1.set_rs1(t0);
    add_t1.set_rs2(t1);
    leader_gen_warp_->scratchpad.emit(&add_t1);

    riscv_instr jalr_zero_t1(
        riscv_meta_list::instance().instr(riscv_instr_name_t::JALR));
    jalr_zero_t1.set_rd(riscv_reg_t::ZERO);
    jalr_zero_t1.set_rs1(t1);
    jalr_zero_t1.set_imm(12);
    leader_gen_warp_->scratchpad.emit(&jalr_zero_t1);

    first_core_entry_pc_ = leader_gen_warp_->scratchpad.tell().begin;
  }

  // create the grids
  max_warp_entry_sz_ =
      (1 + riscv_meta::imm_setter_max_instrs()) * riscv_meta::ilen;

  grid_gen_warps_.resize(nr_grids_);
  for (unsigned g = 0; g < nr_grids_; ++g) {
    grid_gen_warps_[g].gen_warp = std::make_shared<GenWarp>(
        state_, trace_file_prefix_, 32, 32, 0, vlmul, num_active_threads);

    const auto warps_this_grid =
        std::min(vortex_nr_parallel_warps_per_core,
                 Config::instance().num_genwarps -
                     g * vortex_nr_parallel_warps_per_core);

    const auto grid_sz =
        (5 + riscv_meta::imm_setter_max_instrs()) * riscv_meta::ilen +
        max_warp_entry_sz_ * warps_this_grid;

    grid_gen_warps_[g].mem_blk =
        state_.mem_alloc.allocate(MemBlkType::CODE, 0, 0, uint64_t(-1), grid_sz,
                                  riscv_meta::instr_alignment);
    assert(grid_gen_warps_[g].mem_blk.valid());
    state_.mem_alloc.shrink(grid_gen_warps_[g].mem_blk, grid_sz);

    auto &grid = grid_gen_warps_[g].gen_warp;

    grid->scratchpad.seek(grid_gen_warps_[g].mem_blk);

    spdlog::debug("emit grid {} prologue", g);

    auto t0 = grid->pick_rd({riscv_reg_t::ZERO});
    auto t1 = grid->pick_rd({riscv_reg_t::ZERO, t0});
    riscv_instr csrr_t1_VX_CSR_WARP_ID(
        riscv_meta_list::instance().instr(riscv_instr_name_t::CSRRS));
    csrr_t1_VX_CSR_WARP_ID.set_rd(t0);
    csrr_t1_VX_CSR_WARP_ID.set_csr(VX_CSR_WARP_ID);
    csrr_t1_VX_CSR_WARP_ID.set_rs1(riscv_reg_t::ZERO);
    grid->scratchpad.emit(&csrr_t1_VX_CSR_WARP_ID);

    riscv_meta::emit_imm_setter(
        t1, max_warp_entry_sz_, *grid,
        [&](riscv_instr *instr) { grid->scratchpad.emit(instr); }, {t0});

    riscv_instr mul_t0(
        riscv_meta_list::instance().instr(riscv_instr_name_t::MUL));
    mul_t0.set_rd(t0);
    mul_t0.set_rs1(t0);
    mul_t0.set_rs2(t1);
    grid->scratchpad.emit(&mul_t0);

    riscv_instr auipc_t1(
        riscv_meta_list::instance().instr(riscv_instr_name_t::AUIPC));
    auipc_t1.set_rd(t1);
    auipc_t1.set_imm(0);
    grid->scratchpad.emit(&auipc_t1);

    riscv_instr add_t1(
        riscv_meta_list::instance().instr(riscv_instr_name_t::ADD));
    add_t1.set_rd(t1);
    add_t1.set_rs1(t0);
    add_t1.set_rs2(t1);
    grid->scratchpad.emit(&add_t1);

    riscv_instr jalr_zero_t1(
        riscv_meta_list::instance().instr(riscv_instr_name_t::JALR));
    jalr_zero_t1.set_rd(riscv_reg_t::ZERO);
    jalr_zero_t1.set_rs1(t1);
    jalr_zero_t1.set_imm(12);
    grid->scratchpad.emit(&jalr_zero_t1);

    grid_gen_warps_[g].first_warp_entry_pc = grid->scratchpad.tell().begin;
    // the generation of the grid prologue is eariler than the wspawn (where it
    // links the return address), while the executation order is reversed thus
    // we don't wanna t0 or t1 be used as the return address register, otherwire
    // they may be clobbered by the grid prologue!
    grid_gen_warps_[g].exclude_regs.push_back(t0);
    grid_gen_warps_[g].exclude_regs.push_back(t1);
  }

  //
  // add grids to the cores' leader warp's wspawn lists
  //

  for (vortex::CoreGID vortex_core_gid = 0;
       vortex_core_gid < Config::instance().arch.vortex_nr_total_cores;
       vortex_core_gid++) {

    // add garbage paddings to align to this core's entry
    auto expected_core_entry_pc =
        first_core_entry_pc_ + vortex_core_gid * max_wspawn_list_core_entry_sz_;

    assert(leader_gen_warp_->scratchpad.tell().begin <= expected_core_entry_pc);

    for (size_t pad_pc = leader_gen_warp_->scratchpad.tell().begin;
         pad_pc < expected_core_entry_pc; pad_pc += riscv_meta::ilen) {
      riscv_instr padding(
          riscv_meta_list::instance().instr(riscv_instr_name_t::NOP));
      leader_gen_warp_->scratchpad.emit(&padding);
    }
    assert(leader_gen_warp_->scratchpad.tell().begin == expected_core_entry_pc);

    //
    // grids per core
    //
    size_t grids_this_core;
    const auto grids_in_last_level =
        nr_grids_ % Config::instance().arch.vortex_nr_total_cores;
    if (!grids_in_last_level) {
      // full for each levels
      grids_this_core = max_nr_grids_per_core_;
    } else {
      grids_this_core = vortex_core_gid < grids_in_last_level
                            ? max_nr_grids_per_core_
                            : max_nr_grids_per_core_ - 1;
    }

    for (unsigned ii = 0; ii < grids_this_core; ++ii) {
      const auto grid_id =
          vortex_core_gid + ii * Config::instance().arch.vortex_nr_total_cores;
      assert(grid_id < nr_grids_);

      const auto required_num_warps = std::min(
          vortex_nr_parallel_warps_per_core,
          Config::instance().num_genwarps -
              grid_id * vortex_nr_parallel_warps_per_core);
      spdlog::debug("emit wspawn for core {} grid {}, wspawn {} warps",
                    vortex_core_gid, grid_id, required_num_warps);

      auto t0 = leader_gen_warp_->pick_rd({riscv_reg_t::ZERO});
      riscv_meta::emit_imm_setter(
          t0, required_num_warps, *leader_gen_warp_,
          [&](riscv_instr *instr) { leader_gen_warp_->scratchpad.emit(instr); },
          {});

      auto t1 = leader_gen_warp_->pick_rd({riscv_reg_t::ZERO, t0});

      riscv_meta::emit_imm_setter(
          t1, grid_gen_warps_[grid_id].mem_blk.begin, *leader_gen_warp_,
          [&](riscv_instr *instr) { leader_gen_warp_->scratchpad.emit(instr); },
          {t0});
      riscv_instr wspawn(
          riscv_meta_list::instance().instr(riscv_instr_name_t::VX_WSPAWN));
      wspawn.set_rs1(t0);
      wspawn.set_rs2(t1);
      leader_gen_warp_->scratchpad.emit(&wspawn);

      std::vector<riscv_reg_t> excluded_regs = {riscv_reg_t::ZERO};
      excluded_regs.insert(excluded_regs.end(),
                           grid_gen_warps_[grid_id].exclude_regs.begin(),
                           grid_gen_warps_[grid_id].exclude_regs.end());
      const auto tra = grid_gen_warps_[grid_id].ret_addr_reg =
          leader_gen_warp_->pick_rd(excluded_regs);
      grid_gen_warps_[grid_id].gen_warp->exclude_int_regs(tra);

      riscv_instr jalr(
          riscv_meta_list::instance().instr(riscv_instr_name_t::JALR));
      jalr.set_rd(tra);
      jalr.set_rs1(t1);
      jalr.set_imm(0);
      leader_gen_warp_->scratchpad.emit(&jalr);
    }
    riscv_instr tmc_zero(
        riscv_meta_list::instance().instr(riscv_instr_name_t::VX_TMC));
    tmc_zero.set_rs1(riscv_reg_t::ZERO);
    leader_gen_warp_->scratchpad.emit(&tmc_zero);
  }
}

vortex::CoreGID
VortexScheduler::vortex_get_core_gid(difftest::WarpGID rt_warp_gid) const {
  return (rt_warp_gid / Config::instance().arch.vortex_nr_warps_per_core);
}

vortex::ClusterGID
VortexScheduler::vortex_get_cluster_gid(difftest::WarpGID rt_warp_gid) const {
  const auto core_gid = vortex_get_core_gid(rt_warp_gid);
  return (core_gid / Config::instance().arch.vortex_nr_cores_per_cluster);
}

void VortexScheduler::add_warp(size_t genwarp_idx, uint64_t entry_addr,
                               const std::shared_ptr<GenWarp> &gen_warp) {

  const auto grid_gid =
      genwarp_idx / vortex_nr_parallel_warps_per_core;

  // vortex supports static warp scheduling only,
  // thus, we can resolve the core gid at generation time!
  const auto vortex_core_gid =
      grid_gid % Config::instance().arch.vortex_nr_total_cores;

  difftest::WarpGID leader_rt_warp_gid =
      0 + vortex_core_gid * Config::instance().arch.vortex_nr_warps_per_core;

  const auto warp_id_per_grid =
      genwarp_idx % vortex_nr_parallel_warps_per_core;
  //
  // add this warp to its grid's warp list
  //
  auto &grid = grid_gen_warps_[grid_gid].gen_warp;

  // add garbage paddings to align to this core's entry
  auto expected_warp_entry_pc = grid_gen_warps_[grid_gid].first_warp_entry_pc +
                                warp_id_per_grid * max_warp_entry_sz_;

  assert(grid->scratchpad.tell().begin <= expected_warp_entry_pc);

  for (size_t pad_pc = grid->scratchpad.tell().begin;
       pad_pc < expected_warp_entry_pc; pad_pc += riscv_meta::ilen) {
    riscv_instr padding(
        riscv_meta_list::instance().instr(riscv_instr_name_t::NOP));
    grid->scratchpad.emit(&padding);
  }
  assert(grid->scratchpad.tell().begin == expected_warp_entry_pc);

  const auto t0 = grid->pick_rd({riscv_reg_t::ZERO});
  riscv_meta::emit_imm_setter(
      t0, entry_addr, *grid,
      [&](riscv_instr *instr) { grid->scratchpad.emit(instr); }, {});
  riscv_instr jmp(riscv_meta_list::instance().instr(riscv_instr_name_t::JALR));
  jmp.set_rd(riscv_reg_t::ZERO);
  jmp.set_rs1(t0);
  jmp.set_imm(0);
  const auto pc_jmp = grid->scratchpad.emit(&jmp);

  if (!grid_gen_warps_[grid_gid].emulated_warp0) {
    // emulate the leader warp until activating the current warp
    leader_gen_warp_->set_warp_gid(leader_rt_warp_gid);
    leader_gen_warp_->emulate_until(pc_jmp, false);
    grid_gen_warps_[grid_gid].emulated_warp0 = true;

    leader_gen_warp_->flush_trace();
  } else {
    // remaining warps are already activated by the leader warp
  }

  //
  // sanity check
  //

  difftest::WarpGID rt_warp_gid =
      leader_rt_warp_gid +
      (genwarp_idx % vortex_nr_parallel_warps_per_core);

  assert(vortex_core_gid == vortex_get_core_gid(rt_warp_gid));

  assert(state_.isa_emu.schedulable(rt_warp_gid));

  gen_warp->set_warp_gid(rt_warp_gid);

  if (!warp_id_per_grid) {
    gen_warp->exclude_int_regs(grid_gen_warps_[grid_gid].ret_addr_reg);
  }

  spdlog::info("VortexScheduler: Genwarp {}  assigned to Warp GID {} core GID "
               "{} cluster "
               "GID {}, {}-th in grid GID {}",
               genwarp_idx, rt_warp_gid, vortex_core_gid,
               vortex_get_cluster_gid(rt_warp_gid), warp_id_per_grid, grid_gid);

  gen_warp->set_warp_gid(rt_warp_gid);
}

void VortexScheduler::emit_end_of_warp(
    size_t genwarp_idx, const std::shared_ptr<GenWarp> &gen_warp) {
  const auto grid_gid =
      genwarp_idx / vortex_nr_parallel_warps_per_core;

  const auto warp_id_per_grid =
      genwarp_idx % vortex_nr_parallel_warps_per_core;

  if (!warp_id_per_grid) {
    // disable all the threads expect the leader thread
    const auto t0 = gen_warp->pick_rd(
        {grid_gen_warps_[grid_gid].ret_addr_reg, riscv_reg_t::ZERO});
    riscv_meta::emit_imm_setter(
        t0, 1, *gen_warp,
        [&](riscv_instr *instr) { gen_warp->scratchpad.emit(instr); }, {});
    riscv_instr tmc_one(
        riscv_meta_list::instance().instr(riscv_instr_name_t::VX_TMC));
    tmc_one.set_rs1(t0);
    const auto pc_tmc = gen_warp->scratchpad.emit(&tmc_one);

    // get the updated tmask
    gen_warp->emulate_until(pc_tmc, false);

    riscv_instr jalr(
        riscv_meta_list::instance().instr(riscv_instr_name_t::JALR));
    jalr.set_rd(riscv_reg_t::ZERO);
    jalr.set_rs1(grid_gen_warps_[grid_gid].ret_addr_reg);
    jalr.set_imm(0);
    const auto pc_jalr = gen_warp->scratchpad.emit(&jalr);

    // dump trace until the end of the warp
    gen_warp->emulate_until(pc_jalr, false);

  } else {
    riscv_instr tmc_zero(
        riscv_meta_list::instance().instr(riscv_instr_name_t::VX_TMC));
    tmc_zero.set_rs1(riscv_reg_t::ZERO);
    const auto pc_tmc = gen_warp->scratchpad.emit(&tmc_zero);

    // get the updated tmask
    gen_warp->emulate_until(pc_tmc, false);

    // ensure that the warp emulation terminated when the generation
    // terminates, since we may not come back to this warp later
    // (e.g., go to the leader warp instead)
    assert(gen_warp->tmask().none() &&
           "not all warps terminated in the emulation");
  }
}

/**
 * @brief get the scoped warp tree per grid level beforing these warps are
 * added to the scheduler
 */
std::shared_ptr<ScopedWarpTree>
VortexScheduler::get_scoped_warp_tree_per_grid_level(
    unsigned num_generated_warps) const {

  const auto warps_per_grid_level =
      Config::instance().arch.vortex_nr_total_cores *
      vortex_nr_parallel_warps_per_core;
  const auto required_warps =
      std::min(Config::instance().num_genwarps - num_generated_warps,
               warps_per_grid_level);

  const auto warps_per_cluster =
      Config::instance().arch.vortex_nr_cores_per_cluster *
      vortex_nr_parallel_warps_per_core;

  const auto required_clusters =
      std::ceil(double(required_warps) / warps_per_cluster);

  const auto required_cores =
      std::ceil(double(required_warps) /
                vortex_nr_parallel_warps_per_core);

  spdlog::debug("num_generated_warps {} => required_clusters {} required_cores "
                "{} required_warps {}",
                num_generated_warps, required_clusters, required_cores,
                required_warps);

  // gpu     : global * vortex_nr_total_clusters
  // global  : workgroup * vortex_nr_cores_per_cluster
  // workgroup   : warps * vortex_nr_parallel_warps_per_core (<=
  // vortex_nr_warps_per_core)
  auto gpu = std::make_shared<ScopedWarpTree>();
  gpu->scope = difftest::BarrierScope::GPU;
  for (unsigned cluster_id = 0; cluster_id < required_clusters; cluster_id++) {
    auto gl = std::make_shared<ScopedWarpTree>();
    gl->scope = difftest::BarrierScope::Global;
    const auto nr_cores_per_cluster =
        (cluster_id + 1 == required_clusters)
            ? (required_cores -
               cluster_id * Config::instance().arch.vortex_nr_cores_per_cluster)
            : Config::instance().arch.vortex_nr_cores_per_cluster;

    for (unsigned core_id = 0; core_id < nr_cores_per_cluster; core_id++) {
      auto local = std::make_shared<ScopedWarpTree>();
      local->scope = difftest::BarrierScope::Workgroup;
      const auto nr_warps_per_core =
          (cluster_id + 1 == required_clusters &&
           core_id + 1 == nr_cores_per_cluster)
              ? (required_warps - cluster_id * warps_per_cluster -
                 core_id * vortex_nr_parallel_warps_per_core)
              : vortex_nr_parallel_warps_per_core;

      for (unsigned warp_id = 0; warp_id < nr_warps_per_core; warp_id++) {
        auto warp = std::make_shared<ScopedWarpTree>();
        warp->scope = difftest::BarrierScope::Warp;
        warp->warp_gid =
            cluster_id * Config::instance().arch.vortex_nr_cores_per_cluster *
                Config::instance().arch.vortex_nr_warps_per_core +
            core_id * Config::instance().arch.vortex_nr_warps_per_core +
            warp_id;
        local->children.push_back(warp);
      }
      gl->children.push_back(local);
    }
    gpu->children.push_back(gl);
  }
  return gpu;
}

/**
 * @brief get only the warps of the scheduler
 */
const std::vector<std::shared_ptr<GenWarp>> VortexScheduler::gen_warps() const {
  std::vector<std::shared_ptr<GenWarp>> res;
  res.push_back(leader_gen_warp_);
  for (const auto &grid : grid_gen_warps_) {
    res.push_back(grid.gen_warp);
  }
  return res;
}

/**
 * @return true if a new grid level begins
 */
bool VortexScheduler::grid_level_begins(size_t genwarp_idx) {
  const auto grid_gid =
      genwarp_idx / vortex_nr_parallel_warps_per_core;
  const auto warp_id_per_grid =
      genwarp_idx % vortex_nr_parallel_warps_per_core;

  return (grid_gid % Config::instance().arch.vortex_nr_total_cores == 0 &&
          warp_id_per_grid == 0);
}

difftest::WarpGID VortexScheduler::max_warp_gid() {
  return Config::instance().arch.vortex_nr_total_warps;
}

unsigned VortexScheduler::vortex_get_warp_id_per_grid(
    difftest::WarpGID rt_warp_gid) const {
  return rt_warp_gid % vortex_nr_parallel_warps_per_core;
}

unsigned
VortexScheduler::vortex_get_remaining_warps_per_core(vortex::CoreGID core_gid) {
  assert(vortex_nr_parallel_warps_per_core >=
         genwarp_ids_per_core_[core_gid].size());
  return vortex_nr_parallel_warps_per_core -
         genwarp_ids_per_core_[core_gid].size();
}

unsigned VortexScheduler::vortex_get_remaining_cores_per_cluster(
    vortex::ClusterGID cluster_gid) {
  assert(Config::instance().arch.vortex_nr_cores_per_cluster >=
         gen_cores_per_cluster_[cluster_gid].size());
  return Config::instance().arch.vortex_nr_cores_per_cluster -
         gen_cores_per_cluster_[cluster_gid].size();
}

} // namespace fuzzgpu

#endif // ARCH_VORTEX