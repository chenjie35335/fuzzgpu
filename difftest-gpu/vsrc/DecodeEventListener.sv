module DecodeEventListener import Difftest_pkg::*; #(
    parameter XLEN, NR_THREADS, IID_W
)(
    input bit                       clk,
                                    en,
                [XLEN-1:0]          pc,
          int                       warp_gid,
                [NR_THREADS-1:0]    tmask,
          bit                       rd_wen,
    input reg_id_t                  rd,
          int                       opcode,
          bit [IID_W-1:0]           iid
);

    always_comb assert (IID_W <= 64); // fit it in the uint64_t

    import "DPI-C" function void dpi_decode_event(
        input longint                   pc,
              int                       warp_gid,
              bit [NR_THREADS-1:0]      tmask,
              bit                       rd_wen,
              int                       rd_type,
                                        rd_idx,
                                        opcode,
              longint                   iid
    );

    always @(posedge clk)
        if (en)
            dpi_decode_event(64'(pc), warp_gid, tmask, rd_wen, rd.rtype, rd.idx, opcode, 64'(iid));


endmodule
