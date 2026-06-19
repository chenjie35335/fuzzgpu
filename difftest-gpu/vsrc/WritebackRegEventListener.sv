module WritebackRegEventListener import Difftest_pkg::*; #(
    parameter XLEN, NR_THREADS, IID_W
)(
    input bit                       clk,
                                    en,
                [IID_W-1:0]         iid,
          int                       warp_gid,
          bit [NR_THREADS-1:0]      wb_tmask,
                [XLEN-1:0]          pc,
          reg_id_t                  rd,
          bit                       rd_wen,
                 [NR_THREADS-1:0][XLEN-1:0] rd_dat
);

    always_comb assert (IID_W <= 64); // fit it in the uint64_t

    import "DPI-C" function void dpi_writeback_reg_event(
        input longint                iid,
              int                    warp_gid,
              bit [NR_THREADS-1:0]   wb_tmask,
              longint                pc,
              int                    rd_type,
                                     rd_idx,
              bit                    rd_wen,
              bit [NR_THREADS-1:0][XLEN-1:0] rd_dat 
    );

    always @(posedge clk)
        if (en)
            dpi_writeback_reg_event(64'(iid), warp_gid, wb_tmask, 64'(pc), rd.rtype, rd.idx, rd_wen, rd_dat);

endmodule

