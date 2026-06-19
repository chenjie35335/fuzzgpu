#include <chrono>

#include "bug_filter.hh"
#include "config.hh"
#include "exchange_file.hh"
#include "fuzzer_state.hh"
#include "gen_warp.hh"
#include "interleaved_state.hh"
#include "isa/riscv/riscv_amo_meta.hh"
#include "isa/riscv/riscv_floating_point_meta.hh"
#include "isa/riscv/riscv_instr.hh"
#include "isa/riscv/riscv_meta.hh"
#include "isa/riscv/riscv_meta_list.hh"
#include "isa/riscv/vortex/csr.hh"
#include "types.hh"

#include "generator.hh"

namespace fuzzgpu {

Generator::Generator(FuzzerState &state)
  : state_(state)
  , max_trampoline_sz_(riscv_meta::ilen *
                       (riscv_meta::imm_setter_max_instrs() + 1)) // li + jalr
{

  init_trampoline_candidates_();
  init_mmio_addrs_();

  GenWarp gen_warp(state_, "", riscv_meta::gpr_encoding_space,
                   riscv_meta::gpr_encoding_space,
                   riscv_meta::gpr_encoding_space, 1, 1);
  spdlog::debug("Supported ISA instruction:");
  spdlog::debug("{:>10} | ASM Example", "INSTR");
  fmt::memory_buffer buf;
  for (size_t category = 0; category < size_t(riscv_instr_category_t::Count);
       category++) {
    spdlog::debug("-{}:",
                  magic_enum::enum_name(riscv_instr_category_t(category)));
    for (const auto &meta : riscv_meta_list::instance().instr_per_category(
             riscv_instr_category_t(category))) {
      auto *instr = state_.instr_pool.create(meta);
      instr->meta()->randomize(state_, gen_warp, instr, {}, {}, false);

      spdlog::debug("{:>10}", magic_enum::enum_name(meta->instr_name));
    }
  }
}

size_t Generator::get_basic_block_sz_(size_t num_target_instrs) const {
  return max_trampoline_sz_ + num_target_instrs * riscv_meta::ilen;
}

bool Generator::eof_basic_block_(
    const std::shared_ptr<GenWarp> &gen_warp,
    const std::shared_ptr<riscv_meta> &meta) const {
  size_t max_new_instrs = 1;
  // place for the refresh instructions
  if (meta->is_vector() && gen_warp->no_available_vec_reg()) {
    max_new_instrs += 1;
  } else if (gen_warp->no_available_int_reg()) {
    max_new_instrs += 1;
  }
  switch (meta->category) {
  // for branch/jump, we may need add extra trampoline instructions for
  // far jumps
  case riscv_instr_category_t::BRANCH:
    max_new_instrs += get_max_branch_trampoline_num_instr_() - 1;
    break;
  case riscv_instr_category_t::JUMP:
    max_new_instrs += get_max_jump_trampoline_num_instr_() - 1;
    break;
  default:
    if (meta->is_thread_mask()) {
      // for thread mask setting instr, we may need extra imm_setter
      max_new_instrs += riscv_meta::imm_setter_max_instrs();
    } else if (meta->is_thread_join()) {
      max_new_instrs += GenWarp::join_reconv_point_instrs();
    }
  }
#if defined(ARCH_VENTUS)
  const bool regext_xd = (meta->operand_mask & riscv_meta::HAS_FD);
  const bool regext_xs1 = (meta->operand_mask & riscv_meta::HAS_FS1);
  const bool regext_xs2 = (meta->operand_mask & riscv_meta::HAS_FS2);
  const bool regext_xs3 = (meta->operand_mask & riscv_meta::HAS_FS3);
  if (regext_xd || regext_xs1 || regext_xs2 || regext_xs3) {
    max_new_instrs += 1; // regext
  }
#endif

  const size_t max_new_instr_bytes = riscv_meta::ilen * max_new_instrs;
  bool ret = (
      // out of the max. instr count per bb
      gen_warp->bb_snapshot.num_gen_bb_instrs + max_new_instrs >
      gen_warp->bb_snapshot.num_target_instrs);
  [[maybe_unused]] bool eof_bb1 = (
      // no enough space for another instruction + trampoline
      gen_warp->scratchpad.tell().begin + max_trampoline_sz_ +
          max_new_instr_bytes >
      gen_warp->bb_snapshot.mem_blk.end + 1);
  assert((ret || !eof_bb1) &&
         "bug: check the allocated memory block size or the bb_instrs count");

  return ret;
}

void Generator::init_mmio_addrs_() {
  const auto &arch = Config::instance().arch;
  for (const auto &mmio_region : arch.mmio_regions) {
    // we assume mmio regions are NOT too large and byte-addressable
    assert(mmio_region.second - mmio_region.first < (1 << 20) &&
           "MMIO region too large, fix the implementation");
    for (uint64_t addr = mmio_region.first; addr < mmio_region.second; ++addr) {
      mmio_addrs_.insert(addr);
    }
  }
}

void Generator::generate_backbone(std::ostream &instr_stream,
                                  std::ostream &data_stream,
                                  std::ostream &aux_stream,
                                  nlohmann::json &meta_json,
                                  const std::string &trace_file_prefix) {

  auto num_active_threads =
      (BugFilter::instance().filter("var_thread_num") && BugFilter::instance().filter("csr_tid"))
          ? Config::instance().arch.nr_threads_per_warp // full mask
          : std::uniform_int_distribution<unsigned>(
                Config::instance().min_tmask_flips,
                Config::instance().arch.nr_threads_per_warp)(state_.rng);
#if defined(ARCH_VENTUS)
  num_active_threads =
      std::bit_ceil(num_active_threads); // vector requires power-of-2 VL
#endif
  assert(num_active_threads <= Config::instance().arch.nr_threads_per_warp);

  spdlog::info("Number of active threads per warp: {} {} {}", num_active_threads,
  BugFilter::instance().filter("var_thread_num") , BugFilter::instance().filter("csr_tid")
  
  );

  size_t max_warp_gid;
#if defined(ARCH_VORTEX)
  max_warp_gid = VortexScheduler::max_warp_gid();
#elif defined(ARCH_VENTUS)
  max_warp_gid = VentusScheduler::max_warp_gid();
#endif
  // truncate all the existing trace files
  if (!trace_file_prefix.empty()) {
    for (size_t warp_gid = 0; warp_gid < max_warp_gid; ++warp_gid) {
      const std::string trace_file =
          fmt::format("{}_warp_gid_{}.trace", trace_file_prefix, warp_gid);
      std::ofstream(trace_file, std::ios::trunc);
    }
  }

  const auto num_int_regs = riscv_meta::gpr_encoding_space;
  const auto num_fp_regs = riscv_meta::gpr_encoding_space;

#if defined(ARCH_VENTUS)
  const unsigned sgpr_per_warp = num_int_regs + num_fp_regs,
                 vgpr_per_warp = riscv_meta::gpr_encoding_space;
  const unsigned vlmul = 1;
  // ventus currently only supports VLMUL==1
  scheduler_ = std::make_shared<VentusScheduler>(
      state_, trace_file_prefix, sgpr_per_warp, num_int_regs, num_fp_regs,
      vgpr_per_warp, vlmul, num_active_threads);
#elif defined(ARCH_VORTEX)
  const unsigned vgpr_per_warp = 0;
  const unsigned vlmul = 1;
  scheduler_ = std::make_shared<VortexScheduler>(state_, trace_file_prefix,
                                                 vlmul, num_active_threads);
#endif

  //
  // Genenrate the code for each warp
  //
  std::vector<std::shared_ptr<GenWarp>> gen_warps(
      Config::instance().num_genwarps);

  const size_t min_bb_sz =
      Config::instance().min_num_fuzzing_instrs_per_bb * riscv_meta::ilen +
      max_trampoline_sz_;

  const size_t min_prologue_sz =
      32 *
      riscv_meta::ilen; // estimated len of the code gen by gen_init_threads()
  // see gen_interleaved_step_() for details:
  // li t0, SGPR_BASEADDR
  // vid.v v1,
  // li t1, VGPR_BYTES_PER_THREAD
  // vmul.vx v1, v1, t1
  // li t1, VGPR_BASEADDR
  // vmv.v.i v2, t1
  // vadd.vv v2, v2, v1
  // foreach r in num_int_regs:
  //   lw/d r, r*XLEN/8(t0)
  // foreach f in num_fp_regs:
  //   flw/d f, (num_int_regs+r)*XLEN/8(t0)
  // foreach v in num_vec_regs:
  //   vlw v, v*XLEN/8(v2)
#if defined(ARCH_VENTUS)
  const size_t regext_prefix_instrs = 1;
#else
  const size_t regext_prefix_instrs = 0;
#endif
  const size_t reg_init_instrs =
      (riscv_meta::imm_setter_max_instrs() * 3 + 4 + num_int_regs +
       (regext_prefix_instrs + 1) * num_fp_regs + vgpr_per_warp);

  // partition interleaved-warp groups
  /// @brief Contiguous group of warps generated in an interleaved schedule.
  class InterleavedGroup {
  public:
    ///< Inclusive generated-warp index range covered by this group.
    size_t igw_start, igw_end;
    ///< Per-warp interleaved generation states.
    std::vector<InterleavedState> istates;

    /// @brief Create state for warps in the inclusive range.
    InterleavedGroup(size_t igw_start_, size_t igw_end_)
      : igw_start(igw_start_)
      , igw_end(igw_end_)
      , istates(igw_end_ - igw_start_ + 1) {}
  };
  std::vector<std::shared_ptr<InterleavedGroup>> interleaved_groups;
  size_t last_igw_start = 0;
  assert(scheduler_->grid_level_begins(last_igw_start));
  for (size_t igw = 0; igw < gen_warps.size(); ++igw) {
    if (scheduler_->grid_level_begins(igw + 1) || igw == gen_warps.size() - 1) {
      interleaved_groups.push_back(
          std::make_shared<InterleavedGroup>(last_igw_start, igw));
      last_igw_start = igw + 1;
    }
  }

  std::vector<size_t> mem_addr_gen_igw;

  for (auto &igroup : interleaved_groups) {
    //
    // Init warps per interleaved group
    //
    for (size_t igw = igroup->igw_start; igw <= igroup->igw_end; ++igw) {
      auto &gen_warp = gen_warps[igw];
      //
      // allocate memory for the warp
      //
      assert(Config::instance().arch.gmem_size >=
                 1 + Config::instance().arch.gmem_start_addr &&
             "gmem start addr out of bound");
      auto warp_mem_blk = state_.mem_alloc.allocate(
          MemBlkType::CODE, Config::instance().arch.gmem_start_addr, 0,
          Config::instance().arch.gmem_size - 1, min_prologue_sz + min_bb_sz,
          riscv_meta::instr_alignment);

      gen_warps[igw] = std::make_shared<GenWarp>(
          state_, trace_file_prefix, num_int_regs, num_fp_regs, vgpr_per_warp,
          vlmul, num_active_threads);

      //
      // setup the CTA scheduler
      //
      scheduler_->add_warp(igw, warp_mem_blk.begin, gen_warp);

      gen_barrier_init_warp_(gen_warp, igw);

      gen_warp->scratchpad.seek(warp_mem_blk);

      // activate the randomly-selected threads
      gen_warp->gen_init_threads();

      //
      // connect the prologue and the first basic block
      //
      const size_t first_bb_num_target_instrs =
          state_.nr_tgt_instr_dist(state_.rng) + reg_init_instrs;

      const size_t warp_mem_blk_sz =
          (gen_warp->scratchpad.tell().begin - warp_mem_blk.begin + 1) +
          get_basic_block_sz_(first_bb_num_target_instrs);
      state_.mem_alloc.shrink(warp_mem_blk, warp_mem_blk_sz);
      gen_warp->scratchpad.shrink(warp_mem_blk.end);
      gen_warp->switch_basic_block(
          gen_warp->scratchpad.tell(),
          GenWarp::BasicBlockSnapshot(first_bb_num_target_instrs, warp_mem_blk),
          gen_warp->tmask());

      gen_warp->nr_gen_bb = 1;
    }

    do {
      mem_addr_gen_igw.clear();
      for (size_t igw = igroup->igw_start; igw <= igroup->igw_end; ++igw) {
        auto &istate = igroup->istates[igw - igroup->igw_start];
        while (!istate.warp_terminated &&
               istate.last_exit_reason != GenExitReason::MemAddrGen) {
          spdlog::debug("Switch to the interleaved warp GID {}",
                        gen_warps[igw]->warp_gid());
          istate.last_exit_reason =
              gen_interleaved_step_(igw, gen_warps[igw], istate);
          switch (istate.last_exit_reason) {
          case GenExitReason::EndOfBasicBlock:
          case GenExitReason::Step:
          case GenExitReason::Retry:
            break;
          case GenExitReason::MemAddrGen:
            commit_load_store_set_(gen_warps[igw], istate);
            mem_addr_gen_igw.push_back(igw);
            break;
          case GenExitReason::WarpTerminated:
            break;
          default:
            assert(false && "unhandled exit reason");
          }
        }
      }

      // sanity check: now all the interleaved warps in this group have either
      // terminated or completed the memory address generation
      for (size_t igw = igroup->igw_start; igw <= igroup->igw_end; ++igw) {
        [[maybe_unused]] auto &istate =
            igroup->istates[igw - igroup->igw_start];
        assert(istate.warp_terminated ||
               istate.last_exit_reason == GenExitReason::MemAddrGen);
      }

      // emit the load/store instructions with the generated addresses
      for (const auto &igw : mem_addr_gen_igw) {
        [[maybe_unused]] auto &istate =
            igroup->istates[igw - igroup->igw_start];
        gen_load_store_(gen_warps[igw], istate.instr, istate);
        // resume the warp
        istate.last_exit_reason = GenExitReason::Step;
      }

    } while (!mem_addr_gen_igw.empty());
  } // for igroup

  //
  // Print statistics
  //
  state_.stat.dump();
  spdlog::info(" Number of generated warps: {}",
               state_.stat.num_generated_warps);
  unsigned nr_total_instrs = 0;
  for (size_t igw = 0; igw < state_.stat.num_generated_warps; ++igw) {
    const auto &gen_warp = gen_warps[igw];
    spdlog::info(" #{}: Warp GID {}", igw, gen_warp->warp_gid());
    spdlog::info("  generated basic blocks: {}", gen_warp->nr_gen_bb);
    spdlog::info("  generated instrs: {}", gen_warp->nr_gen_instrs);
    gen_warp->simt_stack.stat.dump(1);
    nr_total_instrs += gen_warp->nr_gen_instrs;
    fmt::memory_buffer scopestr;
    dump_barrier_scope_id_(gen_warp->barrier_scope_id, scopestr);
    spdlog::info("  Scope id: {}", fmt::to_string(scopestr));
    spdlog::info("  Barriers:");
    for (const auto &[pc, bar] : barrier_meta_.barriers[gen_warp->warp_gid()]) {
      spdlog::info("    - Barrier id {}, pc {:#x}, scope {}, count {}", bar.id,
                   pc, magic_enum::enum_name(bar.scope), bar.count);
    }
  }
  spdlog::info(" Total generated instructions: {}", nr_total_instrs);

  // Start timing for file writing
  auto file_write_start = std::chrono::high_resolution_clock::now();

  //
  // Write instrs to file
  //
  for (const auto &gen_warp : scheduler_->gen_warps()) {
    gen_warp->scratchpad.write_file(instr_stream);
  }
  for (const auto &gen_warp : gen_warps) {
    gen_warp->scratchpad.write_file(instr_stream);
  }

  //
  // Write data to file
  //
  for (const auto &header : data_mem_regions_) {
    data_stream.write(reinterpret_cast<const char *>(&header), sizeof(header));
  }

  //
  // Write aux file
  //
  for ([[maybe_unused]] const auto &gen_warp : scheduler_->gen_warps()) {
    assert(gen_warp->data_race_loads.empty());
  }
  AuxHeader aux_header;
  aux_header.magic = AuxHeader::MAGIC;
  aux_header.nr_data_race_loads = 0;
  for (const auto &gen_warp : gen_warps) {
    aux_header.nr_data_race_loads += gen_warp->data_race_loads.size();
  }
  aux_header.nr_store_ids = state_.store_id.size();
  aux_header.nr_pending_store_ids = state_.pending_store_id.size();
  aux_stream.write(reinterpret_cast<const char *>(&aux_header),
                   sizeof(aux_header));
  // write data race load PC list
  for (const auto &gen_warp : gen_warps) {
    aux_stream.write(
        reinterpret_cast<const char *>(gen_warp->data_race_loads.data()),
        sizeof(decltype(gen_warp->data_race_loads)::value_type) *
            gen_warp->data_race_loads.size());
  }
  // write store id entries
  for (const auto &[pa, store_id_entries] : state_.store_id) {
    StoreIdSection store_id_section;
    store_id_section.magic = StoreIdSection::MAGIC;
    store_id_section.pa = pa;
    store_id_section.nr_entries = store_id_entries.size();
    aux_stream.write(reinterpret_cast<const char *>(&store_id_section),
                     sizeof(store_id_section));
    for (const auto &entry : store_id_entries) {
      StoreIdEntry store_id_entry;
      store_id_entry.magic = StoreIdEntry::MAGIC;
      store_id_entry.byte_val = entry.first;
      store_id_entry.store_id = entry.second;
      aux_stream.write(reinterpret_cast<const char *>(&store_id_entry),
                       sizeof(store_id_entry));
    }
  }
  // write pending store id list
  for (const auto &pa : state_.pending_store_id) {
    uint64_t pa64 = pa;
    aux_stream.write(reinterpret_cast<const char *>(&pa64), sizeof(pa64));
  }
  // write po relations
  state_.po.serialize(aux_stream);
  PoLocHeader po_loc_header;
  po_loc_header.magic = PoLocHeader::MAGIC;
  po_loc_header.num_po_loc_rela = state_.po_loc.size();
  aux_stream.write(reinterpret_cast<const char *>(&po_loc_header),
                   sizeof(po_loc_header));
  for (const auto &[addr, rela] : state_.po_loc) {
    uint64_t a64 = addr;
    aux_stream.write(reinterpret_cast<const char *>(&a64), sizeof(a64));
    rela.serialize(aux_stream);
  }
  // write load/store opcodes
  LoadStoreOpcodeHeader ldst_opcode_header;
  ldst_opcode_header.magic = LoadStoreOpcodeHeader::MAGIC;
  ldst_opcode_header.num_opcodes = state_.ldst_opcodes.size();
  aux_stream.write(reinterpret_cast<const char *>(&ldst_opcode_header),
                   sizeof(ldst_opcode_header));
  for (const auto &[id, opcode] : state_.ldst_opcodes) {
    aux_stream.write(reinterpret_cast<const char *>(&id), sizeof(id));
    aux_stream.write(reinterpret_cast<const char *>(&opcode), sizeof(opcode));
  }
  // write store trace
  state_.store_trace_.serialize(aux_stream);
  // write data, addr deps
  std::chrono::microseconds perf_dep_analysis_us(0);
  DepHeader header;
  header.magic = DepHeader::MAGIC;
  header.num_warps = gen_warps.size();
  aux_stream.write(reinterpret_cast<const char *>(&header), sizeof(header));
  for (auto &gen_warp : gen_warps) {
    gen_warp->reg_dep_tracker.clear_dep_caches();
    auto tstart = std::chrono::high_resolution_clock::now();
    const auto data_dep = gen_warp->reg_dep_tracker.build_ldst_data_dep();
    const auto addr_dep = gen_warp->reg_dep_tracker.build_ldst_addr_dep();
    auto tend = std::chrono::high_resolution_clock::now();
    perf_dep_analysis_us +=
        std::chrono::duration_cast<std::chrono::microseconds>(tend - tstart);
    DepPerWarpHeader header;
    header.magic = DepPerWarpHeader::MAGIC;
    header.num_data_deps = data_dep.size();
    header.num_addr_deps = addr_dep.size();
    header.warp_gid = gen_warp->warp_gid();
    aux_stream.write(reinterpret_cast<const char *>(&header), sizeof(header));
    for (const auto &[from, tos] : data_dep) {
      uint64_t f64 = from;
      aux_stream.write(reinterpret_cast<const char *>(&f64), sizeof(f64));
      uint64_t num_tos = tos.size();
      aux_stream.write(reinterpret_cast<const char *>(&num_tos),
                       sizeof(num_tos));
      for (const auto &to : tos) {
        uint64_t t64 = to;
        aux_stream.write(reinterpret_cast<const char *>(&t64), sizeof(t64));
      }
    }
    for (const auto &[from, tos] : addr_dep) {
      uint64_t f64 = from;
      aux_stream.write(reinterpret_cast<const char *>(&f64), sizeof(f64));
      uint64_t num_tos = tos.size();
      aux_stream.write(reinterpret_cast<const char *>(&num_tos),
                       sizeof(num_tos));
      for (const auto &to : tos) {
        uint64_t t64 = to;
        aux_stream.write(reinterpret_cast<const char *>(&t64), sizeof(t64));
      }
    }
  }

  spdlog::info("Data/Addr dependency analysis took {} us",
               perf_dep_analysis_us.count());

  // Calculate total file writing time (excluding dependency analysis)
  auto file_write_end = std::chrono::high_resolution_clock::now();
  auto total_file_write_time =
      std::chrono::duration_cast<std::chrono::microseconds>(file_write_end -
                                                            file_write_start);
  state_.stat.file_write_time_us =
      total_file_write_time.count() - perf_dep_analysis_us.count();
  spdlog::info("File writing took {} us (excluding dependency analysis)",
               state_.stat.file_write_time_us);

  //
  // Write meta information
  //
  meta_json["arch_name"] = Config::instance().arch.name;
  meta_json["word_bits"] = Config::instance().arch.word_bits;
  meta_json["num_genblocks"] = Config::instance().num_genblocks;
  meta_json["num_genwarps_per_block"] =
      Config::instance().num_genwarps_per_block;
  meta_json["wavefront_size"] = num_active_threads;
  meta_json["num_instrs"] = nr_total_instrs;
#if defined(ARCH_VENTUS)
  meta_json["sgpr_usage"] = sgpr_per_warp;
  meta_json["vgpr_usage"] = vgpr_per_warp;
  meta_json["workgroup_size"] = {scheduler_->ventus_get_kernel_size()[0],
                                 scheduler_->ventus_get_kernel_size()[1],
                                 scheduler_->ventus_get_kernel_size()[2]};
#endif
  if (!trace_file_prefix.empty()) {
    meta_json["overwritten_pc_set"] = nlohmann::json::array();
    for (const auto &gen_warp : gen_warps) {
      for (const auto &pc : gen_warp->overwritten_pc) {
        meta_json["overwritten_pc_set"].push_back(pc);
      }
    }
    meta_json["taint_pc_set"] = nlohmann::json::array();
    for (const auto &gen_warp : gen_warps) {
      for (const auto &pc : gen_warp->taint_pc) {
        meta_json["taint_pc_set"].push_back(pc);
      }
    }
  }
  meta_json["instr_freq"] = state_.get_instr_freq();
  meta_json["file_write_time_us"] = state_.stat.file_write_time_us;
  meta_json["bug_filter_list"] = nlohmann::json::object();
  for (const auto &[bug_name, filtered] : BugFilter::instance().get_bug_map()) {
    meta_json["bug_filter_list"][bug_name] = filtered;
  }

  //
  // Flush trace
  //
  for (auto &gw : scheduler_->gen_warps()) {
    gw->flush_trace();
  }
  for (auto &gw : gen_warps) {
    gw->flush_trace();
  }
}

GenExitReason
Generator::gen_interleaved_step_(size_t igw, std::shared_ptr<GenWarp> &gen_warp,
                                 InterleavedState &istate) {
  assert(!istate.warp_terminated);

  bool empty_bb = false;
  //
  // Generate the initialization code
  //
  if (!istate.terminating_warp) {
    const auto init_ret = gen_initial_block_(gen_warp, istate);
    if (init_ret != GenExitReason::EndOfInit) {
      return init_ret;
    }
  }

  if (istate.terminating_warp) {
    spdlog::warn("Terminating warp GID {} {} {} > {}", gen_warp->warp_gid(),
                 gen_warp->nr_gen_instrs, state_.get_num_instrs(),
                 Config::instance().max_num_instrs);
    if (gen_warp->simt_stack.size()) {
// need to drain the simt stack
// need to emit a join instruction
#if defined(ARCH_VORTEX)
      istate.meta =
          riscv_meta_list::instance().instr(riscv_instr_name_t::VX_JOIN);
#elif defined(ARCH_VENTUS)
      istate.meta = riscv_meta_list::instance().instr(riscv_instr_name_t::JOIN);
#endif
      spdlog::info("drain simt stack");
    } else if (state_.emitted_barrier && gen_warp->has_unsolved_barrier()) {
#if defined(ARCH_VORTEX)
      istate.meta =
          riscv_meta_list::instance().instr(riscv_instr_name_t::VX_BAR);
#elif defined(ARCH_VENTUS)
      istate.meta =
          riscv_meta_list::instance().instr(riscv_instr_name_t::BARRIER);
#endif
      spdlog::info("drain barrier");

    } else {
      spdlog::info("terminate warp");
      // nned to terminate the warp
      scheduler_->emit_end_of_warp(igw, gen_warp);
      istate.warp_terminated = true;
      end_of_basic_block_(gen_warp, istate);
      end_of_warp_(gen_warp, istate);
      return GenExitReason::WarpTerminated;
    }
  } else {
    if (!gen_warp->dump_regs) {
      gen_warp->emulate_until(gen_warp->scratchpad.tell().begin, true);
      gen_warp->dump_regs = true;
      if (Config::instance().emu_guide) {
        spdlog::debug("**** Dumping registers for warp GID {} ****",
                      gen_warp->warp_gid());
        foreach_thread_lid(gen_warp, tid) {
          if (!gen_warp->tmask().test(tid))
            continue;
          for (unsigned v = 0; v < gen_warp->num_vec_regs(); v++) {
            const auto val = state_.isa_emu.reg_read(
                gen_warp->warp_gid(), tid,
                difftest::RegId(difftest::RegType::Vec, v));
            spdlog::debug("Thread {}, V{} = {:#x}", tid, v, val);
          }
          for (unsigned v = 0; v < gen_warp->num_int_regs(); ++v) {
            const auto val = state_.isa_emu.reg_read(
                gen_warp->warp_gid(), tid,
                difftest::RegId(difftest::RegType::Int, v));
            spdlog::debug("Thread {}, X{} = {:#x}", tid, v, val);
          }
          for (unsigned v = 0; v < gen_warp->num_fp_regs(); ++v) {
            const auto val = state_.isa_emu.reg_read(
                gen_warp->warp_gid(), tid,
                difftest::RegId(difftest::RegType::Float, v));
            spdlog::debug("Thread {}, F{} = {:#x}", tid, v, val);
          }
        }
      }
    }
    // randomly sample the instruction in the category
    istate.meta = state_.pick_instr();
  }

  //
  // generate a instruction for the current basic block
  //
  const bool eof_bb = eof_basic_block_(gen_warp, istate.meta);

  // end of the basic block, emit a trampoline
  const bool is_jmp = istate.meta->category == riscv_instr_category_t::JUMP;
  if (eof_bb || empty_bb || is_jmp) {
    // gen_warp->dump_regs = false;
    std::uniform_int_distribution<size_t> trampoline_type_dist(
        0, trampoline_candidates_.size() - 1);

    unsigned tgt_num_instrs;
    size_t bb_sz;
    // allocate the next new basic block memory
    tgt_num_instrs = state_.nr_tgt_instr_dist(state_.rng);
    bb_sz = get_basic_block_sz_(tgt_num_instrs);

    const auto pc_begin = gen_warp->scratchpad.tell().begin;

    const auto preferred_trampoline_type =
        is_jmp ? get_trampoline_type_(istate.meta)
               : trampoline_type_dist(state_.rng);

    size_t it = preferred_trampoline_type;
    bool failed;
    for (;;) {
      const auto &trampoline = trampoline_candidates_[it];

      uint64_t pc_jmp;
      if (trampoline.pc_jmp_offset == uint64_t(-1)) {
        pc_jmp = 0; // reg-indirect jump
      } else {
        pc_jmp = pc_begin + trampoline.pc_jmp_offset;
      }

      istate.next_bb_mem_blk = state_.mem_alloc.allocate(
          MemBlkType::CODE, pc_jmp, trampoline.jmp_range.first,
          trampoline.jmp_range.second, bb_sz, riscv_meta::instr_alignment);
      failed = !istate.next_bb_mem_blk.valid();

      if (failed) {
        if (is_jmp && !(eof_bb || empty_bb)) {
          // cannot generate a jump instruction to the target address
          // sine it's not the end of the basic block, give up
          goto give_up_jmp;
        }
        // try other trampoline types
        it = (it + 1) % trampoline_candidates_.size();
        if (it == preferred_trampoline_type) {
          spdlog::error("Cannot allocate memory for the next basic block");
          std::abort();
        }
        continue;
      }
      state_.mem_alloc.shrink(istate.next_bb_mem_blk, bb_sz);

      trampoline.emit(gen_warp, istate.next_bb_mem_blk.begin);
      break;
    }
    assert(!failed);
    istate.next_bb_snapshot =
        GenWarp::BasicBlockSnapshot(tgt_num_instrs, istate.next_bb_mem_blk);
    istate.next_bb_tmask = gen_warp->tmask();
    end_of_basic_block_(gen_warp, istate);
    return GenExitReason::EndOfBasicBlock;
  give_up_jmp:
    return GenExitReason::Retry;
  }

  gen_refresh_(gen_warp, istate.meta);

  // ################################################################
  // ######################## Top-level Loop ########################
  // ################################################################

  auto &instr = istate.instr;
  instr = riscv_instr(istate.meta);

  // randomize the instruction to assign register operands
  // note that the tgt_addr is not set here for branch/jump instructions
  if (!instr.meta()->randomize(state_, *gen_warp, &instr, {}, {}, false))
    return GenExitReason::Retry;

  //
  // Memory Operations
  //
  if (istate.meta->category == riscv_instr_category_t::LOAD ||
      istate.meta->category == riscv_instr_category_t::STORE ||
      istate.meta->category == riscv_instr_category_t::AMO) {
    if (!gen_load_store_addr_(gen_warp, instr, istate, true)) {
      return GenExitReason::Retry;
    }
    return GenExitReason::MemAddrGen;

    //
    // Fence
    //
  } else if (istate.meta->category == riscv_instr_category_t::SYNCH) {
    gen_fence_(gen_warp, instr);

    //
    // Branches
    //
  } else if (istate.meta->category == riscv_instr_category_t::BRANCH) {
    if (!gen_bcc_(gen_warp, instr)) {
      return GenExitReason::Retry;
    }

    //
    // Thread Ctrl
    //
  } else if (istate.meta->category == riscv_instr_category_t::THREAD_CTRL) {
    const auto ret =
        gen_thread_ctrl_(gen_warp, instr, istate.next_bb_mem_blk,
                         istate.next_bb_snapshot, istate.next_bb_tmask);
    if (ret == ThreadCtrlGenStatus::SwitchBasicBlock) {
      end_of_basic_block_(gen_warp, istate);
      return GenExitReason::EndOfBasicBlock; // end of the basic
                                             // block, no
                                             // trampoline needed
    } else if (ret == ThreadCtrlGenStatus::Skip) {
      return GenExitReason::Retry;
    } else {
      assert(ret == ThreadCtrlGenStatus::OK);
    }

    //
    // Thread Merge
    //
  } else if (istate.meta->category == riscv_instr_category_t::THREAD_MERGE) {
    if (!gen_thread_merge_(gen_warp, instr)) {
      return GenExitReason::Retry;
    }

    //
    // Tensor
    //
  } else if (istate.meta->category == riscv_instr_category_t::TENSOR) {
    if (!gen_tensor_core_(gen_warp, instr)) {
      return GenExitReason::Retry;
    }

#if defined(ARCH_VENTUS)
  } else if (istate.meta->category == riscv_instr_category_t::REG_CTRL) {
    return GenExitReason::Retry; // should not appear alone
                                 // without a paired instruction
#endif

    //
    // CSR
    //
  } else if (istate.meta->category == riscv_instr_category_t::CSR) {
    if (!gen_csr_(gen_warp, instr)) {
      return GenExitReason::Retry;
    }

    //
    // Others
    //
  } else {
    assert(istate.meta->category != riscv_instr_category_t::JUMP &&
           "jumps should have been handled before");

    // bug filter
    if (instr.meta()->is_floating_point()) {
      if ((instr.meta()->operand_mask & riscv_meta::HAS_RD) &&
          BugFilter::instance().filter("wb_fp_rd_zero") &&
          instr.rd() == riscv_reg_t::ZERO) {
        return GenExitReason::Retry;
      }
    }

#if defined(ARCH_VENTUS)
    gen_regext_(gen_warp, instr);
#endif

    //
    // Normally case:
    // non control-transfer instruction
    //
    gen_warp->scratchpad.emit(&instr);
  }

  return GenExitReason::Step;
}

void Generator::end_of_basic_block_(const std::shared_ptr<GenWarp> &gen_warp,
                                    InterleavedState &istate) {
  // recycle the tail memory of the previous basic block
  const size_t valid_sz =
      gen_warp->scratchpad.tell().begin - gen_warp->bb_snapshot.mem_blk.begin;
  state_.mem_alloc.shrink(gen_warp->bb_snapshot.mem_blk, valid_sz);
  gen_warp->scratchpad.shrink(gen_warp->bb_snapshot.mem_blk.end);

  // checking the end condition
  // Note that it cannot be immediately terminated even if the number of BB
  // exceeds the limit, since we may need to drain the SIMT stack or unsolved
  // barriers
  gen_warp->nr_gen_bb++;
  istate.terminating_warp |=
      state_.get_num_instrs() > Config::instance().max_num_instrs;

  spdlog::debug("*****************************");
  spdlog::debug("EOF basic block for warp GID {}", gen_warp->warp_gid());
  spdlog::debug(" num instrs: {}", gen_warp->bb_snapshot.num_target_instrs);
  if (!istate.warp_terminated) {
    spdlog::debug(" next bb mem blk: {:#x}", istate.next_bb_mem_blk.begin);
    spdlog::debug(" next bb utmask: {}", istate.next_bb_tmask);
    spdlog::debug(" next bb target instrs: {}",
                  istate.next_bb_snapshot.num_target_instrs);
    spdlog::debug(" next bb generated instrs: {}",
                  istate.next_bb_snapshot.num_gen_bb_instrs);
  }
  spdlog::debug("*****************************");

  if (!istate.warp_terminated) {
    assert(istate.next_bb_tmask.any());
    gen_warp->switch_basic_block(istate.next_bb_mem_blk,
                                 istate.next_bb_snapshot, istate.next_bb_tmask);
  } else {
    spdlog::debug("**** Warp GIDs {} terminated ****", gen_warp->warp_gid());
    ++state_.stat.num_generated_warps;
  }
}

void Generator::end_of_warp_(const std::shared_ptr<GenWarp> &gen_warp,
                             InterleavedState &istate) {
  spdlog::debug("**** Begin of dead basic blocks ****");
  for (const auto &[empty_bb, else_pc] : gen_warp->dead_basic_blocks) {
    gen_warp->scratchpad.seek(empty_bb);
#if 0 // verify the CFI
    riscv_instr nop(riscv_meta_list::instance().instr(riscv_instr_name_t::NOP));
    gen_warp->scratchpad.emit(&nop);
#else
    const auto pc_begin = gen_warp->scratchpad.tell().begin;
    size_t it = 0;
    bool failed;
    for (;;) {
      const auto &trampoline = trampoline_candidates_[it];
      const auto pc_jmp = pc_begin + trampoline.pc_jmp_offset;
      failed = !check_trampoline_range_(pc_jmp, trampoline.jmp_range, else_pc);
      if (failed) {
        // try other trampoline types
        it = (it + 1) % trampoline_candidates_.size();
        if (it == 0) {
          std::abort(); // this should not happen
        }
        continue;
      }
      // far jumps may clobber some regiters, but since we already at the
      // abnormal path (the dead code was actually executed) that was not
      // considered by the generator, it's reasonable to ignore them.
      trampoline.emit(gen_warp, else_pc);
      break;
    }
    assert(!failed);
#endif
  }
  spdlog::debug("**** End of dead basic blocks ****");
}

} // namespace fuzzgpu
