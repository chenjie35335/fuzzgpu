#pragma once

#include <cstdint>

namespace fuzzgpu {

#if defined(ARCH_VORTEX)

static constexpr uint32_t VX_CSR_CORE_ID = 0xCC2;
static constexpr uint32_t VX_CSR_WARP_ID = 0xCC1;
static constexpr uint32_t VX_CSR_THREAD_ID = 0xCC0;

// GPGU CSRs

static constexpr uint32_t VX_CSR_ACTIVE_WARPS = 0xCC3;
static constexpr uint32_t VX_CSR_ACTIVE_THREADS =
    0xCC4; // warning! this value is also used in LLVM
static constexpr uint32_t VX_CSR_NUM_THREADS = 0xFC0;
static constexpr uint32_t VX_CSR_NUM_WARPS = 0xFC1;
static constexpr uint32_t VX_CSR_NUM_CORES = 0xFC2;
static constexpr uint32_t VX_CSR_LOCAL_MEM_BASE = 0xFC3;

// User Floating-Point CSRs ///////////////////////////////////////////////////

static constexpr uint32_t VX_CSR_FFLAGS = 0x001;
static constexpr uint32_t VX_CSR_FRM = 0x002;
static constexpr uint32_t VX_CSR_FCSR = 0x003;
static constexpr uint32_t VX_CSR_SATP = 0x180;
static constexpr uint32_t VX_CSR_PMPCFG0 = 0x3A0;
static constexpr uint32_t VX_CSR_PMPADDR0 = 0x3B0;
static constexpr uint32_t VX_CSR_MSTATUS = 0x300;
static constexpr uint32_t VX_CSR_MISA = 0x301;
static constexpr uint32_t VX_CSR_MEDELEG = 0x302;
static constexpr uint32_t VX_CSR_MIDELEG = 0x303;
static constexpr uint32_t VX_CSR_MIE = 0x304;
static constexpr uint32_t VX_CSR_MTVEC = 0x305;
static constexpr uint32_t VX_CSR_MSCRATCH = 0x340;
static constexpr uint32_t VX_CSR_MEPC = 0x341;
static constexpr uint32_t VX_CSR_MCAUSE = 0x342;
static constexpr uint32_t VX_CSR_MNSTATUS = 0x744;
static constexpr uint32_t VX_CSR_MPM_BASE = 0xB00;
static constexpr uint32_t VX_CSR_MPM_BASE_H = 0xB80;
static constexpr uint32_t VX_CSR_MPM_USER = 0xB03;
static constexpr uint32_t VX_CSR_MPM_USER_H = 0xB83;

// Machine Performance-monitoring core counters (Standard) ////////////////////

static constexpr uint32_t VX_CSR_MCYCLE = 0xB00;
static constexpr uint32_t VX_CSR_MCYCLE_H = 0xB80;
static constexpr uint32_t VX_CSR_MPM_RESERVED = 0xB01;
static constexpr uint32_t VX_CSR_MPM_RESERVED_H = 0xB81;
static constexpr uint32_t VX_CSR_MINSTRET = 0xB02;
static constexpr uint32_t VX_CSR_MINSTRET_H = 0xB82;

// Machine Information Registers //////////////////////////////////////////////

static constexpr uint32_t VX_CSR_MVENDORID = 0xF11;
static constexpr uint32_t VX_CSR_MARCHID = 0xF12;
static constexpr uint32_t VX_CSR_MIMPID = 0xF13;
static constexpr uint32_t VX_CSR_MHARTID = 0xF14;

#endif // ARCH_VORTEX

} // namespace fuzzgpu