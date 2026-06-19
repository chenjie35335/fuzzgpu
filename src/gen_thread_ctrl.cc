#include "bug_filter.hh"
#include "isa/riscv/riscv_instr.hh"
#include "isa/riscv/riscv_meta_list.hh"

#include "generator.hh"

namespace fuzzgpu {

Generator::ThreadCtrlGenStatus
Generator::gen_thread_ctrl_(const std::shared_ptr<GenWarp> &gen_warp,
                            riscv_instr &instr, MemoryBlock &next_bb_mem_blk,
                            GenWarp::BasicBlockSnapshot &next_bb_snapshot,
                            difftest::ThreadMask &next_bb_tmask) {
  const auto rt_warp_gid = gen_warp->warp_gid();

  if (instr.meta()->is_thread_join()) {
    //
    // Thread join
    //
    if (gen_warp->simt_stack.empty()) {
      // we have reached the top level, reject to join
      return ThreadCtrlGenStatus::Skip;
    }

#ifdef ARCH_VORTEX
    instr.set_rs1(riscv_reg_t::ZERO);
#endif

    if (!gen_warp->gen_join(&instr, next_bb_mem_blk, next_bb_snapshot,
                            next_bb_tmask)) {
      spdlog::debug("! Warp GID {} join: goto else path: next basic "
                    "block addr {:#x}, next utmask {}",
                    rt_warp_gid, next_bb_mem_blk.begin, next_bb_tmask);
      // not reconverged
      // end of the basic block, no trampoline needed
      return ThreadCtrlGenStatus::SwitchBasicBlock;
    }

  } else if (instr.meta()->is_thread_mask()) {
    //
    // Thread mask setting
    //
    auto next_tmask = gen_warp->tmask();

// we can't randomly enable a disabled thread, since
// it can alter the condition branch results and even change it from
// thread-divergent to non-divergent anymore in vortex
// the root-cause is that the split + bcc instr sequence is not atomic
// TODO: more accurate controling needed
#if 0
              // randomly set the thread mask
              unsigned num_flips = state_.nr_tmask_flip_dist(state_.rng);
              const auto &k_shuffled = state_.shuffle_thread_lid(num_flips);

              difftest::ThreadMask tmask(
                  Config::instance().arch.nr_threads_per_warp);
              for (size_t iwgid = 0; iwgid < rt_warp_gids.size(); ++iwgid) {
                if (!warp_has_activate_thread_(next_tmask, iwgid)) {
                  // no active thread in this warp, since it's deactivated by
                  // the warp-diverged branch
                  return false;
                }
                foreach_thread_lid(gen_warp, tlid) {
                  next_tmask.reset(GenWarp::get_uid(iwgid, tlid));
                }
                for (size_t iks = 0; iks < num_flips; ++iks) {
                  next_tmask.set(GenWarp::get_uid(iwgid, k_shuffled[iks]));
                  tmask.set(k_shuffled[iks]);
                }
              }
              assert(tmask.any());

              riscv_reg_t tmc_oper;
#ifdef ARCH_VORTEX 
                tmc_oper = instr.rs1();
#else
    std::abort();
#endif
              riscv_meta::emit_imm_setter(
                  tmc_oper, tmask.to_ulong(), *gen_warp,
                  [&](riscv_instr *instr) { gen_warp->scratchpad.emit(instr); },
                  {});
              gen_warp->scratchpad.emit(&instr);
#else
    return ThreadCtrlGenStatus::Skip;
#endif

  } else if (instr.meta()->is_barrier()) {
    //
    // Barrier
    //
    if (!gen_barrier_(gen_warp, instr)) {
      return ThreadCtrlGenStatus::Skip;
    } else {
      state_.emitted_barrier = true;
    }

  } else {
#if defined(ARCH_VORTEX)
    //
    // Architecture-specific thread control instructions
    //
    // we can't randomly enable a disabled thread, since
    // it can cause depulicated code generation at the same pc
    // TODO: more accurate controling needed
#if 0
    switch (instr.meta()->instr_name) {
    case riscv_instr_name_t::VX_PRED:
    case riscv_instr_name_t::VX_PRED_N: {
      unsigned resample_times = 0;
      bool legal;
      do {
        legal = true;
        for (size_t iwgid = 0; iwgid < rt_warp_gids.size() && legal;
              ++iwgid) {
          const auto rt_warp_gid = rt_warp_gids[iwgid];

          int32_t thread_last =
              Config::instance().arch.nr_threads_per_warp - 1;
          for (; thread_last >= 0; --thread_last) {
            if (gen_warp->utmask().test(
                    GenWarp::get_uid(iwgid, thread_last))) {
              break;
            }
          }
          if (thread_last < 0)
            return false; // all threads in this warp are inactive

          // rs2 is the else-path tmask that must be non-zero
          const auto tmask = state_.isa_emu.reg_read(
              rt_warp_gid, thread_last,
              difftest::RegId(difftest::RegType::Int,
                              uint32_t(instr.rs2())));
          if (!tmask) {
            legal = false;
            break;
          }
        }
        if (!legal) {
          // resample the operands
          instr.meta()->randomize(state_, *gen_warp, &instr, {}, {}, true);
        }
      } while (!legal && ++resample_times <
                              Config::instance().max_resample_times);

      if (!legal) {
        state_.stat.rejected_pred_generations++;
        return false;
      } else {
        state_.stat.accepted_pred_generations++;
      }
      gen_warp->scratchpad.emit(&instr);

      // get the new tmask by emulating the predicate instruction
      emulate_until_(gen_warp->scratchpad.tell().begin,true, rt_warp_gids, gen_warp);

    } break; // switch

    default:
      return false; // unsupported thread control instruction
    }
#else
    return ThreadCtrlGenStatus::Skip;
#endif
#elif defined(ARCH_VENTUS)
    if (instr.meta()->is_vm_computation()) {
      if (BugFilter::instance().filter("vmask")) {
        return ThreadCtrlGenStatus::Skip; // skip vmask instruction
      } else {
        gen_warp->scratchpad.emit(&instr);
      }
    } else {
      return ThreadCtrlGenStatus::Skip; // unsupported thread control
                                        // instruction
    }

#else
#error "Unsupported architecture"
#endif
  }
  return ThreadCtrlGenStatus::OK;
}

} // namespace fuzzgpu
