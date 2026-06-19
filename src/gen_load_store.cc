#include <random>

#include "isa/riscv/riscv_instr.hh"
#include "taint_prop.hh"

#include "generator.hh"

namespace fuzzgpu {

/// @brief Hash helpers for computed load/store addresses.
class AddressHasher {
public:
  /// @brief Commutative hash of active-lane address values.
  static uint64_t hash_addresses(const VGPRValues &addrs) {
    uint64_t sum = 0;
    for (size_t i = 0; i < addrs.tmask.size(); ++i) {
      if (addrs.tmask[i] && i < addrs.values.size()) {
        sum += addrs.values[i];
      }
    }
    return sum;
  }

  /// @brief Hash a single scalar address.
  static uint64_t hash_single(uint64_t addr) { return addr; }
};

// Helper: Get the first active thread in the warp
static std::optional<difftest::ThreadLID>
get_first_active_thread(const std::shared_ptr<GenWarp> &gen_warp,
                        const riscv_instr &instr) {
  foreach_thread_lid(gen_warp, tid) {
    if (instr.meta()->applied_tmask()) {
      if (!gen_warp->tmask().test(tid) || tid < gen_warp->tstart()) {
        continue;
      }
    }
    return tid;
  }
  return std::nullopt;
}

// Helper: Compute per-thread addresses for VGPR_IMM (vgpr[tid] + imm)
static VGPRValues compute_vgpr_imm_addresses(
    const std::shared_ptr<GenWarp> &gen_warp, riscv_vreg_t vreg, int64_t imm,
    difftest::WarpGID warp_gid, difftest::ISAEmulator &isa_emu) {
  VGPRValues result;
  const auto &tmask = gen_warp->tmask();
  result.values.resize(tmask.size(), 0);
  result.tmask.resize(tmask.size(), false);
  result.warp_id = warp_gid;

  foreach_thread_lid(gen_warp, thread_lid) {
    if (tmask.test(thread_lid)) {
      result.tmask[thread_lid] = true;
      uint64_t vgpr_val = isa_emu.reg_read(
          warp_gid, thread_lid,
          difftest::RegId(difftest::RegType::Vec, uint32_t(vreg)));
      result.values[thread_lid] = vgpr_val + static_cast<uint64_t>(imm);
    }
  }
  return result;
}

// Helper: Compute per-thread addresses for VGPR_VGPR (vgpr1[tid] + vgpr2[tid])
static VGPRValues
compute_vgpr_vgpr_addresses(const std::shared_ptr<GenWarp> &gen_warp,
                            riscv_vreg_t vreg1, riscv_vreg_t vreg2,
                            difftest::WarpGID warp_gid,
                            difftest::ISAEmulator &isa_emu) {
  VGPRValues result;
  const auto &tmask = gen_warp->tmask();
  result.values.resize(tmask.size(), 0);
  result.tmask.resize(tmask.size(), false);
  result.warp_id = warp_gid;

  foreach_thread_lid(gen_warp, thread_lid) {
    if (tmask.test(thread_lid)) {
      result.tmask[thread_lid] = true;
      uint64_t vgpr1_val = isa_emu.reg_read(
          warp_gid, thread_lid,
          difftest::RegId(difftest::RegType::Vec, uint32_t(vreg1)));
      uint64_t vgpr2_val = isa_emu.reg_read(
          warp_gid, thread_lid,
          difftest::RegId(difftest::RegType::Vec, uint32_t(vreg2)));
      result.values[thread_lid] = vgpr1_val + vgpr2_val;
    }
  }
  return result;
}

// Helper: Compute single address for SGPR_IMM (sgpr + imm)
static VGPRValues compute_sgpr_imm_address(uint64_t sgpr_val, int64_t imm,
                                           difftest::WarpGID warp_gid) {
  VGPRValues result;
  result.values.resize(1);
  result.tmask.resize(1, true);
  result.warp_id = warp_gid;
  result.values[0] = sgpr_val + static_cast<uint64_t>(imm);
  return result;
}

// Helper: Check if two address ranges overlap
// [addr1, addr1+size1) and [addr2, addr2+size2)
static bool ranges_overlap(uint64_t addr1, unsigned size1, uint64_t addr2,
                           unsigned size2) {
  uint64_t end1 = addr1 + size1;
  uint64_t end2 = addr2 + size2;
  return addr1 < end2 && addr2 < end1;
}

// Helper: Check if an instruction is a Ventus GPU indexed load/store
// These have unique address semantics: baseBias = PDS + (baseAddr & ~3) * NUMW
// * NUMT + (baseAddr & 3)
static bool is_ventus_gpu_indexed_ldst(riscv_instr_name_t name) {
  switch (name) {
  case riscv_instr_name_t::VLB_V:
  case riscv_instr_name_t::VLBU_V:
  case riscv_instr_name_t::VLH_V:
  case riscv_instr_name_t::VLHU_V:
  case riscv_instr_name_t::VLW_V:
  case riscv_instr_name_t::VSB_V:
  case riscv_instr_name_t::VSH_V:
  case riscv_instr_name_t::VSW_V:
    return true;
  default:
    return false;
  }
}

// Helper: Check if nf/mew/mop fields match between current instruction and
// history
static bool ldst_extra_fields_match(const riscv_instr &instr,
                                    const LoadStoreOperandHistory &hist_entry) {
  const auto &meta = instr.meta();
  const uint64_t curr_mask = meta->operand_mask;
  const uint64_t hist_mask = hist_entry.operand_mask;

  // Check Ventus GPU indexed load/store: must match (both or neither)
  bool curr_is_gpu_indexed = is_ventus_gpu_indexed_ldst(meta->instr_name);
  if (curr_is_gpu_indexed != hist_entry.is_ventus_gpu_indexed) {
    return false;
  }

  // Check nf: both must have it or neither, and values must match
  if ((curr_mask & riscv_meta::HAS_NF) != (hist_mask & riscv_meta::HAS_NF)) {
    return false;
  }
  if ((curr_mask & riscv_meta::HAS_NF) && instr.nf() != hist_entry.nf) {
    return false;
  }

  // Check mew: both must have it or neither, and values must match
  if ((curr_mask & riscv_meta::HAS_MEW) != (hist_mask & riscv_meta::HAS_MEW)) {
    return false;
  }
  if ((curr_mask & riscv_meta::HAS_MEW) && instr.mew() != hist_entry.mew) {
    return false;
  }

  // Check mop: both must have it or neither, and values must match
  if ((curr_mask & riscv_meta::HAS_MOP) != (hist_mask & riscv_meta::HAS_MOP)) {
    return false;
  }
  if ((curr_mask & riscv_meta::HAS_MOP) && instr.mop() != hist_entry.mop) {
    return false;
  }

  return true;
}

// Helper: Read per-thread vGPR values
static VGPRValues read_vgpr_values(const std::shared_ptr<GenWarp> &gen_warp,
                                   riscv_vreg_t vreg,
                                   difftest::WarpGID warp_gid,
                                   difftest::ISAEmulator &isa_emu) {
  VGPRValues result;
  const auto &tmask = gen_warp->tmask();
  result.values.resize(tmask.size(), 0);
  result.tmask.resize(tmask.size(), false);
  result.warp_id = warp_gid;

  foreach_thread_lid(gen_warp, thread_lid) {
    if (tmask.test(thread_lid)) {
      result.tmask[thread_lid] = true;
      result.values[thread_lid] = isa_emu.reg_read(
          warp_gid, thread_lid,
          difftest::RegId(difftest::RegType::Vec, uint32_t(vreg)));
    }
  }
  return result;
}

// Helper: Read per-thread sGPR values
static VGPRValues read_sgpr_values(const std::shared_ptr<GenWarp> &gen_warp,
                                   const riscv_instr &instr, riscv_reg_t reg,
                                   difftest::WarpGID warp_gid,
                                   difftest::ISAEmulator &isa_emu) {
  VGPRValues result;
  const auto &tmask = gen_warp->tmask();
  result.values.resize(tmask.size(), 0);
  result.tmask.resize(tmask.size(), false);
  result.warp_id = warp_gid;

  bool scalar_thread = false;
  foreach_thread_lid(gen_warp, thread_lid) {
    if (instr.meta()->applied_tmask()) {
      if (!tmask.test(thread_lid) || thread_lid < gen_warp->tstart()) {
        continue;
      }
    } else {
      scalar_thread = true;
    }

    result.tmask[thread_lid] = true;
    result.values[thread_lid] = isa_emu.reg_read(
        warp_gid, thread_lid,
        difftest::RegId(difftest::RegType::Int, uint32_t(reg)));

    if (scalar_thread)
      break;
  }
  return result;
}

// Update load/store operand history for replay
void Generator::update_ldst_operand_history_(
    const std::shared_ptr<GenWarp> &gen_warp, const riscv_instr &instr) {
  if (!Config::instance().emu_guide) {
    return;
  }
  const auto warp_gid = gen_warp->warp_gid();
  auto &history = state_.ldst_operand_history;
  const auto &meta = instr.meta();

  if (meta->is_load())
    return;

  // Determine the pair type and compute addresses
  LoadStoreOperandPairType pair_type;
  uint32_t op1_id, op2_id;
  int64_t imm_val;
  VGPRValues computed_addresses;
  VGPRValues op1_values, op2_values;

  if (meta->is_vector() && (meta->operand_mask & riscv_meta::HAS_VS1)) {
    // Vector instruction: vGPR + (vGPR or imm)
    op1_id = uint32_t(instr.vs1());

    // Read op1 (vs1) values for all active threads
    op1_values =
        read_vgpr_values(gen_warp, instr.vs1(), warp_gid, state_.isa_emu);

    if (meta->operand_mask & riscv_meta::HAS_IMM) {
      // VGPR_IMM: address = vgpr[tid] + imm
      pair_type = LoadStoreOperandPairType::VGPR_IMM;
      op2_id = 0;
      imm_val = instr.imm();
      computed_addresses = compute_vgpr_imm_addresses(
          gen_warp, instr.vs1(), imm_val, warp_gid, state_.isa_emu);
      // op2_values stays empty for VGPR_IMM
    } else if (meta->operand_mask & riscv_meta::HAS_VS2) {
      // VGPR_VGPR: address = vgpr1[tid] + vgpr2[tid]
      pair_type = LoadStoreOperandPairType::VGPR_VGPR;
      op2_id = uint32_t(instr.vs2());
      imm_val = 0;
      computed_addresses = compute_vgpr_vgpr_addresses(
          gen_warp, instr.vs1(), instr.vs2(), warp_gid, state_.isa_emu);
      // Read op2 (vs2) values for all active threads
      op2_values =
          read_vgpr_values(gen_warp, instr.vs2(), warp_gid, state_.isa_emu);
    } else {
      return; // Invalid format
    }
  } else if (meta->operand_mask & riscv_meta::HAS_RS1) {
    // Scalar instruction: sGPR + imm
    if (meta->operand_mask & riscv_meta::HAS_IMM) {
      pair_type = LoadStoreOperandPairType::SGPR_IMM;
      op1_id = uint32_t(instr.rs1());
      op2_id = 0;
      imm_val = instr.imm();

      // Get the first active thread's rs1 value
      auto first_active_tid = get_first_active_thread(gen_warp, instr);
      if (first_active_tid) {
        uint64_t rs1_val = state_.isa_emu.reg_read(
            warp_gid, first_active_tid.value(),
            difftest::RegId(difftest::RegType::Int, uint32_t(instr.rs1())));
        computed_addresses =
            compute_sgpr_imm_address(rs1_val, imm_val, warp_gid);

        // For SGPR_IMM, store rs1_val in op1_values (same for all threads)
        const auto &tmask = gen_warp->tmask();
        op1_values.values.resize(tmask.size(), rs1_val);
        op1_values.tmask.resize(tmask.size(), false);
        op1_values.warp_id = warp_gid;
        foreach_thread_lid(gen_warp, thread_lid) {
          if (tmask.test(thread_lid)) {
            op1_values.tmask[thread_lid] = true;
          }
        }
      } else {
        return; // No active threads
      }
    } else {
      spdlog::trace("Load/store Invalid format");
      return; // Invalid format
    }
  } else {
    spdlog::trace("Load/store No address operands");
    return; // No address operands
  }

  // Compute hash from addresses (for potential future use)
  uint64_t addr_hash = AddressHasher::hash_addresses(computed_addresses);

  // Compute op1_hash for operand-based matching
  uint64_t op1_hash = AddressHasher::hash_addresses(op1_values);

  // Check if this is a Ventus GPU indexed load/store
  bool is_gpu_indexed = is_ventus_gpu_indexed_ldst(meta->instr_name);

  // Add entry to history
  LoadStoreOperandHistory entry{
      .pair_type = pair_type,
      .addr_hash = addr_hash,
      .op1_id = op1_id,
      .op2_id = op2_id,
      .imm_val = imm_val,
      .op1_values = op1_values,
      .op2_values = op2_values,
      .op1_hash = op1_hash,
      .computed_addresses = computed_addresses,
      .mem_access_size = meta->mem_access_size(),
      .operand_mask = meta->operand_mask,
      .nf = (meta->operand_mask & riscv_meta::HAS_NF) ? instr.nf() : 0,
      .mew = (meta->operand_mask & riscv_meta::HAS_MEW) ? instr.mew() : false,
      .mop = (meta->operand_mask & riscv_meta::HAS_MOP) ? instr.mop() : 0,
      .is_ventus_gpu_indexed = is_gpu_indexed,
  };

  history.add_history(entry);

  spdlog::trace("Updated load/store operand history for warp {} (size: {}): "
                "pair_type {} op1_id {}, op2_id {}, addr_hash {:#x}, op1_hash "
                "{:#x}",
                warp_gid, history.size(), magic_enum::enum_name(pair_type),
                op1_id, op2_id, addr_hash, op1_hash);
}

static bool ldst_by_multiple_threads(
    const std::unordered_map<uint64_t, LoadStoreInfo> &load_store_set,
    uint64_t pa, const difftest::GlobalThreadId &thread_gid) {
  if (load_store_set.find(pa) == load_store_set.end()) {
    return false;
  }
  return load_store_set.at(pa).thread_gid != thread_gid ||
         load_store_set.at(pa).multiple_threads;
}

static void
merge_ldst_set(std::unordered_map<uint64_t, LoadStoreInfo> &dst,
               const std::unordered_map<uint64_t, LoadStoreInfo> &src) {
  for (const auto &p : src) {
    if (dst.find(p.first) == dst.end()) {
      dst[p.first] = p.second;
    } else {
      dst.at(p.first).multiple_threads =
          dst.at(p.first).multiple_threads || p.second.multiple_threads ||
          (dst.at(p.first).thread_gid != p.second.thread_gid);
      dst.at(p.first).initial_written &= p.second.initial_written;
      // no need to update pc and scope, as they are ANY one
    }
  }
}

static bool is_rfe(const std::unordered_map<uint64_t, LoadStoreInfo> &store_set,
                   uint64_t pa, const difftest::GlobalThreadId &thread_gid) {
  // exempt initial_write cross-thread case only if no other threads have
  // accessed
  spdlog::debug(
      "is_rfe check: pa {:#x}, store_set.find {}, initial_written {}, "
      "multiple_threads {}, thread_gid {}",
      pa, store_set.find(pa) != store_set.end(),
      store_set.find(pa) != store_set.end() ? store_set.at(pa).initial_written
                                            : false,
      store_set.find(pa) != store_set.end() ? store_set.at(pa).multiple_threads
                                            : false,
      thread_gid);

  if (store_set.find(pa) != store_set.end() &&
      store_set.at(pa).initial_written && !store_set.at(pa).multiple_threads) {
    spdlog::debug("is_rfe: false");
    return false;
  }
  return ldst_by_multiple_threads(store_set, pa, thread_gid);
}

bool Generator::illegal_vaddr_(uint64_t va, unsigned access_size) const {
  return va % access_size;
}

bool Generator::illegal_paddr_(uint64_t pa, unsigned access_size,
                               bool is_store) const {
  // check alignment (for RISCV, unaligned access is illegal)
  if (pa % access_size != 0) {
    return true;
  }

  // currently we not allow to read the code region since they may have been
  // allocated by still not yet filled with instructions, while in the later
  // running the instructions are filled in, which alters the load results.
  // We also reject reading MMIO or writing to the read-only section
  // reject writing to the read-only (e.g., code) section
  for (unsigned ibyte = 0; ibyte < access_size; ++ibyte) {
    const auto addr = pa + ibyte;

    // whether the addr can be allocated
    bool in_writebale =
        state_.mem_alloc.min_paddr(MemBlkType::WRITABLE) <= addr &&
        addr <= state_.mem_alloc.max_paddr(MemBlkType::WRITABLE);
    bool in_code = state_.mem_alloc.min_paddr(MemBlkType::CODE) <= addr &&
                   addr <= state_.mem_alloc.max_paddr(MemBlkType::CODE);
    if (is_store) {
      if (!in_writebale)
        return true;
    } else { // is_load
      if (!in_code && !in_writebale)
        return true;
    }
    // check the device memory region
    if (state_.isa_emu.illegal_paddr(addr, access_size, is_store)) {
      return true;
    }

    // not load or store from/to code region
    auto blk = state_.mem_alloc.query(addr);
    if (blk.has_value() && blk->type == MemBlkType::CODE)
      return true;

    if (mmio_addrs_.find(addr) != mmio_addrs_.end())
      return true;
  }
  return false;
}

std::vector<difftest::MemTrace>
Generator::emulate_load_store_(const std::shared_ptr<GenWarp> &gen_warp,
                               riscv_instr &instr, bool &trap_taken) {
#if defined(ARCH_VORTEX)
  // TODO: uniformly implement it via isa emulator
  // currently it relies on the meta info of the riscv_instr to extract the
  // effective address
  std::vector<difftest::MemTrace> mem_trace;
  foreach_thread_lid(gen_warp, thread_lid) {
    if (!gen_warp->tmask().test(thread_lid)) {
      continue;
    }
    // get the effective address
    uint64_t ea, wdata;
    if (instr.meta()->format == riscv_instr_format_t::I_FORMAT ||
        instr.meta()->format == riscv_instr_format_t::S_FORMAT) {
      ea = state_.isa_emu.reg_read(
               gen_warp->warp_gid(), thread_lid,
               difftest::RegId(difftest::RegType::Int, uint32_t(instr.rs1()))) +
           instr.imm();
      // get store data if any
      if (instr.meta()->is_store()) {
        if (instr.meta()->operand_mask & riscv_meta::HAS_RS2) {
          wdata = state_.isa_emu.reg_read(
              gen_warp->warp_gid(), thread_lid,
              difftest::RegId(difftest::RegType::Int, uint32_t(instr.rs2())));
        } else {
          assert(instr.meta()->operand_mask & riscv_meta::HAS_FS2);
          wdata = state_.isa_emu.reg_read(
              gen_warp->warp_gid(), thread_lid,
              difftest::RegId(difftest::RegType::Float, uint32_t(instr.fs2())));
        }
      }
    } else {
      assert(instr.meta()->format == riscv_instr_format_t::R_FORMAT ||
             instr.meta()->format == riscv_instr_format_t::R_ORDERING_FORMAT);
      ea = state_.isa_emu.reg_read(
          gen_warp->warp_gid(), thread_lid,
          difftest::RegId(difftest::RegType::Int, uint32_t(instr.rs1())));
      wdata = state_.isa_emu.reg_read(
          gen_warp->warp_gid(), thread_lid,
          difftest::RegId(difftest::RegType::Int, uint32_t(instr.rs2())));
    }

    mem_trace.emplace_back(difftest::MemTrace{
        .addr = ea,
        .size = instr.meta()->mem_access_size(),
        .wdat = wdata,
        .is_store = instr.meta()->is_store(),
        .scope = state_.isa_emu.get_mem_scope(gen_warp->warp_gid(), ea),
        .thread_lid = thread_lid,
    });
  }
  trap_taken = false;
  return mem_trace;

#elif defined(ARCH_VENTUS)
  std::vector<uint8_t> instr_buf;
  auto prefix = emit_regext_prefix_(instr);
  if (prefix)
    prefix->emit(instr_buf);
  instr.emit(instr_buf);
  return state_.isa_emu.emulate_load_store(
      gen_warp->warp_gid(), instr_buf.data(), instr_buf.size(), trap_taken);
#endif
}

bool Generator::check_store_id_conflict_(
    const std::unordered_map<uint64_t,
                             std::unordered_map<uint8_t, difftest::LoadStoreId>>
        &store_id_map,
    uint64_t pa, uint8_t byte_val) const {
  if (store_id_map.find(pa) != store_id_map.end()) {
    const auto &byte_store_info = store_id_map.at(pa);
    if (byte_store_info.find(byte_val) != byte_store_info.end()) {
      // cannot resolve the rf order via id
      return true;
    }
  }
  return false;
}

void Generator::fix_misaligned_imm_(const std::shared_ptr<GenWarp> &gen_warp,
                                    riscv_instr &instr) {
  if ((instr.meta()->format == riscv_instr_format_t::I_FORMAT ||
       instr.meta()->format == riscv_instr_format_t::S_FORMAT) &&
      !instr.meta()->applied_tmask()) {
    // find the first active thread
    int thread_lid = -1;
    foreach_thread_lid(gen_warp, tid) {
      if (gen_warp->tmask().test(tid)) {
        thread_lid = tid;
        break;
      }
    }
    if (thread_lid == -1) {
      return;
    }

    uint64_t ea =
        state_.isa_emu.reg_read(
            gen_warp->warp_gid(), thread_lid,
            difftest::RegId(difftest::RegType::Int, uint32_t(instr.rs1()))) +
        instr.imm();

    const auto access_size = instr.meta()->mem_access_size();
    if (ea % access_size != 0) {
      instr.meta()->imm_align_bits = 0;
      // +/- unaligned bytes
      int64_t adjust_imm = instr.imm() - static_cast<int64_t>(ea % access_size);
      if (!instr.meta()->legal_imm(adjust_imm)) {
        adjust_imm = instr.imm() + static_cast<int64_t>(ea % access_size);
      }
      if (instr.meta()->legal_imm(adjust_imm)) {
        instr.set_imm(adjust_imm);
      }
    }
  }
}

/**
 * @brief Replay load/store operands based on operand-value matching
 * @return ReplayAttemptResult indicating the outcome of the replay attempt
 */
Generator::ReplayAttemptResult
Generator::replay_load_store_operands_(const std::shared_ptr<GenWarp> &gen_warp,
                                       riscv_instr &instr) {
  // Check if we should attempt replay
  if (!Config::instance().emu_guide) {
    return ReplayAttemptResult::NotAttempted; // Emu guide required
  }
  if (state_.uniform_dist(state_.rng) >=
      Config::instance().replay_load_store_operands_prob) {
    return ReplayAttemptResult::NotAttempted; // Don't replay
  }

  // Count replay attempt (we passed probability gate)
  state_.stat.replay_attempts++;

  const auto warp_gid = gen_warp->warp_gid();

  // Check if shared history exists without creating a new entry
  if (state_.ldst_operand_history.empty()) {
    return ReplayAttemptResult::AttemptedNoMatch; // No history yet
  }

  const auto &meta = instr.meta();

  // Determine the pair type we're looking for
  LoadStoreOperandPairType target_pair_type;
  bool is_vector_addr =
      meta->is_vector() && (meta->operand_mask & riscv_meta::HAS_VS1);

  if (is_vector_addr) {
    if (meta->operand_mask & riscv_meta::HAS_IMM) {
      target_pair_type = LoadStoreOperandPairType::VGPR_IMM;
    } else if (meta->operand_mask & riscv_meta::HAS_VS2) {
      target_pair_type = LoadStoreOperandPairType::VGPR_VGPR;
    } else {
      return ReplayAttemptResult::AttemptedNoMatch;
    }
  } else if (meta->operand_mask & riscv_meta::HAS_RS1) {
    if (meta->operand_mask & riscv_meta::HAS_IMM) {
      target_pair_type = LoadStoreOperandPairType::SGPR_IMM;
    } else {
      return ReplayAttemptResult::AttemptedNoMatch;
    }
  } else {
    return ReplayAttemptResult::AttemptedNoMatch;
  }

  // Get the number of available registers based on instruction type
  const unsigned num_regs =
      is_vector_addr ? gen_warp->num_vec_regs() : gen_warp->num_int_regs();
  unsigned sample_count =
      std::min(num_regs, Config::instance().k_max_replay_sample_regs);

  static std::vector<unsigned> xgpr_idx(gen_warp->num_int_regs()),
      vgpr_idx(gen_warp->num_vec_regs()), vgpr2_idx(gen_warp->num_vec_regs());
  static bool init_gpr_idx = false;
  if (!init_gpr_idx) {
    init_gpr_idx = true;
    std::iota(xgpr_idx.begin(), xgpr_idx.end(), 0);
    std::iota(vgpr_idx.begin(), vgpr_idx.end(), 0);
    std::iota(vgpr2_idx.begin(), vgpr2_idx.end(), 0);
  }

  // Shuffle the candidate registers for sampling
  if (is_vector_addr) {
    topk_shuffle(vgpr_idx, sample_count, state_.rng);
    topk_shuffle(vgpr2_idx, sample_count, state_.rng);
  } else {
    topk_shuffle(xgpr_idx, sample_count, state_.rng);
  }

  // Helper lambda to check if a register is pickable
  auto is_reg_pickable = [&](bool is_vgpr, unsigned reg_idx) -> bool {
    if (is_vgpr) {
      return gen_warp->is_pickable_vec_reg(riscv_vreg_t(reg_idx), /*cti*/ true);
    } else {
      return gen_warp->is_pickable_int_reg(riscv_reg_t(reg_idx), /*cti*/ true);
    }
  };

  // Helper lambda to read register values
  auto read_reg_values = [&](bool is_vgpr, unsigned reg_idx) -> VGPRValues {
    if (is_vgpr) {
      return read_vgpr_values(gen_warp, riscv_vreg_t(reg_idx), warp_gid,
                              state_.isa_emu);
    } else {
      return read_sgpr_values(gen_warp, instr, riscv_reg_t(reg_idx), warp_gid,
                              state_.isa_emu);
    }
  };

  // VGPR_IMM / SGPR_IMM: For each sampled register, try to derive imm that hits
  // history addresses These two cases share the same algorithm, differing only
  // in register type
  if (target_pair_type == LoadStoreOperandPairType::VGPR_IMM ||
      target_pair_type == LoadStoreOperandPairType::SGPR_IMM) {
    const bool is_vgpr =
        (target_pair_type == LoadStoreOperandPairType::VGPR_IMM);

    // Sample history entries instead of iterating all (O(k) vs O(n))
    // Exclude entries from the current warp to ensure cross-warp replay
    const auto sampled_history = state_.ldst_operand_history.sample_by_type(
        target_pair_type, Config::instance().k_max_replay_sample_history,
        state_.rng, warp_gid);

    if (sampled_history.empty()) {
      return ReplayAttemptResult::AttemptedNoMatch;
    }

    for (unsigned i = 0; i < sample_count && sample_count < num_regs; ++i) {
      unsigned reg1_idx = is_vector_addr ? vgpr_idx[i] : xgpr_idx[i];

      if (!is_reg_pickable(is_vgpr, reg1_idx)) {
        ++sample_count;
        continue;
      }

      // Read register values (vGPR or sGPR per-thread)
      VGPRValues op1_vals = read_reg_values(is_vgpr, reg1_idx);

      // Try sampled history entries to see if we can derive a valid imm
      for (const auto *hist_entry_ptr : sampled_history) {
        const auto &hist_entry = *hist_entry_ptr;

        if (!ldst_extra_fields_match(instr, hist_entry))
          continue;

        unsigned target_access_size = meta->mem_access_size();

        // Try each active thread to find a valid imm candidate
        for (size_t src_tid = 0; src_tid < op1_vals.tmask.size(); ++src_tid) {
          if (!op1_vals.tmask[src_tid])
            continue;
          if (src_tid >= hist_entry.computed_addresses.tmask.size() ||
              !hist_entry.computed_addresses.tmask[src_tid])
            continue;

          // Derive imm from this thread: imm = hist_addr[tid] - op1[tid]
          uint64_t new_addr = hist_entry.computed_addresses.values[src_tid];
          int64_t derived_imm =
              static_cast<int64_t>(new_addr - op1_vals.values[src_tid]);

          // Compute alignment adjustment
          int64_t alignment_delta =
              -static_cast<int64_t>(new_addr % target_access_size);
          int64_t adjusted_imm = derived_imm + alignment_delta;

          // Check if adjusted imm is still within n-bit signed range
          if (!instr.meta()->legal_imm(adjusted_imm))
            continue;

          // Compute resulting addresses for all threads with this imm
          VGPRValues result_addresses;
          result_addresses.values.resize(op1_vals.tmask.size(), 0);
          result_addresses.tmask = op1_vals.tmask;
          result_addresses.warp_id = warp_gid;

          for (size_t tid = 0; tid < op1_vals.tmask.size(); ++tid) {
            if (op1_vals.tmask[tid]) {
              result_addresses.values[tid] =
                  op1_vals.values[tid] + static_cast<uint64_t>(adjusted_imm);
            }
          }

          // Check if at least one thread has overlap
          const auto same_warp =
              (hist_entry.computed_addresses.warp_id == warp_gid);
          bool has_overlap = false;
          for (size_t tid = 0; tid < op1_vals.tmask.size(); ++tid) {
            if (!op1_vals.tmask[tid])
              continue;
            if (tid >= hist_entry.computed_addresses.tmask.size() ||
                !hist_entry.computed_addresses.tmask[tid])
              continue;

            uint64_t result_addr = result_addresses.values[tid];
            uint64_t hist_addr = hist_entry.computed_addresses.values[tid];

            if (ranges_overlap(result_addr, target_access_size, hist_addr,
                               hist_entry.mem_access_size) &&
                // cross thread or warp check: must be different warp, or
                // different thread with different register value
                (!same_warp)) {
              spdlog::trace("Sel target addr {:#x} ~ {:#x}", hist_addr,
                            hist_addr + hist_entry.mem_access_size - 1);
              has_overlap = true;
              break;
            }
          }

          if (!has_overlap)
            continue;

          // check alignment for all active threads
          bool misaligned = false;
          for (size_t tid = 0; tid < result_addresses.tmask.size(); ++tid) {
            if (tid >= result_addresses.tmask.size() ||
                !result_addresses.tmask[tid])
              continue;
            if (result_addresses.values[tid] % target_access_size != 0) {
              misaligned = true;
              break;
            }
          }
          if (misaligned)
            continue;

          // Success! Found a valid imm
          if (is_vgpr) {
            instr.set_vs1(riscv_vreg_t(reg1_idx));
          } else {
            instr.set_rs1(riscv_reg_t(reg1_idx));
          }
          instr.set_imm(adjusted_imm);
          state_.stat.replay_successes++;
          state_.stat.replay_immediate_successes++;
          spdlog::trace("Replayed {}: {}={}, imm={} (derived from thread {}) "
                        "sz {} {}",
                        is_vgpr ? "VGPR_IMM" : "SGPR_IMM",
                        is_vgpr ? "vs1" : "rs1", reg1_idx, adjusted_imm,
                        src_tid, result_addresses.tmask.size(),
                        magic_enum::enum_name(instr.meta()->instr_name));
          return ReplayAttemptResult::AttemptedSuccess;
        }
      }
    }
  }
  // VGPR_VGPR: Match op1 first, then derive op2 from history
  else if (target_pair_type == LoadStoreOperandPairType::VGPR_VGPR) {
    for (unsigned i = 0; i < sample_count && sample_count < num_regs; ++i) {
      unsigned reg1_idx = vgpr_idx[i];

      if (!is_reg_pickable(true, reg1_idx)) {
        ++sample_count;
        continue;
      }

      VGPRValues op1_vals = read_vgpr_values(gen_warp, riscv_vreg_t(reg1_idx),
                                             warp_gid, state_.isa_emu);
      uint64_t op1_hash = AddressHasher::hash_addresses(op1_vals);

      auto matching_entries =
          state_.ldst_operand_history.find_matching(target_pair_type, op1_hash);

      if (matching_entries.empty())
        continue;

      spdlog::trace(
          "VGPR_VGPR: Found {} matches for reg {} with op1_hash {:#x}",
          matching_entries.size(), reg1_idx, op1_hash);

      for (const auto &hist_entry : matching_entries) {
        if (!ldst_extra_fields_match(instr, hist_entry))
          continue;

        // Verify op1 values match exactly (check for collision)
        if (!op1_vals.unordered_partial_match(hist_entry.op1_values)) {
          state_.stat.replay_hash_collisions++;
          continue;
        }

        // Now find a vGPR whose values match hist_entry.op2_values
        for (unsigned j = 0; j < sample_count && sample_count < num_regs; ++j) {
          unsigned reg2_idx = vgpr2_idx[j];
          if (reg2_idx == reg1_idx)
            continue;
          if (!is_reg_pickable(true, reg2_idx)) {
            ++sample_count;
            continue;
          }

          VGPRValues op2_vals = read_vgpr_values(
              gen_warp, riscv_vreg_t(reg2_idx), warp_gid, state_.isa_emu);

          if (!op2_vals.unordered_partial_match(hist_entry.op2_values)) {
            state_.stat.replay_hash_collisions++;
            continue;
          }

          // Both op1 and op2 match!
          instr.set_vs1(riscv_vreg_t(reg1_idx));
          instr.set_vs2(riscv_vreg_t(reg2_idx));
          state_.stat.replay_successes++;
          state_.stat.replay_immediate_successes++;
          spdlog::trace("Replayed VGPR_VGPR: vs1={}, vs2={}", reg1_idx,
                        reg2_idx);
          return ReplayAttemptResult::AttemptedSuccess;
        }
      }
    }
  }

  return ReplayAttemptResult::AttemptedNoMatch;
}

// Helper: Resample only the store data register (rs2 for scalar, fs2 for float,
// vs3 for vector) Uses the standard randomize with operand_filter to ensure
// boundary conditions are handled correctly. Returns true if resampling was
// performed successfully
static bool resample_store_data_reg(FuzzerState &state, GenWarp &gen_warp,
                                    riscv_instr &instr) {
  const auto &meta = instr.meta();
  if (!meta->is_store()) {
    return false;
  }

  // Determine which operand filter to use based on store data source register
  // type
  uint64_t operand_filter = 0;
  if (meta->operand_mask & riscv_meta::HAS_RS2) {
    operand_filter = riscv_meta::HAS_RS2;
  } else if (meta->operand_mask & riscv_meta::HAS_FS2) {
    operand_filter = riscv_meta::HAS_FS2;
  } else if (meta->operand_mask & riscv_meta::HAS_VS3) {
    operand_filter = riscv_meta::HAS_VS3;
  } else {
    return false; // No known store data register
  }

  // Use standard randomize with operand_filter and force_mutate=true
  bool ret = meta->randomize(state, gen_warp, &instr, {}, {},
                             /*force_mutate=*/true, operand_filter);

  if (ret) {
    spdlog::trace("Resampled store data register (filter {:#x}) for store ID "
                  "conflict resolution",
                  operand_filter);
  }
  return ret;
}

bool Generator::gen_load_store_addr_(const std::shared_ptr<GenWarp> &gen_warp,
                                     riscv_instr &instr, InterleavedState &ist,
                                     bool can_resample) {
  // we need emulate the load/store address in oracle
  ist.pc_load_store = gen_warp->scratchpad.tell().begin;
  gen_warp->emulate_until(ist.pc_load_store, true);
#if defined(ARCH_VENTUS)
  // regext prefix
  ist.pc_load_store += need_regext_(instr) ? riscv_meta::ilen : 0;
#endif

  const auto &meta = instr.meta();

  fix_misaligned_imm_(gen_warp, instr);

  if (!Config::instance().emu_guide) {
    return true;
  }

  // get the effective address and maintain the store set
  bool rejected, rejected_by_store_id = false, randomize_succ = true;
  unsigned num_resamples = 0;
  unsigned num_store_data_resamples = 0;
  constexpr unsigned max_store_data_resamples =
      8; // Limited retries for store data register
  ReplayAttemptResult replay_result = ReplayAttemptResult::NotAttempted;
  ist.replayed_from_history = false; // Reset for each load/store generation
  do {
    ist.load_set.clear();
    ist.store_set.clear();
    ist.store_id.clear();
    rejected = rejected_by_store_id = false;

    // Try to replay operands from history on first attempt
    if (num_store_data_resamples == 0 && can_resample) {
      replay_result = replay_load_store_operands_(gen_warp, instr);
    }

    if (randomize_succ) {
      bool trap_taken;
      ist.mem_trace = emulate_load_store_(gen_warp, instr, trap_taken);

      if (trap_taken) {
        // but it's not a MMU trap!
        rejected = true;
        spdlog::warn("Load/store instruction at pc {:#x} caused a trap in "
                     "ISA emulator, "
                     "resampling...",
                     ist.pc_load_store);
      } else {
        for (size_t mti = 0; mti < ist.mem_trace.size() && !rejected; ++mti) {
          const auto &memop = ist.mem_trace[mti];

          // check va
          if (illegal_vaddr_(memop.addr, meta->mem_access_size())) {
            spdlog::trace("illegal vaddr {:#x} size {}", memop.addr,
                          meta->mem_access_size());
            rejected = true;
            break;
          }

          for (unsigned ibyte = 0; ibyte < meta->mem_access_size(); ++ibyte) {
            const uint64_t pa = state_.isa_emu.translate_vaddr(
                gen_warp->warp_gid(), memop.addr + ibyte,
                difftest::MemAccessType::LOAD);

            if (illegal_paddr_(pa, 1, meta->is_store())) {
              spdlog::trace("illegal paddr {:#x}", pa);
              rejected = true;
              break;
            }

            if (!memop.is_store &&
                !state_.isa_emu.host_writeable(memop.scope) &&
                state_.store_set.find(pa) == state_.store_set.end()) {
              // cannot read a uninitialized warp- or workgroup-scope memory
              // byte here since it can be a device-specific undefined
              // values. It cannot be pre-written since don't know which
              // SM's shared memory this warp/workgroup dynamically
              // allocated to
              spdlog::trace("load from uninitialized addr {:#x}", pa);
              rejected = true;
              break;
            }

            const auto thread_gid =
                difftest::GlobalThreadId{.warp_gid = gen_warp->warp_gid(),
                                         .thread_lid = memop.thread_lid};

            if (memop.is_store) {
              const uint8_t byte_val = (memop.wdat >> (ibyte * 8)) & 0xFF;
              if (check_store_id_conflict_(ist.store_id, pa, byte_val) ||
                  check_store_id_conflict_(state_.store_id, pa, byte_val)) {
                // we're writing to a location previously written by other
                // threads at different PCs, reject this generation
                rejected = rejected_by_store_id = true;
                spdlog::trace(
                    "store-store id conflict at addr {:#x} byte value {:#x}",
                    pa, byte_val);
                break;
              }
              ist.store_id[pa][byte_val] = difftest::LoadStoreId{
                  .thread_gid = thread_gid,
                  .pc = ist.pc_load_store,
              };
            }

            //
            // LOAD
            // not exclusive with STORE
            //
            if (!memop.is_store) {
              ist.load_set[pa].multiple_threads =
                  ldst_by_multiple_threads(ist.load_set, pa, thread_gid) ||
                  ldst_by_multiple_threads(state_.load_set, pa, thread_gid);
              ist.load_set[pa].thread_gid = thread_gid;
              ist.load_set[pa].pc = ist.pc_load_store;
              ist.load_set[pa].scope = memop.scope;
            }

            //
            // STORE
            //
            if (memop.is_store) {
              ist.store_set[pa].multiple_threads =
                  ldst_by_multiple_threads(ist.store_set, pa, thread_gid) ||
                  ldst_by_multiple_threads(state_.store_set, pa, thread_gid);
              ist.store_set[pa].thread_gid = thread_gid;
              ist.store_set[pa].pc = ist.pc_load_store;
              ist.store_set[pa].scope = memop.scope;
            }
          }
        }
      } // !trap_taken
    } else {
      rejected = true;
    }

    if (rejected) {
      // resample the instruction operands
      if (!can_resample) {
        spdlog::error("Load/store instruction at pc {:#x} rejected but cannot "
                      "resample",
                      ist.pc_load_store);
        std::abort();
      }

      // If rejected due to store ID conflict, first try resampling only the
      // store data register (rs2/fs2/vs3) before falling back to full
      // randomization
      if (rejected_by_store_id &&
          num_store_data_resamples < max_store_data_resamples) {
        if (resample_store_data_reg(state_, *gen_warp, instr)) {
          ++num_store_data_resamples;
          spdlog::trace("Store ID conflict: resampling store data register "
                        "(attempt {}/{})",
                        num_store_data_resamples, max_store_data_resamples);
          continue; // Retry with new store data register without incrementing
                    // num_resamples
        }
      }

      // Fall back to full randomization
      randomize_succ = meta->randomize(state_, *gen_warp, &instr, {}, {}, true);
      if (!randomize_succ)
        rejected = true;
      num_store_data_resamples =
          0; // Reset store data resample counter after full randomization
    } else {
      // succeeded
      break;
    }
  } while (++num_resamples < Config::instance().max_resample_times);

  if (rejected) {
    state_.stat.rejected_load_store_generations++;
    if (rejected_by_store_id) {
      state_.stat.rejected_store_id_conflicts++;
    }
    return false;
  }
  if (replay_result == ReplayAttemptResult::AttemptedSuccess &&
      !num_resamples) {
    state_.stat.replay_eventual_successes++;
    ist.replayed_from_history = true;
  }
  return true;
}

void Generator::add_initial_store_id_(difftest::GlobalThreadId thread_gid,
                                      uint64_t pa, uint8_t rand_byte) {
  const auto store_id = difftest::LoadStoreId{
      .thread_gid = thread_gid,
      .initial_write = true,
      .pc = 0,
      .initial_write_id =
          state_.next_initial_write_id++, // assign globally unique ID
  };
  state_.po.add(store_id);
  state_.po_loc[pa].add(store_id);
  state_.store_id[pa][rand_byte] = store_id;
  state_.ldst_opcodes[store_id] = difftest::LoadStoreOpcode{
      .opcode = difftest::DCacheOpcode::Store,
      .aq = false,
      .rl = false,
      .prec = difftest::FenceFlags::None,
      .succ = difftest::FenceFlags::None,
  };
}

static uint8_t get_ram_initial_byte_val_(uint64_t pa) {
#if defined(ARCH_VENTUS)
  (void)pa;
  return 0;
#elif defined(ARCH_VORTEX)
  const auto page_offset = pa % Config::instance().arch.page_size;
  return (0xbaadf00d >> ((page_offset & 0x3) * 8)) & 0xff;
#endif
}

void Generator::commit_load_store_set_(const std::shared_ptr<GenWarp> &gen_warp,
                                       InterleavedState &ist) {
  riscv_instr &instr = ist.instr;

  // commit all the stores/loads if not rejected
  std::uniform_int_distribution<uint8_t> byte_dist(0, 255);

  // per-byte override used to force init load values into the LDS address range
  std::unordered_map<uint64_t, uint8_t> forced_bytes;
  if (!ist.load_set.empty()) {
#if defined(ARCH_VENTUS)
    const bool init_ireg_phase =
        (ist.inited_int_regs < gen_warp->num_int_regs()) ||
        (ist.inited_vec_regs < gen_warp->num_vec_regs());
    if (init_ireg_phase && Config::instance().force_lds_prob > 0.0) {
      std::bernoulli_distribution force_dist(Config::instance().force_lds_prob);
      if (force_dist(state_.rng)) {
        std::uniform_int_distribution<uint64_t> range_dist(0x70000000ULL,
                                                           0x70020000ULL);
        for (const auto &memop : ist.mem_trace) {
          const uint64_t forced_val = range_dist(state_.rng);
          for (unsigned ibyte = 0; ibyte < memop.size; ++ibyte) {
            forced_bytes[memop.addr + ibyte] =
                static_cast<uint8_t>((forced_val >> (ibyte * 8)) & 0xFF);
          }
        }
        spdlog::trace("Force init LDS value for pc {:#x}", ist.pc_load_store);
      }
    }
#endif
  }
  for (const auto &p : ist.load_set) {
    //
    // initialize the data location before its first derefence
    //
    if (state_.store_set.find(p.first) == state_.store_set.end()) {
      const auto forced_it = forced_bytes.find(p.first);
      const auto rand_byte = (forced_it == forced_bytes.end())
                                 ? byte_dist(state_.rng)
                                 : forced_it->second;

      // allocate the data memory
      MemoryBlock blk;
      bool alloc = false;
      auto org_blk = state_.mem_alloc.query(p.first);
      if (org_blk)
        blk = *org_blk;
      else {
        blk = state_.mem_alloc.allocate(MemBlkType::WRITABLE, p.first, 1);
        if (!blk.valid()) {
          spdlog::error(
              "Failed to allocate data memory for load/store at paddr "
              "{:#x}",
              p.first);
          std::abort();
        }
        state_.mem_alloc.shrink(blk, 1);
        alloc = true;
      }

      spdlog::trace("{} data memory paddr {:#x} value {:#x} for load pc "
                    "{:#x}, warp GID {} scope {}",
                    alloc ? "Allocate" : "Use", p.first, rand_byte,
                    ist.pc_load_store, gen_warp->warp_gid(), p.second.scope);

      data_mem_regions_.emplace_back(p.first, rand_byte, p.second.scope);

      state_.isa_emu.pmem_write(p.second.scope, p.first, &rand_byte,
                                sizeof(rand_byte));
      state_.stat.data_footprint_bytes[p.second.scope.scope()]++;

      // virtual "store" to the store_set to avoid future
      // re-initialization
      state_.store_set[p.first] = p.second;
      state_.store_set[p.first].initial_written = true;
      // this must be before the later load po/po_loc building
      add_initial_store_id_(p.second.thread_gid, p.first, rand_byte);

    } else {
      spdlog::trace("Data memory paddr {:#x} stored by thread GID {}, pc {:#x} "
                    "required by pc {:#x}, warp GID {}",
                    p.first, state_.store_set.at(p.first).thread_gid,
                    state_.store_set.at(p.first).pc, ist.pc_load_store,
                    gen_warp->warp_gid());
    }
  }

  for (const auto &p : ist.store_set) {
    // allocate the data memory
    auto blk = state_.mem_alloc.query(p.first);
    if (!blk.has_value()) {
      auto blk = state_.mem_alloc.allocate(MemBlkType::WRITABLE, p.first, 1);
      state_.mem_alloc.shrink(blk, 1);
      spdlog::trace("Allocate data memory paddr {:#x} for store pc "
                    "{:#x}, warp GID {} scope {}",
                    p.first, ist.pc_load_store, gen_warp->warp_gid(),
                    p.second.scope);

      state_.stat.data_footprint_bytes[p.second.scope.scope()]++;

      // virtual "store" to the store_set to represent the initial values of the
      // RAM. This must be before the later load po/po_loc building
      add_initial_store_id_(p.second.thread_gid, p.first,
                            get_ram_initial_byte_val_(p.first));

    } else {
      spdlog::trace("Use existing data memory paddr {:#x} for store pc "
                    "{:#x}, warp GID {} scope {}",
                    p.first, ist.pc_load_store, gen_warp->warp_gid(),
                    p.second.scope);
      assert(blk->type == MemBlkType::WRITABLE);
    }
  }

  merge_ldst_set(state_.store_set, ist.store_set);
  merge_ldst_set(state_.load_set, ist.load_set);

  //
  // memory taint propagation
  //
  bool taint = false;
  for (const auto &p : ist.load_set) {
    if (state_.taint_paddr.find(p.first) != state_.taint_paddr.end()) {
      taint = true;
      break;
    }
  }
  const bool memdat_taint = Taint::propagate(*gen_warp, instr, taint);
  if (memdat_taint) {
    spdlog::info("memdat taint: pc {:#x} warp GID {}", ist.pc_load_store,
                 gen_warp->warp_gid());
    for (const auto &p : ist.store_set) {
      state_.taint_paddr.insert(p.first);
    }
  } else {
    // remove the taint on these addresses if any
    for (const auto &p : ist.store_set) {
      state_.taint_paddr.erase(p.first);
    }
  }

  // store id
  for (const auto &[pa, byte_store_info] : ist.store_id) {
    if (state_.taint_paddr.find(pa) != state_.taint_paddr.end()) {
      state_.pending_store_id.insert(pa);
    }
    for (const auto &[byte_val, store_id] : byte_store_info) {
      state_.store_id[pa][byte_val] = store_id;
      spdlog::trace("Store id: paddr {:#x} byte_val {:#x} by thread "
                    "GID {}, pc {:#x}",
                    pa, byte_val, store_id.thread_gid, store_id.pc);
    }
  }

  // maintain the load-store program order
  std::unordered_set<unsigned> threads;
  for (const auto &memop : ist.mem_trace) {
    const auto insn = difftest::LoadStoreId{
        .thread_gid = difftest::GlobalThreadId{.warp_gid = gen_warp->warp_gid(),
                                               .thread_lid = memop.thread_lid},
        .pc = ist.pc_load_store,
    };
    state_.po.add(insn);
    state_.po_loc[memop.addr].add(insn);
    threads.insert(memop.thread_lid);
  }

  // load/store opcode list used for memory modeling
  for (const auto &tid : threads) {
    auto &opcode = state_.ldst_opcodes[difftest::LoadStoreId{
        .thread_gid = difftest::GlobalThreadId{.warp_gid = gen_warp->warp_gid(),
                                               .thread_lid = tid},
        .pc = ist.pc_load_store,
    }];
    opcode = difftest::LoadStoreOpcode{
        .opcode = ist.meta->is_store()  ? difftest::DCacheOpcode::Store
                  : ist.meta->is_load() ? difftest::DCacheOpcode::Load
                                        : difftest::DCacheOpcode::Atomic,
        .aq = (ist.meta->operand_mask & riscv_meta::HAS_ORDERING) && instr.aq(),
        .rl = (ist.meta->operand_mask & riscv_meta::HAS_ORDERING) && instr.rl(),
        .prec = difftest::FenceFlags::None,
        .succ = difftest::FenceFlags::None,
    };
  }

  if (!taint && instr.meta()->is_store()) {
    auto &dst = state_.store_trace_.stores[difftest::LoadStoreId{
        .thread_gid = difftest::GlobalThreadId{.warp_gid = gen_warp->warp_gid(),
                                               .thread_lid = 0},
        .pc = ist.pc_load_store,
    }];
    assert(dst.bytes.empty());
    for (const auto &memop : ist.mem_trace) {
      if (memop.is_store) {
        for (unsigned i = 0; i < memop.size; ++i) {
          dst.bytes[memop.addr + i] =
              (static_cast<uint8_t>((memop.wdat >> (i * 8)) & 0xFF));
        }
      }
    }
  }

  update_ldst_operand_history_(gen_warp, instr);
}

void Generator::gen_fence_(const std::shared_ptr<GenWarp> &gen_warp,
                           riscv_instr &instr) {
  const auto pc = gen_warp->scratchpad.emit(&instr, false, false);

  if (instr.meta()->instr_name == riscv_instr_name_t::FENCE) {
    gen_warp->emulate_until(pc, true); // get the latest tmask

    foreach_thread_lid(gen_warp, tid) {
      // no need to check tmask here since fence can "virtually" apply to all
      // threads

      const auto id = difftest::LoadStoreId{
          .thread_gid =
              difftest::GlobalThreadId{.warp_gid = gen_warp->warp_gid(),
                                       .thread_lid = tid},
          .pc = pc,
      };
      state_.ldst_opcodes[id] = difftest::LoadStoreOpcode{
          .opcode = difftest::DCacheOpcode::Fence,
          // currently both ventus and vortex treat fence as full
          .prec = difftest::FenceFlags::R | difftest::FenceFlags::W |
                  difftest::FenceFlags::IO,
          .succ = difftest::FenceFlags::R | difftest::FenceFlags::W |
                  difftest::FenceFlags::IO,
      };
    }
  }
}

void Generator::gen_load_store_(const std::shared_ptr<GenWarp> &gen_warp,
                                riscv_instr &instr, InterleavedState &ist) {

  bool taint = false;
  bool multithread = false;
  switch (instr.meta()->instr_name) {
  case riscv_instr_name_t::LR_W:
  case riscv_instr_name_t::LR_D:
  case riscv_instr_name_t::SC_W:
  case riscv_instr_name_t::SC_D:
    // depends on the architectural state of the reservation set
    taint = true;
    break;
  default:
    for (const auto &ld : ist.load_set) {
      const auto pa = ld.first;
      if (is_rfe(state_.store_set, pa, ld.second.thread_gid)) {
        // we're reading from a location previously written by other
        // threads, taint the load output since it's data raceed
        taint = true;
        multithread = true;
        break;
      }
      if (state_.taint_paddr.find(pa) != state_.taint_paddr.end()) {
        taint = true;
        if (multithread)
          break;
      }
    }
  }
  if (multithread) {
    if (instr.meta()->is_load()) {
      for (const auto &ld : ist.load_set) {
        const auto pa = ld.first;
        spdlog::info("Load from paddr {:#x} by warp GID {} pc {:#x} "
                     "previously written by other threads",
                     pa, gen_warp->warp_gid(), ist.pc_load_store);
      }
      state_.stat.tainted_load++;
    } else {
      state_.stat.tainted_store++;
    }
  }
#if defined(ARCH_VENTUS)
  gen_regext_(gen_warp, instr);
#endif
  const auto pc = gen_warp->scratchpad.emit(&instr, false, taint);
  assert(ist.pc_load_store == pc);
  if (taint) {
    gen_warp->data_race_loads.push_back(pc);
  }

  state_.stat.accepted_load_store_generations++;
}

} // namespace fuzzgpu
