#pragma once

#include <capstone/capstone.h>
#include <list>
#include <memory>

#include "random_mem_allocator.hh"
#include <difftest_gpu/isa_emulator.hh>

namespace fuzzgpu {

class GenWarp;
class RuntimeWarp;
class riscv_instr;

/// @brief per-warp scratchpad for code emission
class Scratchpad {
  ///< Owning generated warp.
  GenWarp *gen_warp_;
  ///< Emulator used to commit emitted instructions.
  difftest::ISAEmulator &isa_emu_;
  ///< Current memory block being emitted.
  MemoryBlock mem_blk_;
  ///< Current program counter.
  uint64_t pc_;

public:
  /// @brief Contiguous emitted byte fragment within a memory block.
  class MemoryFragment {
  public:
    ///< Offset of this fragment within the output memory image.
    uint64_t offset;
    ///< Encoded instruction bytes contained in the fragment.
    std::vector<uint8_t> instrs;
    /// @brief Start a fragment at @p offset_.
    MemoryFragment(uint64_t offset_)
      : offset(offset_) {}
  };

private:
  ///< Ordered fragments emitted by this scratchpad.
  std::list<MemoryFragment> memory_fragments_;
  ///< Fragment currently receiving bytes.
  MemoryFragment *cur_mem_fragment;

  ///< Capstone handle used for instruction diagnostics.
  csh cs_handle_;

public:
  /// @brief Construct a scratchpad for @p gen_warp.
  Scratchpad(GenWarp *gen_warp, difftest::ISAEmulator &isa_emu);

  /// @brief Move emission to @p mem_blk.
  void seek(const MemoryBlock &mem_blk);

  /// @brief Return the current memory block.
  const MemoryBlock &tell() const { return mem_blk_; }

  /// @brief emit and commit an instruction to the scratchpad
  /// @param instr
  /// @return PC of the last emitted instruction
  uint64_t emit(const riscv_instr *instr, bool overwrite = false,
                bool taint_out = false);

  /// @brief Reopen emitted code starting from @p pc for overwrite.
  void overwrite_from(uint64_t pc);

  /// @brief Shrink the current block to end at @p new_end_addr.
  void shrink(uint64_t new_end_addr);

  /// @brief Return all emitted memory fragments.
  const auto &memory_fragments() const { return memory_fragments_; }

  /// @brief Write emitted fragments to a binary stream.
  void write_file(std::ostream &stream) const;
};

} // namespace fuzzgpu
