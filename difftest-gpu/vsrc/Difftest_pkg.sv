package Difftest_pkg;

    // Aligned with csrc/include/types.hh RegId::*

    /* verilator lint_off UNUSEDPARAM */
    localparam int RegTypeInt = 0,
                     RegTypeFp  = 1,
                     RegTypeInvalid = 3;

    localparam int OpcodeOthers = 0,
                   OpcodeCall = 1,
                   OpcodeRet = 2,
                   OpcodeRetCall = 3,
                   OpcodeCtrlStatusRead = 4,
                   OpcodeTmaskWrite = 5,
                   OpcodeFloatingPoint = 6;
    /* verilator lint_on UNUSEDPARAM */

    typedef struct packed {
        int rtype;
        int idx;
    } reg_id_t;

endpackage
