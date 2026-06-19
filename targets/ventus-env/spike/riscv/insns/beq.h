std::cout << insn.rs1() << " RS1 = " << RS1 << ", " << insn.rs2() <<  " RS2 = " << RS2 << ", BRANCH_TARGET = " << BRANCH_TARGET << std::endl;
if(RS1 == RS2)
  set_pc(BRANCH_TARGET);
