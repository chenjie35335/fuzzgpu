module ResetEventListener #(
    parameter int NR_RESET_CYCLES
)(
    input clk,
    input reset
);

    import "DPI-C" function void dpi_reset_begin_event();
    import "DPI-C" function void dpi_reset_end_event();

    bit reset_r = 0;
    always @(posedge clk)
        reset_r <= reset;
    
    bit [31:0] cnt_r;
    
    always @(posedge clk)
        if (~reset_r && reset) begin // start of the reset
            cnt_r <= 0;
            dpi_reset_begin_event();

        end else if (reset) begin
            if (cnt_r >= NR_RESET_CYCLES)
                dpi_reset_end_event();
            cnt_r <= cnt_r + 1;
        end

endmodule
