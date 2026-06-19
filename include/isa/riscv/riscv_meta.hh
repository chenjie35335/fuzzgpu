/**
 * Based on riscv-dv/pygen
 *
Copyright 2020 Google LLC
Copyright 2020 PerfectVIPs Inc.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at
http://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.

 */
#pragma once

#include "spdlog/fmt/fmt.h"
#include "spdlog/spdlog.h"
#include "util.hh"
#include <bitset>
#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>
#include <vector>

#include "fuzzer_state.hh"
#include "gen_warp.hh"
#include "isa/riscv/riscv_meta_list.hh"

namespace fuzzgpu {

class riscv_instr;

/// @brief Static metadata and generation logic for one RISC-V instruction kind.
class riscv_meta {
  void set_imm_len_();

  void set_rand_mode_();

  void set_instr_name_();

  ///< True when the instruction belongs to vector ISA semantics.
  bool is_vector_{false};

protected:
  /// @brief Mark this metadata as vector instruction metadata.
  void set_is_vector() { is_vector_ = true; }

  /// @brief Validate that operand_mask matches the instruction format.
  void check_operand_mask() const;

public:
  ///< Instruction name.
  riscv_instr_name_t instr_name;
  ///< Binary instruction format.
  riscv_instr_format_t format;
  ///< High-level instruction category.
  riscv_instr_category_t category;
  ///< ISA extension group.
  riscv_instr_group_t group;
  ///< Immediate length in bits.
  unsigned imm_len;
  ///< Required low-zero bits for immediates.
  unsigned imm_align_bits;
  ///< Legal vector nf range selector.
  uint32_t nf_range;

  static constexpr uint64_t HAS_RS1 = 1;
  static constexpr uint64_t HAS_RS2 = 1 << 1;
  static constexpr uint64_t HAS_RD = 1 << 2;
  static constexpr uint64_t HAS_IMM = 1 << 3;
  static constexpr uint64_t HAS_TGT_ADDR = 1 << 4;
  static constexpr uint64_t HAS_CSR = 1 << 6;
  static constexpr uint64_t HAS_FS1 = 1 << 8;
  static constexpr uint64_t HAS_FS2 = 1 << 9;
  static constexpr uint64_t HAS_FS3 = 1 << 10;
  static constexpr uint64_t HAS_FD = 1 << 11;
  static constexpr uint64_t HAS_RM = 1 << 12;
  static constexpr uint64_t HAS_ORDERING = 1 << 13; // for aq and rl
  static constexpr uint64_t HAS_VS1 = 1 << 14;
  static constexpr uint64_t HAS_VS2 = 1 << 15;
  static constexpr uint64_t HAS_VD = 1 << 16;
  static constexpr uint64_t HAS_VM = 1 << 17;
  static constexpr uint64_t HAS_MOP = 1 << 18;
  static constexpr uint64_t HAS_MEW = 1 << 19;
  static constexpr uint64_t HAS_NF = 1 << 20;
  static constexpr uint64_t HAS_FMT1 = 1 << 21;
  static constexpr uint64_t HAS_FMT2 = 1 << 22;
  static constexpr uint64_t HAS_VS3 = 1 << 23;
  static constexpr uint64_t INVALID_OPERAND_MASK = uint64_t(-1);

  ///< Operand presence mask made from HAS_* flags.
  uint64_t operand_mask{INVALID_OPERAND_MASK};

  ///< Encoder callback variant for this instruction format.
  riscv_gen gen_cb;

  ///< True when vm is fixed by the instruction definition.
  bool vm_hardcoded{false};
  ///< True when vector operation is masked.
  bool vmasked;

  ///< Instruction length in bytes; fuzzgpu currently emits fixed-length ISA.
  static const size_t ilen;

  ///< Required instruction alignment in bytes.
  static const size_t instr_alignment;

  ///< Size of integer/floating/vector register encoding space.
  static const unsigned gpr_encoding_space;

public:
  /// @brief Construct metadata for one instruction kind.
  riscv_meta(riscv_instr_name_t instr_name_, riscv_instr_format_t format_,
             riscv_instr_category_t category_, riscv_instr_group_t group_,
             const riscv_gen &gen_)
    : instr_name(instr_name_)
    , format(format_)
    , category(category_)
    , group(group_)
    , gen_cb(gen_) {
    set_imm_len_();
    set_rand_mode_();
  }
  virtual ~riscv_meta() {}

  /// @brief Return the legal target-address offset range for branches/jumps.
  std::pair<uint64_t, uint64_t> get_tgt_addr_range() const;

