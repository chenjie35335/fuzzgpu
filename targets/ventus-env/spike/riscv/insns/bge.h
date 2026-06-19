// Print full 64-bit raw and signed values for debugging XLEN/extension issues
#include <iomanip>
std::cout << "BGE: rs1=0x" << std::hex << std::setfill('0') << std::setw(16) << uint64_t(RS1)
          << ", rs2=0x" << std::setw(16) << uint64_t(RS2)
          << " (s_rs1=" << std::dec << int64_t(RS1)
          << ", s_rs2=" << int64_t(RS2) << ")\n";

if (sreg_t(RS1) >= sreg_t(RS2))
  set_pc(BRANCH_TARGET);
