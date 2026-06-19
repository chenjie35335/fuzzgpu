
module DcacheShmemReqEventListener #(
    parameter NUM_LANES,
    parameter XLEN,
    parameter WORD_SIZE
) (
    input logic clk,
    
    input logic valid,
    input logic [NUM_LANES-1:0] mask,
    input logic rw, // 0=Read, 1=Write
    input logic [NUM_LANES-1:0][XLEN-1:0] addr,
    input logic [NUM_LANES-1:0][WORD_SIZE-1:0] byteen,
    input int   wsize, // 1 << wsize_log2
    
    input int   core_id,
    input int   wid,
    input longint uuid,
    input longint PC,
    //input int   op_type,
    
    input logic [NUM_LANES-1:0] is_shared
);

    import "DPI-C" function void dpi_dcache_shmem_req_event(
        input int sm_id,
        input int hardware_warp_id,
        input longint instr_id,
        input int num_lanes,
        input bit [NUM_LANES-1:0] activateMask,
        input longint pc,
        input bit [NUM_LANES-1:0][XLEN-1:0] vaddr_per_lane,
        input bit [NUM_LANES-1:0][WORD_SIZE-1:0] wordOffset1H,
        input int size,
        input int memop,
        input bit [NUM_LANES-1:0] is_shared
    );

    always @(posedge clk) begin
        if (valid) begin
            dpi_dcache_shmem_req_event(
                core_id,
                wid,
                uuid,
                NUM_LANES,
                mask,
                PC,
                addr,
                byteen,
                wsize, 
                (rw ? 1 : 0), 
                is_shared
            );
        end
    end

endmodule
