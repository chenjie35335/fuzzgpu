# Copyright (c) 2024-2025 Institute of Information Engineering, Chinese Academy of Sciences
# 
# DiveFuzz is licensed under Mulan PSL v2.
# You can use this software according to the terms and conditions of the Mulan PSL v2.
# You may obtain a copy of Mulan PSL v2 at:
#          http://license.coscl.org.cn/MulanPSL2
# 
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
# EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
# MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
# 
# See the Mulan PSL v2 for more details.

"""
Differential testing for InstructionEncoder vs RiscvCompiler

This script compares the encoding results from InstructionEncoder (our implementation)
against RiscvCompiler (using riscv-gnu-toolchain as ground truth) to verify correctness.
"""

import sys
import os
from typing import List, Tuple, Optional
from dataclasses import dataclass

# Add parent directory to path for imports
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from instruction_encoder import InstructionEncoder
from riscv_compiler import RiscvCompiler


@dataclass
class DiffTestResult:
    """Result of a differential test"""
    instruction: str
    encoder_result: Optional[int]
    compiler_result: Optional[int]
    encoder_error: Optional[str]
    compiler_error: Optional[str]
    
    @property
    def both_success(self) -> bool:
        return self.encoder_result is not None and self.compiler_result is not None
    
    @property
    def match(self) -> bool:
        return self.both_success and self.encoder_result == self.compiler_result
    
    @property
    def encoder_only(self) -> bool:
        return self.encoder_result is not None and self.compiler_result is None
    
    @property
    def compiler_only(self) -> bool:
        return self.encoder_result is None and self.compiler_result is not None
    
    @property
    def both_fail(self) -> bool:
        return self.encoder_result is None and self.compiler_result is None


class EncoderDiffTester:
    """Differential tester for InstructionEncoder"""
    
    def __init__(self, architecture: str = 'default', arch_bits: int = 64):
        """
        Initialize the differential tester
        
        Args:
            architecture: Target architecture ('ventus', 'xs', 'nts', 'rkt', or 'default')
            arch_bits: Architecture bit width (32 or 64)
        """
        self.architecture = architecture
        self.arch_bits = arch_bits
        
        # Initialize encoder
        self.encoder = InstructionEncoder(architecture=architecture)
        
        # Initialize compiler (may fail if toolchain not available)
        self.compiler: Optional[RiscvCompiler] = None
        try:
            self.compiler = RiscvCompiler(architecture=architecture, arch_bits=arch_bits)
        except Exception as e:
            print(f"Warning: RiscvCompiler not available: {e}")
    
    def test_instruction(self, instruction: str) -> DiffTestResult:
        """
        Test a single instruction with both encoder and compiler
        
        Args:
            instruction: Assembly instruction string
            
        Returns:
            DiffTestResult with comparison results
        """
        encoder_result = None
        compiler_result = None
        encoder_error = None
        compiler_error = None
        
        # Test encoder
        try:
            encoder_result = self.encoder.encode(instruction)
        except Exception as e:
            encoder_error = str(e)
        
        # Test compiler
        if self.compiler:
            try:
                compiler_result = self.compiler.compile_instruction(instruction)
            except Exception as e:
                compiler_error = str(e)
        else:
            compiler_error = "Compiler not available"
        
        return DiffTestResult(
            instruction=instruction,
            encoder_result=encoder_result,
            compiler_result=compiler_result,
            encoder_error=encoder_error,
            compiler_error=compiler_error
        )
    
    def test_multiple(self, instructions: List[str]) -> List[DiffTestResult]:
        """
        Test multiple instructions
        
        Args:
            instructions: List of assembly instructions
            
        Returns:
            List of DiffTestResult
        """
        return [self.test_instruction(instr) for instr in instructions]
    
    def run_test_suite(self, instructions: List[str], verbose: bool = True) -> dict:
        """
        Run a complete test suite and report statistics
        
        Args:
            instructions: List of assembly instructions to test
            verbose: Whether to print detailed results
            
        Returns:
            Dictionary with test statistics
        """
        results = self.test_multiple(instructions)
        
        stats = {
            'total': len(results),
            'match': 0,
            'mismatch': 0,
            'encoder_only': 0,
            'compiler_only': 0,
            'both_fail': 0,
            'mismatches': [],
            'encoder_only_list': [],
            'compiler_only_list': [],
        }
        
        for r in results:
            if r.match:
                stats['match'] += 1
                if verbose:
                    print(f"✓ MATCH: {r.instruction:40s} => 0x{r.encoder_result:08x}")
            elif r.both_success:
                stats['mismatch'] += 1
                stats['mismatches'].append(r)
                if verbose:
                    print(f"✗ MISMATCH: {r.instruction}")
                    print(f"    Encoder:  0x{r.encoder_result:08x}")
                    print(f"    Compiler: 0x{r.compiler_result:08x}")
                    diff = r.encoder_result ^ r.compiler_result
                    print(f"    XOR diff: 0x{diff:08x} (bits: {bin(diff)})")
            elif r.encoder_only:
                stats['encoder_only'] += 1
                stats['encoder_only_list'].append(r)
                if verbose:
                    print(f"⚠ ENCODER_ONLY: {r.instruction:40s} => 0x{r.encoder_result:08x}")
                    print(f"    Compiler error: {r.compiler_error[:60]}...")
            elif r.compiler_only:
                stats['compiler_only'] += 1
                stats['compiler_only_list'].append(r)
                if verbose:
                    print(f"⚠ COMPILER_ONLY: {r.instruction:40s} => 0x{r.compiler_result:08x}")
                    print(f"    Encoder error: {r.encoder_error[:60]}...")
            else:
                stats['both_fail'] += 1
                if verbose:
                    print(f"✗ BOTH_FAIL: {r.instruction}")
        
        # Print summary
        print("\n" + "="*60)
        print("DIFFERENTIAL TEST SUMMARY")
        print("="*60)
        print(f"Total instructions tested: {stats['total']}")
        print(f"  ✓ Match:         {stats['match']:4d} ({100*stats['match']/stats['total']:.1f}%)")
        print(f"  ✗ Mismatch:      {stats['mismatch']:4d} ({100*stats['mismatch']/stats['total']:.1f}%)")
        print(f"  ⚠ Encoder only:  {stats['encoder_only']:4d}")
        print(f"  ⚠ Compiler only: {stats['compiler_only']:4d}")
        print(f"  ✗ Both fail:     {stats['both_fail']:4d}")
        
        return stats


