#pragma once

#include "spdlog/fmt/fmt.h"
#include <cassert>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include "isa/riscv/riscv_meta.hh"
#include "types.hh"

namespace fuzzgpu {

/// @brief Concrete RISC-V instruction with assigned operands.
class riscv_instr {
  ///< Instruction metadata defining format, operands, and encoder.
  std::shared_ptr<riscv_meta> meta_;

  ///< 12-bit CSR operand.
  uint16_t csr_{0};
  ///< Integer source and destination operands.
  riscv_reg_t rs2_{riscv_reg_t::ZERO};
  riscv_reg_t rs1_{riscv_reg_t::ZERO};
  riscv_reg_t rd_{riscv_reg_t::ZERO};
  ///< Immediate or target-address offset storage.
  int64_t imm_{-1};

  ///< Floating-point source and destination operands.
  riscv_fpr_t fs1_{riscv_fpr_t::FT0};
  riscv_fpr_t fs2_{riscv_fpr_t::FT0};
  riscv_fpr_t fs3_{riscv_fpr_t::FT0};
  riscv_fpr_t fd_{riscv_fpr_t::FT0};
  ///< Floating-point rounding mode.
  riscv_rm_t rm_{riscv_rm_t::RNE};

  ///< Vector source and destination operands.
  riscv_vreg_t vs1_{0};
  riscv_vreg_t vs2_{0};
  riscv_vreg_t vs3_{0};
  riscv_vreg_t vd_{0};
  ///< Vector mask enable bit.
  bool vm_{false};
  ///< Vector memory operation mode.
  uint32_t mop_{0};
  ///< Vector memory element-width bit.
  bool mew_{false};
  ///< Vector segment field count.
  uint32_t nf_{0};

  ///< AMO acquire bit.
  bool aq_{false};
  ///< AMO release bit.
  bool rl_{false};

  ///< Tensor-core operand formats.
  uint32_t fmt1_{0};
  uint32_t fmt2_{0};

private:
  template <typename Tv>
  void set_imm_unsafe_(Tv imm) {
    assert(meta_->imm_len);

    if (meta_->unsigned_imm()) {
      typename std::make_unsigned<Tv>::type imm_u = imm;
      imm_ = uint64_t(imm_u); // zero extend

    } else {
      typename std::make_signed<Tv>::type imm_s = imm;
      imm_ = int64_t(imm_s); // signed extension
    }

    assert_log(meta_->legal_imm(imm), "imm {} is not legal for instruction {}",
               imm, fmt::underlying(meta_->instr_name));
  }

  ///< Bit mask of operands already assigned on this instruction.
  uint64_t assigned_operands_{0};

public:
  /// @brief Construct an instruction bound to metadata @p meta.
  riscv_instr(const std::shared_ptr<riscv_meta> &meta)
    : meta_(meta) {}

  /// @brief Set the CSR operand.
  void set_csr(uint16_t x) { // 12 bits
    assert(meta_->operand_mask & riscv_meta::HAS_CSR);
    csr_ = x;
    assigned_operands_ |= riscv_meta::HAS_CSR;
  }
  /// @brief Set integer source register rs2.
  void set_rs2(riscv_reg_t x) {
    assert(meta_->operand_mask & riscv_meta::HAS_RS2);
    assert(unsigned(x) < riscv_meta::gpr_encoding_space);
    rs2_ = x;
    assigned_operands_ |= riscv_meta::HAS_RS2;
  }
  /// @brief Set integer source register rs1.
  void set_rs1(riscv_reg_t x) {
    assert(meta_->operand_mask & riscv_meta::HAS_RS1);
    assert(unsigned(x) < riscv_meta::gpr_encoding_space);
    rs1_ = x;
    assigned_operands_ |= riscv_meta::HAS_RS1;
  }
  /// @brief Set integer destination register rd.
  void set_rd(riscv_reg_t x) {
    assert(meta_->operand_mask & riscv_meta::HAS_RD);
    assert(unsigned(x) < riscv_meta::gpr_encoding_space);
    rd_ = x;
    assigned_operands_ |= riscv_meta::HAS_RD;
  }

  /// @brief Set a branch/jump target by encoding target - pc.
  void set_tgt_addr(uint64_t pc, uint64_t target) { // for jmp and branches
    assert(meta_->operand_mask & riscv_meta::HAS_TGT_ADDR);
    assert(!(pc & 1));
    assert_log(!(target & 1),
               "Target address must be aligned to 2-byte boundary");

    if (target >= pc) {
      uint64_t diff = target - pc;
      assert(diff <=
             static_cast<uint64_t>(std::numeric_limits<int64_t>::max()));
      set_imm_unsafe_(static_cast<int64_t>(diff));

    } else {
      uint64_t diff = pc - target;
      assert(diff <=
             static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) + 1);
      set_imm_unsafe_(-static_cast<int64_t>(diff));
    }
    assigned_operands_ |= riscv_meta::HAS_TGT_ADDR;
  }

