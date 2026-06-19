#pragma once

#include "decode.h"
#include "trap.h"
struct processor_t;
typedef reg_t (*insn_func_t)(processor_t*, insn_t, reg_t);

reg_t illegal_instruction(processor_t* p, insn_t insn, reg_t pc);

struct insn_desc_t  //mask
{
  insn_bits_t match;
  insn_bits_t mask;
  insn_func_t rv32i;
  insn_func_t rv64i;
  insn_func_t rv32e;
  insn_func_t rv64e;

  insn_func_t func(int xlen, bool rve)
  {
    if (rve)
      return xlen == 64 ? rv64e : rv32e;
    else
      return xlen == 64 ? rv64i : rv32i;
  }

  static insn_desc_t illegal()
  {
    return {0, 0, &illegal_instruction, &illegal_instruction, &illegal_instruction, &illegal_instruction};
  }
};
