import rv.rvprotoinstrs as rvprotoinstrs

# vortex
def vortex_tmc(rs1: int):
    return rvprotoinstrs.instruc_rtype(0x0B, 0, 0, rs1, 0, 0b0000000)

def vortex_wspawn(rs1: int, rs2: int):
    return rvprotoinstrs.instruc_rtype(0x0B, 0, 1, rs1, rs2, 0b0000000)

def vortex_vxbar(rs1: int, rs2: int):
    return rvprotoinstrs.instruc_rtype(0x0B, 0, 4, rs1, rs2, 0b0000000)

def vortex_join(rs1: int):
    return rvprotoinstrs.instruc_rtype(0x0B, 0, 3, rs1, 0, 0b0000000)

def vortex_pred(rs1: int, rs2: int):
    return rvprotoinstrs.instruc_rtype(0x0B, 0, 5, rs1, rs2, 0b0000000)

def vortex_pred_n(rs1: int, rs2: int):
    return rvprotoinstrs.instruc_rtype(0x0B, 1, 5, rs1, rs2, 0b0000000)

def vortex_split(rd: int, rs1: int):
    return rvprotoinstrs.instruc_rtype(0x0B, rd, 2, rs1, 0, 0b0000000)

def vortex_split_n(rd: int, rs1: int):
    return rvprotoinstrs.instruc_rtype(0x0B, rd, 2, rs1, 1, 0b0000000)

def vortex_wmma(fmt1: int, fmt2: int):
    return rvprotoinstrs.instruc_rtype(0x0B, fmt1, 0, fmt2, 0, 0b0000010)


