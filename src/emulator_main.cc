#include <argparse/argparse.hpp>
#include <csignal>
#include <fstream>
#include <functional>
#include <future>
#include <iostream>
#include <set>
#include <stdexcept>

#include <capstone/capstone.h>
#include <elf.h>

#include "difftest_gpu/api.hh"
#include "difftest_gpu/dut.hh"
#include "difftest_gpu/isa_emulator.hh"
#include "emulator_dbg_cmd.hh"
#include "exchange_file.hh"
#include "isa/riscv/riscv_meta.hh"

#include "spdlog/spdlog.h"
#include <nlohmann/json.hpp>

using namespace fuzzgpu;

std::function<void()> g_save_coverage_callback;

void signal_handler(int signal) {
  if (g_save_coverage_callback) {
    std::cerr << "Signal " << signal << " received, saving coverage..."
              << std::endl;
    g_save_coverage_callback();
  }
  std::_Exit(128 + signal);
}

void terminate_handler() {
  if (g_save_coverage_callback) {
    std::cerr << "std::terminate called, saving coverage..." << std::endl;
    g_save_coverage_callback();
  }
  std::_Exit(1);
}

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

      // If the segment is executable
      if (phdr.p_flags & PF_X) {
        for (uint64_t pc = phdr.p_paddr; pc < phdr.p_paddr + phdr.p_filesz;
             pc += riscv_meta::ilen) {
          // pcset[pc] = false; // pc set is not accurate (includes non-instr
          // bytes)
        }
      }
    }
  }
  return true;
}