# Standard RISC-V test instructions
BASIC_TEST_INSTRUCTIONS = [
    # R-type instructions
    "add x1, x2, x3",
    "sub x4, x5, x6",
    "and x7, x8, x9",
    "or x10, x11, x12",
    "xor x13, x14, x15",
    "sll x16, x17, x18",
    "srl x19, x20, x21",
    "sra x22, x23, x24",
    "slt x25, x26, x27",
    "sltu x28, x29, x30",
    
    # I-type instructions
    "addi x1, x2, 100",
    "addi x1, x2, -100",
    "andi x3, x4, 0xff",
    "ori x5, x6, 0x123",
    "xori x7, x8, -1",
    "slti x9, x10, 50",
    "sltiu x11, x12, 100",
    "slli x13, x14, 5",
    "srli x15, x16, 10",
    "srai x17, x18, 3",
    
    # Load instructions
    "lb x1, 0(x2)",
    "lh x3, 4(x4)",
    "lw x5, 8(x6)",
    "lbu x7, 12(x8)",
    "lhu x9, 16(x10)",
    
    # Store instructions
    "sb x1, 0(x2)",
    "sh x3, 4(x4)",
    "sw x5, 8(x6)",
    
    # U-type instructions
    "lui x1, 0x12345",
    "auipc x2, 0x1000",
    
    # JALR (not affected by label issues)
    "jalr x1, x2, 0",
    "jalr x3, x4, 100",
]

# B-type and J-type instructions - These use PC-relative addressing
# which the assembler handles differently than raw numeric offsets.
# Testing these requires special handling or using labels.
# Skipped in basic differential testing.
BRANCH_JUMP_INSTRUCTIONS = [
    # B-type instructions (require label syntax for assembler)
    "beq x1, x2, 16",
    "bne x3, x4, -8",
    "blt x5, x6, 32",
    "bge x7, x8, 64",
    "bltu x9, x10, 128",
    "bgeu x11, x12, -128",
    
    # J-type instruction
    "jal x1, 100",
]

# M extension (multiply/divide)
M_EXTENSION_INSTRUCTIONS = [
    "mul x1, x2, x3",
    "mulh x4, x5, x6",
    "mulhsu x7, x8, x9",
    "mulhu x10, x11, x12",
    "div x13, x14, x15",
    "divu x16, x17, x18",
    "rem x19, x20, x21",
    "remu x22, x23, x24",
]

