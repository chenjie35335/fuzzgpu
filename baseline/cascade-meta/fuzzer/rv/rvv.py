# Copyright 2023 Flavien Solt, ETH Zurich.
# Licensed under the General Public License, Version 3.0, see LICENSE for details.
# SPDX-License-Identifier: GPL-3.0-only

import rv.rvprotoinstrs as rvprotoinstrs

RVV_OPCODE = 0b1010111
RVV_LOAD_OPCODE = 0b0000111
RVV_STORE_OPCODE = 0b0100111

# Helper for OPIVV, OPIVX, OPIVI, OPFVV, OPFVF, OPMVV, OPMVX
def _rvv_rtype(vd, funct3, rs1_or_vs1_or_imm, vs2, funct6, vm):
    return rvprotoinstrs.instruc_rtype(RVV_OPCODE, vd, funct3, rs1_or_vs1_or_imm, vs2, (funct6 << 1) | vm)

# Arithmetic / Logical / Compare

def rvv_vadd_vi(vd, imm, vs2, vm=1): return _rvv_rtype(vd, 0b011, imm & 0b11111, vs2, 0b000000, vm)
def rvv_vadd_vv(vd, vs1, vs2, vm=1): return _rvv_rtype(vd, 0b000, vs1, vs2, 0b000000, vm)
def rvv_vadd_vx(vd, rs1, vs2, vm=1): return _rvv_rtype(vd, 0b100, rs1, vs2, 0b000000, vm)

def rvv_vand_vi(vd, imm, vs2, vm=1): return _rvv_rtype(vd, 0b011, imm & 0b11111, vs2, 0b001001, vm)
def rvv_vand_vv(vd, vs1, vs2, vm=1): return _rvv_rtype(vd, 0b000, vs1, vs2, 0b001001, vm)
def rvv_vand_vx(vd, rs1, vs2, vm=1): return _rvv_rtype(vd, 0b100, rs1, vs2, 0b001001, vm)

def rvv_vdiv_vv(vd, vs1, vs2, vm=1): return _rvv_rtype(vd, 0b010, vs1, vs2, 0b100001, vm)
def rvv_vdiv_vx(vd, rs1, vs2, vm=1): return _rvv_rtype(vd, 0b110, rs1, vs2, 0b100001, vm)

def rvv_vdivu_vv(vd, vs1, vs2, vm=1): return _rvv_rtype(vd, 0b010, vs1, vs2, 0b100000, vm)
def rvv_vdivu_vx(vd, rs1, vs2, vm=1): return _rvv_rtype(vd, 0b110, rs1, vs2, 0b100000, vm)

def rvv_vfadd_vf(vd, rs1, vs2, vm=1): return _rvv_rtype(vd, 0b101, rs1, vs2, 0b000000, vm)
def rvv_vfadd_vv(vd, vs1, vs2, vm=1): return _rvv_rtype(vd, 0b001, vs1, vs2, 0b000000, vm)

def rvv_vfclass_v(vd, vs2, vm=1): return _rvv_rtype(vd, 0b001, 0b10000, vs2, 0b010011, vm)

def rvv_vfcvt_f_x_v(vd, vs2, vm=1): return _rvv_rtype(vd, 0b001, 0b00011, vs2, 0b010010, vm)
def rvv_vfcvt_f_xu_v(vd, vs2, vm=1): return _rvv_rtype(vd, 0b001, 0b00010, vs2, 0b010010, vm)
def rvv_vfcvt_rtz_x_f_v(vd, vs2, vm=1): return _rvv_rtype(vd, 0b001, 0b00111, vs2, 0b010010, vm)
def rvv_vfcvt_rtz_xu_f_v(vd, vs2, vm=1): return _rvv_rtype(vd, 0b001, 0b00110, vs2, 0b010010, vm)
def rvv_vfcvt_x_f_v(vd, vs2, vm=1): return _rvv_rtype(vd, 0b001, 0b00001, vs2, 0b010010, vm)
def rvv_vfcvt_xu_f_v(vd, vs2, vm=1): return _rvv_rtype(vd, 0b001, 0b00000, vs2, 0b010010, vm)

