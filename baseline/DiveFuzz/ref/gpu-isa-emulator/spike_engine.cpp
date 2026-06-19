#include "spike_engine.h"
#include "difftest_gpu/isa_emulator.hh"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>

#include <elf.h>

static std::vector<std::string> xpr_name = {
    "zero", "ra", "sp", "gp", "tp",  "t0",  "t1", "t2", "s0", "s1", "a0",
    "a1",   "a2", "a3", "a4", "a5",  "a6",  "a7", "s2", "s3", "s4", "s5",
    "s6",   "s7", "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6"};

static std::vector<std::string> fpr_name = {
    "ft0", "ft1", "ft2",  "ft3",  "ft4", "ft5", "ft6",  "ft7",
    "fs0", "fs1", "fa0",  "fa1",  "fa2", "fa3", "fa4",  "fa5",
    "fa6", "fa7", "fs2",  "fs3",  "fs4", "fs5", "fs6",  "fs7",
    "fs8", "fs9", "fs10", "fs11", "ft8", "ft9", "ft10", "ft11"};

namespace spike_engine {

//==============================================================================
// SpikeEngine Implementation
//==============================================================================

template <typename Ehdr, typename Phdr>
static bool load_elf_segments(
    std::ifstream &ifs,
    const std::function<void(difftest::ScopeIdentifier mem_scope_id,
                             uint64_t paddr, const uint8_t *data, size_t sz)>
        &pmem_write) {

  Ehdr ehdr;
  ifs.seekg(0);
  if (!ifs.read(reinterpret_cast<char *>(&ehdr), sizeof(ehdr)))
    return false;

  std::vector<Phdr> phdrs(ehdr.e_phnum);
  ifs.seekg(ehdr.e_phoff);
  if (!ifs.read(reinterpret_cast<char *>(phdrs.data()),
                sizeof(Phdr) * ehdr.e_phnum))
    return false;

  for (const auto &phdr : phdrs) {
    if (phdr.p_type == PT_LOAD) {
      std::vector<uint8_t> buffer(phdr.p_filesz);
      ifs.seekg(phdr.p_offset);
      if (!ifs.read(reinterpret_cast<char *>(buffer.data()), phdr.p_filesz))
        return false;

      pmem_write(difftest::ScopeIdentifier(difftest::ScopeIdentifier::global()),
                 phdr.p_paddr, buffer.data(), phdr.p_filesz);

      if (phdr.p_memsz > phdr.p_filesz) {
        size_t bss_size = phdr.p_memsz - phdr.p_filesz;
        std::vector<uint8_t> zeros(bss_size, 0);
        pmem_write(
            difftest::ScopeIdentifier(difftest::ScopeIdentifier::global()),
            phdr.p_paddr + phdr.p_filesz, zeros.data(), bss_size);
      }
    }
  }
  return true;
}

static uint64_t read_symbol_address(const std::string &elf_path,
                                    const std::string &symbol_name) {
  // Use objdump to read symbol table from ELF
  // Format: objdump -t <elf_file> | grep <symbol>
  // Output: "address  flags section size name"

  std::string cmd = "riscv64-unknown-elf-objdump -t " + elf_path +
                    " 2>/dev/null | grep ' " + symbol_name + "$'";

  FILE *pipe = popen(cmd.c_str(), "r");
  if (!pipe) {
    return 0;
  }

  char buffer[256];
  uint64_t address = 0;

  if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
    // Parse first field (address in hex)
    char *endptr;
    address = strtoull(buffer, &endptr, 16);
  }

  pclose(pipe);
  return address;
}

SpikeEngine::SpikeEngine(const std::string &elf_path, const std::string &isa,
                         size_t num_instrs, bool verbose)
  : elf_path_(elf_path)
  , num_instrs_(num_instrs)
  , isa_emulator(new difftest::ISAEmulator())
  , arch(isa_emulator->get_arch())
  , trace(arch) {}

size_t SpikeEngine::get_instruction_size(uint32_t machine_code) {
  // RISC-V instruction length encoding (from RISC-V spec):
  // - If bits[1:0] != 0b11, it's a 16-bit compressed instruction
  // - If bits[1:0] == 0b11 and bits[4:2] != 0b111, it's a 32-bit instruction
  // - Otherwise, it's a longer instruction (48/64-bit, not commonly used)

  uint8_t opcode_low = machine_code & 0x3;

  if (opcode_low != 0x3) {
    // Compressed instruction (16-bit)
    return 2;
  }

  uint8_t opcode_mid = (machine_code >> 2) & 0x7;
  if (opcode_mid != 0x7) {
    // Standard 32-bit instruction
    return 4;
  }

  // Instructions longer than 32 bits are not supported
  throw std::runtime_error("Unsupported instruction length (>32 bits)");
}

