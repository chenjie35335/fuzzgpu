`include "VX_define.vh"

`ifdef DIFFTEST
package VX_difftest_pkg;
    import VX_gpu_pkg::*;

    // Convert a VX register number to a Difftest register identifier
    function Difftest_pkg::reg_id_t make_reg_id(input [NUM_REGS_BITS-1:0] reg_num);
        Difftest_pkg::reg_id_t rid;
        case (get_reg_type(reg_num))
            REG_TYPE_I: rid.rtype = Difftest_pkg::RegTypeInt;
            REG_TYPE_F:  rid.rtype = Difftest_pkg::RegTypeFp;
            default:      rid.rtype = Difftest_pkg::RegTypeInvalid;
        endcase
        rid.idx = 32'(get_reg_idx(reg_num));
        return rid;
    endfunction

    /* verilator lint_off UNUSEDSIGNAL */
    function int make_warp_id(input [ISSUE_WIS_W-1:0] wis, input [SIMD_IDX_W-1:0] sid, input [ISSUE_ISW_W-1:0] is);
        // Convert wis (warp-in-slice) and is (issue-slot-index) to actual warp ID
        // Note: sid (SIMD index) should NOT affect warp_gid - it only affects thread_id within warp
        // sid parameter is kept for interface compatibility but not used in warp_gid calculation
        return 32'(wis_to_wid(wis, is));
    endfunction
    /* verilator lint_on UNUSEDSIGNAL */
    
    `IGNORE_UNUSED_BEGIN
    function [31:0] make_iid(input [UUID_WIDTH-1:0] uuid);
        return uuid[31:0];
    endfunction
    `IGNORE_UNUSED_END

    function [`NUM_THREADS-1:0] make_tmask_from_simd(input [`SIMD_WIDTH-1:0] tmask, input [SIMD_IDX_W-1:0] sid);
        logic [`NUM_THREADS-1:0] full_tmask;
        full_tmask = '0;
        for (int i = 0; i < `SIMD_WIDTH; i++) begin
            full_tmask[sid * `SIMD_WIDTH + i] = tmask[i];
        end
        return full_tmask;
    endfunction 

    function int decode_opcode(input [EX_BITS-1:0] ex_type,
                               input [INST_OP_BITS-1:0] op_type,
                               input op_args_t op_args,
                               input [NUM_REGS_BITS-1:0] rs1,
                               input [NUM_REGS_BITS-1:0] rd);

        case(ex_type)
            EX_ALU: begin
                case (op_args.alu.xtype)
                    ALU_TYPE_BRANCH: begin
                        case (INST_BR_BITS'(op_type))
                            INST_BR_JAL:
                                if (rd == NUM_REGS_BITS'(1)) // ra
                                    return Difftest_pkg::OpcodeCall;

                            INST_BR_JALR: begin
                                if (rs1 == NUM_REGS_BITS'(1) && rd == NUM_REGS_BITS'(1)) // ra
                                    return Difftest_pkg::OpcodeRetCall;
                                else if (rs1 == NUM_REGS_BITS'(1)) // ra
                                    return Difftest_pkg::OpcodeRet;
                                else if (rd == NUM_REGS_BITS'(1)) // ra
                                    return Difftest_pkg::OpcodeCall;
                            end
                            default:
                                ;
                        endcase
                    end
                    default:
                        ;
                endcase
            end

            EX_FPU: begin
                return Difftest_pkg::OpcodeFloatingPoint;
            end

        `ifdef EXT_TCU_ENABLE
            EX_TCU: begin
                return Difftest_pkg::OpcodeFloatingPoint;
            end
        `endif

            EX_SFU: begin
                case (INST_SFU_BITS'(op_type))
                    INST_SFU_CSRRW,
                    INST_SFU_CSRRS,
                    INST_SFU_CSRRC:
                        return Difftest_pkg::OpcodeCtrlStatusRead;

                    INST_SFU_TMC,
                    INST_SFU_BAR:
                        return Difftest_pkg::OpcodeTmaskWrite;
                
                    default:
                        ;
                endcase
            end

            default:
                ;

        endcase
        return Difftest_pkg::OpcodeOthers;
    endfunction

endpackage
`endif
