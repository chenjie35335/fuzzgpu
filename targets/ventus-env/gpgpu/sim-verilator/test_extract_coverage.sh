#!/bin/bash
# Test script for extract_coverage_off.py

set -e

# Create test directory
TEST_DIR=$(mktemp -d)
trap "rm -rf $TEST_DIR" EXIT

echo "Testing extract_coverage_off.py..."
echo "Test directory: $TEST_DIR"
echo ""

# Create sample Verilog files with SYNTHESIS blocks
mkdir -p "$TEST_DIR/verilog"

# Test file 1: Simple module with one SYNTHESIS block
cat > "$TEST_DIR/verilog/module1.sv" << 'EOF'
module test_module (
    input clk,
    input reset
);
    
    // Normal logic
    wire data;
    
    // Some assertions for simulation
    `ifndef SYNTHESIS
        initial begin
            assert (1) else $error("Test assertion");
        end
        
        always @(posedge clk) begin
            if (reset) begin
                $display("Reset signal detected");
            end
        end
    `endif
    
    // More normal logic
    wire valid_signal;
    
endmodule
EOF

# Test file 2: Module with multiple SYNTHESIS blocks
cat > "$TEST_DIR/verilog/module2.v" << 'EOF'
module test_module2 (
    input clk
);
    
    `ifndef SYNTHESIS
        initial $display("Module2 initialization");
    `endif
    
    wire signal_a;
    
    `ifndef SYNTHESIS
        always @(*) begin
            $display("Signal A = %b", signal_a);
        end
    `endif
    
endmodule
EOF

# Test file 3: Nested conditionals
cat > "$TEST_DIR/verilog/module3.sv" << 'EOF'
module test_module3;
    
    `ifdef SIMULATION
        `ifndef SYNTHESIS
            initial begin
                $display("Simulation without synthesis");
            end
        `endif
    `endif
    
endmodule
EOF

# Run the extraction script
echo "Running extraction script..."
python3 extract_coverage_off.py "$TEST_DIR/verilog" "$TEST_DIR/coverage_off.txt"

echo ""
echo "Generated coverage_off directives:"
echo "=================================="
cat "$TEST_DIR/coverage_off.txt"
echo ""

# Verify the output format
echo "Verifying output format..."
if grep -q "coverage_off" "$TEST_DIR/coverage_off.txt"; then
    echo "✓ Coverage_off directives found"
else
    echo "✗ No coverage_off directives found"
    exit 1
fi

if grep -q "\-file" "$TEST_DIR/coverage_off.txt"; then
    echo "✓ File references found"
else
    echo "✗ No file references found"
    exit 1
fi

echo ""
echo "✓ All tests passed!"
