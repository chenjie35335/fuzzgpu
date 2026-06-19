// Copyright (c) 2024-2025 DiveFuzz Project
// SPDX-License-Identifier: Mulan PSL v2

#ifndef SPIKE_ENGINE_H
#define SPIKE_ENGINE_H

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <difftest_gpu/isa_emulator.hh>

namespace spike_engine {

// Special value to indicate instruction without immediate operand
constexpr int64_t IMMEDIATE_NOT_PRESENT = INT64_MIN;

class SpikeEngine {
public:
  /**
   * Constructor
   * @param num_instrs Number of instructions to generate (nops in ELF)
   * @param verbose Enable verbose output (default: false)
   */
  SpikeEngine(const std::string &elf_path, const std::string &isa,
              size_t num_instrs, bool verbose = false);

  /**
   * Destructor
   */
  ~SpikeEngine();

  /**
   * Initialize Spike and run until main function entry
   * This executes template initialization code
   * @param immediate Optional immediate value to include in source_values
   * (default: 0)
   * @return true on success, false on error
   */
  bool initialize();

  std::vector<uint64_t>
  get_operands(const std::vector<std::string> &source_regs,
               int64_t immediate = 0);

  /**
   * Execute one instruction
   *
   * Automatically detects instruction size (2 or 4 bytes), writes it to the
   * next available memory address, executes it, and captures register values
   * before and after execution.
   *
   * @param machine_code 2 * 32-bit instruction encoding (may be 16-bit
   * compressed instruction zero-padded to 32 bits) LSBs : instruction 0 MSBs :
   * instruction 1 (if any, else zero)
   *
   * @throws std::runtime_error if:
   *   - Engine not initialized
   *   - Out of instruction region space
   *   - PC mismatch (internal consistency error)
   *   - Memory write failure
   *   - Instruction execution failure
   *
   * @note For compressed instructions (bits[1:0] != 0b11), only the low 16 bits
   *       are written to memory. Spike automatically updates PC by 2 or 4 bytes
   *       based on the instruction encoding.
   */
  void execute_instruction(uint64_t machine_code);

  // as GPU cannot create checkpoints, we implement a workaround by reading
  // source operands before the instruction finally committed to the elf. By
  // this way, bug filter cannot use dest_values_after, but at least can use
  // source_values_before to filter some invalid instructions.
  void set_checkpoint() {}
  void restore_checkpoint() {}

  /**
   * Get last error message
   */
  std::string get_last_error() const { return last_error_; }

  /**
   * Detect instruction size from machine code (static utility)
   *
   * Determines whether an instruction is 16-bit compressed or 32-bit standard
   * based on RISC-V encoding rules specified in the ISA manual.
   *
   * RISC-V Instruction Length Encoding (bits[1:0]):
   *   - bits[1:0] != 0b11  →  16-bit compressed instruction (RV*C)
   *   - bits[1:0] == 0b11  →  32-bit or longer instruction
   *     - If bits[4:2] != 0b111  →  32-bit standard instruction
   *     - If bits[4:2] == 0b111  →  48/64-bit instruction (not supported)
   *
   * @param machine_code 32-bit instruction encoding (may contain zero-padded
   *                     16-bit compressed instruction in lower half)
   *
   * @return Instruction size in bytes: 2 for compressed, 4 for standard
   *
   * @throws std::runtime_error if instruction length > 32 bits (unsupported)
   *
   * @note This is a static method and can be called without an instance:
   *       size_t sz = SpikeEngine::get_instruction_size(0x00008522);
   */
  static size_t get_instruction_size(uint32_t machine_code);

private:
  bool initialized_{false};
  std::string elf_path_;
  size_t num_instrs_;
  difftest::ISAEmulator *isa_emulator;
  difftest::Arch arch;
  difftest::Trace trace;
  std::string last_error_;

  // Instruction region
  uint64_t instruction_region_start_;
  uint64_t instruction_region_end_;
  uint64_t next_instruction_addr_; // Next available address for instruction

  void execute_instruction_(uint32_t machine_code);
};

} // namespace spike_engine

#endif // SPIKE_ENGINE_H
