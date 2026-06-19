// std::cout << "barrier" << std::endl;
SET_BARRIER_1;
if (IS_ALL_TRUE) {
  // set_pc(pc+4);
  SET_BARRIER_0;
}
#ifndef FUZZGPU_ISA_EMULATOR
else {
  set_pc(pc);
}
#else
// we don't need re-execute this insn again but still maintain equivalent
// behavior, since in fuzzgpu, we will ensure the barriers are always met (in
// the code generator) or compared with DUT via schedulable() predicate (in the
// difftest_gpu)
#endif
