  switch (instr_name) {
    case riscv_instr_name_t::VADD12_VI:
      operand_mask = (HAS_VD | HAS_VS1 | HAS_IMM);
      break;
    case riscv_instr_name_t::VBEQ:
      operand_mask = (HAS_VS1 | HAS_VS2 | HAS_TGT_ADDR);
      break;
    case riscv_instr_name_t::VBGE:
      operand_mask = (HAS_VS1 | HAS_VS2 | HAS_TGT_ADDR);
      break;
    case riscv_instr_name_t::VBGEU:
      operand_mask = (HAS_VS1 | HAS_VS2 | HAS_TGT_ADDR);
      break;
    case riscv_instr_name_t::VBLT:
      operand_mask = (HAS_VS1 | HAS_VS2 | HAS_TGT_ADDR);
      break;
    case riscv_instr_name_t::VBLTU:
      operand_mask = (HAS_VS1 | HAS_VS2 | HAS_TGT_ADDR);
      break;
    case riscv_instr_name_t::VBNE:
      operand_mask = (HAS_VS1 | HAS_VS2 | HAS_TGT_ADDR);
      break;
    case riscv_instr_name_t::VFEXP_V:
      operand_mask = (HAS_VD | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VFTTA_VV:
      operand_mask = (HAS_VD | HAS_VS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VLB12_V:
      operand_mask = (HAS_VD | HAS_VS1 | HAS_IMM);
      break;
    case riscv_instr_name_t::VLB_V:
      operand_mask = (HAS_VD | HAS_VS1 | HAS_IMM);
      break;
    case riscv_instr_name_t::VLBU12_V:
      operand_mask = (HAS_VD | HAS_VS1 | HAS_IMM);
      break;
    case riscv_instr_name_t::VLBU_V:
      operand_mask = (HAS_VD | HAS_VS1 | HAS_IMM);
      break;
    case riscv_instr_name_t::VLH12_V:
      operand_mask = (HAS_VD | HAS_VS1 | HAS_IMM);
      break;
    case riscv_instr_name_t::VLH_V:
      operand_mask = (HAS_VD | HAS_VS1 | HAS_IMM);
      break;
    case riscv_instr_name_t::VLHU12_V:
      operand_mask = (HAS_VD | HAS_VS1 | HAS_IMM);
      break;
    case riscv_instr_name_t::VLHU_V:
      operand_mask = (HAS_VD | HAS_VS1 | HAS_IMM);
      break;
    case riscv_instr_name_t::VLW12_V:
      operand_mask = (HAS_VD | HAS_VS1 | HAS_IMM);
      break;
    case riscv_instr_name_t::VLW_V:
      operand_mask = (HAS_VD | HAS_VS1 | HAS_IMM);
      break;
    case riscv_instr_name_t::VSB12_V:
      operand_mask = (HAS_VS1 | HAS_VS2 | HAS_IMM);
      break;
    case riscv_instr_name_t::VSB_V:
      operand_mask = (HAS_VS1 | HAS_VS2 | HAS_IMM);
      break;
    case riscv_instr_name_t::VSH12_V:
      operand_mask = (HAS_VS1 | HAS_VS2 | HAS_IMM);
      break;
    case riscv_instr_name_t::VSH_V:
      operand_mask = (HAS_VS1 | HAS_VS2 | HAS_IMM);
      break;
    case riscv_instr_name_t::VSUB12_VI:
      operand_mask = (HAS_VD | HAS_VS1 | HAS_IMM);
      break;
    case riscv_instr_name_t::VSW12_V:
      operand_mask = (HAS_VS1 | HAS_VS2 | HAS_IMM);
      break;
    case riscv_instr_name_t::VSW_V:
      operand_mask = (HAS_VS1 | HAS_VS2 | HAS_IMM);
      break;
    case riscv_instr_name_t::JOIN:
      operand_mask = (0);
      break;
    case riscv_instr_name_t::BARRIER:
      operand_mask = (0);
      break;
    case riscv_instr_name_t::ENDPRG:
      operand_mask = (0);
      break;
    case riscv_instr_name_t::REGEXT:
      operand_mask = (HAS_IMM);
      break;
    case riscv_instr_name_t::SETRPC:
      operand_mask = (HAS_RS1 | HAS_IMM);
      break;
    default:
      break;
  }
switch (instr_name) {
    case riscv_instr_name_t::BARRIER:
      vm_hardcoded = true;
      vmasked = true;
      break;
    case riscv_instr_name_t::ENDPRG:
      vm_hardcoded = true;
      vmasked = true;
      break;
    default:
      break;
  }