  /// @brief Set floating-point source register fs1.
  void set_fs1(riscv_fpr_t x) {
    assert(meta_->operand_mask & riscv_meta::HAS_FS1);
    assert(unsigned(x) < riscv_meta::gpr_encoding_space);
    fs1_ = x;
    assigned_operands_ |= riscv_meta::HAS_FS1;
  }
  /// @brief Set floating-point source register fs2.
  void set_fs2(riscv_fpr_t x) {
    assert(meta_->operand_mask & riscv_meta::HAS_FS2);
    assert(unsigned(x) < riscv_meta::gpr_encoding_space);
    fs2_ = x;
    assigned_operands_ |= riscv_meta::HAS_FS2;
  }
  /// @brief Set floating-point source register fs3.
  void set_fs3(riscv_fpr_t x) {
    assert(meta_->operand_mask & riscv_meta::HAS_FS3);
    assert(unsigned(x) < riscv_meta::gpr_encoding_space);
    fs3_ = x;
    assigned_operands_ |= riscv_meta::HAS_FS3;
  }
  /// @brief Set floating-point destination register fd.
  void set_fd(riscv_fpr_t x) {
    assert(meta_->operand_mask & riscv_meta::HAS_FD);
    assert(unsigned(x) < riscv_meta::gpr_encoding_space);
    fd_ = x;
    assigned_operands_ |= riscv_meta::HAS_FD;
  }
  /// @brief Set floating-point rounding mode.
  void set_rm(riscv_rm_t x) {
    assert(meta_->operand_mask & riscv_meta::HAS_RM);
    rm_ = x;
    assigned_operands_ |= riscv_meta::HAS_RM;
  }
  /// @brief Set vector destination register vd.
  void set_vd(riscv_vreg_t x) {
    assert(meta_->operand_mask & riscv_meta::HAS_VD);
    assert(unsigned(x) < riscv_meta::gpr_encoding_space);
    vd_ = x;
    assigned_operands_ |= riscv_meta::HAS_VD;
  }
  /// @brief Set vector source register vs1.
  void set_vs1(riscv_vreg_t x) {
    assert(meta_->operand_mask & riscv_meta::HAS_VS1);
    assert(unsigned(x) < riscv_meta::gpr_encoding_space);
    vs1_ = x;
    assigned_operands_ |= riscv_meta::HAS_VS1;
  }
  /// @brief Set vector source register vs2.
  void set_vs2(riscv_vreg_t x) {
    assert(meta_->operand_mask & riscv_meta::HAS_VS2);
    assert(unsigned(x) < riscv_meta::gpr_encoding_space);
    vs2_ = x;
    assigned_operands_ |= riscv_meta::HAS_VS2;
  }
  /// @brief Set vector source register vs3.
  void set_vs3(riscv_vreg_t x) {
    assert(meta_->operand_mask & riscv_meta::HAS_VS3);
    assert(unsigned(x) < riscv_meta::gpr_encoding_space);
    vs3_ = x;
    assigned_operands_ |= riscv_meta::HAS_VS3;
  }
  /// @brief Set vector mask bit.
  void set_vm(bool x) {
    assert(meta_->operand_mask & riscv_meta::HAS_VM);
    vm_ = x;
    assigned_operands_ |= riscv_meta::HAS_VM;
  }
  /// @brief Set vector memory operation mode.
  void set_mop(uint32_t x) {
    assert(meta_->operand_mask & riscv_meta::HAS_MOP);
    mop_ = x;
    assigned_operands_ |= riscv_meta::HAS_MOP;
  }
  /// @brief Set vector memory element-width bit.
  void set_mew(bool x) {
    assert(meta_->operand_mask & riscv_meta::HAS_MEW);
    mew_ = x;
    assigned_operands_ |= riscv_meta::HAS_MEW;
  }
  /// @brief Set vector segment field count.
  void set_nf(uint32_t x) {
    assert(meta_->operand_mask & riscv_meta::HAS_NF);
    switch (meta_->nf_range) {
    case 0:
      assert(x < 4);
      break;
    case 1:
      assert(x >= 4);
      [[fallthrough]];
    default:
      assert(x < 8);
    }
    nf_ = x;
    assigned_operands_ |= riscv_meta::HAS_NF;
  }
  /// @brief Set AMO acquire/release ordering bits.
  void set_ordering(bool aq, bool rl) {
    assert(meta_->operand_mask & riscv_meta::HAS_ORDERING);
    aq_ = aq;
    rl_ = rl;
    assigned_operands_ |= riscv_meta::HAS_ORDERING;
  }

  /// @brief Set a legal immediate operand.
  template <typename Tv>
  void set_imm(Tv imm_) {
    assert(meta_->operand_mask & riscv_meta::HAS_IMM);
    set_imm_unsafe_(imm_);
    assigned_operands_ |= riscv_meta::HAS_IMM;
  }
  /// @brief Set first tensor-core format operand.
  void set_fmt1(uint32_t x) {
    assert(meta_->operand_mask & riscv_meta::HAS_FMT1);
    fmt1_ = x;
    assigned_operands_ |= riscv_meta::HAS_FMT1;
  }
  /// @brief Set second tensor-core format operand.
  void set_fmt2(uint32_t x) {
    assert(meta_->operand_mask & riscv_meta::HAS_FMT2);
    fmt2_ = x;
    assigned_operands_ |= riscv_meta::HAS_FMT2;
  }

