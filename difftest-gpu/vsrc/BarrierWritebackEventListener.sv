module BarrierWritebackEventListener import Difftest_pkg::*; #(
    parameter XLEN, NR_THREADS, IID_W
)(
    input bit                       clk,
                                    en,
                [IID_W-1:0]         iid,
          int                       warp_gid,
          int                       warp_id, 
          bit [NR_THREADS-1:0]      wb_tmask,
                [XLEN-1:0]          pc
);

    always_comb assert (IID_W <= 64); // fit it in the uint64_t

    import "DPI-C" function void dpi_barrier_writeback_event(
        input longint                iid,
              int                    warp_gid,
              int                    warp_id,
              bit [NR_THREADS-1:0]   wb_tmask,
              longint                pc
    );

    always @(posedge clk)
        if (en)
            dpi_barrier_writeback_event(64'(iid), warp_gid, warp_id, wb_tmask, 64'(pc));

endmodule
