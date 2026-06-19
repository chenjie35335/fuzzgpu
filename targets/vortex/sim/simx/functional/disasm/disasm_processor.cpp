#include "disasm_processor.h"

reg_t illegal_instruction(processor_t *, insn_t insn, reg_t) {
  throw trap_illegal_instruction(insn.bits());
}