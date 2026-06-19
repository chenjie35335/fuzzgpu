  switch (instr_name) {
    case riscv_instr_name_t::VADD_VI:
      operand_mask = (HAS_VD | HAS_VS2 | HAS_IMM | HAS_VM);
      break;
    case riscv_instr_name_t::VADD_VV:
      operand_mask = (HAS_VD | HAS_VS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VADD_VX:
      operand_mask = (HAS_VD | HAS_RS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VAND_VI:
      operand_mask = (HAS_VD | HAS_VS2 | HAS_IMM | HAS_VM);
      break;
    case riscv_instr_name_t::VAND_VV:
      operand_mask = (HAS_VD | HAS_VS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VAND_VX:
      operand_mask = (HAS_VD | HAS_RS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VDIV_VV:
      operand_mask = (HAS_VD | HAS_VS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VDIV_VX:
      operand_mask = (HAS_VD | HAS_RS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VDIVU_VV:
      operand_mask = (HAS_VD | HAS_VS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VDIVU_VX:
      operand_mask = (HAS_VD | HAS_RS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VFADD_VF:
      operand_mask = (HAS_VD | HAS_RS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VFADD_VV:
      operand_mask = (HAS_VD | HAS_VS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VFCLASS_V:
      operand_mask = (HAS_VD | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VFCVT_F_X_V:
      operand_mask = (HAS_VD | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VFCVT_F_XU_V:
      operand_mask = (HAS_VD | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VFCVT_RTZ_X_F_V:
      operand_mask = (HAS_VD | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VFCVT_RTZ_XU_F_V:
      operand_mask = (HAS_VD | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VFCVT_X_F_V:
      operand_mask = (HAS_VD | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VFCVT_XU_F_V:
      operand_mask = (HAS_VD | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VFDIV_VF:
      operand_mask = (HAS_VD | HAS_RS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VFDIV_VV:
      operand_mask = (HAS_VD | HAS_VS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VFMACC_VF:
      operand_mask = (HAS_VD | HAS_RS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VFMACC_VV:
      operand_mask = (HAS_VD | HAS_VS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VFMADD_VF:
      operand_mask = (HAS_VD | HAS_RS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VFMADD_VV:
      operand_mask = (HAS_VD | HAS_VS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VFMAX_VF:
      operand_mask = (HAS_VD | HAS_RS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VFMAX_VV:
      operand_mask = (HAS_VD | HAS_VS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VFMIN_VF:
      operand_mask = (HAS_VD | HAS_RS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VFMIN_VV:
      operand_mask = (HAS_VD | HAS_VS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VFMSAC_VF:
      operand_mask = (HAS_VD | HAS_RS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VFMSAC_VV:
      operand_mask = (HAS_VD | HAS_VS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VFMSUB_VF:
      operand_mask = (HAS_VD | HAS_RS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VFMSUB_VV:
      operand_mask = (HAS_VD | HAS_VS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VFMUL_VF:
      operand_mask = (HAS_VD | HAS_RS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VFMUL_VV:
      operand_mask = (HAS_VD | HAS_VS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VFMV_F_S:
      operand_mask = (HAS_RD | HAS_VS2);
      break;
    case riscv_instr_name_t::VFMV_S_F:
      operand_mask = (HAS_VD | HAS_RS1);
      break;
    case riscv_instr_name_t::VFMV_V_F:
      operand_mask = (HAS_VD | HAS_RS1);
      break;
    case riscv_instr_name_t::VFNMACC_VF:
      operand_mask = (HAS_VD | HAS_RS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VFNMACC_VV:
      operand_mask = (HAS_VD | HAS_VS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VFNMADD_VF:
      operand_mask = (HAS_VD | HAS_RS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VFNMADD_VV:
      operand_mask = (HAS_VD | HAS_VS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VFNMSAC_VF:
      operand_mask = (HAS_VD | HAS_RS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VFNMSAC_VV:
      operand_mask = (HAS_VD | HAS_VS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VFNMSUB_VF:
      operand_mask = (HAS_VD | HAS_RS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VFNMSUB_VV:
      operand_mask = (HAS_VD | HAS_VS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VFRDIV_VF:
      operand_mask = (HAS_VD | HAS_RS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VFRSUB_VF:
      operand_mask = (HAS_VD | HAS_RS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VFSGNJ_VF:
      operand_mask = (HAS_VD | HAS_RS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VFSGNJ_VV:
      operand_mask = (HAS_VD | HAS_VS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VFSGNJN_VF:
      operand_mask = (HAS_VD | HAS_RS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VFSGNJN_VV:
      operand_mask = (HAS_VD | HAS_VS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VFSGNJX_VF:
      operand_mask = (HAS_VD | HAS_RS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VFSGNJX_VV:
      operand_mask = (HAS_VD | HAS_VS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VFSQRT_V:
      operand_mask = (HAS_VD | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VFSUB_VF:
      operand_mask = (HAS_VD | HAS_RS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VFSUB_VV:
      operand_mask = (HAS_VD | HAS_VS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VID_V:
      operand_mask = (HAS_VD | HAS_VM);
      break;
    case riscv_instr_name_t::VLE32_V:
      operand_mask = (HAS_VD | HAS_RS1 | HAS_VM | HAS_NF);
      nf_range = 2;
      break;
    case riscv_instr_name_t::VLOXEI32_V:
      operand_mask = (HAS_VD | HAS_RS1 | HAS_VS2 | HAS_VM | HAS_NF);
      nf_range = 2;
      break;
    case riscv_instr_name_t::VLSE32_V:
      operand_mask = (HAS_VD | HAS_RS1 | HAS_RS2 | HAS_VM | HAS_NF);
      nf_range = 2;
      break;
    case riscv_instr_name_t::VMACC_VV:
      operand_mask = (HAS_VD | HAS_VS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VMACC_VX:
      operand_mask = (HAS_VD | HAS_RS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VMADD_VV:
      operand_mask = (HAS_VD | HAS_VS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VMADD_VX:
      operand_mask = (HAS_VD | HAS_RS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VMAND_MM:
      operand_mask = (HAS_VD | HAS_VS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VMANDN_MM:
      operand_mask = (HAS_VD | HAS_VS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VMAX_VV:
      operand_mask = (HAS_VD | HAS_VS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VMAX_VX:
      operand_mask = (HAS_VD | HAS_RS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VMAXU_VV:
      operand_mask = (HAS_VD | HAS_VS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VMAXU_VX:
      operand_mask = (HAS_VD | HAS_RS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VMERGE_VIM:
      operand_mask = (HAS_VD | HAS_VS2 | HAS_IMM);
      break;
    case riscv_instr_name_t::VMERGE_VVM:
      operand_mask = (HAS_VD | HAS_VS1 | HAS_VS2);
      break;
    case riscv_instr_name_t::VMERGE_VXM:
      operand_mask = (HAS_VD | HAS_RS1 | HAS_VS2);
      break;
    case riscv_instr_name_t::VMFEQ_VF:
      operand_mask = (HAS_VD | HAS_RS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VMFEQ_VV:
      operand_mask = (HAS_VD | HAS_VS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VMFGE_VF:
      operand_mask = (HAS_VD | HAS_RS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VMFGT_VF:
      operand_mask = (HAS_VD | HAS_RS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VMFLE_VF:
      operand_mask = (HAS_VD | HAS_RS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VMFLE_VV:
      operand_mask = (HAS_VD | HAS_VS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VMFLT_VF:
      operand_mask = (HAS_VD | HAS_RS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VMFLT_VV:
      operand_mask = (HAS_VD | HAS_VS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VMFNE_VF:
      operand_mask = (HAS_VD | HAS_RS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VMFNE_VV:
      operand_mask = (HAS_VD | HAS_VS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VMIN_VV:
      operand_mask = (HAS_VD | HAS_VS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VMIN_VX:
      operand_mask = (HAS_VD | HAS_RS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VMINU_VV:
      operand_mask = (HAS_VD | HAS_VS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VMINU_VX:
      operand_mask = (HAS_VD | HAS_RS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VMNAND_MM:
      operand_mask = (HAS_VD | HAS_VS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VMNOR_MM:
      operand_mask = (HAS_VD | HAS_VS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VMOR_MM:
      operand_mask = (HAS_VD | HAS_VS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VMORN_MM:
      operand_mask = (HAS_VD | HAS_VS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VMSEQ_VI:
      operand_mask = (HAS_VD | HAS_VS2 | HAS_IMM | HAS_VM);
      break;
    case riscv_instr_name_t::VMSEQ_VV:
      operand_mask = (HAS_VD | HAS_VS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VMSEQ_VX:
      operand_mask = (HAS_VD | HAS_RS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VMSGT_VI:
      operand_mask = (HAS_VD | HAS_VS2 | HAS_IMM | HAS_VM);
      break;
    case riscv_instr_name_t::VMSGT_VX:
      operand_mask = (HAS_VD | HAS_RS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VMSGTU_VI:
      operand_mask = (HAS_VD | HAS_VS2 | HAS_IMM | HAS_VM);
      break;
    case riscv_instr_name_t::VMSGTU_VX:
      operand_mask = (HAS_VD | HAS_RS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VMSLE_VI:
      operand_mask = (HAS_VD | HAS_VS2 | HAS_IMM | HAS_VM);
      break;
    case riscv_instr_name_t::VMSLE_VV:
      operand_mask = (HAS_VD | HAS_VS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VMSLE_VX:
      operand_mask = (HAS_VD | HAS_RS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VMSLEU_VI:
      operand_mask = (HAS_VD | HAS_VS2 | HAS_IMM | HAS_VM);
      break;
    case riscv_instr_name_t::VMSLEU_VV:
      operand_mask = (HAS_VD | HAS_VS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VMSLEU_VX:
      operand_mask = (HAS_VD | HAS_RS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VMSLT_VV:
      operand_mask = (HAS_VD | HAS_VS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VMSLT_VX:
      operand_mask = (HAS_VD | HAS_RS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VMSLTU_VV:
      operand_mask = (HAS_VD | HAS_VS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VMSLTU_VX:
      operand_mask = (HAS_VD | HAS_RS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VMSNE_VI:
      operand_mask = (HAS_VD | HAS_VS2 | HAS_IMM | HAS_VM);
      break;
    case riscv_instr_name_t::VMSNE_VV:
      operand_mask = (HAS_VD | HAS_VS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VMSNE_VX:
      operand_mask = (HAS_VD | HAS_RS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VMUL_VV:
      operand_mask = (HAS_VD | HAS_VS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VMUL_VX:
      operand_mask = (HAS_VD | HAS_RS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VMULH_VV:
      operand_mask = (HAS_VD | HAS_VS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VMULH_VX:
      operand_mask = (HAS_VD | HAS_RS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VMULHSU_VV:
      operand_mask = (HAS_VD | HAS_VS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VMULHSU_VX:
      operand_mask = (HAS_VD | HAS_RS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VMULHU_VV:
      operand_mask = (HAS_VD | HAS_VS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VMULHU_VX:
      operand_mask = (HAS_VD | HAS_RS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VMV_S_X:
      operand_mask = (HAS_VD | HAS_RS1);
      break;
    case riscv_instr_name_t::VMV_V_I:
      operand_mask = (HAS_VD | HAS_IMM);
      break;
    case riscv_instr_name_t::VMV_V_V:
      operand_mask = (HAS_VD | HAS_VS1);
      break;
    case riscv_instr_name_t::VMV_V_X:
      operand_mask = (HAS_VD | HAS_RS1);
      break;
    case riscv_instr_name_t::VMV_X_S:
      operand_mask = (HAS_RD | HAS_VS2);
      break;
    case riscv_instr_name_t::VMXNOR_MM:
      operand_mask = (HAS_VD | HAS_VS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VMXOR_MM:
      operand_mask = (HAS_VD | HAS_VS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VNMSAC_VV:
      operand_mask = (HAS_VD | HAS_VS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VNMSAC_VX:
      operand_mask = (HAS_VD | HAS_RS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VNMSUB_VV:
      operand_mask = (HAS_VD | HAS_VS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VNMSUB_VX:
      operand_mask = (HAS_VD | HAS_RS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VOR_VI:
      operand_mask = (HAS_VD | HAS_VS2 | HAS_IMM | HAS_VM);
      break;
    case riscv_instr_name_t::VOR_VV:
      operand_mask = (HAS_VD | HAS_VS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VOR_VX:
      operand_mask = (HAS_VD | HAS_RS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VREM_VV:
      operand_mask = (HAS_VD | HAS_VS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VREM_VX:
      operand_mask = (HAS_VD | HAS_RS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VREMU_VV:
      operand_mask = (HAS_VD | HAS_VS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VREMU_VX:
      operand_mask = (HAS_VD | HAS_RS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VRSUB_VI:
      operand_mask = (HAS_VD | HAS_VS2 | HAS_IMM | HAS_VM);
      break;
    case riscv_instr_name_t::VRSUB_VX:
      operand_mask = (HAS_VD | HAS_RS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VSE32_V:
      operand_mask = (HAS_VS3 | HAS_RS1 | HAS_VM | HAS_NF);
      nf_range = 2;
      break;
    case riscv_instr_name_t::VSETIVLI:
      operand_mask = (HAS_RD | HAS_VS1 | HAS_IMM);
      break;
    case riscv_instr_name_t::VSETVL:
      operand_mask = (HAS_RD | HAS_RS1 | HAS_RS2);
      break;
    case riscv_instr_name_t::VSETVLI:
      operand_mask = (HAS_RD | HAS_RS1 | HAS_IMM);
      break;
    case riscv_instr_name_t::VSLL_VI:
      operand_mask = (HAS_VD | HAS_VS2 | HAS_IMM | HAS_VM);
      break;
    case riscv_instr_name_t::VSLL_VV:
      operand_mask = (HAS_VD | HAS_VS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VSLL_VX:
      operand_mask = (HAS_VD | HAS_RS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VSOXEI32_V:
      operand_mask = (HAS_VS3 | HAS_RS1 | HAS_VS2 | HAS_VM | HAS_NF);
      nf_range = 2;
      break;
    case riscv_instr_name_t::VSRA_VI:
      operand_mask = (HAS_VD | HAS_VS2 | HAS_IMM | HAS_VM);
      break;
    case riscv_instr_name_t::VSRA_VV:
      operand_mask = (HAS_VD | HAS_VS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VSRA_VX:
      operand_mask = (HAS_VD | HAS_RS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VSRL_VI:
      operand_mask = (HAS_VD | HAS_VS2 | HAS_IMM | HAS_VM);
      break;
    case riscv_instr_name_t::VSRL_VV:
      operand_mask = (HAS_VD | HAS_VS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VSRL_VX:
      operand_mask = (HAS_VD | HAS_RS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VSSE32_V:
      operand_mask = (HAS_VS3 | HAS_RS1 | HAS_RS2 | HAS_VM | HAS_NF);
      nf_range = 2;
      break;
    case riscv_instr_name_t::VSUB_VV:
      operand_mask = (HAS_VD | HAS_VS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VSUB_VX:
      operand_mask = (HAS_VD | HAS_RS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VXOR_VI:
      operand_mask = (HAS_VD | HAS_VS2 | HAS_IMM | HAS_VM);
      break;
    case riscv_instr_name_t::VXOR_VV:
      operand_mask = (HAS_VD | HAS_VS1 | HAS_VS2 | HAS_VM);
      break;
    case riscv_instr_name_t::VXOR_VX:
      operand_mask = (HAS_VD | HAS_RS1 | HAS_VS2 | HAS_VM);
      break;
    default:
      break;
  }
switch (instr_name) {
    case riscv_instr_name_t::VFMV_F_S:
      vm_hardcoded = true;
      vmasked = false;
      break;
    case riscv_instr_name_t::VFMV_S_F:
      vm_hardcoded = true;
      vmasked = false;
      break;
    case riscv_instr_name_t::VFMV_V_F:
      vm_hardcoded = true;
      vmasked = false;
      break;
    case riscv_instr_name_t::VMERGE_VIM:
      vm_hardcoded = true;
      vmasked = true;
      break;
    case riscv_instr_name_t::VMERGE_VVM:
      vm_hardcoded = true;
      vmasked = true;
      break;
    case riscv_instr_name_t::VMERGE_VXM:
      vm_hardcoded = true;
      vmasked = true;
      break;
    case riscv_instr_name_t::VMV_S_X:
      vm_hardcoded = true;
      vmasked = false;
      break;
    case riscv_instr_name_t::VMV_V_I:
      vm_hardcoded = true;
      vmasked = false;
      break;
    case riscv_instr_name_t::VMV_V_V:
      vm_hardcoded = true;
      vmasked = false;
      break;
    case riscv_instr_name_t::VMV_V_X:
      vm_hardcoded = true;
      vmasked = false;
      break;
    case riscv_instr_name_t::VMV_X_S:
      vm_hardcoded = true;
      vmasked = false;
      break;
    case riscv_instr_name_t::VSETVL:
      vm_hardcoded = true;
      vmasked = true;
      break;
    default:
      break;
  }
