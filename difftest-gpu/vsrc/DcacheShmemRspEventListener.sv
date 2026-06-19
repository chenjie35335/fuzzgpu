
module DcacheShmemRspEventListener #(
    parameter NUM_LANES,
    parameter WORD_SIZE,
    parameter TAG_WIDTH,
    parameter UUID_WIDTH
) (
    input logic clk,
    
    input logic valid,
    input logic [NUM_LANES-1:0] mask,
    input logic [NUM_LANES-1:0][(WORD_SIZE*8)-1:0] data,
    input logic [TAG_WIDTH-1:0] tag,
    
    input int   core_id
);

    import "DPI-C" function void dpi_dcache_shmem_rsp_event(
        input int sm_id,
        input longint instr_id,
        input int num_lanes,
        input int data_width,
        input bit [NUM_LANES-1:0] activateMask,
        input bit [NUM_LANES-1:0][(WORD_SIZE*8)-1:0] data
    );

    wire [63:0] uuid = 64'(tag[TAG_WIDTH-1 : TAG_WIDTH-UUID_WIDTH]);
    `UNUSED_VAR (tag)

    always @(posedge clk) begin
        if (valid) begin
            dpi_dcache_shmem_rsp_event(
                core_id,
                uuid,
                NUM_LANES,
                WORD_SIZE * 8,
                mask,
                data
            );
        end
    end

endmodule
