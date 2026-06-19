#include <algorithm>
#include <cassert>
#include <spdlog/fmt/fmt.h>

#include "bug_filter.hh"
#include "difftest_gpu/util/riscv_fp.hh"
#include "exchange_file.hh"
#include "gen_tensor_core.hh"
#include "gen_warp.hh"
#include "isa/riscv/riscv_instr.hh"
#include "isa/riscv/riscv_meta_list.hh"
#include "isa/riscv/riscv_vector_meta.hh"
#include "taint_prop.hh"

#include "scratchpad.hh"

namespace fuzzgpu {

Scratchpad::Scratchpad(GenWarp *gen_warp, difftest::ISAEmulator &isa_emu)
  : gen_warp_(gen_warp)
  , isa_emu_(isa_emu) {

  // init the disassembler for verbose logging
  if (spdlog::get_level() <= spdlog::level::debug) {
    if (cs_open(CS_ARCH_RISCV,
                Config::instance().arch.word_bits == 32 ? CS_MODE_RISCV32
                                                        : CS_MODE_RISCV64,
                &cs_handle_) != CS_ERR_OK) {
      throw std::runtime_error("Failed to initialize Capstone");
    }

    cs_option(cs_handle_, CS_OPT_DETAIL, CS_OPT_OFF);
  }
}

void Scratchpad::seek(const MemoryBlock &mem_blk) {
  mem_blk_ = mem_blk;
  cur_mem_fragment = &memory_fragments_.emplace_back(mem_blk_.begin);
}

uint64_t Scratchpad::emit(const riscv_instr *instr, bool overwrite,
                          bool taint_out) {
  gen_warp_->bb_snapshot.num_gen_bb_instrs++;
  assert(cur_mem_fragment->offset + cur_mem_fragment->instrs.size() ==
         mem_blk_.begin);

  const auto prev_sz = cur_mem_fragment->instrs.size();
  instr->emit(cur_mem_fragment->instrs);
  const uint32_t insn_bits =
      *(uint32_t *)(cur_mem_fragment->instrs.data() + prev_sz);
  const auto instr_sz = cur_mem_fragment->instrs.size() - prev_sz;
  assert(instr_sz >= riscv_meta::ilen);
  assert(instr_sz % riscv_meta::ilen == 0 &&
         "instruction size must be aligned to ilen");
  const auto num_emitted_instrs = instr_sz / riscv_meta::ilen;

  if (!overwrite) {
    gen_warp_->nr_gen_instrs += num_emitted_instrs;
    gen_warp_->fuzzer_state().add_instr_freq(instr->meta()->instr_name);

    //
    // taint propagation for nodet registers
    //
    if (difftest::is_float_point(insn_bits)) {
      switch (instr->meta()->instr_name) {
      case riscv_instr_name_t::VFMV_F_S:
      case riscv_instr_name_t::VFMV_V_F:
      case riscv_instr_name_t::VFMV_S_F:
      case riscv_instr_name_t::FMV_X_W:
      case riscv_instr_name_t::FMV_W_X:
      case riscv_instr_name_t::FMV_X_D:
      case riscv_instr_name_t::FMV_D_X:
      case riscv_instr_name_t::FLW:
      case riscv_instr_name_t::FLD:
        // no precision losses
        taint_out = false;
        break;
      default:
        taint_out = true; // due to the limited numeric precision
      }
    }

    // taint thread mask computation instructions
    if (instr->meta()->is_vm_computation()) {
      taint_out = true;
    }

    // taint bug instructions
    if (BugFilter::instance().filter(gen_warp_, *instr)) {
      taint_out = true;
      spdlog::debug("Tainting bug insn pc {:#x}", tell().begin);
    }

    const auto taint = Config::instance().taint
                           ? Taint::propagate(*gen_warp_, *instr, taint_out)
                           : false;

    // since we have ensured that tmask is updated whenever it may change,
    // we can use tmask here without steping the emulation
    // FIXME: more precise name of ClearNodetMode...
    GenWarp::ClearNodetMode clear_nodet = GenWarp::ClearNodetMode::FollowTmask;
    if (instr->meta()->is_vd_first_lane() || !instr->meta()->applied_tmask()) {
      clear_nodet = GenWarp::ClearNodetMode::ForcedFirstThread;
    }
#if defined(ARCH_VORTEX)
    if (instr->meta()->instr_name == riscv_instr_name_t::VX_WMMA) {
      clear_nodet = GenWarp::ClearNodetMode::ForcedAllThread;
    }
#endif
    if (instr->meta()->is_vm_computation()) {
      clear_nodet = GenWarp::ClearNodetMode::ForcedAllThread;
    }

    spdlog::debug("TP: PC {:#x}, taint={}, clear_nodet_single_thread={}",
                  mem_blk_.begin, taint ? "true" : "false",
                  magic_enum::enum_name(clear_nodet));

    // update the prob dist of the sampler only when the instr is eventually
    // emitted
    // update status tags (nondet, mask) of the used registers
    bool taint_xd = false;
    if (instr->meta()->operand_mask & riscv_meta::HAS_RD) {
      gen_warp_->touch_rd(instr->rd(), clear_nodet);
      if (taint) {
        gen_warp_->mark_int_reg_nodet(instr->rd());
        taint_xd = true;
        spdlog::debug("Tainting RD for instruction at pc {:#x}", tell().begin);
      }
    }
    if (instr->meta()->operand_mask & riscv_meta::HAS_FD) {
      gen_warp_->touch_fd(instr->fd(), clear_nodet);
      taint_xd = taint;
    }
    if (instr->meta()->operand_mask & riscv_meta::HAS_VD) {
      riscv_vector_meta::foreach_vector_nfield_(
          *instr, [&](unsigned reg_offset) {
            const auto regid = unsigned(instr->vd()) + reg_offset;
            if (regid >= gen_warp_->num_vec_regs())
              return true;
            gen_warp_->touch_vd(riscv_vreg_t(regid), clear_nodet);
            return true;
          });
      if (taint) {
        riscv_vector_meta::foreach_vector_nfield_(
            *instr, [&](unsigned reg_offset) {
              gen_warp_->mark_vec_reg_nodet(
                  riscv_vreg_t(unsigned(instr->vd()) + reg_offset),
                  clear_nodet);
              return true;
            });
        taint_xd = true;
        spdlog::debug("Tainting VD for instruction at pc {:#x}", tell().begin);
      }
    }
    if (taint_xd) {
      gen_warp_->taint_pc.push_back(mem_blk_.begin);
    }

    // handle addr deps
    gen_warp_->reg_dep_tracker.propagate_dep(*instr, mem_blk_.begin);

#if defined(ARCH_VORTEX)
    if (instr->meta()->instr_name == riscv_instr_name_t::VX_WMMA) {
      // vortex tcu will output the results to these fixed regs:
      for (unsigned fpr = vortex_tensor_config::instance().get_rd_base();
           fpr <= vortex_tensor_config::instance().get_rd_end(); ++fpr) {
        gen_warp_->touch_fd(riscv_fpr_t(fpr), clear_nodet);
      }
    }
#endif
  } else {
    gen_warp_->nr_gen_instrs += num_emitted_instrs;
    gen_warp_->overwritten_pc.push_back(mem_blk_.begin);
    gen_warp_->fuzzer_state().add_instr_freq(instr->meta()->instr_name);
  }

  // show verbose disassembly
  if (spdlog::get_level() <= spdlog::level::debug) {
    uint64_t address = mem_blk_.begin;
    cs_insn *insn;

    std::stringstream ss;
    for (size_t i = prev_sz; i < prev_sz + instr_sz; i += riscv_meta::ilen) {
      if (i + riscv_meta::ilen > prev_sz + instr_sz)
        break;

      auto count = cs_disasm(cs_handle_, cur_mem_fragment->instrs.data() + i,
                             riscv_meta::ilen, address, 1, &insn);

      ss.clear();
      ss << std::hex << std::setw(8) << std::setfill('0') << address << ": "
         << std::setfill(' ');

      // show machine code
      for (size_t j = 0; j < riscv_meta::ilen; j++) {
        ss << std::hex << std::setw(2) << std::setfill('0')
           << (int)cur_mem_fragment->instrs[i + j];
      }
      ss << ": " << std::setfill(' ');

      if (count > 0) {
        // show mnemonic
        ss << std::setw(20) << std::left
           << (std::string(insn[0].mnemonic) + " " +
               std::string(insn[0].op_str));
        cs_free(insn, count);
      } else {
        ss << std::setw(20) << std::left << "unknown";
      }
      ss << " // " << magic_enum::enum_name(instr->meta()->instr_name);
      spdlog::debug("emit {}", ss.str());
      address += riscv_meta::ilen;
    }
  }

  // adavance the writing pointer
  assert(mem_blk_.begin + instr_sz <= mem_blk_.end + 1 &&
         "Memory block overflow");

  isa_emu_.pmem_write(
      difftest::ScopeIdentifier(difftest::ScopeIdentifier::global()),
      mem_blk_.begin, cur_mem_fragment->instrs.data() + prev_sz, instr_sz);

  // note that we dont need to flush the cache here, as the ISA emulator
  // will always read from the pmem directly

  const auto pc_ = mem_blk_.begin;
  mem_blk_.begin += instr_sz;

  return pc_;
}

void Scratchpad::overwrite_from(uint64_t pc) {
  assert(pc <= mem_blk_.begin && pc >= cur_mem_fragment->offset &&
         "overwrite only allowed for emitted instructions");

  // deduct the overwritten instructions from the count
  const auto overwritten_bytes = mem_blk_.begin - pc;
  assert(overwritten_bytes % riscv_meta::ilen == 0 &&
         "overwritten bytes must be aligned to instruction length");
  const auto overwritten_instrs = overwritten_bytes / riscv_meta::ilen;
  assert(gen_warp_->nr_gen_instrs >= overwritten_instrs &&
         "nr_gen_instrs underflow");
  gen_warp_->nr_gen_instrs -= overwritten_instrs;

  mem_blk_.begin = pc;
  cur_mem_fragment->instrs.resize(pc - cur_mem_fragment->offset);
}

void Scratchpad::shrink(uint64_t new_end_addr) {
  // mem_blk_.begin is the last emitted position + 1
  assert(new_end_addr + 1 >= mem_blk_.begin &&
         "cannot eat the emitted content");
  assert(new_end_addr <= mem_blk_.end);
  mem_blk_.end = new_end_addr;
}

void Scratchpad::write_file(std::ostream &stream) const {
  for (const auto &frag : memory_fragments_) {
    LinearMemoryInstrFragmentHeader header;
    header.paddr_offset = frag.offset;
    header.sz = frag.instrs.size();
    stream.write(reinterpret_cast<const char *>(&header), sizeof(header));
    stream.write(reinterpret_cast<const char *>(frag.instrs.data()),
                 frag.instrs.size());
  }
}

} // namespace fuzzgpu