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
The hybrid RISC-V instruction encoder combines a fast encoder and compiler rollback mechanism to support all RISC-V instructions
"""

try:
    from .instruction_encoder import InstructionEncoder
    from .riscv_compiler import RiscvCompiler
except ImportError:
    from instruction_encoder import InstructionEncoder
    from riscv_compiler import RiscvCompiler


class HybridEncoder:
    """
    Hybrid encoder: Prioritize the use of fast encoders and roll back to the compiler in case of failure

    Features
    - First attempt to use InstructionEncoder (supporting 97 extensions)
    - Automatically roll back to RISC-V Compiler in case of failure (supporting all instructions)
    """

    def __init__(
        self,
        march: str = "rv64imafdcv_zicsr_zifencei_zba_zbb_zbc_zbs_zfh",
        quiet: bool = False,
        architecture: str = 'default',
        arch_bits: int = 64
    ):
        """
        Initialize the hybrid encoder

        Args:
            march: The schema string used by the compiler
            quiet: Silent mode, no information is printed
            architecture: Target architecture ('ventus', 'xs', 'nts', 'rkt', or 'default')
            arch_bits: Architecture bit width (32 or 64)
        """
        self.encoder = InstructionEncoder(architecture=architecture)
        self.compiler = RiscvCompiler(default_march=march, architecture=architecture, arch_bits=arch_bits)
        self.quiet = quiet

        self.stats = {
            'encoder_success': 0,
            'fallback_used': 0,
            'total_calls': 0 
        }

    def encode(self, instruction: str) -> int:
        """
        The encoding process for a single instruction is machine code:
        
        1. Try the encoder -> If successful, return directly
        2. Use compiler Rollback -> Return
        
        For instructions with regext prefix:
        - Low 32 bits: regext instruction machine code
        - High 32 bits: main instruction machine code
        - Otherwise: High 32 bits are 0

        Args:
            instruction: RISC-V assembly instruction (may include regext prefix)

        Returns:
            64-bit machine code (low 32 bits for regext if present, high 32 bits for main instruction)

        Raises:
            RuntimeError: If both the encoder and the compiler fail
        """
        self.stats['total_calls'] += 1

        # Check if instruction contains regext prefix
        lines = instruction.strip().split('\n')
        if len(lines) > 1 and lines[0].startswith('regext '):
            # Handle regext + main instruction pair
            regext_instr = lines[0]
            main_instr = '\n'.join(lines[1:])
            
            # Encode regext instruction
            regext_code = self._encode_single(regext_instr)
            
            # Encode main instruction
            main_code = self._encode_single(main_instr)
            
            # Combine: low 32 bits for regext, high 32 bits for main instruction
            result = (main_code << 32) | regext_code
            return result
        else:
            assert len(lines) == 1
            # Single instruction without regext
            main_code = self._encode_single(instruction)
            # High 32 bits are 0
            return main_code

    def _encode_single(self, instruction: str) -> int:
        """
        Encode a single instruction without regext handling

        Args:
            instruction: RISC-V assembly instruction

        Returns:
            32-bit machine code

        Raises:
            RuntimeError: If both the encoder and the compiler fail
        """
        # Step 1: Try the encoder
        encoder_error = None
        try:
            result = self.encoder.encode(instruction)
            self.stats['encoder_success'] += 1
            return result
        except ValueError as e:
            # The encoder failed. Proceed to the fallback mechanism
            encoder_error = e
            #print('fallback compiler for {}'.format(instruction))

        # Step 2: Compiler rollback
        try:
            self.stats['fallback_used'] += 1
            result = self.compiler.compile_instruction(instruction)
            return result
        except RuntimeError as compiler_error:
            # Both the encoder and the compiler failed, throwing a detailed error
            raise RuntimeError(
                f"Failed to encode instruction: '{instruction}'\n"
                f"Encoder error: {encoder_error}\n"
                f"Compiler error: {compiler_error}"
            )

    def encode_multiple(self, instructions: list[str]) -> list[int]:
        """
        Batch encode multiple instructions

        Args:
            instructions: Instruction list

        Returns:
            Machine code list
        """
        return [self.encode(inst) for inst in instructions]

    def encode_to_hex(self, instruction: str) -> str:
        """
        Encode the instruction and return a hexadecimal string

        Args:
            instruction: RISC-V assembly instruction

        Returns:
            Hexadecimal machine code string (such as "0x003100b3" for single instruction)
            or "0xmain_instructionregext_instruction" for regext paired instructions
        """
        machine_code = self.encode(instruction)
        # Check if this is a regext pair (has high 32 bits set)
        if machine_code > 0xFFFFFFFF:
            # 64-bit result with regext
            return f"0x{machine_code:016x}"
        else:
            # 32-bit result
            return f"0x{machine_code:08x}"

    def encode_to_bytes(self, instruction: str) -> bytes:
        """
        Encode the instruction and return the byte sequence (little-endian order)

        Args:
            instruction: RISC-V assembly instruction

        Returns:
            4-byte machine code for single instruction, or 8-byte for regext paired instructions
        """
        machine_code = self.encode(instruction)
        # Check if this is a regext pair (has high 32 bits set)
        if machine_code > 0xFFFFFFFF:
            # 64-bit result with regext
            return machine_code.to_bytes(8, byteorder='little')
        else:
            # 32-bit result
            return machine_code.to_bytes(4, byteorder='little')

    def get_stats(self) -> dict:
        """
        Obtain performance statistics

        Returns:
            Statistical dictionary, including:
            - encoder_success: Number of successful encoders
            - fallback_used: The number of compiler fallbacks
            - total_calls: Total number of calls
            - encoder_hit_rate: Encoder hit rate
            - fallback_rate: Compiler rollback rate
        """
        total = self.stats['total_calls']
        if total == 0:
            return {
                **self.stats,
                'encoder_hit_rate': 0.0,
                'fallback_rate': 0.0
            }

        return {
            **self.stats,
            'encoder_hit_rate': self.stats['encoder_success'] / total,
            'fallback_rate': self.stats['fallback_used'] / total
        }

    def print_stats(self):
        """ Print Performance Statistics """
        stats = self.get_stats()
        total = stats['total_calls']

        print("\n" + "="*60)
        print("HybridEncoder Performance Statistics")
        print("="*60)
        print(f"Total calls:           {total}")
        print(f"Encoder success:       {stats['encoder_success']:6d} ({stats['encoder_hit_rate']*100:5.1f}%)")
        print(f"Fallback used:         {stats['fallback_used']:6d} ({stats['fallback_rate']*100:5.1f}%)")
        print("="*60 + "\n")


if __name__ == "__main__":
    # Test the hybrid encoder
    encoder = HybridEncoder()

    print("Testing HybridEncoder with various instructions...\n")

    # Standard instructions (encoders should be used)
    standard_instructions = [
        "add x1, x2, x3",
        "addi x1, x2, 100",
        "lw x1, 100(x2)",
        "sw x2, 200(x3)",
    ]

    print("Standard instructions (should use encoder):")
    for inst in standard_instructions:
        result = encoder.encode_to_hex(inst)
        print(f"  {inst:30s} -> {result}")

    
    encoder.print_stats()

    print("\nNote: For testing fallback with P/B extension instructions,")
    print("those instructions need to be added manually in a test environment.")