static bool load_binary(
    const std::string &instr_binfile, const std::string &data_binfile,
    const std::string &elf_file, const difftest::Arch &arch,
    const std::function<void(difftest::ScopeIdentifier mem_scope_id,
                             uint64_t paddr, const uint8_t *data, size_t sz)>
        &pmem_write,
    std::unordered_map<uint64_t, bool> &pcset) {

  if (!elf_file.empty()) {
    spdlog::info("Loading ELF file: \"{}\"", elf_file);
    // read ELF program headers and load to memory
    std::ifstream elf_ifs(elf_file, std::ios::binary);
    if (!elf_ifs) {
      spdlog::error("failed to open input ELF file: {}", elf_file);
      return false;
    }

    unsigned char e_ident[EI_NIDENT];
    if (!elf_ifs.read(reinterpret_cast<char *>(e_ident), EI_NIDENT)) {
      spdlog::error("failed to read ELF header identification");
      return false;
    }

    if (e_ident[EI_MAG0] != ELFMAG0 || e_ident[EI_MAG1] != ELFMAG1 ||
        e_ident[EI_MAG2] != ELFMAG2 || e_ident[EI_MAG3] != ELFMAG3) {
      spdlog::error("Not a valid ELF file");
      return false;
    }

    if (e_ident[EI_CLASS] == ELFCLASS32) {
      return load_elf_segments<Elf32_Ehdr, Elf32_Phdr>(elf_ifs, pmem_write);
    } else if (e_ident[EI_CLASS] == ELFCLASS64) {
      return load_elf_segments<Elf64_Ehdr, Elf64_Phdr>(elf_ifs, pmem_write);
    } else {
      spdlog::error("Unknown ELF class");
      return false;
    }
  }

  //
  // Load instr
  //
  spdlog::info("Loading instr binary file: \"{}\"", instr_binfile);

  std::ifstream bin_ifs(instr_binfile, std::ios::binary);
  if (!bin_ifs) {
    spdlog::error("failed to open input binary file: {}", instr_binfile);
    return false;
  }

#ifndef VERBOSE_DISASM
  csh cs_handle_;
  cs_insn *insn;
  // init the disassembler for verbose logging
  if (spdlog::get_level() <= spdlog::level::trace) {
    if (cs_open(CS_ARCH_RISCV,
                arch.word_bits == 32 ? CS_MODE_RISCV32 : CS_MODE_RISCV64,
                &cs_handle_) != CS_ERR_OK) {
      throw std::runtime_error("Failed to initialize Capstone");
    }

    cs_option(cs_handle_, CS_OPT_DETAIL, CS_OPT_OFF);
  }
#endif

  size_t num_loaded_fragments = 0;
  std::vector<uint8_t> buffer;

  while (true) {
    LinearMemoryInstrFragmentHeader header;
    if (!bin_ifs.read(reinterpret_cast<char *>(&header), sizeof(header))) {
      if (bin_ifs.bad()) {
        spdlog::error("Failed to read the file: {}", std::strerror(errno));
        return false;
      }
      assert(bin_ifs.eof());
      spdlog::info("Finished loading binary file.");
      break;
    }

    auto paddr = static_cast<uint64_t>(header.paddr_offset);
    auto sz = static_cast<size_t>(header.sz);

    buffer.resize(sz);

    if (!bin_ifs.read(reinterpret_cast<char *>(buffer.data()), sz)) {
      spdlog::error("Unexpected EOF while reading memory fragment payload "
                    "(expected {} bytes)",
                    sz);
      return false;
    }

#ifndef VERBOSE_DISASM
    if (spdlog::get_level() <= spdlog::level::trace) {
      std::stringstream ss;
      for (size_t i = 0; i + riscv_meta::ilen <= header.sz;
           i += riscv_meta::ilen) {
        auto count = cs_disasm(cs_handle_, buffer.data() + i, riscv_meta::ilen,
                               header.paddr_offset + i, 1, &insn);

        ss << std::hex << std::setw(8) << std::setfill('0')
           << header.paddr_offset + i << ": " << std::setfill(' ');

        // show machine code
        for (size_t j = 0; j < riscv_meta::ilen; j++) {
          ss << std::hex << std::setw(2) << std::setfill('0')
             << (int)buffer[i + j];
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
        ss << std::endl;
      }
      spdlog::trace("{}", ss.str());
    }
#endif

    num_loaded_fragments++;

    pmem_write(difftest::ScopeIdentifier(difftest::ScopeIdentifier::global()),
               paddr, buffer.data(), sz);

    for (uint64_t pc = paddr; pc < paddr + sz;
         pc += riscv_meta::ilen /* fixed length only */) {
      pcset[pc] = false;
    }
  }

  spdlog::info("Total instr memory fragments loaded: {}", num_loaded_fragments);

  //
  // Load data
  //
  num_loaded_fragments = 0;

  spdlog::info("Loading data binary file: \"{}\"", data_binfile);

  std::ifstream data_ifs(data_binfile, std::ios::binary);
  if (!data_ifs) {
    spdlog::error("failed to open input data binary file: {}", data_binfile);
    return false;
  }
  while (true) {
    LinearMemoryDataFragmentHeader header;
    if (!data_ifs.read(reinterpret_cast<char *>(&header), sizeof(header))) {
      if (data_ifs.bad()) {
        spdlog::error("Failed to read the file: {}", std::strerror(errno));
        return false;
      }
      assert(data_ifs.eof());
      spdlog::info("Finished loading data binary file.");
      break;
    }

    auto paddr = static_cast<uint64_t>(header.paddr_offset);
    auto dat = static_cast<uint8_t>(header.dat);
    assert(size_t(header.scope_id.scope()) <
           magic_enum::enum_count<difftest::MemScope>());

#ifndef NDEBUG
    spdlog::trace("Loading data fragment at scope {}, paddr {:#x}, data: {:#x}",
                  magic_enum::enum_name(header.scope_id.scope()), paddr, dat);
#endif

    pmem_write(header.scope_id, paddr, &dat, 1);

    num_loaded_fragments++;
  }

  spdlog::info("Total data memory fragments loaded: {}", num_loaded_fragments);

  return true;
}

static void init_isa_emulator(difftest::ISAEmulator *isa_emulator,
                              const difftest::Arch &arch,
                              const nlohmann::json &meta_json) {
#if defined(ARCH_VORTEX)
  isa_emulator->bootstrap(arch.startup_addr);
#elif defined(ARCH_VENTUS)
  unsigned workgroup_size[3] = {
      meta_json["workgroup_size"][0],
      meta_json["workgroup_size"][1],
      meta_json["workgroup_size"][2],
  };
  isa_emulator->bootstrap(arch.startup_addr, workgroup_size,
                          meta_json["num_genwarps_per_block"],
                          meta_json["wavefront_size"], meta_json["sgpr_usage"],
                          meta_json["vgpr_usage"]);
#endif
  isa_emulator->reset();
}

static bool run_isa_emulator(
    difftest::ISAEmulator *isa_emulator, const difftest::Arch &arch,
    const std::string &trace_file_prefix, const nlohmann::json &meta_json,
    unsigned tmask_size, const std::vector<uint64_t> &data_race_load_pc,
    const std::unordered_set<difftest::WarpGID> &warps_limit,
    bool disasm_trace) {

  std::set<difftest::WarpGID> schedulable_warps;

  difftest::WarpGID max_warp_gid;
#if defined(ARCH_VORTEX)
  max_warp_gid = arch.vortex_nr_total_warps;
#elif defined(ARCH_VENTUS)
  max_warp_gid = meta_json["num_genblocks"].get<unsigned>() *
                 meta_json["num_genwarps_per_block"].get<unsigned>();
#endif

  //
  // load the trace file
  //
  /// @brief Reference trace payload for one PC in one warp.
  struct RefTrace {
    ///< Serialized trace header.
    TraceFragmentHeader hdr;
    ///< Register IDs written by the traced instruction.
    std::vector<difftest::RegId> trace_rd;
    ///< Register data written by the traced instruction.
    std::vector<uint64_t> trace_rd_data;
  };
  std::unordered_map<difftest::WarpGID, std::unordered_map<uint64_t, RefTrace>>
      pc_trace_map;
  std::unordered_set<uint64_t> data_race_load_pc_set;
  std::unordered_set<uint64_t> whitelist_pc_set, taint_pc_set;
  if (!trace_file_prefix.empty()) {
    for (int64_t warp_gid = max_warp_gid - 1; warp_gid >= 0; --warp_gid) {

      const std::string trace_file =
          fmt::format("{}_warp_gid_{}.trace", trace_file_prefix, warp_gid);
      spdlog::info("Opening trace file for warp gid {}: {}", warp_gid,
                   trace_file);
      std::ifstream trace_ifs(trace_file, std::ios::binary);
      if (!trace_ifs.is_open()) {
        spdlog::error("failed to open input trace file: {}", trace_file);
        return false;
      }

      while (true) {
        TraceFragmentHeader hdr;
        std::streampos old_pos = trace_ifs.tellg();
        trace_ifs.read(reinterpret_cast<char *>(&hdr), sizeof(hdr));
        if (trace_ifs.eof()) {
          break;
        } else if (trace_ifs.fail()) {
          spdlog::error("Failed to read the trace file: {} gcount={}", warp_gid,
                        trace_ifs.gcount());
          return false;
        } else {
          if (hdr.magic != TraceFragmentHeader::MAGIC_TRUTH) {
            spdlog::error(
                "Trace file warp GID {} corrupted at offset {}, invalid magic "
                "{:#x} (expected {:#x})",
                warp_gid, old_pos, hdr.magic, TraceFragmentHeader::MAGIC_TRUTH);
            return false;
          }
          if (pc_trace_map[warp_gid].find(hdr.pc) !=
              pc_trace_map[warp_gid].end()) {
            spdlog::error(
                "Trace file warp GID {} contains duplicate trace entries for "
                "PC {:#x}",
                warp_gid, hdr.pc);
            // just for debugging, ignore it
            whitelist_pc_set.insert(hdr.pc);
          }
          auto &dst = pc_trace_map[warp_gid][hdr.pc];
          dst.hdr = hdr;

          dst.trace_rd.resize(hdr.nr_rd);
          for (unsigned i = 0; i < hdr.nr_rd; ++i) {
            trace_ifs.read(reinterpret_cast<char *>(dst.trace_rd.data() + i),
                           sizeof(difftest::RegId));
            if (trace_ifs.fail()) {
              spdlog::error("Failed to read the trace file: warp GID {}",
                            warp_gid);
              return false;
            }
          }

          dst.trace_rd_data.resize(hdr.nr_rd_data);
          for (unsigned i = 0; i < hdr.nr_rd_data; ++i) {
            trace_ifs.read(
                reinterpret_cast<char *>(dst.trace_rd_data.data() + i),
                sizeof(uint64_t));
            if (trace_ifs.fail()) {
              spdlog::error("Failed to read the trace file: warp GID {}",
                            warp_gid);
              return false;
            }
          }
        }
      } // while
    } // for warp_gid

    for (const auto &pc : data_race_load_pc)
      data_race_load_pc_set.insert(pc);

    for (const auto &pc : meta_json["overwritten_pc_set"]) {
      whitelist_pc_set.insert(pc.get<uint64_t>());
    }
    for (const auto &pc : meta_json["taint_pc_set"]) {
      taint_pc_set.insert(pc.get<uint64_t>());
    }
  }

  std::vector<difftest::Trace> state_per_arch(max_warp_gid, arch);

  while (!isa_emulator->terminated()) {
    schedulable_warps.clear();
    unsigned executed_warps = 0;
    for (int64_t warp_gid = max_warp_gid - 1;
         !isa_emulator->terminated() && warp_gid >= 0; --warp_gid) {
      if (isa_emulator->schedulable(warp_gid)) {
        schedulable_warps.insert(warp_gid);
        if (!warps_limit.empty() &&
            warps_limit.find(warp_gid) == warps_limit.end()) {
          continue;
        }
        ++executed_warps;

        if (1 || warp_gid == 4) {
          spdlog::info("Warp GID {} next PC {:#x} tmask {}", warp_gid,
                       state_per_arch[warp_gid].next_pc,
                       state_per_arch[warp_gid].tmask);

          for (unsigned tlid = 0; tlid < tmask_size; ++tlid) {
            if (state_per_arch[warp_gid].next_pc == 0x940424a0) {
              spdlog::info("v16 {:#x}",
                           isa_emulator->reg_read(
                               warp_gid, tlid,
                               difftest::RegId(difftest::RegType::Vec,
                                               unsigned(riscv_vreg_t::V16))));
            }
            if (state_per_arch[warp_gid].next_pc == 0x7e65baf8 ||
                state_per_arch[warp_gid].next_pc == 0x0b150550) {
              spdlog::info("{:#x} a3  {:#x}", state_per_arch[warp_gid].next_pc,
                           isa_emulator->reg_read(
                               warp_gid, tlid,
                               difftest::RegId(difftest::RegType::Int,
                                               unsigned(riscv_reg_t::A3))));
            }
          }
        }

        isa_emulator->execute_warp(warp_gid, &state_per_arch[warp_gid]);

        if (disasm_trace) {
          // mainly for the motivation.py
          spdlog::info("Disasm: {} wid={} PC {:#x} tmask {}",
                       state_per_arch[warp_gid].disasm, warp_gid,
                       state_per_arch[warp_gid].pc,
                       state_per_arch[warp_gid].tmask);
        }

        if (state_per_arch[warp_gid].trap_taken) {
          spdlog::error(
              "Warp GID {} trapped at PC {:#x} during ISA emulation, aborting "
              "fuzzing.",
              warp_gid, state_per_arch[warp_gid].pc);
          throw std::runtime_error("fuzzing aborted");
        }

        // difftest
        if (!pc_trace_map.empty()) {
          if (pc_trace_map[warp_gid].find(state_per_arch[warp_gid].pc) !=
              pc_trace_map[warp_gid].end()) {
            const auto &ref =
                pc_trace_map[warp_gid].at(state_per_arch[warp_gid].pc);

            if (data_race_load_pc_set.find(state_per_arch[warp_gid].pc) !=
                data_race_load_pc_set.end()) {
              spdlog::warn("Warp GID {} at PC {:#x} is a data-race load, "
                           "skipping diff-test.",
                           warp_gid, state_per_arch[warp_gid].pc);

            } else if (taint_pc_set.find(state_per_arch[warp_gid].pc) !=
                       taint_pc_set.end()) {
              spdlog::warn("Warp GID {} at PC {:#x} is a taint PC, "
                           "skipping diff-test.",
                           warp_gid, state_per_arch[warp_gid].pc);

            } else if (whitelist_pc_set.find(state_per_arch[warp_gid].pc) !=
                       whitelist_pc_set.end()) {
              spdlog::warn("Warp GID {} at PC {:#x} is in the whitelist, "
                           "skipping diff-test.",
                           warp_gid, state_per_arch[warp_gid].pc);
            } else {
              if (state_per_arch[warp_gid].regs.size() != ref.trace_rd.size()) {
                spdlog::error("Trace file warp GID {} wb reg size mismatch: "
                              "expected {}, got {}",
                              warp_gid, ref.trace_rd.size(),
                              state_per_arch[warp_gid].regs.size());
                return false;
              }

              if (state_per_arch[warp_gid].applied_tmask !=
                  ref.hdr.applied_tmask) {
                spdlog::error(
                    "Trace file warp GID {} applied_tmask mismatch: expected "
                    "{}, got {}",
                    warp_gid, ref.hdr.applied_tmask,
                    state_per_arch[warp_gid].applied_tmask);
                return false;
              }

              bool reg_err = false;
              for (size_t irg = 0;
                   irg < std::min(ref.trace_rd.size(),
                                  state_per_arch[warp_gid].regs.size());
                   ++irg) {

                bool scalar_thread = false;
                for (unsigned tlid = 0; !scalar_thread && tlid < tmask_size;
                     ++tlid) {
                  if (ref.hdr.applied_tmask) {
                    if (!state_per_arch[warp_gid].tmask.test(tlid)) {
                      continue;
                    }
                  } else {
                    scalar_thread = true;
                  }

                  const auto trace_rd_dat_ =
                      ref.trace_rd_data[irg * tmask_size + tlid];

                  const auto a = trace_rd_dat_, b = state_per_arch[warp_gid]
                                                        .regs[irg]
                                                        .rd_data[tlid];
                  bool ok;
                  // although these checks are bugy, they are for temp debugging
                  // only!
                  if (ref.hdr.opcode == difftest::Opcode::FloatingPoint) {
                    ok = true; // ulp <= fp2int_ulp_tolerance;
                  } else {
                    ok = (a == b);
                  }
                  if (ok)
                    continue;
                  spdlog::error(
                      "    RD_Data {}{} [{}]: expected {:#x}, got {:#x}",
                      state_per_arch[warp_gid].regs[irg].rd.type_str(),
                      state_per_arch[warp_gid].regs[irg].rd.idx, tlid,
                      trace_rd_dat_,
                      state_per_arch[warp_gid].regs[irg].rd_data[tlid]);
                  reg_err =
                      (ref.trace_rd[irg].type != difftest::RegType::Float);
                }
                if (ref.trace_rd[irg] !=
                    state_per_arch[warp_gid].regs[irg].rd) {
                  spdlog::error(
                      "    RD: expected {}{}.{}, got {}{}.{}",
                      ref.trace_rd[irg].type_str(), ref.trace_rd[irg].idx,
                      ref.trace_rd[irg].sew,
                      state_per_arch[warp_gid].regs[irg].rd.type_str(),
                      state_per_arch[warp_gid].regs[irg].rd.idx,
                      state_per_arch[warp_gid].regs[irg].rd.sew);
                  reg_err = true;
                }

              } // for (irg)
              if (/*ref.hdr.tmask != state_per_arch[warp_gid].tmask.to_ulong()
                     ||*/
                  reg_err ||
                  ref.hdr.opcode != state_per_arch[warp_gid].opcode ||
                  (ref.hdr.opcode == difftest::Opcode::CtrlStatusRead &&
                   ref.hdr.csr_addr != state_per_arch[warp_gid].csr_addr)) {
                spdlog::error("Trace mismatch at PC {:#x} warp GID {}:",
                              state_per_arch[warp_gid].pc, warp_gid);
                spdlog::error("  Expected: PC {:#x} TMask {:#x}"
                              "Opcode {} CSR_Addr {:#x}",
                              ref.hdr.pc, ref.hdr.tmask,
                              magic_enum::enum_name(ref.hdr.opcode),
                              ref.hdr.csr_addr);
                spdlog::error(
                    "  Got:      PC {:#x} TMask {:#x}"
                    "Opcode {} CSR_Addr {:#x}",
                    state_per_arch[warp_gid].pc,
                    state_per_arch[warp_gid].tmask.to_ulong(),
                    magic_enum::enum_name(state_per_arch[warp_gid].opcode),
                    state_per_arch[warp_gid].csr_addr);

                return false;
              }
            } // if(data_race_load_pc_set.find)
          } else {
            spdlog::warn("Warp GID {}: No trace entry found for PC {:#x}. This "
                         "execution will NOT be checked.",
                         warp_gid, state_per_arch[warp_gid].pc);
          }
        }

        if (state_per_arch[warp_gid].pc == 0x9fd47624) {
          for (unsigned tlid = 0; tlid < tmask_size; ++tlid) {
            spdlog::info("v16 {:#x}",
                         isa_emulator->reg_read(
                             warp_gid, tlid,
                             difftest::RegId(difftest::RegType::Vec,
                                             unsigned(riscv_vreg_t::V16))));

            /*spdlog::info(
                "{:#x} x8  {:#x}", state_per_arch[warp_gid].next_pc,
                isa_emulator->reg_read(
                    warp_gid, tlid,
                    difftest::RegId(difftest::RegType::Int, unsigned(8))));*/
          }
        }

        if (state_per_arch[warp_gid].pc == 0xcdb963a8) {
          spdlog::info("Warp GID {} {:#x}", warp_gid,
                       state_per_arch[warp_gid].pc);

          for (unsigned tlid = 0; tlid < tmask_size; ++tlid) {
            spdlog::info("a6  {:#x}",
                         isa_emulator->reg_read(
                             warp_gid, tlid,
                             difftest::RegId(difftest::RegType::Int,
                                             unsigned(riscv_reg_t::A6))));
            spdlog::info("s1  {:#x}",
                         isa_emulator->reg_read(
                             warp_gid, tlid,
                             difftest::RegId(difftest::RegType::Int,
                                             unsigned(riscv_reg_t::S1))));
          }
        }
      }
    }

    if (!isa_emulator->terminated() && schedulable_warps.empty()) {
      spdlog::error("No schedulable warps found, but the emulation is not yet "
                    "terminated. Possible deadlock?");
      for (difftest::WarpGID warp_gid = 0; warp_gid < max_warp_gid;
           ++warp_gid) {
        spdlog::error("  Warp GID {} last PC: {:#x}", warp_gid,
                      state_per_arch[warp_gid].pc);
      }
      return false;
    }
    if (!executed_warps) {
      break;
    }
  }
  return true;
}

static void sanity_check(const nlohmann::json &meta_json,
                         const difftest::Arch &arch) {
  if (meta_json["arch_name"] != arch.name) {
    spdlog::error("Meta json arch ({}) does not match the emulator arch ({})",
                  meta_json["arch_name"], arch.name);
    throw std::runtime_error("sanity check failed");
  }
  if (meta_json["word_bits"] != arch.word_bits) {
    spdlog::error(
        "Meta json word_bits ({}) does not match the emulator word_bits ({})",
        meta_json["word_bits"], arch.word_bits);
    throw std::runtime_error("sanity check failed");
  }
}

int main(int argc, char **argv) {
  argparse::ArgumentParser program("emulator");

  program.add_argument("--instr-bin")
      .help("input binary file to emulate")
      .default_value("");

  program.add_argument("--data-bin")
      .help("input data binary file to emulate")
      .default_value("");

  program.add_argument("--aux")
      .help("input aux binary file to emulate")
      .default_value("");

  program.add_argument("--elf")
      .help("input ELF file to emulate")
      .default_value("");

  program.add_argument("--elf-num-genblocks")
      .help("number of workgroups (genblocks) for ELF input")
      .default_value(1u)
      .scan<'u', unsigned>();
  program.add_argument("--elf-num-genwarps-per-block")
      .help("number of warps per workgroup (genblock) for ELF input")
      .default_value(1u)
      .scan<'u', unsigned>();
  program.add_argument("--elf-num-threads-per-warp")
      .help("number of threads per warp (wavefront) for ELF input")
      .default_value(1u)
      .scan<'u', unsigned>();

  program.add_argument("--meta-json")
      .help("input meta json file")
      .default_value("");

  program.add_argument("--emulate")
      .help("run the ISA emulator only")
      .default_value(false)
      .implicit_value(true);

  program.add_argument("--warps-limit")
      .default_value("")
      .help("limit the warps to be emulated to the given comma-separated list "
            "of warp GIDs");

  program.add_argument("--dbg-cmd")
      .help("debug command file")
      .default_value("");

  program.add_argument("--trace")
      .help("input emulation trace files")
      .default_value("");

  program.add_argument("--stat-jsonfile")
      .help("output statistics json file")
      .default_value("");

  program.add_argument("--log-level")
      .help("set the logging level (trace, debug, info, warn, err, critical, "
            "off)")
      .default_value(std::string("debug"));

  program.add_argument("--coverage-dat-file")
      .help("output coverage data file")
      .default_value(std::string(""));

  program.add_argument("--no-crash-handler")
      .default_value(false)
      .implicit_value(true)
      .help("disable the crash handler that saves coverage on crashes");

  program.add_argument("--disable-mem-checker")
      .default_value(false)
      .implicit_value(true)
      .help("disable the memory access checker in the ISA emulator");

  program.add_argument("--disasm-trace")
      .default_value(false)
      .implicit_value(true)
      .help("enable disassembly trace logging");

  try {
    program.parse_args(argc, argv);
  } catch (const std::exception &err) {
    std::cerr << err.what() << std::endl;
    std::cerr << program;
    return 1;
  }

  const auto log_level_str = program.get<std::string>("--log-level");
  if (log_level_str == "trace") {
    spdlog::set_level(spdlog::level::trace);
  } else if (log_level_str == "debug") {
    spdlog::set_level(spdlog::level::debug);
  } else if (log_level_str == "info") {
    spdlog::set_level(spdlog::level::info);
  } else if (log_level_str == "warn") {
    spdlog::set_level(spdlog::level::warn);
  } else if (log_level_str == "err") {
    spdlog::set_level(spdlog::level::err);
  } else if (log_level_str == "critical") {
    spdlog::set_level(spdlog::level::critical);
  } else if (log_level_str == "off") {
    spdlog::set_level(spdlog::level::off);
  } else {
    spdlog::error("Invalid log level: {}", log_level_str);
    return 1;
  }

  std::string instr_binfile, data_binfile, aux_binfile, elf_file, meta_jsonfile;

  elf_file = program.get<std::string>("--elf");
  if (elf_file.empty()) {
    instr_binfile = program.get<std::string>("--instr-bin");
    data_binfile = program.get<std::string>("--data-bin");
    aux_binfile = program.get<std::string>("--aux");
    meta_jsonfile = program.get<std::string>("--meta-json");
    if (instr_binfile.empty() || data_binfile.empty() || aux_binfile.empty() ||
        meta_jsonfile.empty()) {
      spdlog::error("--instr-bin, --data-bin, --aux, --meta-json are required "
                    "if --elf is "
                    "not specified");
      return 1;
    }
  }

  nlohmann::json meta_json;
  if (elf_file.empty()) {
    std::ifstream meta_ifs(meta_jsonfile);
    if (!meta_ifs) {
      spdlog::error("failed to open meta json file: {}", meta_jsonfile);
      return 1;
    }
    meta_ifs >> meta_json;
  } else {
    // default meta for ELF file
    meta_json["num_genblocks"] = program.get<unsigned>("--elf-num-genblocks");
    meta_json["num_genwarps_per_block"] =
        program.get<unsigned>("--elf-num-genwarps-per-block");
    meta_json["wavefront_size"] =
        program.get<unsigned>("--elf-num-threads-per-warp");
    meta_json["workgroup_size"] = {1, 1, meta_json["num_genblocks"]};
    meta_json["sgpr_usage"] = 64; // int + fp
    meta_json["vgpr_usage"] = 32;
  }

  auto check_magic = [&](uint32_t magic, uint32_t expected_magic,
                         const std::string &section_name) {
    if (magic != expected_magic) {
      spdlog::error("Aux file corrupted at section {}, invalid magic {:#x} "
                    "(expected {:#x})",
                    section_name, magic, expected_magic);
      std::terminate();
    }
  };

  // load the aux file
  std::vector<uint64_t> data_race_load_pc;
  std::ifstream aux_ifs;
  AuxHeader aux_header;
  bool aux_presented = true;
  if (!aux_binfile.empty()) {
    aux_ifs.open(aux_binfile, std::ios::binary);
    if (!aux_ifs) {
      spdlog::error("failed to open input aux binary file: {}", aux_binfile);
      std::terminate();
    }
    aux_ifs.exceptions(std::ifstream::badbit | std::ifstream::failbit);

    aux_ifs.read(reinterpret_cast<char *>(&aux_header), sizeof(aux_header));
    check_magic(aux_header.magic, AuxHeader::MAGIC, "AuxHeader");
    // read data race load PC list
    uint64_t pc;
    for (uint64_t i = 0; i < aux_header.nr_data_race_loads; ++i) {
      aux_ifs.read(reinterpret_cast<char *>(&pc), sizeof(pc));
      data_race_load_pc.push_back(pc);
    }
  } else {
    aux_presented = false;
  }

  std::unordered_map<uint64_t, bool> pcset;

  int ret = 0;
  difftest::ISAEmulator *isa_emulator = nullptr;

  auto save_coverage_stats = [&]() {
    //
    // Show coverage statistics
    //
    if (!isa_emulator)
      return;

    nlohmann::json coverage_json;
    if (elf_file.empty()) {
      size_t num_covered = 0;
      const auto &emu_pc_set = isa_emulator->pc_executed();
      for (const auto &[pc, _] : pcset) {
        if (emu_pc_set.find(pc) != emu_pc_set.end()) {
          ++num_covered;

        }
      }

      coverage_json["num_executed_instr"] = emu_pc_set.size();
      coverage_json["num_executed_instr_trace"] =
          isa_emulator->instr_executed();
      coverage_json["num_covered"] = num_covered;
      coverage_json["total"] = pcset.size();
      coverage_json["coverage_percent"] =
          static_cast<double>(num_covered) / pcset.size() * 100.0;
        
      

      spdlog::info("Emulation finished.");
      spdlog::info("Total instructions covered: {}", num_covered);
      spdlog::info("Instruction coverage: {:.2f}%",
                   (static_cast<double>(num_covered) / pcset.size() * 100.0));
    } else {
      const auto &emu_pc_set = isa_emulator->pc_executed();

      coverage_json["num_executed_instr"] = emu_pc_set.size(); // PCs
      coverage_json["num_executed_instr_trace"] =
          isa_emulator->instr_executed();

      spdlog::info("Total instructions covered: {}", emu_pc_set.size());
    }

    if (!program.get<bool>("--emulate")) { // for DUT mode
      // Add instruction execution frequency statistics
      nlohmann::json insn_stats_json;
      for (const auto &[insn_name, freq] : difftest::get_insn_stats()) {
        insn_stats_json[insn_name] = freq;
      }
      coverage_json["insn_stats"] = insn_stats_json;

      // Add memory checker statistics
      std::unordered_map<std::string, size_t> mem_checker_stats;
      difftest::dump_memory_checker_stats(mem_checker_stats);
      nlohmann::json mem_checker_json;
      for (const auto &[key, value] : mem_checker_stats) {
        mem_checker_json[key] = value;
      }
      coverage_json["mem_checker_stats"] = mem_checker_json;
    }

    // Add ISA emulator statistics
    std::unordered_map<std::string, size_t> isa_emulator_stats;
    isa_emulator->get_statistics(isa_emulator_stats);
    nlohmann::json isa_emulator_json;
    for (const auto &[key, value] : isa_emulator_stats) {
      isa_emulator_json[key] = value;
    }
    coverage_json["isa_emulator_stats"] = isa_emulator_json;

    const auto stat_jsonfile = program.get<std::string>("--stat-jsonfile");
    if (!stat_jsonfile.empty()) {
      std::ofstream coverage_ofs(stat_jsonfile);
      coverage_ofs << coverage_json << std::endl;
    }
  };

  if (!program.get<bool>("--no-crash-handler")) {
    g_save_coverage_callback = save_coverage_stats;
    std::set_terminate(terminate_handler);
    std::signal(SIGABRT, signal_handler);
    std::signal(SIGSEGV, signal_handler);
    std::signal(SIGINT, signal_handler);
  }

  if (program.get<bool>("--emulate")) {
    //
    // Run the emulator only
    //
    isa_emulator = new difftest::ISAEmulator();
    const auto &arch = isa_emulator->get_arch();
    spdlog::info("Emulator Arch: {}", arch.name);
    if (!meta_jsonfile.empty())
      sanity_check(meta_json, arch);

    init_isa_emulator(isa_emulator, arch, meta_json);

    if (!load_binary(
            instr_binfile, data_binfile, elf_file, arch,
            [&](auto &&...args) {
              isa_emulator->pmem_write(std::forward<decltype(args)>(args)...);
            },
            pcset)) {
      return 1;
    }

    unsigned tmask_size;
#if defined(ARCH_VORTEX)
    tmask_size = arch.nr_threads_per_warp;
#elif defined(ARCH_VENTUS)
    tmask_size = meta_json["wavefront_size"];
#endif
    const auto dbg_cmd = program.get<std::string>("--dbg-cmd");
    if (!dbg_cmd.empty()) {
      ret = run_debug_commands(dbg_cmd, *isa_emulator);

    } else {
      std::unordered_set<difftest::WarpGID> warps_limit;
      const auto warps_limit_str = program.get<std::string>("--warps-limit");
      if (!warps_limit_str.empty()) {
        std::stringstream ss(warps_limit_str);
        std::string item;
        while (std::getline(ss, item, ',')) {
          warps_limit.insert(static_cast<difftest::WarpGID>(std::stoul(item)));
        }
      }
      ret = !run_isa_emulator(isa_emulator, arch,
                              program.get<std::string>("--trace"), meta_json,
                              tmask_size, data_race_load_pc, warps_limit,
                              program.get<bool>("--disasm-trace"));
    }
  } else {
    //
    // Run both DUT and ISA emulator, and compare them
    //

    if (!aux_ifs) {
      spdlog::error("--aux is required when running both DUT and ISA emulator");
      return 1;
    }

    auto dut = std::make_shared<difftest::DUT>(
        program.get<std::string>("--coverage-dat-file"));

    const bool disable_mem_checker = program.get<bool>("--disable-mem-checker");

    std::promise<void> sig_inited;
    difftest::create_worker_thread([=, &aux_ifs, &isa_emulator, &sig_inited,
                                    &pcset]() {
      // It would run in the DPI thread of the verilator, NOT in the eval
      // thread and the main() threads not terminated yet
      isa_emulator = &difftest::get_isa_emulator();
      const auto &arch = isa_emulator->get_arch();
      spdlog::info("DUT Arch: {}", arch.name);
      if (!meta_jsonfile.empty())
        sanity_check(meta_json, arch);

      if (aux_presented) {
        // load store id entries from the aux file
        for (uint64_t i = 0; i < aux_header.nr_store_ids; ++i) {
          StoreIdSection sec;
          aux_ifs.read(reinterpret_cast<char *>(&sec), sizeof(sec));
          check_magic(sec.magic, StoreIdSection::MAGIC, "StoreIdSection");
          for (uint64_t j = 0; j < sec.nr_entries; ++j) {
            StoreIdEntry entry;
            aux_ifs.read(reinterpret_cast<char *>(&entry), sizeof(entry));
            check_magic(entry.magic, StoreIdEntry::MAGIC, "StoreIdEntry");
            difftest::add_store_id_entry(sec.pa, entry.byte_val,
                                         entry.store_id);
          }
        }
        // load the pending store id list
        for (uint64_t i = 0; i < aux_header.nr_pending_store_ids; ++i) {
          uint64_t pa;
          aux_ifs.read(reinterpret_cast<char *>(&pa), sizeof(pa));
          difftest::add_pending_store_id(pa);
        }
        // load po and po_loc
        difftest::get_po().deserialize(aux_ifs);
        PoLocHeader po_loc_header;
        aux_ifs.read(reinterpret_cast<char *>(&po_loc_header),
                     sizeof(po_loc_header));
        check_magic(po_loc_header.magic, PoLocHeader::MAGIC, "PoLocHeader");
        std::vector<uint64_t> po_loc_addrs;
        for (unsigned i = 0; i < po_loc_header.num_po_loc_rela; ++i) {
          uint64_t a64;
          aux_ifs.read(reinterpret_cast<char *>(&a64), sizeof(a64));
          difftest::get_po_loc(a64).deserialize(aux_ifs);
        }
        // load load/store opcodes
        LoadStoreOpcodeHeader ls_opcode_header;
        aux_ifs.read(reinterpret_cast<char *>(&ls_opcode_header),
                     sizeof(ls_opcode_header));
        check_magic(ls_opcode_header.magic, LoadStoreOpcodeHeader::MAGIC,
                    "LoadStoreOpcodeHeader");
        for (uint64_t i = 0; i < ls_opcode_header.num_opcodes; ++i) {
          difftest::LoadStoreId id;
          aux_ifs.read(reinterpret_cast<char *>(&id), sizeof(id));
          difftest::LoadStoreOpcode opcode;
          aux_ifs.read(reinterpret_cast<char *>(&opcode), sizeof(opcode));
          difftest::add_load_store_opcode(id, opcode);
        }
        // load store trace
        difftest::get_store_trace().deserialize(aux_ifs);
        // load data. addr deps
        DepHeader dep_header;
        aux_ifs.read(reinterpret_cast<char *>(&dep_header), sizeof(dep_header));
        check_magic(dep_header.magic, DepHeader::MAGIC, "DepHeader");
        for (uint64_t i = 0; i < dep_header.num_warps; ++i) {
          DepPerWarpHeader warp_header;
          aux_ifs.read(reinterpret_cast<char *>(&warp_header),
                       sizeof(warp_header));
          check_magic(warp_header.magic, DepPerWarpHeader::MAGIC,
                      "DepPerWarpHeader");
          for (uint64_t j = 0; j < warp_header.num_data_deps; ++j) {
            uint64_t f64;
            aux_ifs.read(reinterpret_cast<char *>(&f64), sizeof(f64));
            uint64_t num_tos;
            aux_ifs.read(reinterpret_cast<char *>(&num_tos), sizeof(num_tos));
            for (uint64_t k = 0; k < num_tos; ++k) {
              uint64_t t64;
              aux_ifs.read(reinterpret_cast<char *>(&t64), sizeof(t64));
              difftest::add_data_dep(warp_header.warp_gid, f64, t64);
            }
          }

          for (uint64_t j = 0; j < warp_header.num_addr_deps; ++j) {
            uint64_t f64;
            aux_ifs.read(reinterpret_cast<char *>(&f64), sizeof(f64));
            uint64_t num_tos;
            aux_ifs.read(reinterpret_cast<char *>(&num_tos), sizeof(num_tos));
            for (uint64_t k = 0; k < num_tos; ++k) {
              uint64_t t64;
              aux_ifs.read(reinterpret_cast<char *>(&t64), sizeof(t64));
              difftest::add_addr_dep(warp_header.warp_gid, f64, t64);
            }
          }
        }

        if (disable_mem_checker) {
          spdlog::error(
              "Memory access checker disabled by command line option");
        } else {
          difftest::init_memory_checker();
        }
      }

      init_isa_emulator(isa_emulator, arch, meta_json);

      if (!load_binary(
              instr_binfile, data_binfile, elf_file, arch,
              [&](auto &&...args) {
                isa_emulator->pmem_write(std::forward<decltype(args)>(args)...);
                dut->pmem_write(std::forward<decltype(args)>(args)...);
              },
              pcset)) {
        std::terminate();
      }

      for (const auto &pc : data_race_load_pc) {
        difftest::add_data_race_load_pc(pc);
      }

#if defined(ARCH_VORTEX)
      dut->new_blocks(meta_json["num_genblocks"],
                      meta_json["num_genwarps_per_block"]);
#elif defined(ARCH_VENTUS)
      const unsigned kernel_size[3] = {
          meta_json["workgroup_size"][0],
          meta_json["workgroup_size"][1],
          meta_json["workgroup_size"][2],
      };
      dut->new_blocks(kernel_size, meta_json["num_genwarps_per_block"],
                      meta_json["wavefront_size"], meta_json["sgpr_usage"],
                      meta_json["vgpr_usage"]);
#endif

      sig_inited.set_value();
    });

    sig_inited.get_future().wait();

    dut->run();

    ret = difftest::dut_terminated() ? 0 : 1;
    // happens-before
    // so we can read the latest values of pcset, isa_emulator,... later
  }

  difftest::wait_worker_thread();

  save_coverage_stats();

  if (program.get<bool>("--emulate")) {
    delete isa_emulator;
  }
  return ret;
}
