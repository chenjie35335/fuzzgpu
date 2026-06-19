#pragma once

#include <cstdint>
#include <memory>

namespace fuzzgpu {
class riscv_instr;
class riscv_meta;

/// @brief Arena allocator for generated RISC-V instruction objects.
class InstrPool {
  /// @brief Fixed-size allocation chunk inside the instruction arena.
  class Chunk {
    ///< Capacity measured in instruction objects.
    size_t num_instrs_;
    ///< Number of objects allocated from this chunk.
    size_t allocated_{0};

  public:
    ///< Raw storage backing instruction placement-new.
    uint8_t *blocks;
    /// @brief Allocate storage for @p num_instrs instruction objects.
    Chunk(size_t num_instrs);
    ~Chunk();

    /// @brief Return true when no more instructions fit in this chunk.
    bool full() const { return allocated_ == num_instrs_; }
    /// @brief Create an instruction with @p meta in this chunk.
    riscv_instr *create(const std::shared_ptr<riscv_meta> &meta);
  };

  ///< Chunk currently receiving new instructions.
  Chunk *cur_instr_chunk_{nullptr};
  ///< Chunks that have reached capacity.
  std::vector<Chunk *> full_instr_chunk_;

public:
  /// @brief Create an instruction in the arena.
  riscv_instr *create(const std::shared_ptr<riscv_meta> &meta);
};

}; // namespace fuzzgpu