def rvv_vfdiv_vf(vd, rs1, vs2, vm=1): return _rvv_rtype(vd, 0b101, rs1, vs2, 0b100000, vm)
def rvv_vfdiv_vv(vd, vs1, vs2, vm=1): return _rvv_rtype(vd, 0b001, vs1, vs2, 0b100000, vm)

def rvv_vfmacc_vf(vd, rs1, vs2, vm=1): return _rvv_rtype(vd, 0b101, rs1, vs2, 0b101100, vm)
def rvv_vfmacc_vv(vd, vs1, vs2, vm=1): return _rvv_rtype(vd, 0b001, vs1, vs2, 0b101100, vm)

def rvv_vfmadd_vf(vd, rs1, vs2, vm=1): return _rvv_rtype(vd, 0b101, rs1, vs2, 0b101000, vm)
def rvv_vfmadd_vv(vd, vs1, vs2, vm=1): return _rvv_rtype(vd, 0b001, vs1, vs2, 0b101000, vm)

def rvv_vfmax_vf(vd, rs1, vs2, vm=1): return _rvv_rtype(vd, 0b101, rs1, vs2, 0b000110, vm)
def rvv_vfmax_vv(vd, vs1, vs2, vm=1): return _rvv_rtype(vd, 0b001, vs1, vs2, 0b000110, vm)

def rvv_vfmin_vf(vd, rs1, vs2, vm=1): return _rvv_rtype(vd, 0b101, rs1, vs2, 0b000100, vm)
def rvv_vfmin_vv(vd, vs1, vs2, vm=1): return _rvv_rtype(vd, 0b001, vs1, vs2, 0b000100, vm)

def rvv_vfmsac_vf(vd, rs1, vs2, vm=1): return _rvv_rtype(vd, 0b101, rs1, vs2, 0b101110, vm)
def rvv_vfmsac_vv(vd, vs1, vs2, vm=1): return _rvv_rtype(vd, 0b001, vs1, vs2, 0b101110, vm)

def rvv_vfmsub_vf(vd, rs1, vs2, vm=1): return _rvv_rtype(vd, 0b101, rs1, vs2, 0b101010, vm)
def rvv_vfmsub_vv(vd, vs1, vs2, vm=1): return _rvv_rtype(vd, 0b001, vs1, vs2, 0b101010, vm)

def rvv_vfmul_vf(vd, rs1, vs2, vm=1): return _rvv_rtype(vd, 0b101, rs1, vs2, 0b100100, vm)
def rvv_vfmul_vv(vd, vs1, vs2, vm=1): return _rvv_rtype(vd, 0b001, vs1, vs2, 0b100100, vm)

def rvv_vfmv_f_s(rd, vs2): return _rvv_rtype(rd, 0b001, 0b00000, vs2, 0b010000, 1)
def rvv_vfmv_s_f(vd, rs1): return _rvv_rtype(vd, 0b101, rs1, 0b00000, 0b010000, 1)
def rvv_vfmv_v_f(vd, rs1): return _rvv_rtype(vd, 0b101, rs1, 0b00000, 0b010111, 1)

def rvv_vfnmacc_vf(vd, rs1, vs2, vm=1): return _rvv_rtype(vd, 0b101, rs1, vs2, 0b101101, vm)
def rvv_vfnmacc_vv(vd, vs1, vs2, vm=1): return _rvv_rtype(vd, 0b001, vs1, vs2, 0b101101, vm)

def rvv_vfnmadd_vf(vd, rs1, vs2, vm=1): return _rvv_rtype(vd, 0b101, rs1, vs2, 0b101001, vm)
def rvv_vfnmadd_vv(vd, vs1, vs2, vm=1): return _rvv_rtype(vd, 0b001, vs1, vs2, 0b101001, vm)

