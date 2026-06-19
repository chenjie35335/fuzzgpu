#include <cassert>
#include <cstring>
#include <memory>
#include <unordered_map>
#include <unordered_set>

#include "workgroup.h"

#include "riscv/disasm.h"
#include "riscv/encoding.h"

#include <difftest_gpu/isa_emulator.hh>
#include <difftest_gpu/util/parse_ventus_opcode.hpp>
#include <difftest_gpu/util/riscv_fp.hh>

namespace difftest {

// TODO: read parameters from the chisel code
#define XLEN 32
#define XLEN_MASK 0xFFFFFFFF
#define NUM_THREADS 32
#define NUM_WARP 8 // in a block
#define NUMBER_SGPR_SLOTS (256 * NUM_WARP)
#define NUMBER_VGPR_SLOTS (128 * NUM_WARP)
#define NUM_SM 2
#define MMU_ENABLED false

#define GLOBAL_MEM_BASE 0x80000000
#define GLOBAL_MEM_END 0xFFFFFFFF
#define DATA_VADDR_START 0x70000000
#define DATA_VADDR_END 0xFFFFFFFF
#define CODE_VADDR_START GLOBAL_MEM_BASE
#define CODE_VADDR_END 0xFFFFFFFF
#define START_PC 0x80000000

#define LDS_SIZE 0x20000 // 128KB per workgroup (see RTL)

static const auto ilen = 4; // ventus has no compressed insn

class ISAEmulatorOpaque {
  std::unordered_map<uint32_t, std::unique_ptr<Workgroup>> wg;
  uint32_t num_workgroup;
  uint32_t num_warp{0}; // the same per workgroup

  std::unordered_map<uint64_t, bool> pc_set;
  size_t instr_count{0};

  friend class ISAEmulator;

public:
  ISAEmulatorOpaque() { wg.insert({0, std::make_unique<Workgroup>()}); }
  ~ISAEmulatorOpaque() { wg[0]->clear_buffer_data(); }

  int vt_buf_alloc(uint64_t size, uint64_t *vaddr, int BUF_TYPE,
                   uint64_t taskID, uint64_t kernelID) {
    if (size <= 0)
      return -1;
    int ret = 0;
    ret = wg[0]->alloc_local_mem(size, vaddr);
    return ret;
  }
  int vt_buf_free(uint64_t size, uint64_t *vaddr, uint64_t taskID,
                  uint64_t kernelID) {
    if (size <= 0)
      return -1;
    int ret = 0;
    ret = wg[0]->free_local_mem();
    return ret;
  }
  int vt_one_buf_free(uint64_t size, uint64_t *vaddr, uint64_t taskID,
                      uint64_t kernelID) {
    if (size <= 0)
      return -1;
    int ret = 0;
    ret = wg[0]->free_local_mem(*vaddr);
    return ret;
  }
  int vt_copy_to_dev(uint64_t dev_vaddr, const void *src_addr, uint64_t size,
                     uint64_t taskID, uint64_t kernelID) {
    if (size <= 0)
      return -1;
    int ret = 0;
    wg[0]->copy_to_local_mem(dev_vaddr, size, src_addr);
    return 0;
  }
  int vt_copy_to_const_mem(uint64_t dev_vaddr, const void *src_addr,
                           uint64_t size, uint64_t taskID, uint64_t kernelID) {
    if (size <= 0)
      return -1;
    int ret = 0;
    wg[0]->copy_to_const_mem(dev_vaddr, size, src_addr);
    return 0;
  }
  int copy_from_dev_mem(uint64_t dev_vaddr, void *dst_addr, uint64_t size,
                        uint64_t taskID, uint64_t kernelID) {
    if (size <= 0)
      return -1;
    int ret = 0;
    wg[0]->copy_from_local_mem(dst_addr, dev_vaddr, size);
    return ret;
  }

