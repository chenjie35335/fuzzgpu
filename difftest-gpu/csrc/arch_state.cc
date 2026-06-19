#include "term.hh"

#include "arch_state.hh"
#include "config.hh"
#include "util.hh"

namespace difftest {

ArchState::ArchState()
  : arf_int(
#if defined(ARCH_VORTEX)
        Config::instance().arch.vortex_nr_int_regs,
        std::vector<Word>(Config::instance().arch.nr_threads_per_warp))
#elif defined(ARCH_VENTUS)
        Config::instance().arch.ventus_nr_scalar_regs, std::vector<Word>(1))
#endif

#if defined(ARCH_VORTEX)
  , arf_fp(Config::instance().arch.vortex_nr_fp_regs,
           std::vector<uint64_t>(Config::instance().arch.nr_threads_per_warp))
#elif defined(ARCH_VENTUS)
  , arf_vec(Config::instance().arch.ventus_nr_vector_regs,
            std::vector<uint64_t>(Config::instance().arch.nr_threads_per_warp))
#endif
{
  reset();
}

void ArchState::reset() {
  std::for_each(arf_int.begin(), arf_int.end(),
                [](auto &tr) { std::fill(tr.begin(), tr.end(), 0); });
#if defined(ARCH_VORTEX)
  std::for_each(arf_fp.begin(), arf_fp.end(),
                [](auto &tr) { std::fill(tr.begin(), tr.end(), 0); });
#elif defined(ARCH_VENTUS)
  std::for_each(arf_vec.begin(), arf_vec.end(),
                [](auto &tr) { std::fill(tr.begin(), tr.end(), 0); });
#endif
}

void ArchState::commit(const ROB::Entry &oldest) {
  // commit to ARF
  pc = oldest.pc;
  for (const auto &regwb : *oldest.regs) {
    switch (regwb.rd.type) {
    case RegType::Int: {
      for (unsigned tid = 0; tid < oldest.wb_tmask.size(); ++tid) {
        if (!oldest.wb_tmask.test(tid))
          continue;
        if (is_null_reg(regwb.rd))
          continue;
        arf_int[regwb.rd.idx][tid] = (Word)regwb.rd_data[tid];
      }
    } break;

    case RegType::Float: {
      for (unsigned tid = 0; tid < oldest.wb_tmask.size(); ++tid) {
        if (!oldest.wb_tmask.test(tid))
          continue;
        if (is_null_reg(regwb.rd))
          continue;
#if defined(ARCH_VORTEX)
        arf_fp[regwb.rd.idx][tid] = regwb.rd_data[tid];
#elif defined(ARCH_VENTUS) // shared int & fp regfile
        arf_int[regwb.rd.idx][tid] = regwb.rd_data[tid];
#endif
      }
    } break;

#if defined(ARCH_VENTUS)
    case RegType::Vec: {
      for (unsigned tid = 0; tid < oldest.wb_tmask.size(); ++tid) {
        if (!oldest.wb_tmask.test(tid))
          continue;
        if (is_null_reg(regwb.rd))
          continue;
        arf_vec[regwb.rd.idx][tid] = regwb.rd_data[tid];
      }
    } break;
#endif

    default:
      assert(0);
    }
  }

  // commit to calling stack
  switch (oldest.opcode) {
  case Opcode::Call:
    calling_stk.push_front(Calling{oldest.pc});
    break;

  case Opcode::Ret:
    if (!calling_stk.empty())
      calling_stk.pop_front();
    else
      spdlog::warn("Calling stack underflow on RET at PC {:#x}", oldest.pc);
    break;

  case Opcode::RetCall:
    if (!calling_stk.empty())
      calling_stk.front() = Calling(oldest.pc);
    else
      spdlog::warn("Calling stack underflow on RETCALL at PC {:#x}", oldest.pc);
    break;
  default:
    break;
  }
}