  /// @brief Return integer destination register rd.
  riscv_reg_t rd() const {
    assert((meta_->operand_mask & riscv_meta::HAS_RD) != 0);
    return rd_;
  }
  /// @brief Return integer source register rs1.
  riscv_reg_t rs1() const {
    assert((meta_->operand_mask & riscv_meta::HAS_RS1) != 0);
    return rs1_;
  }
  /// @brief Return integer source register rs2.
  riscv_reg_t rs2() const {
    assert((meta_->operand_mask & riscv_meta::HAS_RS2) != 0);
    return rs2_;
  }
  /// @brief Return immediate operand.
  int64_t imm() const {
    // immediate or target address uses the same storage, but this func is
    // dedicated to imm
    assert((meta_->operand_mask & riscv_meta::HAS_IMM) != 0);
    return imm_;
  }
  /// @brief Return branch/jump target offset.
  uint64_t tgt_addr_offset() const {
    assert((meta_->operand_mask & riscv_meta::HAS_TGT_ADDR) != 0);
    return imm_;
  }
  /// @brief Return CSR operand.
  uint16_t csr() const {
    assert((meta_->operand_mask & riscv_meta::HAS_CSR) != 0);
    return csr_;
  }
  /// @brief Return floating-point destination register fd.
  riscv_fpr_t fd() const {
    assert((meta_->operand_mask & riscv_meta::HAS_FD) != 0);
    return fd_;
  }
  /// @brief Return floating-point source register fs1.
  riscv_fpr_t fs1() const {
    assert((meta_->operand_mask & riscv_meta::HAS_FS1) != 0);
    return fs1_;
  }
  /// @brief Return floating-point source register fs2.
  riscv_fpr_t fs2() const {
    assert((meta_->operand_mask & riscv_meta::HAS_FS2) != 0);
    return fs2_;
  }
  /// @brief Return floating-point source register fs3.
  riscv_fpr_t fs3() const {
    assert((meta_->operand_mask & riscv_meta::HAS_FS3) != 0);
    return fs3_;
  }
  /// @brief Return floating-point rounding mode.
  riscv_rm_t rm() const {
    assert((meta_->operand_mask & riscv_meta::HAS_RM) != 0);
    return rm_;
  }
  /// @brief Return vector destination register vd.
  riscv_vreg_t vd() const {
    assert((meta_->operand_mask & riscv_meta::HAS_VD) != 0);
    return vd_;
  }
  /// @brief Return vector source register vs1.
  riscv_vreg_t vs1() const {
    assert((meta_->operand_mask & riscv_meta::HAS_VS1) != 0);
    return vs1_;
  }
  /// @brief Return vector source register vs2.
  riscv_vreg_t vs2() const {
    assert((meta_->operand_mask & riscv_meta::HAS_VS2) != 0);
    return vs2_;
  }
  /// @brief Return vector source register vs3.
  riscv_vreg_t vs3() const {
    assert((meta_->operand_mask & riscv_meta::HAS_VS3) != 0);
    return vs3_;
  }
  /// @brief Return vector mask bit.
  bool vm() const {
    assert((meta_->operand_mask & riscv_meta::HAS_VM) != 0);
    return vm_;
  }
  /// @brief Return vector memory operation mode.
  uint32_t mop() const {
    assert((meta_->operand_mask & riscv_meta::HAS_MOP) != 0);
    return mop_;
  }
  /// @brief Return vector memory element-width bit.
  bool mew() const {
    assert((meta_->operand_mask & riscv_meta::HAS_MEW) != 0);
    return mew_;
  }
  /// @brief Return vector segment field count.
  uint32_t nf() const {
    assert((meta_->operand_mask & riscv_meta::HAS_NF) != 0);
    return nf_;
  }
  /// @brief Return AMO acquire bit.
  bool aq() const {
    assert((meta_->operand_mask & riscv_meta::HAS_ORDERING) != 0);
    return aq_;
  }
  /// @brief Return AMO release bit.
  bool rl() const {
    assert((meta_->operand_mask & riscv_meta::HAS_ORDERING) != 0);
    return rl_;
  }
  /// @brief Return first tensor-core format operand.
  bool fmt1() const {
    assert((meta_->operand_mask & riscv_meta::HAS_FMT1) != 0);
    return fmt1_;
  }
  /// @brief Return second tensor-core format operand.
  bool fmt2() const {
    assert((meta_->operand_mask & riscv_meta::HAS_FMT2) != 0);
    return fmt2_;
  }

  /// @brief Return metadata for this instruction.
  const auto &meta() const { return meta_; }

  /// @brief Encode this instruction into @p instr_buf.
  void emit(std::vector<uint8_t> &instr_buf) const;
};

} // namespace fuzzgpu