  int vt_start(void *metaData, uint64_t knl_start_pc, uint64_t taskID) {
    auto knl_data = (fg_meta_data *)metaData;
    num_workgroup = (knl_data->kernel_size[0]) * (knl_data->kernel_size[1]) *
                    (knl_data->kernel_size[2]);
    num_warp = knl_data->wg_size;

    for (uint32_t i = 0 + 1; i < 0 + num_workgroup; i++) {
      // to share global memory while not sharing the LDS per
      // workgroup
      wg.insert({i, std::make_unique<Workgroup>(*wg[0], false)});
    }
    for (uint32_t i = 0; i < 0 + num_workgroup; i++) {
      wg[i]->init_sim(knl_data, knl_start_pc, i - 0);
      assert(wg[i]->proc[0]->VU.VLEN >= 32 &&
             "you may need refactor data type in the struct Trace");

      // note that sim_t does not set PDS base addr for multiple workgroups,
      knl_data->pdsBaseAddr +=
          knl_data->wf_size * knl_data->wg_size * knl_data->pdsSize;
    }
    return 0;
  }

  uint32_t warp_gid_2_wg_id(WarpGID warp_gid) {
    assert(num_warp);
    return warp_gid / num_warp;
  }
  uint32_t warp_gid_2_warp_id(WarpGID warp_gid) {
    assert(num_warp);
    return warp_gid % num_warp;
  }
};

#define DECL_IMPL() auto impl = reinterpret_cast<ISAEmulatorOpaque *>(opaque_)

ISAEmulator::ISAEmulator()
  : opaque_(new ISAEmulatorOpaque()) {}

ISAEmulator::~ISAEmulator() {
  delete reinterpret_cast<ISAEmulatorOpaque *>(opaque_);
}

Arch ISAEmulator::get_arch() const {
  DECL_IMPL();

  return Arch("ventus", XLEN, true, GLOBAL_MEM_BASE, START_PC,
              std::make_pair(DATA_VADDR_START, DATA_VADDR_END),
              std::make_pair(CODE_VADDR_START, CODE_VADDR_END), 4096,
              NUMBER_SGPR_SLOTS / NUM_WARP, NUMBER_VGPR_SLOTS / NUM_WARP,
              NUM_WARP, NUM_SM, NUM_THREADS, MMU_ENABLED,
              Arch::ventus | Arch::rv32i | Arch::rv32a | Arch::rv32m |
                  Arch::rv32f | Arch::rvv,
              XLEN * NUM_THREADS, true, /*no hardware resource limit of num_barriers*/ unsigned(-1),
              /*mmio_regions*/ std::vector<std::pair<uint64_t, uint64_t>>{});
}

void ISAEmulator::bootstrap(uint64_t boot_vaddr, const uint32_t kernel_size[3],
                            unsigned num_wavefront_per_wavegroup,
                            unsigned wavefront_size, uint64_t sgpr_usage,
                            uint64_t vgpr_usage) {
  bootstrap(boot_vaddr, kernel_size, num_wavefront_per_wavegroup,
            wavefront_size, sgpr_usage, vgpr_usage,
            /*metadata_base=*/0, /*lds_size=*/LDS_SIZE, /*pds_size=*/0x1000,
            /*pds_base=*/0x78000000);
}

void ISAEmulator::bootstrap(uint64_t boot_vaddr, const uint32_t kernel_size[3],
                            unsigned num_wavefront_per_wavegroup,
                            unsigned wavefront_size, uint64_t sgpr_usage,
                            uint64_t vgpr_usage, uint64_t metadata_base,
                            uint64_t lds_size, uint64_t pds_size,
                            uint64_t pds_base) {
  DECL_IMPL();
  int err;

  // allocate global memory
  uint64_t gmem_vaddr;
  err = impl->vt_buf_alloc(GLOBAL_MEM_END - GLOBAL_MEM_BASE + 1, &gmem_vaddr, 0,
                           0, 0);
  if (err) {
    std::abort();
  }
  assert(gmem_vaddr == GLOBAL_MEM_BASE);

  fg_meta_data driver_meta;
  driver_meta.kernel_id = 0;
  driver_meta.kernel_size[0] = kernel_size[0];
  driver_meta.kernel_size[1] = kernel_size[1];
  driver_meta.kernel_size[2] = kernel_size[2];
  driver_meta.wf_size = wavefront_size;
  driver_meta.wg_size = num_wavefront_per_wavegroup; // per workgroup
  driver_meta.metaDataBaseAddr = metadata_base;
  driver_meta.ldsSize = lds_size;
  driver_meta.pdsSize = pds_size;
  driver_meta.sgprUsage = sgpr_usage;
  driver_meta.vgprUsage = vgpr_usage;
  driver_meta.pdsBaseAddr = pds_base;

  err = impl->vt_start(&driver_meta, boot_vaddr, 0);

  assert(0 == err);
}

void ISAEmulator::reset() {
  DECL_IMPL();
  impl->pc_set.clear();
  impl->instr_count = 0;
}

ThreadMask ISAEmulator::get_thread_mask(WarpGID warp_gid) const {
  DECL_IMPL();
  const auto sw_wg_id = impl->warp_gid_2_wg_id(warp_gid);
  const auto sw_warp_id = impl->warp_gid_2_warp_id(warp_gid);
  auto &wg = impl->wg[sw_wg_id];
  return ThreadMask(wg->num_thread,
                    wg->proc[sw_warp_id]->gpgpu_unit.simt_stack.get_mask());
}

bool ISAEmulator::terminated() const {
  DECL_IMPL();
  bool running = false;
  for (uint64_t wg_id = 0; wg_id < impl->num_workgroup; wg_id++) {
    for (auto &proc : impl->wg[wg_id]->proc) {
      for (uint64_t wid = 0; wid < impl->num_warp; wid++) {
        if (!proc->gpgpu_unit.w->get_warp_ended(wid)) {
          running = true;
          break;
        }
      }
      if (running)
        break;
    }
    if (running)
      break;
  }

  return !running;
}

bool ISAEmulator::schedulable(WarpGID warp_gid) {
  DECL_IMPL();
  const auto sw_wg_id = impl->warp_gid_2_wg_id(warp_gid);
  const auto sw_warp_id = impl->warp_gid_2_warp_id(warp_gid);

  auto &wg_sch = impl->wg[sw_wg_id]
                     ->proc[sw_warp_id]
                     ->gpgpu_unit.w; // sw_warp_id is useless here, to get the
                                     // pointer to the wg->workgroups[sw_wg_id]
  return !wg_sch->get_barrier_pending(sw_warp_id) &&
         !wg_sch->get_warp_ended(sw_warp_id);
}

/// @brief Schedule to execute a warp
/// @param core_gid
/// @param warp_lid
/// @param trace_out
void ISAEmulator::execute_warp(WarpGID warp_gid, difftest::Trace *trace_out) {
  DECL_IMPL();
  const auto sw_wg_id = impl->warp_gid_2_wg_id(warp_gid);
  const auto sw_warp_id = impl->warp_gid_2_warp_id(warp_gid);

  auto &p = impl->wg[sw_wg_id]->proc[sw_warp_id];

  const auto current_pc = p->get_state()->pc;
  trace_out->pc = (p->get_state()->pc &
                   XLEN_MASK); // spike uses 64 bits PC with sign-extension

  // omit the leading zeros of tmask
  trace_out->tmask = ThreadMask(
      impl->wg[sw_wg_id]->num_thread,
      impl->wg[sw_wg_id]->proc[sw_warp_id]->gpgpu_unit.simt_stack.get_mask());
  const auto full_tmask = ThreadMask(
      NUM_THREADS,
      impl->wg[sw_wg_id]->proc[sw_warp_id]->gpgpu_unit.simt_stack.get_mask());
  assert(trace_out->tmask == full_tmask && "isa emulator internal error");

  const auto vl = p->VU.vl->read();
  const auto vsew = p->VU.vsew;
  // execute one instruction
  impl->wg[sw_wg_id]->step(sw_warp_id);

  trace_out->next_pc = (p->get_state()->pc & XLEN_MASK);

  // omit the leading zeros of tmask
  trace_out->next_tmask = ThreadMask(
      impl->wg[sw_wg_id]->num_thread,
      impl->wg[sw_wg_id]->proc[sw_warp_id]->gpgpu_unit.simt_stack.get_mask());
  const auto full_next_tmask = ThreadMask(
      NUM_THREADS,
      impl->wg[sw_wg_id]->proc[sw_warp_id]->gpgpu_unit.simt_stack.get_mask());
  assert(trace_out->next_tmask == full_next_tmask &&
         "isa emulator internal error");

  int priv = p->get_state()->last_inst_priv;
  int xlen = p->get_state()->last_inst_xlen;
  int flen = p->get_state()->last_inst_flen;

  trace_out->regs.clear();

  // riscv have no such insns that write multiple registers in one scalar insn
  // if it does, it must be a vector insn (or a csrrw?).
  for (const auto &wbreg : p->get_state()->log_reg_write) {
    if (wbreg.first == 0)
      continue;

    int rd = wbreg.first >> 4; // note that this is the extended rd
    switch (wbreg.first & 0xf) {
    case 0: {
      trace_out->regs.emplace_back(impl->wg[sw_wg_id]->num_thread);
      auto &regs = trace_out->regs.back();
      regs.rd = RegId(RegType::Int, rd);
      regs.rd_data[0] = (wbreg.second.v[0] & XLEN_MASK);
      // note that a Int-rd insn may also belong to floating point insns
    } break;
    case 1: {
      trace_out->regs.emplace_back(impl->wg[sw_wg_id]->num_thread);
      auto &regs = trace_out->regs.back();
      regs.rd = RegId(RegType::Float, rd);
      regs.rd_data[0] = (wbreg.second.v[0] & XLEN_MASK); // F ext: 32 bits
    } break;
    case 2: {
      assert(p->VU.vl->read() == impl->wg[sw_wg_id]->num_thread);
      trace_out->regs.emplace_back(p->VU.vl->read());
      auto &regs = trace_out->regs.back();
      regs.rd = RegId(RegType::Vec, rd, vsew);
      if (rd == 24 && warp_gid == 3) {
        std::cout << "w v24 warp gid " << warp_gid << " pc " << std::hex
                  << trace_out->pc << std::dec << "\n";
      }
      switch (vsew) {
      case 8:
        for (uint32_t ii = 0; ii < vl; ++ii)
          regs.rd_data[ii] = p->VU.elt<uint8_t>(-1 /*not extend*/, rd, ii);
        break;
      case 16:
        for (uint32_t ii = 0; ii < vl; ++ii)
          regs.rd_data[ii] = p->VU.elt<uint16_t>(-1 /*not extend*/, rd, ii);
        break;
      case 32:
        for (uint32_t ii = 0; ii < vl; ++ii)
          regs.rd_data[ii] = p->VU.elt<uint32_t>(-1 /*not extend*/, rd, ii);
        break;
      case 64:
        for (uint32_t ii = 0; ii < vl; ++ii)
          regs.rd_data[ii] = p->VU.elt<uint64_t>(-1 /*not extend*/, rd, ii);
        break;
      default:
        std::abort();
      }
    } break;
    case 3:
      // vstatus write hint
      break;
    case 4:
      // csr write
      break;
    default:
      assert("can't been here" && 0);
      break;
    }
  }

  for (const auto &mt : p->get_state()->log_mem_write) {

    MemTrace mem_trace;
    mem_trace.addr = std::get<0>(mt) & XLEN_MASK;
    mem_trace.wdat = std::get<1>(mt) & XLEN_MASK;
    mem_trace.size = std::get<2>(mt);
    mem_trace.scope = get_mem_scope(warp_gid, mem_trace.addr);
    mem_trace.thread_lid = std::get<3>(mt);
/// DEBUGGING verbose prints
    if (1 || (mem_trace.addr <= 0x78005db4 &&
              0x78005db4 <= mem_trace.addr + mem_trace.size - 1)) {
      std::cout << "mem write 0x" << std::hex << mem_trace.addr << "<- 0x"
                << std::hex << mem_trace.wdat << " pc: 0x" << std::hex
                << trace_out->pc << std::dec << " size:" << mem_trace.size
                << "\n";
    }
/// DEBUGGING verbose prints
  }

/// DEBUGGING verbose prints
  static std::unordered_set<uint64_t> breakpoints = {0x98331f60};
  if (breakpoints.find(trace_out->pc) != breakpoints.end()) {
    std::cout << std::hex << "PC 0x" << trace_out->pc << std::dec << " ->";
    for (const auto &reg : trace_out->regs) {
      std::cout << " " << reg.rd.type_str() << reg.rd.idx << "={";
      for (const auto &d : reg.rd_data) {
        std::cout << " 0x" << std::hex << d;
      }
      std::cout << " }";
    }
    std::cout << std::dec << "\n";
    for (const auto &mt : p->get_state()->log_mem_write) {
      MemTrace mem_trace;
      mem_trace.addr = std::get<0>(mt) & XLEN_MASK;
      mem_trace.wdat = std::get<1>(mt) & XLEN_MASK;
      mem_trace.size = std::get<2>(mt);
      mem_trace.scope = get_mem_scope(warp_gid, mem_trace.addr);
      mem_trace.thread_lid = std::get<3>(mt);
      std::cout << "  MemW: addr=0x" << std::hex << mem_trace.addr << " wdat=0x"
                << mem_trace.wdat << " size=" << std::dec << mem_trace.size
                << " thread_lid=" << mem_trace.thread_lid << "\n";
    }
  }
  if (trace_out->pc == 0xd51e7024) {
    std::cout << "pc = " << std::hex << trace_out->pc << std::dec << "\n";
    std::cout << "x9 = " << " 0x" << std::hex << p->get_state()->XPR[9]
              << std::dec << "\n";
  }
/// DEBUGGING stuffs

  auto &insn = p->last_executed_instr;
  const auto opcode_info = parse_ventus_opcode(insn.bits());
  trace_out->opcode = opcode_info.opcode;
  trace_out->csr_addr = opcode_info.csr_addr;
  trace_out->applied_tmask = opcode_info.applied_tmask;

  trace_out->num_format =
      difftest::get_rd_float_point_format(insn.bits(), vsew);

  trace_out->trap_taken = p->get_state()->log_trap;

  trace_out->disasm = p->get_disassembler()->disassemble(insn);

  if (trace_out->trap_taken) {
    // currently do not support exception handling
    // as the exception raise, no GPR, MEM, or SIMT stack should be modified,
    // we just restore the PC
    trace_out->next_pc = current_pc + 4; // no compressed insn, so we're safe
    pc_write(warp_gid, trace_out->next_pc & XLEN_MASK);
  }

  trace_out->insn_bits = insn.bits();

  trace_out->dummy_wb = opcode_info.dummy_writeback;

  impl->pc_set[trace_out->pc] = true;
  impl->instr_count++;
}

ScopeIdentifier ISAEmulator::get_mem_scope(WarpGID warp_gid, uint64_t vaddr) {
  DECL_IMPL();
  const auto sw_wg_id = impl->warp_gid_2_wg_id(warp_gid);
  const auto sw_warp_id = impl->warp_gid_2_warp_id(warp_gid);
  auto &p = impl->wg[sw_wg_id]->proc[sw_warp_id];

  if (vaddr >= p->gpgpu_unit.w->lds_base &&
      vaddr < p->gpgpu_unit.w->lds_base + p->gpgpu_unit.w->lds_size) {
    return ScopeIdentifier(ScopeIdentifier::workgroup(), warp_gid);

  } else if (vaddr >= p->gpgpu_unit.w->pds_base &&
             vaddr < p->gpgpu_unit.w->pds_base + p->gpgpu_unit.w->pds_size) {
    return ScopeIdentifier(ScopeIdentifier::warp(), warp_gid);
  } else {
    return ScopeIdentifier(ScopeIdentifier::global());
  }
}

std::vector<MemTrace> ISAEmulator::emulate_load_store(WarpGID warp_gid,
                                                      const uint8_t *insn,
                                                      size_t insn_bytes,
                                                      bool &trap_taken) {
  DECL_IMPL();
  std::vector<MemTrace> mem_traces;
  const auto sw_wg_id = impl->warp_gid_2_wg_id(warp_gid);
  const auto sw_warp_id = impl->warp_gid_2_warp_id(warp_gid);
  auto &p = impl->wg[sw_wg_id]->proc[sw_warp_id];
  const auto old_pc = p->get_state()->pc;

  assert(!p->ext_valid() && "the regext prefix should be cleared before");

  size_t i;
  for (i = 0; i + ilen <= insn_bytes; i += ilen) {
    p->is_pinned_insn = true;
    p->trace_mem_only = true;
    p->pinned_insn = *reinterpret_cast<const uint32_t *>(
        insn + i); // for little-endian host only

    impl->wg[sw_wg_id]->step(sw_warp_id);
  }
  p->trace_mem_only = false;
  p->is_pinned_insn = false;
  assert(i == insn_bytes && "not all insns are executed");

  trap_taken = p->get_state()->log_trap;

  // clear the regext prefix since exception may happen and regext not applied
  if (trap_taken) {
    p->get_state()->regext_enable = false;
    p->ext_clear();
  }

  p->get_state()->pc = old_pc;

  // sanity check
  for (const auto &wbreg : p->get_state()->log_reg_write) {
    if (wbreg.first == 0)
      continue;
    int type = wbreg.first & 0xf;
    if (type == 3)
      continue;
    std::abort();
  }

  for (const auto &memw : p->get_state()->log_mem_write) {
    MemTrace mt;
    mt.is_store = true;
    mt.addr = std::get<0>(memw) & XLEN_MASK;
    mt.wdat = std::get<1>(memw) & XLEN_MASK;
    mt.size = std::get<2>(memw);
    mt.scope = get_mem_scope(warp_gid, mt.addr);
    mt.thread_lid = std::get<3>(memw);
#if 0
    std::cout << "thread_lid: " << mt.thread_lid << ", store addr: 0x"
              << std::hex << mt.addr << ", size: " << std::dec << mt.size
              << ", scope: " << static_cast<int>(mt.scope.scope()) << "\n";
#endif
    mem_traces.push_back(mt);
  }

  for (const auto &memr : p->get_state()->log_mem_read) {
    MemTrace mt;
    mt.is_store = false;
    mt.addr = std::get<0>(memr) & XLEN_MASK;
    mt.size = std::get<2>(memr);
    mt.scope = get_mem_scope(warp_gid, mt.addr);
    mt.thread_lid = std::get<3>(memr);
    mem_traces.push_back(mt);
#if 0
    std::cout << "thread_lid: " << mt.thread_lid << ", load addr: 0x"
              << std::hex << mt.addr << ", size: " << std::dec << mt.size
              << ", scope: " << static_cast<int>(mt.scope.scope()) << "\n";
#endif
  }

  return mem_traces;
}

const std::unordered_map<uint64_t, bool> &ISAEmulator::pc_executed() const {
  DECL_IMPL();
  return impl->pc_set;
}

size_t ISAEmulator::instr_executed() const {
  DECL_IMPL();
  return impl->instr_count;
}

void ISAEmulator::pmem_write(const ScopeIdentifier &scope, Word paddr,
                             const uint8_t *buf, size_t len) {

  DECL_IMPL();

  switch (scope.scope()) {
  case MemScope::Global:
  case MemScope::Warp: // the warp mem is physically per-workgroup, but
                       // partioned logically per-warp
    impl->vt_copy_to_dev(paddr, buf, len, 0, 0);
#if 0
    // FIXME: only flush the dirty TLB entry
    emu.tlb_shot_down();
#endif
    break;

  case MemScope::Workgroup: {
    std::abort();
  }

  default:
    assert(0);
  }
}

void ISAEmulator::pmem_read(const ScopeIdentifier &scope, Word paddr,
                            uint8_t *out, size_t len) {
  DECL_IMPL();
  switch (scope.scope()) {
  case MemScope::Global:
  case MemScope::Warp: // the warp mem is physically in global memory, but
                       // partitioned logically per-warp
    impl->copy_from_dev_mem(paddr, out, len, 0, 0);
    break;

  case MemScope::Workgroup: {
    const auto wg_id = impl->warp_gid_2_wg_id(scope.warp_gid());
    impl->wg[wg_id]->copy_from_const_mem(paddr, out, len);
    break;
  }

  default:
    assert(0);
  }
}

bool ISAEmulator::host_writeable(const ScopeIdentifier &scope) const {
  switch (scope.scope()) {
  case MemScope::Global:
  case MemScope::Warp: // the warp mem is physically in global memory, but
                       // partitioned logically per-warp
    return true;
  default:
    return false;
  }
}

bool ISAEmulator::illegal_paddr(uint64_t paddr, size_t access_size,
                                bool is_store) const {
  // no mmu version
  // FIXME: does LDS and PDS not using any address translation???
  return paddr < 0x70000000 || paddr >= GLOBAL_MEM_END - access_size;
}

uint64_t ISAEmulator::translate_vaddr(WarpGID, uint64_t ea, MemAccessType) {
  return ea;
}

static uint64_t sext(const uint64_t val, int bits) {
  assert(bits <= 64);
  const uint64_t mask = (1ULL << bits) - 1;
  const uint64_t sign_bit = 1ULL << (bits - 1);
  uint64_t res = val & mask;
  if (res & sign_bit) {
    res |= ~mask;
  }
  return res;
}

void ISAEmulator::reg_write(WarpGID warp_gid, const ThreadMask &tmask,
                            const RegId &rd,
                            const std::vector<uint64_t> &rd_data) {
  DECL_IMPL();
  const auto wg_id = impl->warp_gid_2_wg_id(warp_gid);
  const auto warp_id = impl->warp_gid_2_warp_id(warp_gid);
  auto &p = impl->wg[wg_id]->proc[warp_id];
  const auto vl = p->VU.vl->read();
  switch (rd.type) {
  case RegType::Int:
    p->get_state()->XPR.write(rd.idx, sext(rd_data[0], XLEN));
    break;
  case RegType::Float: {
    // F ext: 32bit float
    freg_t fv;
    fv.v[0] = rd_data[0];
#if XLEN == 32
    fv.v[0] |= 0xffffffff00000000ULL; // boxed fp32/16
#endif
    fv.v[1] = ~uint64_t(0); // boxed fp64, 32 or 16
    p->get_state()->FPR.write(rd.idx, fv);
    p->get_state()->XPR.write(rd.idx, fv.v[0]);
  } break;
  case RegType::Vec:
    // not require rd。idx aligned to vflmul here
    for (size_t t = 0; t < tmask.size(); ++t) {
      if (!tmask.test(t))
        continue;
      if (t > vl) {
        std::cout << "warning: write vreg element beyond vl: vl=" << vl
                  << ", tid=" << t << "\n";
        continue;
      }
      switch (rd.sew) {
      case 8:
        p->VU.elt<uint8_t>(-1 /* not extend*/, rd.idx, t) = rd_data[t];
        break;
      case 16:
        p->VU.elt<uint16_t>(-1 /*not extend*/, rd.idx, t) = rd_data[t];
        break;
      case 32:
        p->VU.elt<uint32_t>(-1 /*not extend*/, rd.idx, t) = rd_data[t];
        break;
      case 64:
        p->VU.elt<uint64_t>(-1 /*not extend*/, rd.idx, t) = rd_data[t];
        break;
      default:
        std::abort();
      }
    }
    break;
  default:
    std::abort();
  }
}

void ISAEmulator::pc_write(WarpGID warp_gid, uint64_t pc) {
  DECL_IMPL();
  const auto sw_wg_id = impl->warp_gid_2_wg_id(warp_gid);
  const auto sw_warp_id = impl->warp_gid_2_warp_id(warp_gid);
  auto &p = impl->wg[sw_wg_id]->proc[sw_warp_id];
  p->get_state()->pc = sext(pc, XLEN);
}

uint64_t ISAEmulator::reg_read(WarpGID warp_gid, ThreadLID thread_lid,
                               const RegId &rs) {
  DECL_IMPL();
  const auto wg_id = impl->warp_gid_2_wg_id(warp_gid);
  const auto warp_id = impl->warp_gid_2_warp_id(warp_gid);
  auto &p = impl->wg[wg_id]->proc[warp_id];
  switch (rs.type) {
  case RegType::Int:
    return (p->get_state()->XPR[rs.idx] & XLEN_MASK);
  case RegType::Float:
    // currently we only support F (means boxed 32bit float)
    return p->get_state()->FPR[rs.idx].v[0] & XLEN_MASK;
  case RegType::Vec: {
    // assert(rs.idx % p->VU.vflmul == 0 && "unaligned vreg idx");
    // assert (rs.sew == p->VU.vsew->read());
    // can randomly addressing
    assert(thread_lid <= p->VU.vl->read());
    switch (rs.sew) {
    case 8:
      return p->VU.elt<uint8_t>(-1 /*not extend*/, rs.idx, thread_lid);
    case 16:
      return p->VU.elt<uint16_t>(-1 /*not extend*/, rs.idx, thread_lid);
    case 32:
      return p->VU.elt<uint32_t>(-1 /*not extend*/, rs.idx, thread_lid);
    case 64:
      return p->VU.elt<uint64_t>(-1 /*not extend*/, rs.idx, thread_lid);
    default:
      std::abort();
    }
  } break;
  default:
    std::abort();
  }
}

void ISAEmulator::ventus_set_simt_stack_top_rpc(WarpGID warp_gid,
                                                uint64_t rpc) {
  DECL_IMPL();
  const auto sw_wg_id = impl->warp_gid_2_wg_id(warp_gid);
  const auto sw_warp_id = impl->warp_gid_2_warp_id(warp_gid);
  auto &p = impl->wg[sw_wg_id]->proc[sw_warp_id];
  p->gpgpu_unit.simt_stack.top().r_pc = sext(rpc, XLEN);
}
void ISAEmulator::ventus_set_simt_stack_top_else_pc(WarpGID warp_gid,
                                                    uint64_t else_pc) {
  DECL_IMPL();
  const auto sw_wg_id = impl->warp_gid_2_wg_id(warp_gid);
  const auto sw_warp_id = impl->warp_gid_2_warp_id(warp_gid);
  auto &p = impl->wg[sw_wg_id]->proc[sw_warp_id];
  assert(p->gpgpu_unit.simt_stack.top().else_pc ==
             p->gpgpu_unit.simt_stack.top().r_pc &&
         "not in the fallthrough path");
  p->gpgpu_unit.simt_stack.top().else_pc = sext(else_pc, XLEN);
}

void ISAEmulator::get_statistics(std::unordered_map<std::string, size_t> &stats) const {
  DECL_IMPL();

  // Aggregate statistics from all workgroups and warps
  uint64_t total_divergent_branches = 0;
  uint64_t total_branches = 0;
  size_t max_simt_stack_depth = 0;
  uint64_t total_local_barrier_success = 0;

  for (const auto &[wg_id, wg] : impl->wg) {
    for (const auto &proc : wg->proc) {
      total_divergent_branches += proc->gpgpu_unit.simt_stack.get_divergent_branches();
      total_branches += proc->gpgpu_unit.simt_stack.get_total_branches();
      auto depth = proc->gpgpu_unit.simt_stack.get_max_stack_depth();
      if (depth > max_simt_stack_depth) {
        max_simt_stack_depth = depth;
      }
    }
    // Get local barrier success count from the warp scheduler (shared per workgroup)
    // All procs in a workgroup share the same warp_schedule_t, so just get from first proc
    if (!wg->proc.empty()) {
      total_local_barrier_success += wg->proc[0]->gpgpu_unit.w->get_local_barrier_success_count();
    }
  }

  stats["thread_divergent_branches"] = total_divergent_branches;
  stats["thread_total_branches"] = total_branches;
  stats["max_simt_stack_depth"] = max_simt_stack_depth;
  stats["local_barrier_success_count"] = total_local_barrier_success;
  // Ventus-spike does not have global barrier mechanism
  stats["global_barrier_success_count"] = 0;
}

} // namespace difftest
