#pragma once

#include <cstdint>

namespace fuzzgpu {

#if defined(ARCH_VENTUS)
// GPGU CSRs
/* rvv-gpgpu extension csr */
#define CSR_TID 0x800
#define CSR_NUMW 0x801
#define CSR_NUMT 0x802
#define CSR_KNL 0x803
#define CSR_WGID 0x804
#define CSR_WID 0x805
#define CSR_LDS 0x806
#define CSR_PDS 0x807
#define CSR_GIDX 0x808
#define CSR_GIDY 0x809
#define CSR_GIDZ 0x80a
#define CSR_PRINT 0x80b
#define CSR_RPC 0x80c

#define CSR_MVENDORID 0xf11
#define CSR_MARCHID 0xf12
#define CSR_MIMPID 0xf13
#define CSR_MHARTID 0xf14

#define CSR_MTVT 0x307
#define CSR_MNXTI 0x345
#define CSR_MINTSTATUS 0x346
#define CSR_MSCRATCHCSW 0x348
#define CSR_MSCRATCHCSWL 0x349
#define CSR_MSTATUS 0x300
#define CSR_MISA 0x301
#define CSR_MEDELEG 0x302
#define CSR_MIDELEG 0x303
#define CSR_MIE 0x304
#define CSR_MTVEC 0x305
#define CSR_MCOUNTEREN 0x306
#define CSR_MENVCFG 0x30a
#define CSR_MSTATEEN0 0x30c
#define CSR_MSTATEEN1 0x30d
#define CSR_MSTATEEN2 0x30e
#define CSR_MSTATEEN3 0x30f
#define CSR_MCOUNTINHIBIT 0x320
#define CSR_MSCRATCH 0x340
#define CSR_MEPC 0x341
#define CSR_MCAUSE 0x342
#define CSR_MTVAL 0x343
#define CSR_MIP 0x344

#define CSR_SSTATUS 0x100

#define CSR_FFLAGS 0x1
#define CSR_FRM 0x2
#define CSR_FCSR 0x3

// encodings
#define SSTATUS_FS          0x00006000
#endif

} // namespace fuzzgpu