def rvv_vfnmsac_vf(vd, rs1, vs2, vm=1): return _rvv_rtype(vd, 0b101, rs1, vs2, 0b101111, vm)
def rvv_vfnmsac_vv(vd, vs1, vs2, vm=1): return _rvv_rtype(vd, 0b001, vs1, vs2, 0b101111, vm)

def rvv_vfnmsub_vf(vd, rs1, vs2, vm=1): return _rvv_rtype(vd, 0b101, rs1, vs2, 0b101011, vm)
def rvv_vfnmsub_vv(vd, vs1, vs2, vm=1): return _rvv_rtype(vd, 0b001, vs1, vs2, 0b101011, vm)

def rvv_vfrdiv_vf(vd, rs1, vs2, vm=1): return _rvv_rtype(vd, 0b101, rs1, vs2, 0b100001, vm)
def rvv_vfrsub_vf(vd, rs1, vs2, vm=1): return _rvv_rtype(vd, 0b101, rs1, vs2, 0b100111, vm)

def rvv_vfsgnj_vf(vd, rs1, vs2, vm=1): return _rvv_rtype(vd, 0b101, rs1, vs2, 0b001000, vm)
def rvv_vfsgnj_vv(vd, vs1, vs2, vm=1): return _rvv_rtype(vd, 0b001, vs1, vs2, 0b001000, vm)

def rvv_vfsgnjn_vf(vd, rs1, vs2, vm=1): return _rvv_rtype(vd, 0b101, rs1, vs2, 0b001001, vm)
def rvv_vfsgnjn_vv(vd, vs1, vs2, vm=1): return _rvv_rtype(vd, 0b001, vs1, vs2, 0b001001, vm)

def rvv_vfsgnjx_vf(vd, rs1, vs2, vm=1): return _rvv_rtype(vd, 0b101, rs1, vs2, 0b001010, vm)
def rvv_vfsgnjx_vv(vd, vs1, vs2, vm=1): return _rvv_rtype(vd, 0b001, vs1, vs2, 0b001010, vm)

def rvv_vfsqrt_v(vd, vs2, vm=1): return _rvv_rtype(vd, 0b001, 0b00000, vs2, 0b010011, vm)

def rvv_vfsub_vf(vd, rs1, vs2, vm=1): return _rvv_rtype(vd, 0b101, rs1, vs2, 0b000010, vm)
def rvv_vfsub_vv(vd, vs1, vs2, vm=1): return _rvv_rtype(vd, 0b001, vs1, vs2, 0b000010, vm)

def rvv_vid_v(vd, vm=1): return _rvv_rtype(vd, 0b010, 0b10001, 0b00000, 0b010100, vm)

def rvv_vmacc_vv(vd, vs1, vs2, vm=1): return _rvv_rtype(vd, 0b010, vs1, vs2, 0b101101, vm)
def rvv_vmacc_vx(vd, rs1, vs2, vm=1): return _rvv_rtype(vd, 0b110, rs1, vs2, 0b101101, vm)

def rvv_vmadd_vv(vd, vs1, vs2, vm=1): return _rvv_rtype(vd, 0b010, vs1, vs2, 0b101001, vm)
def rvv_vmadd_vx(vd, rs1, vs2, vm=1): return _rvv_rtype(vd, 0b110, rs1, vs2, 0b101001, vm)

def rvv_vmand_mm(vd, vs1, vs2, vm=1): return _rvv_rtype(vd, 0b010, vs1, vs2, 0b011001, vm)
def rvv_vmandn_mm(vd, vs1, vs2, vm=1): return _rvv_rtype(vd, 0b010, vs1, vs2, 0b011000, vm)

def rvv_vmax_vv(vd, vs1, vs2, vm=1): return _rvv_rtype(vd, 0b000, vs1, vs2, 0b000111, vm)
def rvv_vmax_vx(vd, rs1, vs2, vm=1): return _rvv_rtype(vd, 0b100, rs1, vs2, 0b000111, vm)