SpikeEngine::~SpikeEngine() {
  // sim_ will be automatically destroyed
}

bool SpikeEngine::initialize() {
#if defined(ARCH_VORTEX)
  isa_emulator->bootstrap(arch.startup_addr);
#elif defined(ARCH_VENTUS)
  const unsigned workgroup_size[3] = {1, 1, 1};
  const unsigned num_wavefront_per_wavegroup = 1;
  const unsigned wavefront_size = 1;
  isa_emulator->bootstrap(arch.startup_addr, workgroup_size,
                          num_wavefront_per_wavegroup, wavefront_size, 64, 32);
#endif
  isa_emulator->reset();

  // read ELF program headers and load to memory
  // although we cannot mmap the elf into the emulator memory,
  // the execute_instruction() will write instructions to the memory
  // region we set up here.
  std::ifstream elf_ifs(elf_path_, std::ios::binary);
  if (!elf_ifs) {
    throw std::runtime_error("failed to open input ELF file: " + elf_path_);
  }

  unsigned char e_ident[EI_NIDENT];
  if (!elf_ifs.read(reinterpret_cast<char *>(e_ident), EI_NIDENT)) {
    throw std::runtime_error("failed to read ELF header identification");
  }

  if (e_ident[EI_MAG0] != ELFMAG0 || e_ident[EI_MAG1] != ELFMAG1 ||
      e_ident[EI_MAG2] != ELFMAG2 || e_ident[EI_MAG3] != ELFMAG3) {
    throw std::runtime_error("Not a valid ELF file");
  }

  auto pmem_write = [&](difftest::ScopeIdentifier mem_scope_id, uint64_t paddr,
                        const uint8_t *data, size_t sz) {
    isa_emulator->pmem_write(mem_scope_id, paddr, data, sz);
  };
  std::cout << "load elf\n";
  if (e_ident[EI_CLASS] == ELFCLASS32) {
    load_elf_segments<Elf32_Ehdr, Elf32_Phdr>(elf_ifs, pmem_write);
  } else if (e_ident[EI_CLASS] == ELFCLASS64) {
    load_elf_segments<Elf64_Ehdr, Elf64_Phdr>(elf_ifs, pmem_write);
  } else {
    throw std::runtime_error("Unknown ELF class");
  }

  std::cout << "read_symbol_address elf\n";

  // Find instruction region using ELF symbol table
  // Read the 'main' symbol address to locate the instruction region

  // Try to read 'main' symbol address from ELF
  uint64_t main_addr = read_symbol_address(elf_path_, "main");
  if (main_addr == 0) {
    // Fallback: try common alternatives
    main_addr = read_symbol_address(elf_path_, "_start");
    if (main_addr == 0) {
      last_error_ = "Failed to find 'main' or '_start' symbol in ELF";
      throw std::runtime_error(last_error_);
    }
  }

  std::cout << "main_addr: " << std::hex << main_addr << std::dec << "\n";

  // Set instruction region bounds
  instruction_region_start_ = main_addr;
  next_instruction_addr_ = main_addr;

  // Calculate available space (assuming worst case: all 4-byte instructions)
  // This provides enough space even if all instructions are standard 32-bit
  instruction_region_end_ = main_addr + num_instrs_ * 4;

  if (!isa_emulator->schedulable(0)) {
    throw std::runtime_error("Warp 0 not schedulable for execution");
  }

  trace.next_pc = arch.startup_addr;

  do {
    const auto current_pc = trace.next_pc;
    isa_emulator->execute_warp(0, &trace);

    // Update next instruction address to current PC
    // (Spike automatically updates PC based on instruction length)
    if (trace.trap_taken) {
      // ventus & vortex currently do not support exception handling

      // as the exception raise, no GPR, MEM, or SIMT stack should be modified,
      // we just restore the PC
      trace.next_pc = current_pc;
      isa_emulator->pc_write(0, trace.next_pc);
      throw std::runtime_error("Instruction caused a trap");
    }
  } while (trace.next_pc != next_instruction_addr_);

  initialized_ = true;
  return true;
}

#define MATCH_REGEXT 0x200b
#define MASK_REGEXT 0x707f

void SpikeEngine::execute_instruction(uint64_t machine_code) {
  if (arch.name == "ventus") {
    // regext prefix
    if ((machine_code & MASK_REGEXT) == MATCH_REGEXT) {
      execute_instruction_(static_cast<uint32_t>(machine_code & 0xFFFFFFFF));
      execute_instruction_(
          static_cast<uint32_t>((machine_code >> 32) & 0xFFFFFFFF));
    } else {
      if ((machine_code >> 32) & 0xFFFFFFFF) {
        throw std::runtime_error("Instruction length > 32 bits and no regext "
                                 "prefix not supported for this arch");
      }
      execute_instruction_(static_cast<uint32_t>(machine_code & 0xFFFFFFFF));
    }

  } else {
    if ((machine_code >> 32) & 0xFFFFFFFF) {
      throw std::runtime_error(
          "Instruction length > 32 bits not supported for this arch");
    }
    execute_instruction_(static_cast<uint32_t>(machine_code & 0xFFFFFFFF));
  }
}