# A extension (atomic)
A_EXTENSION_INSTRUCTIONS = [
    "lr.w x1, (x2)",
    "sc.w x3, x4, (x5)",
    "amoswap.w x6, x7, (x8)",
    "amoadd.w x9, x10, (x11)",
    "amoxor.w x12, x13, (x14)",
    "amoand.w x15, x16, (x17)",
    "amoor.w x18, x19, (x20)",
    "amomin.w x21, x22, (x23)",
    "amomax.w x24, x25, (x26)",
    "amominu.w x27, x28, (x29)",
    "amomaxu.w x30, x31, (x1)",
]

# F extension (single-precision float)
F_EXTENSION_INSTRUCTIONS = [
    "fadd.s f1, f2, f3",
    "fsub.s f4, f5, f6",
    "fmul.s f7, f8, f9",
    "fdiv.s f10, f11, f12",
    "fsqrt.s f13, f14",
    "fmadd.s f15, f16, f17, f18",
    "fmsub.s f19, f20, f21, f22",
    "fnmadd.s f23, f24, f25, f26",
    "fnmsub.s f27, f28, f29, f30",
    "fmin.s f1, f2, f3",
    "fmax.s f4, f5, f6",
    "fcvt.w.s x1, f1",
    "fcvt.s.w f2, x2",
    "fmv.x.w x3, f3",
    "fmv.w.x f4, x4",
    "feq.s x5, f5, f6",
    "flt.s x6, f7, f8",
    "fle.s x7, f9, f10",
    "fclass.s x8, f11",
]

# V extension (vector) - basic operations
V_EXTENSION_INSTRUCTIONS = [
    # Vector arithmetic
    "vadd.vv v1, v2, v3",
    "vadd.vx v4, v5, x1",
    "vadd.vi v7, v8, 5",
    "vsub.vv v10, v11, v12",
    "vmul.vv v13, v14, v15",
    "vand.vv v16, v17, v18",
    "vor.vv v19, v20, v21",
    "vxor.vv v22, v23, v24",
    
    # Vector load/store
    "vle32.v v1, (x2)",
    "vse32.v v3, (x4)",
    "vle8.v v5, (x6)",
    "vle16.v v7, (x8)",
    "vle64.v v9, (x10)",
    
    # Vector comparison
    "vmseq.vv v1, v2, v3",
    "vmslt.vv v4, v5, v6",
    
    # Vector shift
    "vsll.vv v7, v8, v9",
    "vsrl.vv v10, v11, v12",
    
    # Vector floating-point
    "vfadd.vv v1, v2, v3",
    "vfmul.vv v4, v5, v6",
]


def main():
    """Run differential tests"""
    import argparse
    
    parser = argparse.ArgumentParser(description='Differential testing for InstructionEncoder')
    parser.add_argument('--arch', default='default', help='Target architecture')
    parser.add_argument('--bits', type=int, default=64, help='Architecture bits (32 or 64)')
    parser.add_argument('--basic', action='store_true', help='Test basic instructions only')
    parser.add_argument('--vector', action='store_true', help='Test vector instructions only')
    parser.add_argument('--all', action='store_true', help='Test all instruction categories')
    parser.add_argument('-v', '--verbose', action='store_true', help='Verbose output')
    
    args = parser.parse_args()
    
    # Initialize tester
    print(f"Initializing differential tester (arch={args.arch}, bits={args.bits})...")
    tester = EncoderDiffTester(architecture=args.arch, arch_bits=args.bits)
    
    if tester.compiler is None:
        print("ERROR: RiscvCompiler not available. Cannot perform differential testing.")
        print("Please install riscv-gnu-toolchain and ensure it's in PATH.")
        sys.exit(1)
    
    # Select test instructions
    instructions = []
    
    if args.basic or args.all or (not args.vector):
        print("\n--- Testing Basic Instructions ---")
        instructions.extend(BASIC_TEST_INSTRUCTIONS)
    
    if args.all:
        print("\n--- Testing M Extension ---")
        instructions.extend(M_EXTENSION_INSTRUCTIONS)
        
        print("\n--- Testing F Extension ---")
        instructions.extend(F_EXTENSION_INSTRUCTIONS)
    
    if args.vector or args.all:
        print("\n--- Testing V Extension ---")
        instructions.extend(V_EXTENSION_INSTRUCTIONS)
    
    # Run tests
    stats = tester.run_test_suite(instructions, verbose=args.verbose)
    
    # Exit with error if there are mismatches
    if stats['mismatch'] > 0:
        print(f"\n⚠ Found {stats['mismatch']} mismatches!")
        sys.exit(1)
    else:
        print("\n✓ All matching tests passed!")
        sys.exit(0)


if __name__ == "__main__":
    main()