def rvv_vmaxu_vv(vd, vs1, vs2, vm=1): return _rvv_rtype(vd, 0b000, vs1, vs2, 0b000110, vm)
def rvv_vmaxu_vx(vd, rs1, vs2, vm=1): return _rvv_rtype(vd, 0b100, rs1, vs2, 0b000110, vm)

def rvv_vmerge_vim(vd, imm, vs2): return _rvv_rtype(vd, 0b011, imm & 0b11111, vs2, 0b010111, 0)
def rvv_vmerge_vvm(vd, vs1, vs2): return _rvv_rtype(vd, 0b000, vs1, vs2, 0b010111, 0)
def rvv_vmerge_vxm(vd, rs1, vs2): return _rvv_rtype(vd, 0b100, rs1, vs2, 0b010111, 0)

def rvv_vmfeq_vf(vd, rs1, vs2, vm=1): return _rvv_rtype(vd, 0b101, rs1, vs2, 0b011000, vm)
def rvv_vmfeq_vv(vd, vs1, vs2, vm=1): return _rvv_rtype(vd, 0b001, vs1, vs2, 0b011000, vm)

def rvv_vmfge_vf(vd, rs1, vs2, vm=1): return _rvv_rtype(vd, 0b101, rs1, vs2, 0b011111, vm)
def rvv_vmfgt_vf(vd, rs1, vs2, vm=1): return _rvv_rtype(vd, 0b101, rs1, vs2, 0b011101, vm)

def rvv_vmfle_vf(vd, rs1, vs2, vm=1): return _rvv_rtype(vd, 0b101, rs1, vs2, 0b011001, vm)
def rvv_vmfle_vv(vd, vs1, vs2, vm=1): return _rvv_rtype(vd, 0b001, vs1, vs2, 0b011001, vm)

def rvv_vmflt_vf(vd, rs1, vs2, vm=1): return _rvv_rtype(vd, 0b101, rs1, vs2, 0b011011, vm)
def rvv_vmflt_vv(vd, vs1, vs2, vm=1): return _rvv_rtype(vd, 0b001, vs1, vs2, 0b011011, vm)

def rvv_vmfne_vf(vd, rs1, vs2, vm=1): return _rvv_rtype(vd, 0b101, rs1, vs2, 0b011100, vm)
def rvv_vmfne_vv(vd, vs1, vs2, vm=1): return _rvv_rtype(vd, 0b001, vs1, vs2, 0b011100, vm)

def rvv_vmin_vv(vd, vs1, vs2, vm=1): return _rvv_rtype(vd, 0b000, vs1, vs2, 0b000101, vm)
def rvv_vmin_vx(vd, rs1, vs2, vm=1): return _rvv_rtype(vd, 0b100, rs1, vs2, 0b000101, vm)

def rvv_vminu_vv(vd, vs1, vs2, vm=1): return _rvv_rtype(vd, 0b000, vs1, vs2, 0b000100, vm)
def rvv_vminu_vx(vd, rs1, vs2, vm=1): return _rvv_rtype(vd, 0b100, rs1, vs2, 0b000100, vm)

def rvv_vmnand_mm(vd, vs1, vs2, vm=1): return _rvv_rtype(vd, 0b010, vs1, vs2, 0b011101, vm)
def rvv_vmnor_mm(vd, vs1, vs2, vm=1): return _rvv_rtype(vd, 0b010, vs1, vs2, 0b011110, vm)
def rvv_vmor_mm(vd, vs1, vs2, vm=1): return _rvv_rtype(vd, 0b010, vs1, vs2, 0b011010, vm)
def rvv_vmorn_mm(vd, vs1, vs2, vm=1): return _rvv_rtype(vd, 0b010, vs1, vs2, 0b011100, vm)

def rvv_vmseq_vi(vd, imm, vs2, vm=1): return _rvv_rtype(vd, 0b011, imm & 0b11111, vs2, 0b011000, vm)
def rvv_vmseq_vv(vd, vs1, vs2, vm=1): return _rvv_rtype(vd, 0b000, vs1, vs2, 0b011000, vm)
def rvv_vmseq_vx(vd, rs1, vs2, vm=1): return _rvv_rtype(vd, 0b100, rs1, vs2, 0b011000, vm)