void ArchState::dump_regs(std::ostream &out, const ArchState &ref) const {
#if defined(ARCH_VORTEX)
  const auto nr_threads_per_warp = arf_int[0].size();
  assert(arf_fp[0].size() == arf_int[0].size());
#elif defined(ARCH_VENTUS)
  const auto nr_threads_per_warp = arf_vec[0].size();
#endif

  const auto padding_char_width =
      6 + nr_threads_per_warp * ((sizeof(Word) * 2) + 1);

  out << " PC = ";
  if (ref.pc != pc)
    out << TermColor::RED;
  out << std::hex << std::setfill('0') << std::setw(sizeof(Word) * 2) << pc;
  if (ref.pc != pc)
    out << TermColor::RESET;
  out << std::endl;

  out << std::left << std::setfill(' ') << std::setw(6) << ' '
      << std::setw(padding_char_width - 6) << " Interger regitser state"
      << std::right << "|"
#if defined(ARCH_VORTEX)
      << " Float register state"
#elif defined(ARCH_VENTUS)
      << " Vector register state"
#endif
      << (Config::instance().dump_all_regs ? "" : " (diffs only)") << std::endl;

#if defined(ARCH_VORTEX)
  const auto another_arf_size = ref.arf_fp.size();
#elif defined(ARCH_VENTUS)
  const auto another_arf_size = ref.arf_vec.size();
#endif
  for (uint32_t i = 0; i < std::max(arf_int.size(), another_arf_size); ++i) {

    bool diff = false;
    if (i < arf_int.size()) {
      diff |= ref.arf_int.at(i) != arf_int.at(i);
    }
#if defined(ARCH_VORTEX)
    if (i < arf_fp.size()) {
      diff |= ref.arf_fp.at(i) != arf_fp.at(i);
    }
#elif defined(ARCH_VENTUS)
    if (i < arf_vec.size()) {
      diff |= ref.arf_vec.at(i) != arf_vec.at(i);
    }
#endif

    if (Config::instance().dump_all_regs || diff) {
      out << "  %" << std::setfill('0') << std::setw(2) << i << ':' << std::hex;
      // Integer register file
      if (i < arf_int.size()) {
        for (uint32_t j = 0; j < arf_int.at(i).size(); ++j) {
          bool set_color = false;
          const auto val = arf_int.at(i).at(j);
          if (ref.arf_int.at(i).at(j) != val) {
            out << TermColor::RED;
            set_color = true;
          }

          out << std::setfill('0') << std::setw(sizeof(Word) * 2) << val
              << std::setfill(' ') << ' ';

          if (set_color)
            out << TermColor::RESET;
        }

      } else {
        out << std::setw(padding_char_width) << ' ';
      }

      out << '|';

#if defined(ARCH_VORTEX)
      if (i < arf_fp.size()) {
        // Floating point register file
        for (uint32_t j = 0; j < arf_fp.at(i).size(); ++j) {
          bool set_color = false;
          const auto val = arf_fp.at(i).at(j);
          if (ref.arf_fp.at(i).at(j) != val) {
            out << TermColor::RED;
            set_color = true;
          }

          out << std::setfill('0') << std::setw(16) << val << std::setfill(' ')
              << ' ';

          if (set_color)
            out << TermColor::RESET;
        }
      }
#elif defined(ARCH_VENTUS)
      if (i < arf_vec.size()) {
        // Vector register file
        for (uint32_t j = 0; j < arf_vec.at(i).size(); ++j) {
          bool set_color = false;
          const auto val = arf_vec.at(i).at(j);
          if (ref.arf_vec.at(i).at(j) != val) {
            out << TermColor::RED;
            set_color = true;
          }

          out << std::setfill('0') << std::setw(16) << val << std::setfill(' ')
              << ' ';

          if (set_color)
            out << TermColor::RESET;
        }
      }
#endif

      out << std::dec << std::endl;
    }
  }
}

void ArchState::dump_calling_stack(std::ostream &out) const {
  for (size_t i = 0; i < calling_stk.size(); ++i) {
    out << fmt::format(" Frame #{} Caller PC {:#x}", i,
                       calling_stk[i].caller_pc)
        << std::endl;
  }
}

} // namespace difftest