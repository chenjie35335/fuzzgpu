module L2DcacheReqEventListener #(
    parameter LINE_SIZE // in bytes
) (
    input logic clk,
    
    input logic valid,
    input logic ready,
    input logic rw,
    input logic [63:0] addr,
    input logic [LINE_SIZE*8-1:0] data,
    input logic [LINE_SIZE-1:0] byteen,
    
    input longint source_id // Global source ID
);

    import "DPI-C" function void dpi_l2_dcache_req_event(
        input longint source,
        input int opcode,
        input longint paddr,
        input int cacheline_size,
        input logic [LINE_SIZE*8-1:0] data,
        input logic [LINE_SIZE-1:0] mask
    );
    
    always @(posedge clk) begin
        if (valid && ready) begin
             dpi_l2_dcache_req_event(
                source_id,
                int'(rw),
                addr,
                int'(LINE_SIZE),
                data,
                byteen
             );
        end
    end

endmodule
