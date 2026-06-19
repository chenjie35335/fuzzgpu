#include "isa/riscv/riscv_instr.hh"

#include "generator.hh"

namespace fuzzgpu {

bool Generator::gen_thread_merge_(const std::shared_ptr<GenWarp> &gen_warp,
                                  riscv_instr &instr) {

  gen_warp->emulate_until(gen_warp->scratchpad.tell().begin, true);

  foreach_thread_lid(gen_warp, thread_lid) {
    if (!gen_warp->tmask().test(thread_lid) ||
        thread_lid < gen_warp->tstart()) {
      return false; // do not clobber inactive threads, otherwise it may break
                    // the divergence results
    }
  }
  return true;
}

} // namespace fuzzgpu