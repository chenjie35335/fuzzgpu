#include "spdlog/spdlog.h"

#include "types.h"

#include "constants.h"
#include "functional/core.h"
#include "instr_trace.h"
#include "stringutil.h"
#ifdef EXT_TCU_ENABLE
#include "tensor_cfg.h"
#endif

#include "functional/types_compat.h"

#include "functional/core.h"

#include "difftest_gpu/util/riscv_fp.hh"

namespace vortex {

Core::Core(unsigned core_id, Socket *socket, const Arch &arch,
           const DCRS &dcrs)
  : core_id_(core_id)
  , socket_(socket)
  , arch_(arch)
#ifdef EXT_TCU_ENABLE
  , tensor_unit_(TensorUnit::Create("tcu", arch, this))
#endif
  , emulator_(arch, dcrs, this)
  , mem_coalescers_(NUM_LSU_BLOCKS) {

  // create local memory
  local_mem_ = std::make_shared<LocalMem>(LocalMem::Config{
      (1 << LMEM_LOG_SIZE),
      LSU_WORD_SIZE});

  // create the memory coalescer (for perf counters only)
  for (uint32_t b = 0; b < NUM_LSU_BLOCKS; ++b) {
    mem_coalescers_.at(b) = std::make_shared<MemCoalescer>();
  }

  reset();
}

void Core::reset() {
  emulator_.reset();

  // set GPRs to deterministic initial values for the difftest
  ThreadMask tmask(arch_.num_threads());
  tmask.flip();
  std::vector<uint64_t> zeros(arch_.num_threads());

  for (uint32_t warp_id = 0; warp_id < arch_.num_warps(); ++warp_id) {
    for (uint32_t reg_id = 0; reg_id < MAX_NUM_REGS; ++reg_id) {
      emulator_.set_reg(warp_id, tmask, RegOpd{RegType::Integer, reg_id}, zeros);
      emulator_.set_reg(warp_id, tmask, RegOpd{RegType::Float, reg_id}, zeros);
      // TOOD support V ext
    }
    for (uint32_t thread_lid = 0; thread_lid < arch_.num_threads(); ++thread_lid) {
      // reset SPRs
      emulator_.set_csr(VX_CSR_FFLAGS, 0, warp_id, thread_lid);
      emulator_.set_csr(VX_CSR_FRM, 0, warp_id, thread_lid);
      emulator_.set_csr(VX_CSR_FCSR, 0, warp_id, thread_lid);
      emulator_.set_csr(VX_CSR_MSCRATCH, 0, warp_id, thread_lid);
      emulator_.set_csr(VX_CSR_SATP, 0, warp_id, thread_lid);
    }
  }

  // initialized all the allocated memory
  // already ensured by the class RAM
}

void Core::attach_ram(RAM *ram) {
  emulator_.attach_ram(ram);
}

void Core::resume(uint32_t wid) {
  emulator_.resume(wid);
}

bool Core::barrier(uint32_t bar_id, uint32_t count, uint32_t wid) {
  return emulator_.barrier(bar_id, count, wid);
}

static std::vector<difftest::Trace::RegWriteback> get_rd(Arch &arch_, Emulator &emulator_, instr_trace_t *trace) {
  std::vector<difftest::Trace::RegWriteback> rd(1, arch_.num_threads());

  rd[0].rd = difftest::make_reg_id(trace->dst_reg);

  // get the rdest data
  const auto &warp = emulator_.warp(trace->wid);
  const auto rdest_idx = trace->dst_reg.idx;
  switch (trace->dst_reg.type) {
  case RegType::Integer:
    for (uint32_t t = 0; t < arch_.num_threads(); ++t) {
      if (!trace->tmask.test(t)) {
        continue;
      }

      // FIXME: better debugger interface
#if 0 // a poor-man's break point and watch point
        auto addr_overlapped = [](Word target_addr, unsigned target_size, Word cur_addr, unsigned cur_size) {
          if (target_size == 0 || cur_size == 0) 
              return false;

          // Check if the range [target_addr, target_addr+target_size)
          // overlaps with      [cur_addr,    cur_addr+cur_size)
          return target_addr < cur_addr + cur_size
              && cur_addr    < target_addr + target_size;
        };

        if (std::get_if<LsuType>(&trace->op_type)) {
          auto lsu_type = std::get<LsuType>(trace->op_type);
          auto trace_data = std::dynamic_pointer_cast<LsuTraceData>(trace->data);
          if (trace_data) {
            const auto paddr = trace_data->mem_addrs[t].addr;
            const auto size = trace_data->mem_addrs[t].size;

            if (rdest_idx == 9) {
              std::cout << "!!!!! LSU " << lsu_type << " PC " << std::hex << trace->PC << " paddr=" << trace_data->mem_addrs[t].addr << std::dec << std::endl;
            }

            if (lsu_type == LsuType::STORE && addr_overlapped(0xfffeffe8, 4, paddr, size) ) {
              std::cout << "### PC " << std::hex << trace->PC << std::dec << std::endl;
            }
          }
        }

        if (rdest_idx == 9) {
          std::cout << "!!!!!! PC " << std::hex << trace->PC << std::dec << std::endl;
        }

#endif
      rd[0].rd_data[t] = warp.ireg_file.at(rdest_idx).at(t);
    }
    break;

  case RegType::Float:
    for (uint32_t t = 0; t < arch_.num_threads(); ++t) {
      if (!trace->tmask.test(t)) {
        continue;
      }

      auto fp_val = warp.freg_file.at(rdest_idx).at(t);
      if constexpr (sizeof(Word) == 4) {
        // FLEN = XLEN = 32 in the RTL
        fp_val &= 0xFFFFFFFF;
      }
      rd[0].rd_data[t] = fp_val;
    }
    break;

#ifdef EXT_V_ENABLE
  case RegType::Vector:
    for (uint32_t t = 0; t < _demu->nr_threads; ++t) {
      if (!trace->tmask.test(t)) {
        continue;
      }
      // FIXME! support V
    }
    break;
#endif

  default:
    break;
  }
  return rd;
}

void Core::execute_warp(unsigned warp_id, difftest::Trace *trace_out) {
  execute_warp_impl(warp_id, trace_out, false);
}

void Core::execute_warp_per_uop(unsigned warp_id, difftest::Trace *trace_out) {
  execute_warp_impl(warp_id, trace_out, true);
}

void Core::execute_warp_impl(unsigned warp_id, difftest::Trace *trace_out,
                             bool per_uop) {
  // warp schedule
  auto trace = per_uop ? emulator_.execute_warp_per_uop(warp_id)
                       : emulator_.execute_warp(warp_id);

  if (trace->fetch_stall) {
    emulator_.suspend(trace->wid);
  }

  // Handle FU stalls (ignore delays for our functional model)
  if (FUType::SFU == trace->fu_type) {
    bool release_warp = trace->fetch_stall;

    // warp control instructions
    if (std::get_if<WctlType>(&trace->op_type)) {
      auto wctl_type = std::get<WctlType>(trace->op_type);
      switch (wctl_type) {
      case WctlType::WSPAWN:
        if (trace->eop) {
          auto trace_data = std::dynamic_pointer_cast<SfuTraceData>(trace->data);
          release_warp = emulator_.wspawn(trace_data->arg1, trace_data->arg2);
        }
        break;
      case WctlType::TMC:
      case WctlType::SPLIT:
      case WctlType::JOIN:
      case WctlType::PRED:
        break;
      case WctlType::BAR: {
        if (trace->eop) {
          auto trace_data = std::dynamic_pointer_cast<SfuTraceData>(trace->data);
          release_warp = emulator_.barrier(trace_data->arg1, trace_data->arg2, trace->wid);
        }
      } break;
      default:
        std::abort();
      }
    }

    if (trace->eop && release_warp) {
      emulator_.resume(trace->wid);
    }

  } else if (FUType::ALU == trace->fu_type) {
    if (trace->eop && trace->fetch_stall) {
      emulator_.resume(trace->wid);
    }
  }

  trace_out->pc = trace->PC;
  trace_out->next_pc = emulator_.warp(warp_id).PC;
  trace_out->tmask = (difftest::ThreadMask)difftest::make_varbitset(trace->tmask);
  trace_out->next_tmask = (difftest::ThreadMask)difftest::make_varbitset(emulator_.get_thread_mask(warp_id));
  trace_out->regs.clear();
  if (trace->wb) {
    trace_out->regs = get_rd(arch_, emulator_, trace);
  } // if (wb)

  // initial values
  trace_out->opcode = difftest::Opcode::Others;

  if (std::get_if<BrType>(&trace->op_type)) {
    auto br_type = std::get<BrType>(trace->op_type);

    bool call = (trace->dst_reg.type == RegType::Integer && trace->dst_reg.idx == 1);        // ra
    bool ret = (trace->src_regs[0].type == RegType::Integer && trace->src_regs[0].idx == 1); // ra

    switch (br_type) {
    case BrType::JAL:
      if (call) {
        trace_out->opcode = difftest::Opcode::Call;
      }
      break;

    case BrType::JALR: {
      // FIXME: currently not considered the case that imm!=0
      // e.g., jalr x0, 64(ra)
      if (call && ret) {
        trace_out->opcode = difftest::Opcode::RetCall;
      } else if (call) {
        trace_out->opcode = difftest::Opcode::Call;
      } else if (ret) {
        trace_out->opcode = difftest::Opcode::Ret;
      }
    } break;

    default:
      break;
    }

  } else if (std::get_if<CsrType>(&trace->op_type)) {
    trace_out->csr_addr = trace->csr_addr;
    trace_out->opcode = difftest::Opcode::CtrlStatusRead;

  } else if (FUType::SFU == trace->fu_type) {
    // warp control instructions
    if (std::get_if<WctlType>(&trace->op_type)) {
      auto wctl_type = std::get<WctlType>(trace->op_type);
      if (wctl_type == WctlType::TMC || wctl_type == WctlType::BAR) {
        trace_out->opcode = difftest::Opcode::TmaskWrite;
      }
    }

  } else if (FUType::FPU == trace->fu_type ||
#ifdef EXT_TCU_ENABLE
             FUType::TCU == trace->fu_type ||
#endif
#ifdef EXT_V_ENABLE
#warning FIXME: handle VPU fp or int
#endif
             0) {
    trace_out->opcode = difftest::Opcode::FloatingPoint;
  }

  trace_out->num_format = difftest::get_rd_float_point_format(trace->insn_bits, XLEN);

  trace_out->trap_taken = false; // vortex currently does not support traps

  trace_out->applied_tmask = true;

  trace_out->dummy_wb = false;

  trace_out->insn_bits = trace->insn_bits;

  trace_out->disasm = trace->disasm;

  // delete the trace
  trace_pool().deallocate(trace, 1);
}

} // namespace vortex