def rvv_vmsgt_vi(vd, imm, vs2, vm=1): return _rvv_rtype(vd, 0b011, imm & 0b11111, vs2, 0b011111, vm)
def rvv_vmsgt_vx(vd, rs1, vs2, vm=1): return _rvv_rtype(vd, 0b100, rs1, vs2, 0b011111, vm)

def rvv_vmsgtu_vi(vd, imm, vs2, vm=1): return _rvv_rtype(vd, 0b011, imm & 0b11111, vs2, 0b011110, vm)
def rvv_vmsgtu_vx(vd, rs1, vs2, vm=1): return _rvv_rtype(vd, 0b100, rs1, vs2, 0b011110, vm)

def rvv_vmsle_vi(vd, imm, vs2, vm=1): return _rvv_rtype(vd, 0b011, imm & 0b11111, vs2, 0b011101, vm)
def rvv_vmsle_vv(vd, vs1, vs2, vm=1): return _rvv_rtype(vd, 0b000, vs1, vs2, 0b011101, vm)
def rvv_vmsle_vx(vd, rs1, vs2, vm=1): return _rvv_rtype(vd, 0b100, rs1, vs2, 0b011101, vm)

def rvv_vmsleu_vi(vd, imm, vs2, vm=1): return _rvv_rtype(vd, 0b011, imm & 0b11111, vs2, 0b011100, vm)
def rvv_vmsleu_vv(vd, vs1, vs2, vm=1): return _rvv_rtype(vd, 0b000, vs1, vs2, 0b011100, vm)
def rvv_vmsleu_vx(vd, rs1, vs2, vm=1): return _rvv_rtype(vd, 0b100, rs1, vs2, 0b011100, vm)

def rvv_vmslt_vv(vd, vs1, vs2, vm=1): return _rvv_rtype(vd, 0b000, vs1, vs2, 0b011011, vm)
def rvv_vmslt_vx(vd, rs1, vs2, vm=1): return _rvv_rtype(vd, 0b100, rs1, vs2, 0b011011, vm)

def rvv_vmsltu_vv(vd, vs1, vs2, vm=1): return _rvv_rtype(vd, 0b000, vs1, vs2, 0b011010, vm)
def rvv_vmsltu_vx(vd, rs1, vs2, vm=1): return _rvv_rtype(vd, 0b100, rs1, vs2, 0b011010, vm)

def rvv_vmsne_vi(vd, imm, vs2, vm=1): return _rvv_rtype(vd, 0b011, imm & 0b11111, vs2, 0b011001, vm)
def rvv_vmsne_vv(vd, vs1, vs2, vm=1): return _rvv_rtype(vd, 0b000, vs1, vs2, 0b011001, vm)
def rvv_vmsne_vx(vd, rs1, vs2, vm=1): return _rvv_rtype(vd, 0b100, rs1, vs2, 0b011001, vm)

def rvv_vmul_vv(vd, vs1, vs2, vm=1): return _rvv_rtype(vd, 0b010, vs1, vs2, 0b100101, vm)
def rvv_vmul_vx(vd, rs1, vs2, vm=1): return _rvv_rtype(vd, 0b110, rs1, vs2, 0b100101, vm)

def rvv_vmulh_vv(vd, vs1, vs2, vm=1): return _rvv_rtype(vd, 0b010, vs1, vs2, 0b100111, vm)
def rvv_vmulh_vx(vd, rs1, vs2, vm=1): return _rvv_rtype(vd, 0b110, rs1, vs2, 0b100111, vm)

def rvv_vmulhsu_vv(vd, vs1, vs2, vm=1): return _rvv_rtype(vd, 0b010, vs1, vs2, 0b100110, vm)
def rvv_vmulhsu_vx(vd, rs1, vs2, vm=1): return _rvv_rtype(vd, 0b110, rs1, vs2, 0b100110, vm)