void SpikeEngine::execute_instruction_(uint32_t machine_code) {
  // Detect instruction size (2 for compressed, 4 for standard)
  size_t instr_size = get_instruction_size(machine_code);

  // Check if we have enough space
  if (next_instruction_addr_ + instr_size > instruction_region_end_) {
    throw std::runtime_error("Out of instruction region space");
  }

  // Verify PC matches next instruction address
  uint64_t current_pc = trace.next_pc;
  if (current_pc != next_instruction_addr_) {
    std::ostringstream oss;
    oss << "PC mismatch: expected 0x" << std::hex << next_instruction_addr_
        << " but got 0x" << current_pc << std::dec;
    throw std::runtime_error(oss.str());
  }

  // Get instruction address
  uint64_t instr_addr = next_instruction_addr_;

  if (!isa_emulator->schedulable(0)) {
    throw std::runtime_error("Warp 0 not schedulable for execution");
  }

  // Write machine code with correct size
  isa_emulator->pmem_write(
      difftest::ScopeIdentifier::global(), instr_addr,
      reinterpret_cast<const uint8_t *>(
          &machine_code), // for the little-endian host (e.g., x86)
      instr_size);

  isa_emulator->execute_warp(0, &trace);

  // Update next instruction address to current PC
  // (Spike automatically updates PC based on instruction length)
  if (trace.trap_taken) {
    // ventus & vortex currently do not support exception handling

    // as the exception raise, no GPR, MEM, or SIMT stack should be modified, we
    // just restore the PC
    trace.next_pc = current_pc;
    isa_emulator->pc_write(0, trace.next_pc);
    throw std::runtime_error("Instruction caused a trap");
  }

  next_instruction_addr_ = trace.next_pc;
}

std::vector<uint64_t>
SpikeEngine::get_operands(const std::vector<std::string> &source_regs,
                          int64_t immediate) {
  if (!initialized_) {
    throw std::runtime_error("SpikeEngine not initialized");
  }

  // STEP 1: Read source register values BEFORE execution
  // This ensures we capture the values that will be used by the instruction,
  // even if the destination register overlaps with source registers
  // (e.g., add x10, x10, x11 - we want the OLD value of x10, not the result)
  std::vector<uint64_t> source_values;
  for (const auto &reg_idx : source_regs) {
    if (reg_idx[0] == 'v' || reg_idx[0] == 'V') { // vector register
      int reg_id = std::stoi(reg_idx.substr(1));

      for (unsigned thread_id = 0; thread_id < 1; ++thread_id) {
        uint64_t val = isa_emulator->reg_read(
            0, thread_id,
            difftest::RegId(difftest::RegType::Vec, reg_id, arch.word_bits));
        source_values.push_back(val);
      }

    } else if (reg_idx[0] == 'x' || reg_idx[0] == 'X') { // integer register
      int reg_id = std::stoi(reg_idx.substr(1));
      uint64_t reg_val = isa_emulator->reg_read(
          0, 0,
          difftest::RegId(difftest::RegType::Int, reg_id, arch.word_bits));
      source_values.push_back(reg_val);

    } else {
      // parse xpr_name and fpr_name, read the reg
      // Check if it's an integer register by name
      auto xpr_it = std::find(xpr_name.begin(), xpr_name.end(), reg_idx);
      if (xpr_it != xpr_name.end()) {
        int reg_id = std::distance(xpr_name.begin(), xpr_it);
        uint64_t reg_val = isa_emulator->reg_read(
            0, 0,
            difftest::RegId(difftest::RegType::Int, reg_id, arch.word_bits));
        source_values.push_back(reg_val);
      } else {
        // Check if it's a floating point register by name
        auto fpr_it = std::find(fpr_name.begin(), fpr_name.end(), reg_idx);
        if (fpr_it != fpr_name.end()) {
          int reg_id = std::distance(fpr_name.begin(), fpr_it);
          uint64_t reg_val =
              isa_emulator->reg_read(0, 0,
                                     difftest::RegId(difftest::RegType::Float,
                                                     reg_id, arch.word_bits));
          source_values.push_back(reg_val);
        } else {
          throw std::runtime_error("Invalid register identifier: " + reg_idx);
        }
      }
    }
  }

  // Add immediate if instruction has one (including immediate=0)
  if (immediate != IMMEDIATE_NOT_PRESENT) {
    source_values.push_back(static_cast<uint64_t>(immediate));
  }
  return source_values;
}

} // namespace spike_engine
