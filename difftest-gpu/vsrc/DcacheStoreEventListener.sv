
module DcacheStoreEventListener #(
    parameter NUM_LANES,
    parameter XLEN,
    parameter WORD_SIZE
) (
    input logic clk,
    
    input logic valid,  // no_rsp_buf_valid && no_rsp_buf_ready (store committed)
    input logic [NUM_LANES-1:0] mask,
    input logic [NUM_LANES-1:0][XLEN-1:0] addr,
    input logic [NUM_LANES-1:0][(WORD_SIZE*8)-1:0] data,
    input logic [NUM_LANES-1:0][WORD_SIZE-1:0] byteen,
    input int   wsize, // 1 << wsize_log2
    
    input int   core_id,
    input int   wid,
    input longint uuid,
    input longint PC,
    
    input logic [NUM_LANES-1:0] is_shared
);

    import "DPI-C" function void dpi_dcache_store_event(
        input int sm_id,
        input int hardware_warp_id,
        input longint instr_id,
        input int num_lanes,
        input bit [NUM_LANES-1:0] activateMask,
        input longint pc,
        input bit [NUM_LANES-1:0][XLEN-1:0] vaddr_per_lane,
        input bit [NUM_LANES-1:0][(WORD_SIZE*8)-1:0] data_per_lane,
        input bit [NUM_LANES-1:0][WORD_SIZE-1:0] byteen_per_lane,
        input int size,
        input bit [NUM_LANES-1:0] is_shared
    );

    always @(posedge clk) begin
        if (valid) begin
            dpi_dcache_store_event(
                core_id,
                wid,
                uuid,
                NUM_LANES,
                mask,
                PC,
                addr,
                data,
                byteen,
                wsize,
                is_shared
            );
        end
    end

endmodule