def rvv_vmulhu_vv(vd, vs1, vs2, vm=1): return _rvv_rtype(vd, 0b010, vs1, vs2, 0b100100, vm)
def rvv_vmulhu_vx(vd, rs1, vs2, vm=1): return _rvv_rtype(vd, 0b110, rs1, vs2, 0b100100, vm)

def rvv_vmv_s_x(vd, rs1): return _rvv_rtype(vd, 0b110, rs1, 0b00000, 0b010000, 1)
def rvv_vmv_v_i(vd, imm): return _rvv_rtype(vd, 0b011, imm & 0b11111, 0b00000, 0b010111, 1)
def rvv_vmv_v_v(vd, vs1): return _rvv_rtype(vd, 0b000, vs1, 0b00000, 0b010111, 1)
def rvv_vmv_v_x(vd, rs1): return _rvv_rtype(vd, 0b100, rs1, 0b00000, 0b010111, 1)
def rvv_vmv_x_s(rd, vs2): return _rvv_rtype(rd, 0b010, 0b00000, vs2, 0b010000, 1)

def rvv_vmxnor_mm(vd, vs1, vs2, vm=1): return _rvv_rtype(vd, 0b010, vs1, vs2, 0b011111, vm)
def rvv_vmxor_mm(vd, vs1, vs2, vm=1): return _rvv_rtype(vd, 0b010, vs1, vs2, 0b011011, vm)

def rvv_vnmsac_vv(vd, vs1, vs2, vm=1): return _rvv_rtype(vd, 0b010, vs1, vs2, 0b101111, vm)
def rvv_vnmsac_vx(vd, rs1, vs2, vm=1): return _rvv_rtype(vd, 0b110, rs1, vs2, 0b101111, vm)

def rvv_vnmsub_vv(vd, vs1, vs2, vm=1): return _rvv_rtype(vd, 0b010, vs1, vs2, 0b101011, vm)
def rvv_vnmsub_vx(vd, rs1, vs2, vm=1): return _rvv_rtype(vd, 0b110, rs1, vs2, 0b101011, vm)

def rvv_vor_vi(vd, imm, vs2, vm=1): return _rvv_rtype(vd, 0b011, imm & 0b11111, vs2, 0b001010, vm)
def rvv_vor_vv(vd, vs1, vs2, vm=1): return _rvv_rtype(vd, 0b000, vs1, vs2, 0b001010, vm)
def rvv_vor_vx(vd, rs1, vs2, vm=1): return _rvv_rtype(vd, 0b100, rs1, vs2, 0b001010, vm)

def rvv_vrem_vv(vd, vs1, vs2, vm=1): return _rvv_rtype(vd, 0b010, vs1, vs2, 0b100011, vm)
def rvv_vrem_vx(vd, rs1, vs2, vm=1): return _rvv_rtype(vd, 0b110, rs1, vs2, 0b100011, vm)

def rvv_vremu_vv(vd, vs1, vs2, vm=1): return _rvv_rtype(vd, 0b010, vs1, vs2, 0b100010, vm)
def rvv_vremu_vx(vd, rs1, vs2, vm=1): return _rvv_rtype(vd, 0b110, rs1, vs2, 0b100010, vm)

def rvv_vrsub_vi(vd, imm, vs2, vm=1): return _rvv_rtype(vd, 0b011, imm & 0b11111, vs2, 0b000011, vm)
def rvv_vrsub_vx(vd, rs1, vs2, vm=1): return _rvv_rtype(vd, 0b100, rs1, vs2, 0b000011, vm)

def rvv_vsll_vi(vd, imm, vs2, vm=1): return _rvv_rtype(vd, 0b011, imm & 0b11111, vs2, 0b100101, vm)
def rvv_vsll_vv(vd, vs1, vs2, vm=1): return _rvv_rtype(vd, 0b000, vs1, vs2, 0b100101, vm)
def rvv_vsll_vx(vd, rs1, vs2, vm=1): return _rvv_rtype(vd, 0b100, rs1, vs2, 0b100101, vm)

