#include "bug_filter.hh"
#include "isa/riscv/riscv_instr.hh"
#include "isa/riscv/riscv_meta.hh"

#include "generator.hh"

namespace fuzzgpu {

difftest::BarrierScope Generator::max_support_barrier_scope_() const {
#if defined(ARCH_VORTEX)
  return difftest::BarrierScope::Workgroup;
#elif defined(ARCH_VENTUS)
  return difftest::BarrierScope::Workgroup;
#else
#error
#endif
}

difftest::BarrierScope Generator::min_support_barrier_scope_() const {
#if defined(ARCH_VORTEX)
  if ((Config::instance().arch.riscv_exts_mask & difftest::Arch::vortex_gbar) &&
      !BugFilter::instance().filter("global_bar")) {
    return difftest::BarrierScope::Global;
  }
  return difftest::BarrierScope::Workgroup;
#elif defined(ARCH_VENTUS)
  return difftest::BarrierScope::Workgroup;
#endif
}

//-

BarrierScopePrefix
Generator::get_barrier_scope_id_(difftest::WarpGID rt_warp_gid) {
  BarrierScopePrefix ret;
#if defined(ARCH_VORTEX)
  ret.resize(unsigned(difftest::BarrierScope::Warp) + 1);
  ret[size_t(difftest::BarrierScope::Global)] =
      scheduler_->vortex_get_cluster_gid(rt_warp_gid);
  ret[size_t(difftest::BarrierScope::Workgroup)] =
      scheduler_->vortex_get_core_gid(rt_warp_gid);
  ret[size_t(difftest::BarrierScope::Warp)] = rt_warp_gid;
#elif defined(ARCH_VENTUS)
  ret.resize(unsigned(difftest::BarrierScope::Workgroup) + 1);
  ret[size_t(difftest::BarrierScope::Workgroup)] =
      rt_warp_gid / Config::instance().num_genwarps_per_block;
#endif
  return ret;
}

void Generator::dump_barrier_scope_id_(const BarrierScopePrefix &scope_id,
                                       fmt::memory_buffer &buf) const {
  fmt::format_to(std::back_inserter(buf), "[");
  for (unsigned bs = 0; bs < scope_id.size(); ++bs) {
    fmt::format_to(std::back_inserter(buf), "{}:{} ",
                   magic_enum::enum_name(difftest::BarrierScope(bs)),
                   scope_id[bs]);
  }
  fmt::format_to(std::back_inserter(buf), "]");
}

void ScopedWarpTree::insert_all_warps(
    std::unordered_set<difftest::WarpGID> &ret) const {
  if (warp_gid.has_value())
    ret.insert(warp_gid.value());
  for (const auto &child : children) {
    child->insert_all_warps(ret);
  }
}

void Generator::gen_barrier_plan_(
    const std::shared_ptr<ScopedWarpTree> &warps) {
  double new_barrier_prob;
#if defined(ARCH_VORTEX)
  new_barrier_prob = state_.get_instr_prob(riscv_instr_name_t::VX_BAR);
#elif defined(ARCH_VENTUS)
  new_barrier_prob = state_.get_instr_prob(riscv_instr_name_t::BARRIER);
#endif
  // estiminated max. num
  const size_t u_num_barriers =
      Config::instance().max_num_instrs * new_barrier_prob;
  spdlog::info("Estiminated barriers: {}", u_num_barriers);

  barrier_plan_.clear();
  barrier_num_.clear();
  barrier_num_.resize(unsigned(max_support_barrier_scope_()) + 1, 0);
  for (unsigned i = 0; i < u_num_barriers; i++) {
    std::uniform_int_distribution<unsigned> iscope_dist(
        (unsigned)min_support_barrier_scope_(),
        (unsigned)max_support_barrier_scope_());
    const auto iscope = iscope_dist(state_.rng);

    if (barrier_num_[iscope] >= Config::instance().arch.num_barriers) {
      // cannot create more barriers due to hardware resource
      // limitation, give up
      continue;
    }
    const auto barrier_id =
        (barrier_num_[iscope]++) % Config::instance().arch.num_barriers;

    const auto scope = difftest::BarrierScope(iscope);

    // randomly select a child
    auto cur = warps;
    while (cur->scope != scope) {
      assert(cur->scope < scope);
      std::uniform_int_distribution<size_t> child_id_dist(
          0, cur->children.size() - 1);
      const auto child_id = child_id_dist(state_.rng);
      cur = cur->children.at(child_id);
    }

    std::vector<size_t> sel_participants;
    for (size_t i = 0; i < cur->children.size(); ++i) {
      sel_participants.push_back(i);
    }

    // randomly select partial participants if the target supports
    assert(sel_participants.size() > 0);
#if defined(ARCH_VORTEX)
    const auto count = std::uniform_int_distribution<size_t>(
        1, sel_participants.size())(state_.rng);
    // top-k Fisher-Yates shuffe
    for (unsigned i = 0; i < count; ++i) {
      std::uniform_int_distribution<unsigned> dist(i,
                                                   sel_participants.size() - 1);
      unsigned j = dist(state_.rng);
      std::swap(sel_participants[i], sel_participants[j]);
    }
    sel_participants.resize(count);
#elif defined(ARCH_VENTUS)
    const auto count = sel_participants.size();
#endif

    barrier_plan_.emplace_back(scope, barrier_id, count);

    for (const auto &obj_id : sel_participants) {
      const auto &obj = cur->children.at(obj_id);
      obj->insert_all_warps(barrier_plan_.back().participants);
    }
  }
}

void Generator::gen_barrier_init_warp_(const std::shared_ptr<GenWarp> &gen_warp,
                                       unsigned genwarp_idx) {

  if (scheduler_->grid_level_begins(genwarp_idx)) {
    gen_barrier_plan_(
        scheduler_->get_scoped_warp_tree_per_grid_level(genwarp_idx));
  }

  gen_warp->barrier_scope_id = get_barrier_scope_id_(gen_warp->warp_gid());
  gen_warp->nr_pending_barriers = barrier_plan_.size();
  fmt::memory_buffer ss;
  dump_barrier_scope_id_(gen_warp->barrier_scope_id, ss);
  spdlog::info("Warp GID {} barrier scope id: {}", gen_warp->warp_gid(),
               fmt::to_string(ss));
  spdlog::info("Warp GID {} has {} pending barriers", gen_warp->warp_gid(),
               gen_warp->nr_pending_barriers);
  gen_warp->barrier_instr_idx = 0;
}

bool Generator::gen_barrier_(const std::shared_ptr<GenWarp> &gen_warp,
                             riscv_instr &instr) {

  auto emit_barrier = [&](difftest::BarrierScope scope, unsigned barrier_id,
                          size_t count) {
    uint64_t pc_barrier;
#if defined(ARCH_VORTEX)
    // ser barrier id
    uint32_t encoded_bar_id;
    switch (scope) {
    case difftest::BarrierScope::Global:
      assert(barrier_id < 0x80000000);
      encoded_bar_id = barrier_id | 0x80000000;
      break;
    case difftest::BarrierScope::Workgroup:
      encoded_bar_id = barrier_id;
      break;
    default:
      std::abort();
    }
    const auto t0 = gen_warp->pick_rd({riscv_reg_t::ZERO});
    riscv_meta::emit_imm_setter(
        t0, encoded_bar_id, *gen_warp,
        [&](riscv_instr *instr) { gen_warp->scratchpad.emit(instr); }, {});
    instr.set_rs1(t0);

    // set barrier cout
    const auto t1 = gen_warp->pick_rd({t0, riscv_reg_t::ZERO});
    riscv_meta::emit_imm_setter(
        t1, count, *gen_warp,
        [&](riscv_instr *instr) { gen_warp->scratchpad.emit(instr); }, {});
    instr.set_rs2(t1);

    // emit an nop to skip the emulation of this barrier instruction,
    // avoiding stalling the emulator since other warps have not been
    // generated yet!
    riscv_instr nop_instr(
        riscv_meta_list::instance().instr(riscv_instr_name_t::NOP));
    pc_barrier = gen_warp->scratchpad.emit(&nop_instr);
    gen_warp->emulate_until(gen_warp->scratchpad.tell().begin, true);

    gen_warp->scratchpad.overwrite_from(pc_barrier);
    gen_warp->scratchpad.emit(&instr, true /* overwrite */);

#elif defined(ARCH_VENTUS)
    // emit an nop to skip the emulation of this barrier instruction,
    // avoiding stalling the emulator since other warps have not been
    // generated yet!
    riscv_instr nop_instr(
        riscv_meta_list::instance().instr(riscv_instr_name_t::NOP));
    pc_barrier = gen_warp->scratchpad.emit(&nop_instr);
    gen_warp->emulate_until(gen_warp->scratchpad.tell().begin, true);

    if (!BugFilter::instance().filter("inactive_warp_barrier")) {
      gen_warp->scratchpad.overwrite_from(pc_barrier);
      gen_warp->scratchpad.emit(&instr, true /* overwrite */);
    }
#endif

    barrier_meta_.barriers[gen_warp->warp_gid()][pc_barrier] =
        difftest::BarrierMeta::Entry{scope, barrier_id, count};

    state_.stat.num_generated_barriers++;
  };

  if (!Config::instance().barrier_plan) {
    // for ablation study
    // randomly generate barriers
    std::uniform_int_distribution<unsigned> iscope_dist(
        (unsigned)min_support_barrier_scope_(),
        (unsigned)max_support_barrier_scope_());
    const auto iscope = iscope_dist(state_.rng);
    const auto scope = difftest::BarrierScope(iscope);
    const auto barrier_id = std::uniform_int_distribution<unsigned>(
        0, Config::instance().arch.num_barriers - 1)(state_.rng);
    unsigned max_count = 1;
#if defined(ARCH_VORTEX)
    if (scope == difftest::BarrierScope::Global)
      max_count = Config::instance().arch.vortex_nr_cores_per_cluster;
    else
      max_count = Config::instance().arch.vortex_nr_warps_per_core;
#elif defined(ARCH_VENTUS)
    max_count = 1; // unused
#endif
    const auto count =
        std::uniform_int_distribution<size_t>(1, max_count)(state_.rng);
    emit_barrier(scope, barrier_id, count);
    return true;
  }

  while (gen_warp->barrier_instr_idx < gen_warp->nr_pending_barriers) {

    const auto &barrier_info = barrier_plan_.at(gen_warp->barrier_instr_idx);
    if (barrier_info.participants.count(gen_warp->warp_gid()) == 0) {
      // this warp is not a participant of this barrier
      gen_warp->barrier_instr_idx++;
      continue;
    }

    const auto scope = barrier_info.scope;
    const auto barrier_id = barrier_info.barrier_id;
    size_t count = barrier_info.count;
    emit_barrier(scope, barrier_id, count);

    gen_warp->barrier_instr_idx++;
    return true;
  }

  return false;
}

} // namespace fuzzgpu
