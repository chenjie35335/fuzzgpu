#include "config.hh"
#include "fuzzer_state.hh"
#include "isa/riscv/riscv_instr.hh"
#include "bug_filter.hh"

#include "isa/riscv/riscv_meta.hh"

namespace fuzzgpu {

constexpr size_t riscv_meta::ilen = 4;

constexpr size_t riscv_meta::instr_alignment = 4;

constexpr unsigned riscv_meta::gpr_encoding_space =
    unsigned(riscv_reg_t::Count);

void riscv_meta::set_imm_len_() {
  imm_align_bits = 0;
  switch (format) {
  case riscv_instr_format_t::U_FORMAT:
    imm_len = 32;
    break;
  case riscv_instr_format_t::J_FORMAT:
    imm_len = 21;
    imm_align_bits = 1;
    break;
  case riscv_instr_format_t::I_FORMAT:
    // imm as shamt
    if (instr_name == riscv_instr_name_t::SLLIW ||
        instr_name == riscv_instr_name_t::SRLIW ||
        instr_name == riscv_instr_name_t::SRAIW) {
      imm_len = 5;
      break;
    } else if (instr_name == riscv_instr_name_t::SLLI ||
               instr_name == riscv_instr_name_t::SRLI ||
               instr_name == riscv_instr_name_t::SRAI) {
      imm_len = Config::instance().arch.word_bits == 64 ? 6 : 5;
      break;
    }
    [[fallthrough]];
  case riscv_instr_format_t::S_FORMAT:
    imm_len = 12;
    break;
  case riscv_instr_format_t::B_FORMAT:
    imm_align_bits = 1;
    imm_len = 13;
    break;
  case riscv_instr_format_t::R_VIV:
    imm_len = 6;
    break;
  case riscv_instr_format_t::I_UIMM_CSR_FORMAT:
    imm_len = 5;
    break;
  default:
    return; // no imm
  }

  if (imm_len && (category == riscv_instr_category_t::LOAD ||
                  category == riscv_instr_category_t::STORE ||
                  category == riscv_instr_category_t::AMO)) {
    imm_align_bits = unsigned(std::log2(mem_access_size()));
  }
}

void riscv_meta::set_rand_mode_() {
  // bug fixes for riscv-dv
  if (format == riscv_instr_format_t::J_FORMAT) {
    operand_mask = (HAS_RD | HAS_TGT_ADDR);

  } else if (format == riscv_instr_format_t::U_FORMAT) {
    operand_mask = (HAS_RD | HAS_IMM);

  } else if (format == riscv_instr_format_t::I_FORMAT) {
    switch (instr_name) {
    case riscv_instr_name_t::NOP:
    case riscv_instr_name_t::WFI:
    case riscv_instr_name_t::FENCE:
    case riscv_instr_name_t::FENCE_I:
    case riscv_instr_name_t::EBREAK:
      operand_mask = 0;
      break;

    default:
      switch (category) {
      case riscv_instr_category_t::LOAD:
        operand_mask = (HAS_RD | HAS_IMM | HAS_RS1);
        break;

      case riscv_instr_category_t::CSR:
        operand_mask = (HAS_RD | HAS_IMM | HAS_CSR);
        break;

      default:
        operand_mask = (HAS_RD | HAS_RS1 | HAS_IMM);
      }
    }

  } else if (format == riscv_instr_format_t::I_CSR_FORMAT) {
    operand_mask = (HAS_RD | HAS_RS1 | HAS_CSR);

  } else if (format == riscv_instr_format_t::I_UIMM_CSR_FORMAT) {
    operand_mask = (HAS_RD | HAS_IMM | HAS_CSR);

  } else if (format == riscv_instr_format_t::S_FORMAT) {
    if (category == riscv_instr_category_t::STORE) {
      operand_mask = (HAS_RS2 | HAS_IMM | HAS_RS1);

    } else {
      operand_mask = (HAS_RS1 | HAS_RS2 | HAS_IMM);
    }

  } else if (format == riscv_instr_format_t::B_FORMAT) {
    operand_mask = (HAS_RS1 | HAS_RS2 | HAS_TGT_ADDR);

  } else if (format == riscv_instr_format_t::R_FORMAT) {
    switch (instr_name) {
    case riscv_instr_name_t::SFENCE_VMA:
    // vortex
    case riscv_instr_name_t::VX_WSPAWN:
    case riscv_instr_name_t::VX_BAR:
    case riscv_instr_name_t::VX_PRED:
    case riscv_instr_name_t::VX_PRED_N:
      operand_mask = (HAS_RS1 | HAS_RS2);
      break;

    case riscv_instr_name_t::VX_TMC:
    case riscv_instr_name_t::VX_JOIN:
      operand_mask = (HAS_RS1);
      break;

    case riscv_instr_name_t::VX_SPLIT:
    case riscv_instr_name_t::VX_SPLIT_N:
      operand_mask = (HAS_RD | HAS_RS1);
      break;

    default:
      operand_mask = (HAS_RD | HAS_RS1 | HAS_RS2);
    }
  } else if (format == riscv_instr_format_t::FMT_FMT_FORMAT) {
    operand_mask = (HAS_FMT1 | HAS_FMT2);
  }
}

void riscv_meta::check_operand_mask() const {
  if (operand_mask == INVALID_OPERAND_MASK) {
    spdlog::error("Fatal_unsupported_format: {} {}",
                  magic_enum::enum_name(format),
                  magic_enum::enum_name(instr_name));
    std::abort();
  }
}

bool riscv_meta::randomize(FuzzerState &fuzzer_state, GenWarp &warp,
                           riscv_instr *out,
                           const std::vector<riscv_reg_t> &exclude_regs,
                           const std::vector<riscv_fpr_t> &exclude_fprs,
                           bool force_mutate,
                           uint64_t operand_filter) {
  check_operand_mask();

  // If operand_filter is set, only randomize operands that match both
  // operand_mask and operand_filter. Otherwise, randomize all operands.
  const uint64_t effective_mask = operand_filter ? (operand_mask & operand_filter) : operand_mask;

  const bool cti = (category == riscv_instr_category_t::BRANCH ||
                    category == riscv_instr_category_t::JUMP ||
                    category == riscv_instr_category_t::LOAD ||
                    category == riscv_instr_category_t::STORE ||
                    category == riscv_instr_category_t::AMO);

  if (effective_mask & HAS_RS1) {
    out->set_rs1(warp.pick_rs(cti, force_mutate));
  }
  if (effective_mask & HAS_RS2) {
    out->set_rs2(warp.pick_rs(cti, force_mutate));
  }
  if (effective_mask & HAS_RD) {
    out->set_rd(warp.pick_rd(exclude_regs, force_mutate));
    if (BugFilter::instance().filter("wb_zero") &&
        out->rd() == riscv_reg_t::ZERO) {
      return false;
    }
  }
  if (effective_mask & HAS_IMM) {
    uint64_t raw;
    if (imm_align_bits) {
      raw = fuzzer_state.bits_sampler.gen_bits(imm_len - imm_align_bits)
            << imm_align_bits;
    } else if (format == riscv_instr_format_t::U_FORMAT) {
      raw = fuzzer_state.bits_sampler.gen_bits(imm_len - 12) << 12;
    } else {
      raw = fuzzer_state.bits_sampler.gen_bits(imm_len);
    }

    if (unsigned_imm()) {
      out->set_imm(raw);
    } else {
      // sign extension
      int64_t sign_bit = 1ll << (imm_len - 1);
      if (raw & sign_bit) {
        out->set_imm(static_cast<int64_t>(raw | (~0ull << imm_len)));
      } else {
        out->set_imm(static_cast<int64_t>(raw));
      }
    }
  }
  if (effective_mask & HAS_CSR) {
    // warn: you should never set_rd or set_imm after randomized a csr
    // instruction
    bool read, write;
    switch (instr_name) {
    case riscv_instr_name_t::CSRRW:
    case riscv_instr_name_t::CSRRWI:
      write = true;
      read = (out->rd() != riscv_reg_t::ZERO);
      break;

    case riscv_instr_name_t::CSRRS:
    case riscv_instr_name_t::CSRRC:
      read = true;
      write = (out->rs1() != riscv_reg_t::ZERO);
      break;

    case riscv_instr_name_t::CSRRSI:
    case riscv_instr_name_t::CSRRCI:
      read = true;
      write = (out->imm() != 0);
      break;
    default:
      std::abort();
    }
    out->set_csr(fuzzer_state.pick_csr(read, write));
  }
  if (effective_mask & HAS_TGT_ADDR) {
    // handled in generator.cc
  }
  if (effective_mask & HAS_FMT1) {
    // handled in gen_tensor_core.cc
  }
  if (effective_mask & HAS_FMT2) {
    // handled in gen_tensor_core.cc
  }
  return true;
}

std::pair<uint64_t, uint64_t> riscv_meta::get_tgt_addr_range() const {
  switch (format) {
  case riscv_instr_format_t::J_FORMAT:
    return std::make_pair(/*-*/ (1u << 20), (1u << 20) - 2); // bytes

  case riscv_instr_format_t::B_FORMAT:
    return std::make_pair(/*-*/ (1u << 12), (1u << 12) - 2); // bytes

  default:
    assert(instr_name == riscv_instr_name_t::JALR);
    return std::make_pair(uint64_t(-1), uint64_t(-1)); // unlimited
  }
}

bool riscv_meta::unsigned_imm() const {
  return (format == riscv_instr_format_t::I_UIMM_CSR_FORMAT) ||
         // imm as shmat
         instr_name == riscv_instr_name_t::SLLIW ||
         instr_name == riscv_instr_name_t::SRLIW ||
         instr_name == riscv_instr_name_t::SRAIW ||
         instr_name == riscv_instr_name_t::SLLI ||
         instr_name == riscv_instr_name_t::SRLI ||
         instr_name == riscv_instr_name_t::SRAI;
}

void riscv_meta::emit_imm_setter(riscv_reg_t rd, uint64_t imm64, GenWarp &warp,
                                 const GenWarp::fnEmitInstr &fne,
                                 const std::vector<riscv_reg_t> &exclude_regs) {
  unsigned instr_count = 0;
  auto emit = [&](riscv_instr *instr) {
    fne(instr);
    ++instr_count;
  };

  // Case 1: Immediate fits in 12 bits - use single ADDI
  auto simm = static_cast<int64_t>(imm64);
  if (simm >= -2048 && simm <= 2047) {
    auto addi = warp.fuzzer_state().instr_pool.create(
        riscv_meta_list::instance().instr(riscv_instr_name_t::ADDI));
    addi->set_rd(rd);
    addi->set_rs1(riscv_reg_t::ZERO);
    addi->set_imm(simm);
    emit(addi);

  } else {
    uint32_t low32 = imm64 & 0xFFFFFFFF;
    uint32_t high32 = imm64 >> 32;

    // Case 2: 32-bit signed value (high 32 bits are all 1)
    if (high32 == 0xFFFFFFFF) {
      // For 32-bit values, we can use LUI + ADDIW sequence
      int32_t signed_low32 = static_cast<int32_t>(low32);

      // Extract upper 20 bits and lower 12 bits
      int32_t imm20 = signed_low32 >> 12;
      int16_t imm12 = signed_low32 & 0xFFF;

      // Adjust sign extension in ADDIW
      if (imm12 >= 0x800) {
        imm20 += 1;
        imm12 -= 0x1000;
      }

      auto lui = warp.fuzzer_state().instr_pool.create(
          riscv_meta_list::instance().instr(riscv_instr_name_t::LUI));
      lui->set_rd(rd);
      lui->set_imm(imm20 << 12); // LUI takes imm shifted by 12
      emit(lui);

      assert(Config::instance().arch.word_bits == 64 && "abs(imm) is too big");
      if (imm12) {
        auto addiw = warp.fuzzer_state().instr_pool.create(
            riscv_meta_list::instance().instr(riscv_instr_name_t::ADDIW));
        addiw->set_rd(rd);
        addiw->set_rs1(rd);
        addiw->set_imm(imm12);
        emit(addiw);
      }

      // Case 3: 32-bit unsigned value (high 32 bits are all 0)
    } else if (high32 == 0) {
      // For 32-bit values, we can use LUI + ADDIW sequence
      int32_t signed_low32 = static_cast<int32_t>(low32);

      // Extract upper 20 bits and lower 12 bits
      int32_t imm20 = signed_low32 >> 12;
      int16_t imm12 = signed_low32 & 0xFFF;

      // Adjust sign extension in ADDI
      if (imm12 >= 0x800) {
        imm20 += 1;
        imm12 -= 0x1000;
      }

      auto lui = warp.fuzzer_state().instr_pool.create(
          riscv_meta_list::instance().instr(riscv_instr_name_t::LUI));
      lui->set_rd(rd);
      lui->set_imm(imm20 << 12); // LUI takes imm shifted by 12
      emit(lui);

      if (imm12) {
        auto addi = warp.fuzzer_state().instr_pool.create(
            riscv_meta_list::instance().instr(riscv_instr_name_t::ADDI));
        addi->set_rd(rd);
        addi->set_rs1(rd);
        addi->set_imm(imm12);
        emit(addi);
      }

      if ((low32 >> 31) && Config::instance().arch.word_bits == 64) {
        // sign extension happened, need to clear upper bits
        riscv_instr slli(
            riscv_meta_list::instance().instr(riscv_instr_name_t::SLLI));
        slli.set_rd(rd);
        slli.set_rs1(rd);
        slli.set_imm(32);
        emit(&slli);
        riscv_instr srli(
            riscv_meta_list::instance().instr(riscv_instr_name_t::SRLI));
        srli.set_rd(rd);
        srli.set_rs1(rd);
        srli.set_imm(32);
        emit(&srli);
      }

    } else {
      // Case 4: Full 64-bit immediate - use LUI + ADDI for both halves
      assert(Config::instance().arch.word_bits == 64);

      // Load lower 32 bits first
      emit_imm_setter(rd, static_cast<uint32_t>(low32), warp, emit,
                      exclude_regs);

      std::vector all_exclude_regs{riscv_reg_t::ZERO, rd};
      all_exclude_regs.insert(all_exclude_regs.end(), exclude_regs.begin(),
                              exclude_regs.end());

      // allocate a temporary register for the upper part
      auto tmp_reg = warp.pick_rd(all_exclude_regs, false);

      // Load upper 32 bits
      emit_imm_setter(tmp_reg, static_cast<uint32_t>(high32), warp, emit,
                      all_exclude_regs);

      auto slli = warp.fuzzer_state().instr_pool.create(
          riscv_meta_list::instance().instr(riscv_instr_name_t::SLLI));
      slli->set_rd(tmp_reg);
      slli->set_rs1(tmp_reg);
      slli->set_imm(32);
      emit(slli);

      auto or_ = warp.fuzzer_state().instr_pool.create(
          riscv_meta_list::instance().instr(riscv_instr_name_t::OR));
      or_->set_rd(rd);
      or_->set_rs1(rd);
      or_->set_rs2(tmp_reg);
      emit(or_);
    }
  }

  assert(instr_count <= riscv_meta::imm_setter_max_instrs());
}

size_t riscv_meta::imm_setter_max_instrs() {
  if (Config::instance().arch.word_bits == 64) {
    return 10; // conservative upper bound
  } else {
    return 6;
  }
}

static uint64_t sext(uint64_t val, unsigned bits) {
  assert(bits <= 64);
  if (bits == 64)
    return val; // avoid undefined behavior of the shift
  uint64_t sign_bit = 1ull << (bits - 1);
  if (val & sign_bit) {
    return val | (~uint64_t(0) << bits);
  } else {
    return val;
  }
}

bool riscv_meta::emulate_branch_cond(uint64_t rs1_val, uint64_t rs2_val) const {
  spdlog::debug("Emulate branch cond: {} rs1_val={} rs2_val={}",
                magic_enum::enum_name(instr_name), rs1_val, rs2_val);
  switch (instr_name) {
  case riscv_instr_name_t::VBEQ:
  case riscv_instr_name_t::BEQ:
    return (rs1_val == rs2_val);
  case riscv_instr_name_t::VBNE:
  case riscv_instr_name_t::BNE:
    return (rs1_val != rs2_val);
  case riscv_instr_name_t::VBLT:
    rs1_val = sext(rs1_val, Config::instance().arch.word_bits);
    rs2_val = sext(rs2_val, Config::instance().arch.word_bits);
    return (int64_t(rs2_val) < int64_t(rs1_val));
  case riscv_instr_name_t::BLT:
    rs1_val = sext(rs1_val, Config::instance().arch.word_bits);
    rs2_val = sext(rs2_val, Config::instance().arch.word_bits);
    return (int64_t(rs1_val) < int64_t(rs2_val));
  case riscv_instr_name_t::VBGE:
    rs1_val = sext(rs1_val, Config::instance().arch.word_bits);
    rs2_val = sext(rs2_val, Config::instance().arch.word_bits);
    return (int64_t(rs2_val) >= int64_t(rs1_val));
  case riscv_instr_name_t::BGE:
    rs1_val = sext(rs1_val, Config::instance().arch.word_bits);
    rs2_val = sext(rs2_val, Config::instance().arch.word_bits);
    return (int64_t(rs1_val) >= int64_t(rs2_val));

  case riscv_instr_name_t::VBLTU:
    return (uint64_t(rs2_val) < uint64_t(rs1_val));
  case riscv_instr_name_t::BLTU:
    return (uint64_t(rs1_val) < uint64_t(rs2_val));
  case riscv_instr_name_t::VBGEU:
    return (uint64_t(rs2_val) >= uint64_t(rs1_val));
  case riscv_instr_name_t::BGEU:
    return (uint64_t(rs1_val) >= uint64_t(rs2_val));
  default:
    std::abort();
  }
}

std::shared_ptr<riscv_meta> riscv_meta::opposite_branch() const {
  switch (instr_name) {
  case riscv_instr_name_t::VBEQ:
    return riscv_meta_list::instance().instr(riscv_instr_name_t::VBNE);
  case riscv_instr_name_t::VBNE:
    return riscv_meta_list::instance().instr(riscv_instr_name_t::VBEQ);
  case riscv_instr_name_t::VBLT:
    return riscv_meta_list::instance().instr(riscv_instr_name_t::VBGE);
  case riscv_instr_name_t::VBGE:
    return riscv_meta_list::instance().instr(riscv_instr_name_t::VBLT);
  case riscv_instr_name_t::VBLTU:
    return riscv_meta_list::instance().instr(riscv_instr_name_t::VBGEU);
  case riscv_instr_name_t::VBGEU:
    return riscv_meta_list::instance().instr(riscv_instr_name_t::VBLTU);

  case riscv_instr_name_t::BEQ:
    return riscv_meta_list::instance().instr(riscv_instr_name_t::BNE);
  case riscv_instr_name_t::BNE:
    return riscv_meta_list::instance().instr(riscv_instr_name_t::BEQ);
  case riscv_instr_name_t::BLT:
    return riscv_meta_list::instance().instr(riscv_instr_name_t::BGE);
  case riscv_instr_name_t::BGE:
    return riscv_meta_list::instance().instr(riscv_instr_name_t::BLT);
  case riscv_instr_name_t::BLTU:
    return riscv_meta_list::instance().instr(riscv_instr_name_t::BGEU);
  case riscv_instr_name_t::BGEU:
    return riscv_meta_list::instance().instr(riscv_instr_name_t::BLTU);
  default:
    std::abort();
  }
}

unsigned riscv_meta::mem_access_size() const {
  switch (instr_name) {
  case riscv_instr_name_t::LB:
  case riscv_instr_name_t::LBU:
  case riscv_instr_name_t::SB:

  case riscv_instr_name_t::VLB12_V:
  case riscv_instr_name_t::VLB_V:
  case riscv_instr_name_t::VLBU12_V:
  case riscv_instr_name_t::VLBU_V:
  case riscv_instr_name_t::VSB12_V:
  case riscv_instr_name_t::VSB_V:
    return 1;

  case riscv_instr_name_t::LH:
  case riscv_instr_name_t::LHU:
  case riscv_instr_name_t::SH:

  case riscv_instr_name_t::VLH12_V:
  case riscv_instr_name_t::VLH_V:
  case riscv_instr_name_t::VLHU12_V:
  case riscv_instr_name_t::VLHU_V:
  case riscv_instr_name_t::VSH12_V:
  case riscv_instr_name_t::VSH_V:
    return 2;

  case riscv_instr_name_t::LW:
  case riscv_instr_name_t::LWU:
  case riscv_instr_name_t::LR_W:
  case riscv_instr_name_t::SW:
  case riscv_instr_name_t::SC_W:
  case riscv_instr_name_t::AMOSWAP_W:
  case riscv_instr_name_t::AMOADD_W:
  case riscv_instr_name_t::AMOAND_W:
  case riscv_instr_name_t::AMOOR_W:
  case riscv_instr_name_t::AMOXOR_W:
  case riscv_instr_name_t::AMOMIN_W:
  case riscv_instr_name_t::AMOMAX_W:
  case riscv_instr_name_t::AMOMINU_W:
  case riscv_instr_name_t::AMOMAXU_W:

  case riscv_instr_name_t::VLE32_V:
  case riscv_instr_name_t::VLOXEI32_V:
  case riscv_instr_name_t::VLSE32_V:
  case riscv_instr_name_t::VSE32_V:
  case riscv_instr_name_t::VSOXEI32_V:
  case riscv_instr_name_t::VSSE32_V:

  case riscv_instr_name_t::VLW12_V:
  case riscv_instr_name_t::VLW_V:
  case riscv_instr_name_t::VSW12_V:
  case riscv_instr_name_t::VSW_V:
    return 4;

  case riscv_instr_name_t::LD:
  case riscv_instr_name_t::LR_D:
  case riscv_instr_name_t::SD:
  case riscv_instr_name_t::SC_D:
  case riscv_instr_name_t::AMOSWAP_D:
  case riscv_instr_name_t::AMOADD_D:
  case riscv_instr_name_t::AMOAND_D:
  case riscv_instr_name_t::AMOOR_D:
  case riscv_instr_name_t::AMOXOR_D:
  case riscv_instr_name_t::AMOMIN_D:
  case riscv_instr_name_t::AMOMAX_D:
  case riscv_instr_name_t::AMOMINU_D:
  case riscv_instr_name_t::AMOMAXU_D:
    return 8;

  case riscv_instr_name_t::FLW:
  case riscv_instr_name_t::FSW:
    return 4;

  case riscv_instr_name_t::FLD:
  case riscv_instr_name_t::FSD:
    return 8;

  default:
    std::abort();
  }
}

bool riscv_meta::applied_tmask() const {
#if defined(ARCH_VORTEX)
  return true;
#else
  return (is_vector() && !(category == riscv_instr_category_t::THREAD_CTRL ||
                           category == riscv_instr_category_t::THREAD_MERGE));
#endif
}

uint32_t riscv_meta::encode_vtype(unsigned vsew, unsigned vlmul, bool ta,
                                  bool ma) {
  uint32_t vsew_enc = 0, vlmul_enc = 0;
  switch (vlmul) {
  case 1:
    vlmul_enc = 0;
    break;
  case 2:
    vlmul_enc = 1;
    break;
  case 4:
    vlmul_enc = 2;
    break;
  case 8:
    vlmul_enc = 3;
    break;
  default:
    std::abort();
  }
  switch (vsew) {
  case 8:
    vsew_enc = 0;
    break;
  case 16:
    vsew_enc = 1;
    break;
  case 32:
    vsew_enc = 2;
    break;
  case 64:
    vsew_enc = 3;
    break;
  default:
    std::abort();
  }
  return (vlmul_enc) | (vsew_enc << 3) | (ta ? (1 << 6) : 0) |
         (ma ? (1 << 7) : 0);
}

bool riscv_meta::is_vd_first_lane() const {
  switch (instr_name) {
  case riscv_instr_name_t::VMV_S_X:
  case riscv_instr_name_t::VFMV_S_F:

  // TODO reductions (currently useless)
  case riscv_instr_name_t::VREDSUM_VS:
  case riscv_instr_name_t::VREDMAXU_VS:
  case riscv_instr_name_t::VREDMAX_VS:
  case riscv_instr_name_t::VREDMINU_VS:
  case riscv_instr_name_t::VREDMIN_VS:
  case riscv_instr_name_t::VREDAND_VS:
  case riscv_instr_name_t::VREDOR_VS:
  case riscv_instr_name_t::VREDXOR_VS:
  case riscv_instr_name_t::VFWREDSUM_VS:
    return true;
  default:
    return false;
  }
}

bool riscv_meta::read_vd() const {
  switch (instr_name) {
  case riscv_instr_name_t::VFTTA_VV:
  case riscv_instr_name_t::VMACC_VV:
  case riscv_instr_name_t::VMACC_VX:
  case riscv_instr_name_t::VNMSAC_VV:
  case riscv_instr_name_t::VNMSAC_VX:
  case riscv_instr_name_t::VMADD_VV:
  case riscv_instr_name_t::VMADD_VX:
  case riscv_instr_name_t::VNMSUB_VV:
  case riscv_instr_name_t::VNMSUB_VX:
#if 0 // unused for now
  case riscv_instr_name_t::VWMACCU_VV:
case riscv_instr_name_t::VWMACCU_VX:
case riscv_instr_name_t::VWMACC_VV:
case riscv_instr_name_t::VWMACC_VX:
case riscv_instr_name_t::VWMACCSU_VV:
case riscv_instr_name_t::VWMACCSU_VX:
case riscv_instr_name_t::VWMACCUS_VX:
#endif
    return true;
  default:
    return false;
  }
}

bool riscv_meta::is_vm_computation() const {
  switch (instr_name) {
  case riscv_instr_name_t::VMAND_MM:
  case riscv_instr_name_t::VMANDN_MM:
  case riscv_instr_name_t::VMNAND_MM:
  case riscv_instr_name_t::VMNOR_MM:
  case riscv_instr_name_t::VMOR_MM:
  case riscv_instr_name_t::VMORN_MM:
  case riscv_instr_name_t::VMXNOR_MM:
  case riscv_instr_name_t::VMXOR_MM:
    return true;
  default:
    return false;
  }
}

bool riscv_meta::is_floating_point() const {
  switch (group) {
  case riscv_instr_group_t::RV32F:
  case riscv_instr_group_t::RV64F:
  case riscv_instr_group_t::RV32D:
  case riscv_instr_group_t::RV64D:
    return true;
  default:
    return false;
  }
}

} // namespace fuzzgpu