def rvv_vsra_vi(vd, imm, vs2, vm=1): return _rvv_rtype(vd, 0b011, imm & 0b11111, vs2, 0b101001, vm)
def rvv_vsra_vv(vd, vs1, vs2, vm=1): return _rvv_rtype(vd, 0b000, vs1, vs2, 0b101001, vm)
def rvv_vsra_vx(vd, rs1, vs2, vm=1): return _rvv_rtype(vd, 0b100, rs1, vs2, 0b101001, vm)

def rvv_vsrl_vi(vd, imm, vs2, vm=1): return _rvv_rtype(vd, 0b011, imm & 0b11111, vs2, 0b101000, vm)
def rvv_vsrl_vv(vd, vs1, vs2, vm=1): return _rvv_rtype(vd, 0b000, vs1, vs2, 0b101000, vm)
def rvv_vsrl_vx(vd, rs1, vs2, vm=1): return _rvv_rtype(vd, 0b100, rs1, vs2, 0b101000, vm)

def rvv_vsub_vv(vd, vs1, vs2, vm=1): return _rvv_rtype(vd, 0b000, vs1, vs2, 0b000010, vm)
def rvv_vsub_vx(vd, rs1, vs2, vm=1): return _rvv_rtype(vd, 0b100, rs1, vs2, 0b000010, vm)

def rvv_vxor_vi(vd, imm, vs2, vm=1): return _rvv_rtype(vd, 0b011, imm & 0b11111, vs2, 0b001011, vm)
def rvv_vxor_vv(vd, vs1, vs2, vm=1): return _rvv_rtype(vd, 0b000, vs1, vs2, 0b001011, vm)
def rvv_vxor_vx(vd, rs1, vs2, vm=1): return _rvv_rtype(vd, 0b100, rs1, vs2, 0b001011, vm)

# VSETVLI
def rvv_vsetvli(rd, rs1, vtypei):
    return rvprotoinstrs.instruc_itype(RVV_OPCODE, rd, 0b111, rs1, vtypei)

# Loads and Stores (Simplified, assuming unit stride and no special fields for now)
# VLE32_V: opcode=0000111, funct3=110, rs1=base, vs2=0 (lumop=0), vm, nf
def rvv_vle32_v(vd, rs1, vm=1):
    # nf=0, mop=0, mew=0
    return rvprotoinstrs.instruc_rtype(RVV_LOAD_OPCODE, vd, 0b110, rs1, 0b00000, 0b0000000 | vm)

def rvv_vloxei32_v(vd, rs1, vs2, vm=1, nf=0):
    return rvprotoinstrs.instruc_rtype(RVV_LOAD_OPCODE, vd, 0b110, rs1, vs2, 0b0000110 | vm | (nf << 4))

def rvv_vlse32_v(vd, rs1, rs2, vm=1, nf=0):
    return rvprotoinstrs.instruc_rtype(RVV_LOAD_OPCODE, vd, 0b110, rs1, rs2, 0b0000100 | vm | (nf << 4))

# VSE32_V: opcode=0100111, funct3=110, rs1=base, vs3=data, vm, nf
def rvv_vse32_v(vs3, rs1, vm=1):
    # nf=0, mop=0, mew=0
    return rvprotoinstrs.instruc_rtype(RVV_STORE_OPCODE, vs3, 0b110, rs1, 0b00000, 0b0000000 | vm)

def rvv_vsoxei32_v(vs3, rs1, vs2, vm=1, nf=0):
    return rvprotoinstrs.instruc_rtype(RVV_STORE_OPCODE, vs3, 0b110, rs1, vs2, 0b0000110 | vm | (nf << 4))

def rvv_vsse32_v(vs3, rs1, rs2, vm=1, nf=0):
    return rvprotoinstrs.instruc_rtype(RVV_STORE_OPCODE, vs3, 0b110, rs1, rs2, 0b0000100 | vm | (nf << 4))
