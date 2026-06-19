#include "isa/riscv/riscv_instr.hh"
#include "isa/riscv/riscv_meta.hh"

#include "bug_filter.hh"
#include "generator.hh"

namespace fuzzgpu {

bool Generator::gen_bcc_(const std::shared_ptr<GenWarp> &gen_warp,
                         riscv_instr &instr) {
  const auto rt_warp_gid = gen_warp->warp_gid();

  const auto nr_threads_per_gen = gen_warp->tmask().size();
  difftest::ThreadMask then_mask(nr_threads_per_gen),
      else_mask(nr_threads_per_gen);
  bool thread_diverged;

  auto branch_cond_flipped = [&]() {
    std::swap(then_mask, else_mask);
    // it doesn't change the divergence behavior
  };

  if (Config::instance().emu_guide) {
    // we need emulate the branch condition in oracle
    gen_warp->emulate_until(gen_warp->scratchpad.tell().begin, true);

    spdlog::debug("bcc tmask = {}", gen_warp->tmask());

    // @clobber then_mask
    // @clobber else_mask
    // @clobber thread_diverged
    // @clobber genwarp_divergence_mixed  Returns false if either all
    // the warps are thread-diverged, or none of them is
    // thread-diverged.
    auto get_rt_taken = [&]() {
      then_mask.reset();
      else_mask.reset();
      thread_diverged = false;

      difftest::RegId rs1_id, rs2_id;
#if defined(ARCH_VENTUS)
      if (instr.meta()->is_vector()) {
        const auto vsew = Config::instance().arch.word_bits;
        rs1_id = difftest::RegId(difftest::RegType::Vec, uint32_t(instr.vs1()),
                                 vsew);
        rs2_id = difftest::RegId(difftest::RegType::Vec, uint32_t(instr.vs2()),
                                 vsew);
      } else
#endif
      {
        rs1_id = difftest::RegId(difftest::RegType::Int, uint32_t(instr.rs1()));
        rs2_id = difftest::RegId(difftest::RegType::Int, uint32_t(instr.rs2()));
      }

      spdlog::debug("rs1 id = {}, rs2 id = {}", rs1_id.idx, rs2_id.idx);

      unsigned last_taken = unsigned(-1);
      bool scalar_thread = false;
      foreach_thread_lid(gen_warp, thread_lid) {
        if (instr.meta()->applied_tmask()) {
          if (!gen_warp->tmask().test(thread_lid) ||
              thread_lid < gen_warp->tstart()) {
            continue;
          }
        } else {
          scalar_thread = true;
        }

        // get the operand values and emulate the branch condition
        bool thread_taken = instr.meta()->emulate_branch_cond(
            state_.isa_emu.reg_read(rt_warp_gid, thread_lid, rs1_id),
            state_.isa_emu.reg_read(rt_warp_gid, thread_lid, rs2_id));

        if (last_taken == unsigned(-1)) {
          last_taken = !!thread_taken;
        } else if (last_taken != !!thread_taken) {
          thread_diverged = true;
        }

        if (thread_taken) {
          then_mask.set(thread_lid);
        } else {
          else_mask.set(thread_lid);
        }

        if (scalar_thread)
          break;
      } // for each thread

      // scalar insn shouldn't cause thread divergence
      assert(instr.meta()->applied_tmask() || !thread_diverged);
    };

    get_rt_taken();

    bool expected_thread_divergent =
        thread_diverged || (instr.meta()->applied_tmask() &&
                            state_.uniform_dist(state_.rng) <
                                Config::instance().branch_divergent_prob);

    // resample until the divergence behavior matches or timeout
    size_t resample_times = 0;
    while ((thread_diverged != expected_thread_divergent) &&
           resample_times < Config::instance().max_resample_times) {
      // resample the instruction opcode
      const auto &bcc_meta_list =
          riscv_meta_list::instance().instr_per_category(
              riscv_instr_category_t::BRANCH);
      const auto &bcc_meta =
          bcc_meta_list[std::uniform_int_distribution<size_t>(
              0, bcc_meta_list.size() - 1)(state_.rng)];
      instr = riscv_instr(bcc_meta);
      // resample the instruction operands
      instr.meta()->randomize(state_, *gen_warp, &instr, {}, {}, true);
      // re-emulate
      get_rt_taken();

      ++resample_times;
    };

    if (thread_diverged != expected_thread_divergent) {
      state_.stat.rejected_thread_divergence_preference++;
    } else {
      state_.stat.accepted_thread_divergence_preference++;
    }

    spdlog::debug("then mask = {}, else mask = {}", then_mask, else_mask);

    //
    // guide the branch direction to match the expected distribution
    //
    bool neg_bcc = false;
    if (then_mask.none() || else_mask.none()) {
      // we can force whether it's taken or not
      bool expected_taken = state_.uniform_dist(state_.rng) <
                            Config::instance().branch_taken_prob;
      if (expected_taken != (then_mask.any())) {
        neg_bcc = true;
      }
    } else {
      // both sides are taken, we can only guide the majorly-taken side
      bool expected_major_taken = state_.uniform_dist(state_.rng) <
                                  Config::instance().branch_major_taken_prob;
      if (then_mask.count() < else_mask.count()) {
        neg_bcc = expected_major_taken;
      } else {
        neg_bcc = !expected_major_taken;
      }
    }

    if (neg_bcc) {
      // replace the branch with its opposite one
      riscv_instr opposite_instr(instr.meta()->opposite_branch());
      if (opposite_instr.meta()->is_vector()) {
        opposite_instr.set_vs1(instr.vs1());
        opposite_instr.set_vs2(instr.vs2());
      } else {
        opposite_instr.set_rs1(instr.rs1());
        opposite_instr.set_rs2(instr.rs2());
      }
      instr = opposite_instr;
      spdlog::debug("neg");
      branch_cond_flipped();
    }
    spdlog::debug("end of reshape, then mask = {}, else mask = {}", then_mask,
                  else_mask);

  } else {
    //
    // randomly generate the masks (for ablation study)
    //
    then_mask.reset();
    else_mask.reset();
    if (instr.meta()->applied_tmask()) {
      foreach_thread_lid(gen_warp, thread_lid) {
        if (!gen_warp->tmask().test(thread_lid) ||
            thread_lid < gen_warp->tstart()) {
          continue;
        }
        if (state_.uniform_dist(state_.rng) < 0.5) {
          then_mask.set(thread_lid);
        } else {
          else_mask.set(thread_lid);
        }
      }
    }
    if (!instr.meta()->applied_tmask()) {
      if (state_.uniform_dist(state_.rng) < 0.5) {
        then_mask = gen_warp->tmask();
      } else {
        else_mask = gen_warp->tmask();
      }
    }
    thread_diverged = (then_mask.any() && else_mask.any());
  }

  MemoryBlock tgt_mem_blk;
  GenWarp::BasicBlockSnapshot new_bb_snapshot;
  difftest::ThreadMask tgt_bb_utmask;

  //
  // allocate the basic block memory for the branch target
  //

  bool bb_dead = false;
  unsigned tgt_num_instrs;
  size_t bb_sz;
  if (then_mask.any()) {
    // these are unused when pinned_tgt_bb
    tgt_num_instrs = state_.nr_tgt_instr_dist(state_.rng);
    bb_sz = get_basic_block_sz_(tgt_num_instrs);
  } else {
// throw an exception in the dead basic block
#if defined(ARCH_VORTEX) || defined(ARCH_VENTUS)
    tgt_num_instrs = riscv_meta::imm_setter_max_instrs() + 1; // li + jmp
#endif
    bb_sz = tgt_num_instrs * riscv_meta::ilen;
    bb_dead = true;
  }

  bool target_flipped = false;
  uint64_t pc_tsplit;
  size_t it = 0;
  std::optional<riscv_reg_t> reg_to_mask;
  bool failed;
  for (;;) {
    const auto &trampoline = branch_candidates_[it];

    uint64_t codesnippet_pc = gen_warp->scratchpad.tell().begin;
    std::vector<const riscv_instr *> codesnippet;
    auto emit = [&](const riscv_instr *i) {
      codesnippet.push_back(i);
      auto ret = codesnippet_pc;
      codesnippet_pc += riscv_meta::ilen;
      return ret;
    };

    auto ret = trampoline.emit_prologue(gen_warp, &instr, thread_diverged, emit,
                                        reg_to_mask, [&]() {
                                          branch_cond_flipped();
                                          target_flipped ^= true;
                                        });
    uint64_t pc_jmp;
    if (trampoline.jmp_range == std::make_pair(uint64_t(0), uint64_t(-1))) {
      pc_jmp = 0; // reg-indirect jump
    } else {
      pc_jmp = std::get<0>(ret);
    }
    pc_tsplit = std::get<1>(ret);

    tgt_mem_blk = state_.mem_alloc.allocate(
        MemBlkType::CODE, pc_jmp, trampoline.jmp_range.first,
        trampoline.jmp_range.second, bb_sz, riscv_meta::instr_alignment);
    failed = !tgt_mem_blk.valid();

    if (failed) {
      // try other trampoline types
      it = (it + 1) % branch_candidates_.size();
      if (!it) {

        spdlog::warn("Branch: Cannot allocate memory for the next "
                     "basic block");
        std::abort();
        goto out_bcc;
      }
      continue;
    }

    state_.mem_alloc.shrink(tgt_mem_blk, bb_sz);

    trampoline.emit_epilogue(ret, gen_warp, emit, tgt_mem_blk.begin);

    assert(codesnippet.size() <= get_max_branch_trampoline_num_instr_());
    for (const auto *i : codesnippet) {
      gen_warp->scratchpad.emit(i);
    }
    if (reg_to_mask.has_value()) {
      gen_warp->mask_int_reg(*reg_to_mask);
    }
    break;
  }
  assert(!failed);
out_bcc:
  if (failed) {
    // reject this branch generation, this should be rare
    state_.stat.memalloc_failed_branch_generations++;
    return false;
  }
  //
  // after this line all the operations should be emitted, while
  // operations may be cancelled before
  //
  new_bb_snapshot = GenWarp::BasicBlockSnapshot(tgt_num_instrs, tgt_mem_blk);
  gen_warp->nr_gen_bb++;

  state_.stat.succ_branch_generations++;

  difftest::ThreadMask orig_tmask = gen_warp->tmask();

  spdlog::debug("final then mask = {}, else mask = {} instr = {}", then_mask,
                else_mask, magic_enum::enum_name(instr.meta()->instr_name));

  if (instr.meta()->is_vector()) {
    spdlog::debug("vs1 = {}, vs2 = {}", (unsigned)instr.vs1(),
                  (unsigned)instr.vs2());
  } else {
    spdlog::debug("rs1 = {}, rs2 = {}", (unsigned)instr.rs1(),
                  (unsigned)instr.rs2());
  }

  if (Config::instance().emu_guide) {
    // now emulate the emitted branch in oracle
    gen_warp->emulate_until(pc_tsplit, false);
  } else {
    // for ablation study, randomly pick a path
    if (state_.uniform_dist(state_.rng) < 0.5) {
      // taken
      *gen_warp->tmask_proxy() = then_mask.any() ? then_mask : else_mask;
    } else {
      // not taken
      *gen_warp->tmask_proxy() = else_mask.any() ? else_mask : then_mask;
    }
  }

  bool goto_tgt, goto_then;
  if (instr.meta()->applied_tmask()) {
    assert(((gen_warp->tmask() == then_mask) ||
            (gen_warp->tmask() == else_mask)) &&
           "isa emulator mismatch");
    goto_then = (gen_warp->tmask() == then_mask);
    goto_tgt = goto_then ^ target_flipped;
  } else {
    goto_then = then_mask.any();
    assert((!Config::instance().emu_guide ||
            ((!goto_then &&
              gen_warp->emu_state.next_pc == pc_tsplit + riscv_meta::ilen) ||
             (goto_then &&
              gen_warp->emu_state.next_pc != pc_tsplit + riscv_meta::ilen))) &&
           "isa emulator mismatch"); // tgt_addr != fallthrough_addr
    goto_tgt = goto_then ^ target_flipped;
  }

  //
  // Switch to the next basic block
  //

  assert(thread_diverged == (then_mask.any() && else_mask.any()));
  if (thread_diverged) {
    state_.stat.thread_divergence_branch_generations++;

    // determine the next path for the gen warp
    const auto next_tmask = goto_then ? then_mask : else_mask;

    const auto ntaken_utmask = ~gen_warp->tmask() & orig_tmask;

    // push reconvergence and not-taken thread mask onto the stack
    gen_warp->simt_stack.emplace({
        .else_mem_blk = goto_tgt ? gen_warp->scratchpad.tell() : tgt_mem_blk,
        .orig_bb_snapshot = goto_tgt ? gen_warp->bb_snapshot : new_bb_snapshot,
        .orig_tmask = orig_tmask,
        .else_tmask = ntaken_utmask,
#if defined(ARCH_VENTUS)
        .pc_vbranch = pc_tsplit,
        .vsetrpc_clobber_reg = *reg_to_mask,
#endif
    });

    spdlog::debug("! Branch caused thread divergence:"
                  "\n\tthen_mask {}\n\telse_mask {}\n\tnext_mask {}"
                  "\nSIMT stack push:"
                  "\n\tgoto_tgt {}\n\tgoto_then {}\n\telse pc {:#x}"
                  "\n\torig_bb_snapshot {}"
                  "\n\torig_tmask {}\n\telse_tmask {}",
                  then_mask, else_mask, next_tmask, goto_tgt, goto_then,
                  gen_warp->simt_stack.top().else_mem_blk.begin,
                  gen_warp->simt_stack.top().orig_bb_snapshot,
                  gen_warp->simt_stack.top().orig_tmask,
                  gen_warp->simt_stack.top().else_tmask);
  }

  uint64_t goto_pc;
  if (goto_tgt) {
    assert(!bb_dead);
    goto_pc = tgt_mem_blk.begin;
    gen_warp->switch_basic_block(tgt_mem_blk, new_bb_snapshot,
                                 gen_warp->tmask());
  } else {
    // continue in the current basic block
    goto_pc = gen_warp->scratchpad.tell().begin;
    spdlog::debug("{} falling through the branch",
                  magic_enum::enum_name(instr.meta()->instr_name));
  }

  if (bb_dead) {
    gen_warp->dead_basic_blocks.push_back(std::make_pair(tgt_mem_blk, goto_pc));
  }
  return true;
}

unsigned GenWarp::join_reconv_point_instrs() {
#if defined(ARCH_VENTUS)
  // see Generator::eof_basic_block_()
  return riscv_meta::imm_setter_max_instrs() + 1; // la + jr
#else
  return 0;
#endif
}

/***
 * @brief Apply misalignment randomization to max free degree
 * @param gen_warp The warp context
 * @param tgt_addr The original target address
 * @return The potentially misaligned address
 */
static uint64_t randomize_addr(GenWarp &gen_warp, uint64_t tgt_addr) {
  if (!BugFilter::instance().filter("jalr_align")) {
    auto &state_ = gen_warp.fuzzer_state();
    uint64_t rand_bit =
        std::uniform_int_distribution<uint64_t>(0, 1)(state_.rng);
    return (tgt_addr & ~1ULL) | rand_bit;
  }
  return tgt_addr;
}

/***
 * @brief Generate the join instruction and handle the simt stack
 * reconvergence
 * @param join_instr The join instruction to emit (nullptr for gen reconv.)
 * @param next_bb_mem_blk_ The next basic block memory block (in/out)
 * @param next_bb_snapshot_ The next basic block snapshot (in/out)
 * @param next_bb_utmask_ The next basic block unioned thread mask (in/out)
 * @return true if reconverged, false if need to redirect to the else path
 */
bool GenWarp::gen_join(riscv_instr *join_instr, MemoryBlock &next_bb_mem_blk_,
                       GenWarp::BasicBlockSnapshot &next_bb_snapshot_,
                       difftest::ThreadMask &next_bb_utmask_) {
  auto &simt_top = simt_stack.top();

  assert(join_instr);
  const auto pc_join = scratchpad.emit(join_instr);

#if defined(ARCH_VENTUS)
  if (Config::instance().emu_guide) {
    if (simt_top.fallthrough) {
      fuzzer_state_.isa_emu.ventus_set_simt_stack_top_else_pc(
          warp_gid(), pc_join /* rpc */);
    }
    fuzzer_state_.isa_emu.ventus_set_simt_stack_top_rpc(warp_gid(), pc_join);
  }
#endif

  // emulate this join instruction to update the utmask
  emulate_until(pc_join, false);

#if defined(ARCH_VENTUS)
  // see Generator::eof_basic_block_()
  const unsigned jt_num_instrs = join_reconv_point_instrs();
  const auto jt_sz = jt_num_instrs * riscv_meta::ilen;
#endif

  if (simt_top.fallthrough) {
    if (Config::instance().emu_guide) {
      assert(tmask() == simt_top.orig_tmask &&
             "isa emulator's SIMT stack behavior mismatch");
    } else {
      *tmask_proxy() = simt_top.orig_tmask;
    }

#if defined(ARCH_VENTUS)
    auto old_pos = scratchpad.tell();

    //
    // relocate the join trampoline
    //
    const auto new_pos =
        MemoryBlock(MemBlkType::CODE, simt_top.pc_join_trampoline,
                    simt_top.pc_join_trampoline + jt_sz);
    scratchpad.seek(new_pos);
    const auto t0 = simt_stack.top().pc_join_trampoline_clobber_reg;

    uint64_t actual_pc_join = randomize_addr(*this, pc_join);
    riscv_meta::emit_imm_setter(t0, actual_pc_join, *this,
                                [&](riscv_instr *instr) {
                                  scratchpad.emit(instr, true /* overwrite */);
                                },
                                {});
    riscv_instr jalr(
        riscv_meta_list::instance().instr(riscv_instr_name_t::JALR));
    jalr.set_rd(riscv_reg_t::ZERO);
    jalr.set_rs1(t0);
    jalr.set_imm(0);
    scratchpad.emit(&jalr, true /* overwrite */);

    //
    // relocate the setrpc
    //
    std::vector<riscv_instr *> codesnippet;
    const auto t1 = simt_stack.top().vsetrpc_clobber_reg;

    riscv_meta::emit_imm_setter(
        t1, pc_join, *this,
        [&](riscv_instr *instr) { codesnippet.push_back(instr); }, {});
    riscv_instr setrpc_instr(
        riscv_meta_list::instance().instr(riscv_instr_name_t::SETRPC));
    setrpc_instr.set_rs1(t1);
    setrpc_instr.set_imm(0);
    codesnippet.push_back(&setrpc_instr);

    const auto setrpc_sz = codesnippet.size() * riscv_meta::ilen;
    const auto new_setrpc_pos =
        MemoryBlock(MemBlkType::CODE, simt_stack.top().pc_vbranch - setrpc_sz,
                    simt_stack.top().pc_vbranch - 1);
    scratchpad.seek(new_setrpc_pos);
    for (auto instr : codesnippet) {
      scratchpad.emit(instr, true /* overwrite */);
    }
    assert(scratchpad.tell().begin == simt_stack.top().pc_vbranch);

    scratchpad.seek(old_pos);
#endif

    simt_stack.pop();
    // reconverged
    return true;

  } else {
    // redirect the generation to the else path
    next_bb_mem_blk_ = simt_top.else_mem_blk;
    next_bb_snapshot_ = simt_top.orig_bb_snapshot;
    next_bb_utmask_ = simt_top.else_tmask;

    if (Config::instance().emu_guide) {
      assert(tmask() == next_bb_utmask_ &&
             "isa emulator's SIMT stack behavior mismatch");
    } else {
      *tmask_proxy() = next_bb_utmask_;
    }

#if defined(ARCH_VENTUS)
    simt_stack.top().pc_join_trampoline = pc_join;
    scratchpad.overwrite_from(pc_join);
    //
    // reserve a join trampoline at the reconvergence point
    //
    for (unsigned i = 0; i < jt_num_instrs; i++) {
      riscv_instr nop(
          riscv_meta_list::instance().instr(riscv_instr_name_t::NOP));
      scratchpad.emit(&nop);
    }
    // pre-allocate the register
    simt_stack.top().pc_join_trampoline_clobber_reg =
        pick_rd({riscv_reg_t::ZERO});
    mask_int_reg(simt_stack.top().pc_join_trampoline_clobber_reg);
#endif

    simt_top.fallthrough = true;
    return false;
  }
}

/***
 * @brief Emit branch instruction with prologue if needed
 * @return PC of the branch instruction
 */
static uint64_t emit_bcc_prologue(
    const std::shared_ptr<GenWarp> &gen_warp, riscv_instr *binstr,
    bool thread_diverged, std::function<uint64_t(const riscv_instr *)> emit,
    std::optional<riscv_reg_t> &reg_to_mask, uint64_t &tsplit_pc) {
  auto &state_ = gen_warp->fuzzer_state();
#if defined(ARCH_VORTEX)
  // emit a split instruction before the branch when needed
  if (thread_diverged) {
    auto t0 =
        gen_warp->pick_rd({riscv_reg_t::ZERO, binstr->rs1(), binstr->rs2()});

    bool neg_cond = false;
    switch (binstr->meta()->instr_name) {
    case riscv_instr_name_t::BNE:
      neg_cond = true;
      [[fallthrough]];
    case riscv_instr_name_t::BEQ: {
      auto xor_instr = state_.instr_pool.create(
          riscv_meta_list::instance().instr(riscv_instr_name_t::XOR));
      xor_instr->set_rd(t0);
      xor_instr->set_rs1(binstr->rs1());
      xor_instr->set_rs2(binstr->rs2());
      emit(xor_instr);

      auto sltiu = state_.instr_pool.create(
          riscv_meta_list::instance().instr(riscv_instr_name_t::SLTIU));
      sltiu->set_rd(t0);
      sltiu->set_rs1(t0);
      sltiu->set_imm(1);
      emit(sltiu);
    } break;

    case riscv_instr_name_t::BGE:
      neg_cond = true;
      [[fallthrough]];
    case riscv_instr_name_t::BLT: {
      auto slt_instr = state_.instr_pool.create(
          riscv_meta_list::instance().instr(riscv_instr_name_t::SLT));
      slt_instr->set_rd(t0);
      slt_instr->set_rs1(binstr->rs1());
      slt_instr->set_rs2(binstr->rs2());
      emit(slt_instr);
    } break;

    case riscv_instr_name_t::BGEU:
      neg_cond = true;
      [[fallthrough]];
    case riscv_instr_name_t::BLTU: {
      auto sltu_instr = state_.instr_pool.create(
          riscv_meta_list::instance().instr(riscv_instr_name_t::SLTU));
      sltu_instr->set_rd(t0);
      sltu_instr->set_rs1(binstr->rs1());
      sltu_instr->set_rs2(binstr->rs2());
      emit(sltu_instr);
    } break;

    default:
      std::abort();
    }

    auto split_instr =
        state_.instr_pool.create(riscv_meta_list::instance().instr(
            neg_cond ? riscv_instr_name_t::VX_SPLIT_N
                     : riscv_instr_name_t::VX_SPLIT));
    split_instr->set_rd(riscv_reg_t::ZERO);
    split_instr->set_rs1(t0);
    tsplit_pc = emit(split_instr);
  }
  const auto pc_branch = emit(binstr);
  if (!thread_diverged)
    tsplit_pc = pc_branch;
  return pc_branch;

#elif defined(ARCH_VENTUS)
  reg_to_mask.reset();
  if (thread_diverged) {
    // reserved for the later relocation
    std::vector<riscv_reg_t> exclude_regs = {riscv_reg_t::ZERO};
    if (!binstr->meta()->is_vector()) {
      exclude_regs.insert(exclude_regs.end(), {binstr->rs1(), binstr->rs2()});
    }
    reg_to_mask = gen_warp->pick_rd(exclude_regs);

    for (unsigned i = 0;
         i < riscv_meta::imm_setter_max_instrs() + 1 /* la + setrpc */; ++i) {
      auto nop = state_.instr_pool.create(
          riscv_meta_list::instance().instr(riscv_instr_name_t::NOP));
      emit(nop);
    }
  }
  const auto pc_branch = emit(binstr);
  tsplit_pc = pc_branch;
  return pc_branch;
#endif
}

void Generator::init_trampoline_candidates_() {
  trampoline_candidates_ = {
      {// branch
       .jmp_range = riscv_meta_list::instance()
                        .instr(riscv_instr_name_t::BEQ)
                        ->get_tgt_addr_range(),
       .pc_jmp_offset = 0,
       .jmp_instr = riscv_instr_name_t::BEQ,
       .emit =
           [](const std::shared_ptr<GenWarp> &gen_warp, uint64_t tgt_addr) {
             riscv_instr beq(
                 riscv_meta_list::instance().instr(riscv_instr_name_t::BEQ));
             beq.set_rs1(riscv_reg_t::ZERO);
             beq.set_rs2(riscv_reg_t::ZERO);
             beq.set_tgt_addr(gen_warp->scratchpad.tell().begin, tgt_addr);
             gen_warp->scratchpad.emit(&beq);
           }},

      {// PC-relative JAL
       .jmp_range = riscv_meta_list::instance()
                        .instr(riscv_instr_name_t::JAL)
                        ->get_tgt_addr_range(),
       .pc_jmp_offset = 0,
       .jmp_instr = riscv_instr_name_t::JAL,
       .emit =
           [](const std::shared_ptr<GenWarp> &gen_warp, uint64_t tgt_addr) {
             riscv_instr jal(
                 riscv_meta_list::instance().instr(riscv_instr_name_t::JAL));
             jal.set_rd(riscv_reg_t::ZERO);
             jal.set_tgt_addr(gen_warp->scratchpad.tell().begin, tgt_addr);
             gen_warp->scratchpad.emit(&jal);
           }},

      {// reg-indirect JALR
       .jmp_range = {0, uint64_t(-1)},
       .pc_jmp_offset = uint64_t(-1),
       .jmp_instr = riscv_instr_name_t::JALR,
       .emit =
           [](const std::shared_ptr<GenWarp> &gen_warp, uint64_t tgt_addr) {
             auto r = gen_warp->pick_rd({riscv_reg_t::ZERO});

             uint64_t actual_tgt_addr = randomize_addr(*gen_warp, tgt_addr);

             // FIXME: consider LUI (upper 20bits) + JALR (lower 12bits)
             // patterns
             riscv_meta::emit_imm_setter(
                 r, actual_tgt_addr, *gen_warp,
                 [&](riscv_instr *instr) { gen_warp->scratchpad.emit(instr); },
                 {});

             riscv_instr jalr(
                 riscv_meta_list::instance().instr(riscv_instr_name_t::JALR));
             jalr.set_rd(riscv_reg_t::ZERO);
             jalr.set_rs1(r);
             jalr.set_imm(0);
             gen_warp->scratchpad.emit(&jalr);
           }},
  };

  for (size_t type = 0; type < trampoline_candidates_.size(); ++type) {
    trampoline_type_[trampoline_candidates_[type].jmp_instr] = type;
  }

  branch_candidates_ = {
      {// branch
       .jmp_range = riscv_meta_list::instance()
                        .instr(riscv_instr_name_t::BEQ)
                        ->get_tgt_addr_range(),
       .emit_prologue =
           [](const std::shared_ptr<GenWarp> &gen_warp, riscv_instr *instr,
              bool thread_diverged,
              const std::function<uint64_t(const riscv_instr *)> &emit,
              std::optional<riscv_reg_t> &reg_to_mask,
              const std::function<void()> &branch_cond_flipped) {
             uint64_t tsplit_pc;
             auto pc_branch =
                 emit_bcc_prologue(gen_warp, instr, thread_diverged, emit,
                                   reg_to_mask, tsplit_pc);
             return std::make_tuple(pc_branch, tsplit_pc, instr);
           },
       .emit_epilogue =
           [](std::tuple<uint64_t, uint64_t, riscv_instr *> prologue_ret,
              const std::shared_ptr<GenWarp> &gen_warp,
              const std::function<uint64_t(const riscv_instr *)> &emit,
              uint64_t tgt_addr) {
             std::get<2>(prologue_ret)
                 ->set_tgt_addr(std::get<0>(prologue_ret), tgt_addr);
           }},

      {// branch + JAL
       .jmp_range = riscv_meta_list::instance()
                        .instr(riscv_instr_name_t::JAL)
                        ->get_tgt_addr_range(),
       .emit_prologue =
           [](const std::shared_ptr<GenWarp> &gen_warp, riscv_instr *instr,
              bool thread_diverged,
              const std::function<uint64_t(const riscv_instr *)> &emit,
              std::optional<riscv_reg_t> &reg_to_mask,
              const std::function<void()> &branch_cond_flipped) {
             // replace the original branch with its opposite branch
             riscv_instr opposite_branch(instr->meta()->opposite_branch());
             if (instr->meta()->is_vector()) {
               opposite_branch.set_vs1(instr->vs1());
               opposite_branch.set_vs2(instr->vs2());
             } else {
               opposite_branch.set_rs1(instr->rs1());
               opposite_branch.set_rs2(instr->rs2());
             }
             *instr = opposite_branch;
             branch_cond_flipped();
             uint64_t tsplit_pc;
             auto pc_branch =
                 emit_bcc_prologue(gen_warp, instr, thread_diverged, emit,
                                   reg_to_mask, tsplit_pc);
             instr->set_tgt_addr(pc_branch, pc_branch + 2 * riscv_meta::ilen);

             auto jmp_to_tgt = gen_warp->fuzzer_state().instr_pool.create(
                 riscv_meta_list::instance().instr(riscv_instr_name_t::JAL));
             jmp_to_tgt->set_rd(riscv_reg_t::ZERO);
             const auto pc_jal = pc_branch + riscv_meta::ilen;
             // mp_to_tgt->set_tgt_addr(pc_jal, tgt_addr);
             [[maybe_unused]] const auto actual_pc_jal = emit(jmp_to_tgt);
             assert(actual_pc_jal == pc_jal);
             return std::make_tuple(pc_jal, tsplit_pc, jmp_to_tgt);
           },

       .emit_epilogue =
           [](std::tuple<uint64_t, uint64_t, riscv_instr *> prologue_ret,
              const std::shared_ptr<GenWarp> &gen_warp,
              const std::function<uint64_t(const riscv_instr *)> &emit,
              uint64_t tgt_addr) {
             std::get<2>(prologue_ret)
                 ->set_tgt_addr(std::get<0>(prologue_ret), tgt_addr);
           }},

      {// branch + JALR
       .jmp_range = {0, uint64_t(-1)},
       .emit_prologue =
           [](const std::shared_ptr<GenWarp> &gen_warp, riscv_instr *instr,
              bool thread_diverged,
              const std::function<uint64_t(const riscv_instr *)> &emit,
              std::optional<riscv_reg_t> &reg_to_mask,
              const std::function<void()> &branch_cond_flipped) {
             // replace the original branch with its opposite branch
             riscv_instr opposite_branch(instr->meta()->opposite_branch());
             if (instr->meta()->is_vector()) {
               opposite_branch.set_vs1(instr->vs1());
               opposite_branch.set_vs2(instr->vs2());
             } else {
               opposite_branch.set_rs1(instr->rs1());
               opposite_branch.set_rs2(instr->rs2());
             }
             *instr = opposite_branch;
             branch_cond_flipped();
             uint64_t tsplit_pc;
             const auto pc_branch =
                 emit_bcc_prologue(gen_warp, instr, thread_diverged, emit,
                                   reg_to_mask, tsplit_pc);
             return std::make_tuple(pc_branch, tsplit_pc, instr);
           },

       .emit_epilogue =
           [](std::tuple<uint64_t, uint64_t, riscv_instr *> prologue_ret,
              const std::shared_ptr<GenWarp> &gen_warp,
              const std::function<uint64_t(const riscv_instr *)> &emit,
              uint64_t tgt_addr) {
             auto t0 = gen_warp->pick_rd({riscv_reg_t::ZERO});
             std::vector<riscv_instr *> la_instrs;

             uint64_t actual_tgt_addr = randomize_addr(*gen_warp, tgt_addr);

             riscv_meta::emit_imm_setter(
                 t0, actual_tgt_addr, *gen_warp,
                 [&](riscv_instr *instr) { la_instrs.push_back(instr); }, {});

             std::get<2>(prologue_ret)
                 ->set_tgt_addr(std::get<0>(prologue_ret),
                                std::get<0>(prologue_ret) +
                                    (1 + la_instrs.size() + 1) *
                                        riscv_meta::ilen);

             // generate a LA
             for (auto &pi : la_instrs) {
               emit(pi);
             }
             auto jmp_to_tgt = gen_warp->fuzzer_state().instr_pool.create(
                 riscv_meta_list::instance().instr(riscv_instr_name_t::JALR));
             jmp_to_tgt->set_rd(riscv_reg_t::ZERO);
             jmp_to_tgt->set_rs1(t0);
             jmp_to_tgt->set_imm(0);
             emit(jmp_to_tgt);
           }},
  };
}

size_t
Generator::get_trampoline_type_(const std::shared_ptr<riscv_meta> &meta) const {
  assert(trampoline_type_.find(meta->instr_name) != trampoline_type_.end() &&
         "unknon jmp instruction");
  return trampoline_type_.find(meta->instr_name)->second;
}

bool Generator::check_trampoline_range_(
    uint64_t pc_jmp, const std::pair<uint64_t, uint64_t> &jmp_range,
    uint64_t pc_tgt) {
  if (jmp_range == std::make_pair(uint64_t(0), uint64_t(-1))) {
    // reg-indirect jump
    return true;
  }
  uint64_t lower = pc_jmp > jmp_range.first ? pc_jmp - jmp_range.first : 0;
  size_t upper = saturated_add(pc_jmp, jmp_range.second);
  return (pc_tgt >= lower && pc_tgt <= upper);
}

size_t Generator::get_max_branch_trampoline_num_instr_() const {
#if defined(ARCH_VORTEX)
  return 1 /*bcc*/ + 2 + 1 + riscv_meta::imm_setter_max_instrs() +
         1; // (xor+seqz, ...) + split + la + jalr or split + jal
#elif defined(ARCH_VENTUS)
  return 1 /*bcc*/ +
         std::max(riscv_meta::imm_setter_max_instrs() + 1, // li + setrpc
                  2 * riscv_meta::imm_setter_max_instrs() +
                      2); //  li + setrpc + la + jalr
#endif
}

size_t Generator::get_max_jump_trampoline_num_instr_() const {
  return 1 + riscv_meta::imm_setter_max_instrs(); // la
}

} // namespace fuzzgpu
