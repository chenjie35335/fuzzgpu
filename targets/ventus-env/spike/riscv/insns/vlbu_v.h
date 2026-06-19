VI_GPU_LD_INDEX(e32,true,({
    reg_t baseAddr = index[i] + insn.v_simm11();
    reg_t baseBias = P.get_csr(CSR_PDS) + (baseAddr & ~3) * P.get_csr(CSR_NUMW) * P.get_csr(CSR_NUMT) + (baseAddr & 3);
     std::cout <<
    "lane:" << i <<
    "index: " << std::dec << index[i] << std::dec <<
    "simm:" << std::hex << insn.v_simm11() << std::dec <<
    "CSR_PDS: 0x" << std::hex << P.get_csr(CSR_PDS) << std::dec <<
    "CSR_NUMW: " << P.get_csr(CSR_NUMW) <<
    " CSR_NUMT: " << P.get_csr(CSR_NUMT) <<
        " baseAddr: 0x" << std::hex << baseAddr << std::dec <<
        " baseAddr & 3: 0x" << std::hex << (baseAddr & 3) << std::dec <<
        " baseBias: 0x" << std::hex << baseBias << std::dec <<
        " baseTid: " << std::dec << baseTid <<
        " vreg_inx: " << std::dec << vreg_inx <<
        " load_addr: 0x" << std::hex << (baseBias+((baseTid + vreg_inx)<<2)) << std::dec << "\n";
    MMU.load_uint8(baseBias+((baseTid + vreg_inx)<<2));}
    ));