  /// @brief Return whether the instruction immediate is zero-extended.
  bool unsigned_imm() const;

  /// @brief Randomize operands into @p out under warp and exclusion constraints.
  /// @param operand_filter If non-zero, only operands matching this mask are randomized.
  virtual bool randomize(FuzzerState &fuzzer_state, GenWarp &warp,
                         riscv_instr *out,
                         const std::vector<riscv_reg_t> &exclude_regs,
                         const std::vector<riscv_fpr_t> &exclude_fprs,
                         bool force_mutate,
                         uint64_t operand_filter = 0);

  /// @brief Emit instructions that materialize @p imm into @p rd.
  static void emit_imm_setter(riscv_reg_t rd, uint64_t imm, GenWarp &warp,
                              const GenWarp::fnEmitInstr &emit,
                              const std::vector<riscv_reg_t> &exclude_regs);

  /// @brief Return the maximum number of instructions emit_imm_setter may use.
  static size_t imm_setter_max_instrs();

  /// @brief Encode a RISC-V vector vtype immediate.
  static uint32_t encode_vtype(unsigned vsew, unsigned vlmul, bool ta, bool ma);

  /// @brief Evaluate this branch condition for two integer source values.
  bool emulate_branch_cond(uint64_t rs1_val, uint64_t rs2_val) const;

  /// @brief Return metadata for the opposite branch condition.
  std::shared_ptr<riscv_meta> opposite_branch() const;

  /// @brief Return whether this instruction is an architecture thread join.
  bool is_thread_join() const {
#if defined(ARCH_VORTEX)
    return instr_name == riscv_instr_name_t::VX_JOIN;
#elif defined(ARCH_VENTUS)
    return instr_name == riscv_instr_name_t::JOIN;
#endif
  }

  /// @brief Return whether this instruction writes an explicit thread mask.
  bool is_thread_mask() const {
    return instr_name == riscv_instr_name_t::VX_TMC;
  }

  /// @brief Return whether this instruction is an architecture barrier.
  bool is_barrier() const {
#if defined(ARCH_VORTEX)
    return instr_name == riscv_instr_name_t::VX_BAR;
#elif defined(ARCH_VENTUS)
    return instr_name == riscv_instr_name_t::BARRIER;
#endif
  }

  /// @brief Return whether this instruction reads memory.
  bool is_load() const {
    return category == riscv_instr_category_t::LOAD ||
           category == riscv_instr_category_t::AMO;
  }
  /// @brief Return whether this instruction writes memory.
  bool is_store() const {
    return category == riscv_instr_category_t::STORE ||
           category == riscv_instr_category_t::AMO;
  }
  /// @brief Return whether this instruction is store-conditional.
  bool is_sc() const {
    return instr_name == riscv_instr_name_t::SC_D ||
           instr_name == riscv_instr_name_t::SC_W;
  }
  /// @brief Return whether this instruction is load-reserved.
  bool is_lr() const {
    return instr_name == riscv_instr_name_t::LR_D ||
           instr_name == riscv_instr_name_t::LR_W;
  }
  /// @brief Return whether this metadata is for a vector instruction.
  bool is_vector() const { return is_vector_; }
  /// @brief Return whether this instruction computes vector mask state.
  bool is_vm_computation() const;
  /// @brief Return whether this instruction uses floating-point registers.
  bool is_floating_point() const;
  /// @brief Return whether the instruction observes the current thread mask.
  bool applied_tmask() const;
  /// @brief Return whether vm is effectively hardcoded to zero.
  bool vm_hardcoded_0() const {
    // FIXME: for now only vmerge_* has vmask control dep
    return category == riscv_instr_category_t::THREAD_MERGE;
  }
  /// @brief Return memory access size in bytes.
  unsigned mem_access_size() const;

  /// @brief Return whether only the first lane of vd is written.
  bool is_vd_first_lane() const;
  /// @brief Return whether vd is also read by this instruction.
  bool read_vd() const;

  /// @brief Return whether @p val is legal for this instruction immediate.
  template <typename Tv>
  bool legal_imm(Tv val) const {
    if (imm_align_bits) {
      if ((val & ((1 << imm_align_bits) - 1)))
        return false;
    }
    if (unsigned_imm()) {
      typename std::make_unsigned<Tv>::type imm_u = val;
      auto zext = uint64_t(imm_u);
      return zext < (1ull << imm_len);

    } else {
      typename std::make_signed<Tv>::type imm_s = val;
      auto sext = int64_t(imm_s);
      return sext >= -(1ll << (imm_len - 1)) && sext < (1ll << (imm_len - 1));
    }
  }
};

} // namespace fuzzgpu
