# Copyright 2023 Flavien Solt, ETH Zurich.
# Licensed under the General Public License, Version 3.0, see LICENSE for details.
# SPDX-License-Identifier: GPL-3.0-only

import rv.rvprotoinstrs as rvprotoinstrs

VENTUS_CUSTOM_0 = 0b0001011

def ventus_vadd12_vi(vd, vs1, imm):
    return rvprotoinstrs.instruc_itype(0b0001011, vd, 0b000, vs1, imm)

def ventus_vbeq(vs1, vs2, imm):
    return rvprotoinstrs.instruc_btype(0b1011011, 0b000, vs1, vs2, imm)

def ventus_vbge(vs1, vs2, imm):
    return rvprotoinstrs.instruc_btype(0b1011011, 0b101, vs1, vs2, imm)

def ventus_vbgeu(vs1, vs2, imm):
    return rvprotoinstrs.instruc_btype(0b1011011, 0b111, vs1, vs2, imm)

def ventus_vblt(vs1, vs2, imm):
    return rvprotoinstrs.instruc_btype(0b1011011, 0b100, vs1, vs2, imm)

def ventus_vbltu(vs1, vs2, imm):
    return rvprotoinstrs.instruc_btype(0b1011011, 0b110, vs1, vs2, imm)

def ventus_vbne(vs1, vs2, imm):
    return rvprotoinstrs.instruc_btype(0b1011011, 0b001, vs1, vs2, imm)

def ventus_vfexp_v(vd, vs2, vm):
    return rvprotoinstrs.instruc_rtype(0b0001011, vd, 0b110, 0b00000, vs2, 0b0000100 | vm)

def ventus_vftta_vv(vd, vs1, vs2, vm):
    return rvprotoinstrs.instruc_rtype(0b0001011, vd, 0b100, vs1, vs2, 0b0000110 | vm)

def ventus_vlb12_v(vd, vs1, imm):
    return rvprotoinstrs.instruc_itype(0b1111011, vd, 0b000, vs1, imm)

def ventus_vlb_v(vd, rs1, imm):
    return rvprotoinstrs.instruc_itype_funct1(0b0101011, vd, 0b000, rs1, imm, 0)

def ventus_vlbu12_v(vd, vs1, imm):
    return rvprotoinstrs.instruc_itype(0b1111011, vd, 0b100, vs1, imm)

def ventus_vlbu_v(vd, rs1, imm):
    return rvprotoinstrs.instruc_itype_funct1(0b0101011, vd, 0b100, rs1, imm, 0)

def ventus_vlh12_v(vd, vs1, imm):
    return rvprotoinstrs.instruc_itype(0b1111011, vd, 0b001, vs1, imm)

def ventus_vlh_v(vd, rs1, imm):
    return rvprotoinstrs.instruc_itype_funct1(0b0101011, vd, 0b001, rs1, imm, 0)

def ventus_vlhu12_v(vd, vs1, imm):
    return rvprotoinstrs.instruc_itype(0b1111011, vd, 0b101, vs1, imm)

def ventus_vlhu_v(vd, rs1, imm):
    return rvprotoinstrs.instruc_itype_funct1(0b0101011, vd, 0b101, rs1, imm, 0)

def ventus_vlw12_v(vd, vs1, imm):
    return rvprotoinstrs.instruc_itype(0b1111011, vd, 0b010, vs1, imm)

def ventus_vlw_v(vd, rs1, imm):
    return rvprotoinstrs.instruc_itype_funct1(0b0101011, vd, 0b010, rs1, imm, 0)

def ventus_vsb12_v(vs1, vs2, imm):
    return rvprotoinstrs.instruc_stype(0b1111011, 0b111, vs1, vs2, imm)

def ventus_vsb_v(rs1, rs2, imm):
    return rvprotoinstrs.instruc_stype_funct1(0b0101011, 0b000, rs1, rs2, imm, 1)

def ventus_vsh12_v(vs1, vs2, imm):
    return rvprotoinstrs.instruc_stype(0b1111011, 0b011, vs1, vs2, imm)

def ventus_vsh_v(rs1, rs2, imm):
    return rvprotoinstrs.instruc_stype_funct1(0b0101011, 0b001, rs1, rs2, imm, 1)

def ventus_vsub12_vi(vd, imm, vs2, vm):
    return rvprotoinstrs.instruc_rtype(0b0001011, vd, 0b001, imm & 0b11111, vs2, 0b0000000 | vm)

def ventus_vsw12_v(vs1, vs2, imm):
    return rvprotoinstrs.instruc_stype(0b1111011, 0b110, vs1, vs2, imm)

def ventus_vsw_v(rs1, rs2, imm):
    return rvprotoinstrs.instruc_stype_funct1(0b0101011, 0b010, rs1, rs2, imm, 1)

def ventus_join():
    return rvprotoinstrs.instruc_btype(0b1011011, 0b010, 0b00000, 0b00000, 0)

def ventus_barrier():
    return rvprotoinstrs.instruc_itype(0b0001011, 0b00000, 0b100, 0b00000, 0b000001000000)

def ventus_endprg():
    return rvprotoinstrs.instruc_rtype(0b0001011, 0b00000, 0b100, 0b00000, 0b00000, 0b0000000)

def ventus_setrpc(rs1, imm):
    return rvprotoinstrs.instruc_itype(0b1011011, 0b00000, 0b011, rs1, imm)

def ventus_regext(imm):
    return rvprotoinstrs.instruc_itype(0b0001011, 0, 0b010, 0, imm)
