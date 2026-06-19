#ifdef ARCH_VENTUS

#include <cmath>
#include <random>
#include <tuple>
#include <vector>

#include "isa/riscv/riscv_amo_meta.hh"
#include "isa/riscv/riscv_instr.hh"
#include "isa/riscv/riscv_meta.hh"
#include "isa/riscv/riscv_meta_list.hh"
#include "isa/riscv/ventus/csr.hh"

#include "scheduler_ventus.hh"

namespace fuzzgpu {

/**
 * @return  the prime factorization of N as a vector of pairs: (prime_factor,
 * exponent)
 */
static std::vector<std::pair<int64_t, int>> factor(int64_t N) {
  std::vector<std::pair<int64_t, int>> res;
  if (N <= 1)
    return res;
  // extract factors of 2 first
  int cnt = 0;
  while ((N & 1) == 0) {
    N >>= 1;
    ++cnt;
  }
  if (cnt)
    res.emplace_back(2, cnt);
  // process odd factors
  for (int64_t p = 3; p * p <= N; p += 2) {
    if (N % p == 0) {
      cnt = 0;
      while (N % p == 0) {
        N /= p;
        ++cnt;
      }
      res.emplace_back(p, cnt);
    }
  }
  if (N > 1)
    res.emplace_back(N, 1);
  return res;
}

/**
 * @brief Randomly generate x, y, z such that x * y * z == N
 */
static std::tuple<int64_t, int64_t, int64_t> random_xyz(int64_t N,
                                                        std::mt19937_64 &rng) {
  if (N <= 0)
    throw std::invalid_argument("N must be positive");
  auto fs = factor(N);
  // Randomly partition each prime factor's exponent among x, y, and z
  int64_t x = 1, y = 1, z = 1;
  for (auto &[p, e] : fs) {
    // generate a multinomial random partition of e into three buckets
    std::vector<int> cnt(3, 0);
    for (int i = 0; i < e; i++) {
      std::uniform_int_distribution<int> d(0, 2);
      ++cnt[d(rng)];
    }
    // multiply the corresponding powers into x, y, z
    auto powp = [p](int k) {
      int64_t r = 1, b = p;
      while (k) {
        if (k & 1)
          r *= b;
        b *= b;
        k >>= 1;
      }
      return r;
    };
    x *= powp(cnt[0]);
    y *= powp(cnt[1]);
    z *= powp(cnt[2]);
  }
  return {x, y, z};
}

VentusScheduler::VentusScheduler(FuzzerState &state,
                                 const std::string &trace_file_prefix_,
                                 unsigned sgpr_usage,  unsigned num_int_regs, unsigned num_fp_regs, unsigned vgpr_usage,
                                 unsigned vlmul, unsigned num_activate_threads)
  : state_(state) {

  // Allocate the leader block (LB) BEFORE the warps' memory blocks
  // which acts as a software-defined CTA scheduler (ventus works only this
  // way).
  // All the cores will execute the following code at startup:
  //  csrr t0, CSR_WGID
  //  imm_setter t1, max_wg_entry_sz
  //  mul t0, t0, t1
  //  auipc t1, 0
  //  add t1, t0, t1
  //  jalr zero, t1, 12
  //
  //  foreach wg in genblocks
  //    wg_entry:
  //    csrr t0, CSR_WID
  //    imm_setter t1, max_wf_entry_sz
  //    mul t0, t0, t1
  //    auipc t1, 0
  //    add t1, t0, t1
  //    jalr zero, t1, 12
  //
  //    foreach wf in genwarps(wg)
  //      wf_entry:
  //      imm_setter t0, entry_addr(wf)
  //      jmp zero, t0
  //      ...
  //       (garbage paddings)
  //

  max_wf_entry_sz_ =
      (1 + riscv_meta::imm_setter_max_instrs()) * riscv_meta::ilen;

  const auto wg_entry_prologue_sz =
      (5 + riscv_meta::imm_setter_max_instrs()) * riscv_meta::ilen;

  max_wg_entry_sz_ =
      wg_entry_prologue_sz +
      max_wf_entry_sz_ * Config::instance().num_genwarps_per_block;

  const auto lb_prologue_sz =
      (5 + riscv_meta::imm_setter_max_instrs()) * riscv_meta::ilen;

  size_t lb_sz =
      lb_prologue_sz + max_wg_entry_sz_ * Config::instance().num_genblocks;

  assert(Config::instance().arch.startup_addr % riscv_meta::instr_alignment ==
             0 &&
         "startup addr must be aligned");
  lb_mem_blk_ = state_.mem_alloc.allocate(
      MemBlkType::CODE, Config::instance().arch.startup_addr, lb_sz);
  assert(lb_mem_blk_.valid() && "failed to allocate leader block");
  state_.mem_alloc.shrink(lb_mem_blk_, lb_sz);

  //
  // allocate the workgroup size and wavefront size
  // 
  auto [wg_x, wg_y, wg_z] =
      random_xyz(Config::instance().num_genblocks, state_.rng);
  kernel_size_[0] = wg_x;
  kernel_size_[1] = wg_y;
  kernel_size_[2] = wg_z;

  state_.isa_emu.bootstrap(lb_mem_blk_.begin, kernel_size_,
                           Config::instance().num_genwarps_per_block,
                           num_activate_threads, sgpr_usage, vgpr_usage);

  // leader warp used to launch workgroups (like a bootloader)
  leader_gen_warp_ =
      std::make_shared<GenWarp>(state_, trace_file_prefix_, num_int_regs, num_fp_regs,
                                vgpr_usage, vlmul, num_activate_threads);
  leader_gen_warp_->tmask_proxy()->reset();
  for (unsigned i = 0; i < num_activate_threads; ++i) {
    leader_gen_warp_->tmask_proxy()->set(i);
  }

  // the first warp acts as the leader warp, it will be executed by all the
  // cores after resetting
  leader_gen_warp_->scratchpad.seek(lb_mem_blk_);
  spdlog::debug("emit leader warp");
  auto t0 = leader_gen_warp_->pick_rd({riscv_reg_t::ZERO});
  auto t1 = leader_gen_warp_->pick_rd({riscv_reg_t::ZERO, t0});
  riscv_instr csrr_t1_WGID(
      riscv_meta_list::instance().instr(riscv_instr_name_t::CSRRS));
  csrr_t1_WGID.set_rd(t0);
  csrr_t1_WGID.set_csr(CSR_WGID);
  csrr_t1_WGID.set_rs1(riscv_reg_t::ZERO);
  leader_gen_warp_->scratchpad.emit(&csrr_t1_WGID);

  riscv_meta::emit_imm_setter(
      t1, max_wg_entry_sz_, *leader_gen_warp_,
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

  const auto pc_wg_list_first_entry = leader_gen_warp_->scratchpad.tell().begin;

  spdlog::debug("emit wg list");
  wg_.resize(Config::instance().num_genblocks);

  for (size_t wg_idx = 0; wg_idx < wg_.size(); wg_idx++) {
    const auto starta = pc_wg_list_first_entry + wg_idx * max_wg_entry_sz_;
    leader_gen_warp_->scratchpad.seek(
        MemoryBlock(MemBlkType::CODE, starta, starta + wg_entry_prologue_sz));

    auto t0 = leader_gen_warp_->pick_rd({riscv_reg_t::ZERO});
    auto t1 = leader_gen_warp_->pick_rd({riscv_reg_t::ZERO, t0});
    riscv_instr csrr_t1_WID(
        riscv_meta_list::instance().instr(riscv_instr_name_t::CSRRS));
    csrr_t1_WID.set_rd(t0);
    csrr_t1_WID.set_csr(CSR_WID);
    csrr_t1_WID.set_rs1(riscv_reg_t::ZERO);
    leader_gen_warp_->scratchpad.emit(&csrr_t1_WID);

    riscv_meta::emit_imm_setter(
        t1, max_wf_entry_sz_, *leader_gen_warp_,
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

    wg_[wg_idx].first_wf_entry_addr = leader_gen_warp_->scratchpad.tell().begin;
  }
}

void VentusScheduler::add_warp(size_t genwarp_idx, uint64_t entry_addr,
                               const std::shared_ptr<GenWarp> &gen_warp) {

  const auto wg_id = genwarp_idx / Config::instance().num_genwarps_per_block;
  const auto wf_id = genwarp_idx % Config::instance().num_genwarps_per_block;
  const auto starta = wg_[wg_id].first_wf_entry_addr + wf_id * max_wf_entry_sz_;
  gen_warp->scratchpad.seek(
      MemoryBlock(MemBlkType::CODE, starta, starta + max_wf_entry_sz_));

  const auto t0 = gen_warp->pick_rd({riscv_reg_t::ZERO});
  riscv_meta::emit_imm_setter(
      t0, entry_addr, *gen_warp,
      [&](riscv_instr *instr) { gen_warp->scratchpad.emit(instr); }, {});

  riscv_instr jmp_to_entry(
      riscv_meta_list::instance().instr(riscv_instr_name_t::JALR));
  jmp_to_entry.set_rd(riscv_reg_t::ZERO);
  jmp_to_entry.set_imm(0);
  jmp_to_entry.set_rs1(t0);
  const auto pc_jmp = gen_warp->scratchpad.emit(&jmp_to_entry);

  gen_warp->set_warp_gid(genwarp_idx);

  //
  // sanity check
  //
  leader_gen_warp_->set_warp_gid(gen_warp->warp_gid());
  leader_gen_warp_->emulate_until(pc_jmp, false);
  leader_gen_warp_->flush_trace();

  assert(state_.isa_emu.schedulable(gen_warp->warp_gid()));
}

void VentusScheduler::emit_end_of_warp(
    size_t genwarp_idx, const std::shared_ptr<GenWarp> &gen_warp) {

  riscv_instr endprg(
      riscv_meta_list::instance().instr(riscv_instr_name_t::ENDPRG));
  gen_warp->scratchpad.emit(&endprg);
}

bool VentusScheduler::grid_level_begins(size_t genwarp_idx) {
  return genwarp_idx == 0;
}

std::shared_ptr<ScopedWarpTree>
VentusScheduler::get_scoped_warp_tree_per_grid_level(
    unsigned num_generated_warps) const {
  // gpu     : global * 1
  // global  : workgroup * wg_.size()
  // workgroup   : warps * warps_this_wg
  auto gpu = std::make_shared<ScopedWarpTree>();
  gpu->scope = difftest::BarrierScope::GPU;
  auto gl = std::make_shared<ScopedWarpTree>();
  gl->scope = difftest::BarrierScope::Global;

  for (unsigned wg_id = 0; wg_id < wg_.size(); wg_id++) {
    auto local = std::make_shared<ScopedWarpTree>();
    local->scope = difftest::BarrierScope::Workgroup;
    const auto nr_warps_this_wg =
        std::min(Config::instance().num_genwarps_per_block,
                 Config::instance().num_genwarps -
                     wg_id * Config::instance().num_genwarps_per_block);

    for (unsigned warp_id = 0; warp_id < nr_warps_this_wg; warp_id++) {
      auto warp = std::make_shared<ScopedWarpTree>();
      warp->scope = difftest::BarrierScope::Warp;
      warp->warp_gid =
          wg_id * Config::instance().num_genwarps_per_block + warp_id;
      local->children.push_back(warp);
    }
    gl->children.push_back(local);
  }
  gpu->children.push_back(gl);

  return gpu;
}

difftest::WarpGID VentusScheduler::max_warp_gid() {
  return Config::instance().num_genwarps;
}

/**
 * @brief get only the warps of the scheduler
 */
const std::vector<std::shared_ptr<GenWarp>> VentusScheduler::gen_warps() const {
  std::vector<std::shared_ptr<GenWarp>> res;
  res.push_back(leader_gen_warp_);
  return res;
}

} // namespace fuzzgpu

#endif