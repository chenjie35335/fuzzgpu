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
Spike Session Manager

Manages spike_engine lifecycle and provides high-level instruction validation API.
Uses checkpoint-based rollback for efficient instruction retry without recompilation.
"""

import sys
import os
import json
import fcntl
import importlib
from pathlib import Path
from typing import Optional, List, Tuple
import traceback

def get_spike_engine_path(architecture: str = 'default', arch_bits: int = 64) -> Path:
    """
    Get spike_engine path based on target architecture
    
    Args:
        architecture: Target architecture ('ventus', 'xs', 'nts', 'rkt', or 'default')
    
    Returns:
        Path to spike_engine module
    """
    base_path = Path(__file__).parent.parent.parent.parent
    
    if architecture == 'ventus':
        # Ventus uses gpu-isa-emulator
        return base_path / "ref" / "gpu-isa-emulator" / "build32-ventus"
    elif architecture == 'vortex':
        return base_path / "ref" / "gpu-isa-emulator" / f"build{arch_bits}-vortex"
    elif architecture in ['xs', 'nts', 'rkt', 'default']:
        # Standard RISC-V architectures use riscv-isa-sim-adapter
        return base_path / "ref" / "riscv-isa-sim-adapter" / "spike_engine"
    else:
        # Unknown architecture, use default
        return base_path / "ref" / "riscv-isa-sim-adapter" / "spike_engine"


# Cache for loaded spike_engine modules per architecture
_spike_engine_cache = {}
_loaded_architectures = set()


def _load_spike_engine_for_arch(architecture: str = 'default', arch_bits: int = 64):
    """
    Dynamically load spike_engine module for specific architecture
    
    Args:
        architecture: Target architecture
    
    Returns:
        tuple of (spike_engine_module, is_available, IMMEDIATE_NOT_PRESENT)
    
    Raises:
        RuntimeError: If spike_engine cannot be loaded
    """
    # Return cached version if already loaded
    if architecture in _spike_engine_cache:
        se, avail, immed = _spike_engine_cache[architecture]
        return se, avail, immed
    
    spike_engine_path = get_spike_engine_path(architecture, arch_bits)
    path_str = str(spike_engine_path)

    print(f"Loading ISA emulator for architecture '{architecture}' from: {path_str}")
    
    # Remove old paths to avoid conflicts
    old_paths = [p for p in sys.path if 'spike_engine' in p or 'gpu-isa-emulator' in p]
    for old_path in old_paths:
        if old_path in sys.path:
            sys.path.remove(old_path)
    
    # Add new path to beginning
    if path_str not in sys.path:
        sys.path.insert(0, path_str)
    
    try:
        # Dynamically import spike_engine
        if 'spike_engine' in sys.modules:
            # Remove cached module to force reload from new path
            del sys.modules['spike_engine']
        
        import spike_engine
        
        # Cache the result
        IMMEDIATE_NOT_PRESENT = getattr(spike_engine, 'IMMEDIATE_NOT_PRESENT', -(2**63))
        _spike_engine_cache[architecture] = (spike_engine, True, IMMEDIATE_NOT_PRESENT)
        _loaded_architectures.add(architecture)
        
        return spike_engine, True, IMMEDIATE_NOT_PRESENT
        
    except ImportError as e:
        print(f"\n[WARNING] spike_engine import failed for architecture '{architecture}'")
        print(f"  Import error: {e}")
        print(f"  Expected path: {spike_engine_path}")
        print(f"  Path exists: {spike_engine_path.exists()}")
        print(f"  Python version: {sys.version_info.major}.{sys.version_info.minor}.{sys.version_info.micro}")
        
        if spike_engine_path.exists():
            so_files = list(spike_engine_path.glob("spike_engine*.so"))
            print(f"  Found .so files: {len(so_files)}")
            for so in so_files:
                print(f"    - {so.name}")
        
            if not so_files:
                print(f"  → spike_engine not built. Run: cd {spike_engine_path} && make")
            else:
                expected_name = f"spike_engine.cpython-{sys.version_info.major}{sys.version_info.minor}"
                matching = [s for s in so_files if expected_name in s.name]
                if not matching:
                    print(f"  → No .so file matches current Python {sys.version_info.major}.{sys.version_info.minor}")
                    print(f"    Rebuild with: cd {spike_engine_path} && make clean && make")
        else:
            print(f"  → spike_engine directory not found")
            print(f"    Initialize submodule: git submodule update --init --recursive")
        print()
        
        # Cache failure
        _spike_engine_cache[architecture] = (None, False, -(2**63))
        raise RuntimeError(f"Cannot load spike_engine for architecture '{architecture}'")


# Module-level defaults (spike_engine will be loaded on first SpikeSession creation)
spike_engine = None
SPIKE_ENGINE_AVAILABLE = False
IMMEDIATE_NOT_PRESENT = -(2**63)


def is_spike_engine_available(architecture: str = 'default', arch_bits: int = 64) -> bool:
    """
    Check if spike_engine is available for the specified architecture.
    This function will attempt to load the spike_engine if not already loaded.
    
    Args:
        architecture: Target architecture ('ventus', 'xs', 'nts', 'rkt', or 'default')
    
    Returns:
        True if spike_engine is available and loaded, False otherwise
    """
    try:
        _, is_available, _ = _load_spike_engine_for_arch(architecture, arch_bits)
        return is_available
    except Exception:
        return False


def compute_xor(values: List[int]) -> int:
    """
    Compute XOR value from register values using shifted XOR.
    Replicates the C++ compute_xor algorithm.

    Formula: result = (values[0] << 0) ^ (values[1] << 1) ^ (values[2] << 2) ^ ...

    Args:
        values: List of register values (and optional immediate)

    Returns:
        Computed XOR value as uint64_t equivalent
    """
    result = 0
    for i, value in enumerate(values):
        result ^= (value << i)
    return result


class SpikeSession:
    """
    High-level Spike session manager with checkpoint support

    Workflow:
    1. Initialize: Load ELF with N nops, execute template initialization
    2. For each instruction:
       - Set checkpoint (save current state)
       - Try instruction: encode + execute + compute XOR
       - If unique: confirm (keep state)
       - If duplicate: restore checkpoint (rollback)
    3. Cleanup: Release resources

    Performance: O(n) instead of O(n^2) for n instructions
    """

    def __init__(self, elf_path: str, isa: str, num_instrs: int, shared_xor_cache, architecture: str = 'default', arch_bits: int = 64):
        """
        Create Spike session

        Args:
            elf_path: Path to ELF file with N nop instructions
            isa: ISA string (e.g., "rv64imafdcv_zicsr_zifencei")
            num_instrs: Number of instructions to generate
            shared_xor_cache: Manager.dict for real-time cross-process XOR sharing
            architecture: Target architecture ('ventus', 'xs', 'nts', 'rkt', or 'default')

        Raises:
            RuntimeError: If spike_engine is not available
        """
        # Load architecture-specific spike_engine
        try:
            self.spike_engine_module, is_available, self.IMMEDIATE_NOT_PRESENT = _load_spike_engine_for_arch(architecture, arch_bits)
            if not is_available:
                raise RuntimeError(f"spike_engine not available for architecture '{architecture}'")
        except Exception as e:
            traceback.print_exec()
            raise RuntimeError(f"Failed to load spike_engine for architecture '{architecture}': {e}")

        self.elf_path = elf_path
        self.isa = isa
        self.num_instrs = num_instrs
        self.architecture = architecture

        # Spike engine instance
        self.engine = None  # Will be SpikeEngine when initialized

        # Shared XOR cache (Manager.dict)
        self.confirmed_xor_values = shared_xor_cache

        # State tracking
        self.confirmed_instructions: List[str] = []
        self.checkpoint_set: bool = False
        self.initialized: bool = False

    def initialize(self) -> bool:
        """
        Initialize Spike engine and execute template initialization

        This loads the ELF, runs until the first nop, and prepares for
        instruction-by-instruction execution.

        Returns:
            True on success, False on error

        Side effects:
            - Creates spike_engine instance
            - Executes template initialization code (register setup, etc.)
            - Sets initialized flag to True
        """
        try:
            # Create engine (verbose=False for silent operation)
            self.engine = self.spike_engine_module.SpikeEngine(
                self.elf_path,
                self.isa,
                self.num_instrs,
                verbose=False
            )

            # Initialize (runs template init code until first nop)
            if not self.engine.initialize():
                error_msg = self.engine.get_last_error()
                print(f"[SpikeSession] Initialization failed: {error_msg}")
                return False

            self.initialized = True
            # print(f"[SpikeSession] Initialized successfully")
            # print(f"  ELF: {self.elf_path}")
            # print(f"  ISA: {self.isa}")
            # print(f"  Num instrs: {self.num_instrs}")
            # print(f"  Initial PC: {hex(self.engine.get_pc())}")

            return True

        except Exception as e:
            print(f"[SpikeSession] Exception during initialization: {e}")
            import traceback
            traceback.print_exc()
            return False

    def execute_instruction(self, machine_code: int):
        self.engine.execute_instruction(
                    machine_code)

    def try_instruction(
        self,
        machine_code: int,
        source_regs: List[str],
        dest_regs: List[str],
        immediate: int = 0,
        opcode: str = "unknown"
    ) -> Tuple[Optional[int], List[int], List[int]]:
        """
        Try executing an instruction and check XOR uniqueness within its opcode group

        This method:
        1. Sets checkpoint if not already set
        2. Executes instruction with spike_engine (gets source values before, dest values after)
        3. Computes XOR value from source values
        4. Checks uniqueness against opcode-specific confirmed_xor_values
        5. Returns (XOR, dest_values, source_values) if unique, (None, [], []) if duplicate

        Args:
            machine_code: 32-bit machine code
            source_regs: List of source register indices (read before execution)
            dest_regs: List of destination register indices (read after execution)
            immediate: Immediate value (default: 0)
            opcode: Instruction opcode name (e.g., "add", "sub", "addi")

        Returns:
            Tuple of (xor_value, dest_values, source_values):
            - xor_value: XOR if unique within opcode group, None if duplicate or error
            - dest_values: Destination register values after execution
            - source_values: Source register values before execution (for bug filtering)

        Note:
            This method does NOT modify confirmed state.
            Call confirm_instruction() after getting a unique XOR.

        Design rationale:
            Different instruction types (add vs sub) should have independent XOR pools,
            as they test different hardware units even with same source register values.
        """
        if not self.initialized:
            raise RuntimeError("Session not initialized. Call initialize() first.")

        try:
            # Set checkpoint if not already set
            # (First instruction or after confirm_instruction)
            if not self.checkpoint_set:
                self.engine.set_checkpoint()
                self.checkpoint_set = True

            if False:
                # Execute instruction and get execution result
                execution_result = self.engine.execute_instruction(
                    machine_code,
                    source_regs,
                    dest_regs,
                    immediate
                )
                # Extract values from execution result
                source_values = list(execution_result.source_values_before)
                dest_values = list(execution_result.dest_values_after)

            else:
                # cannot create checkpoint and restore for GPUs, so we implement a workaround here
                source_values = self.engine.get_operands(
                    source_regs,
                    immediate
                )
                class ExecResult:
                    def __init__(self, src_vals):
                        self.source_values_before = src_vals
                        self.dest_values_after = []
                execution_result = ExecResult(source_values)
                dest_values = []

            
            # Compute XOR from source register values (before execution)
            xor_value = compute_xor(execution_result.source_values_before)

            # Create XOR list for this opcode if not exists
            # Retry mechanism for Manager connection issues
            max_retries = 3
            retry_delay = 0.01  # 10ms

            for retry in range(max_retries):
                try:
                    if opcode not in self.confirmed_xor_values:
                        self.confirmed_xor_values[opcode] = []

                    # Check uniqueness (list membership check)
                    if xor_value in self.confirmed_xor_values[opcode]:
                        # Duplicate XOR within this opcode group, restore checkpoint
                        self.engine.restore_checkpoint()
                        return None, [], []
                    else:
                        # Unique XOR within this opcode group
                        return xor_value, dest_values, source_values

                except (TypeError, BrokenPipeError, EOFError) as e:
                    if retry < max_retries - 1:
                        # Retry with exponential backoff
                        import time
                        time.sleep(retry_delay * (2 ** retry))
                        continue
                    else:
                        # All retries failed - this is a serious error
                        print(f"[SpikeSession] CRITICAL: Manager connection failed after {max_retries} retries")
                        print(f"  Opcode: {opcode}, Error: {e}")
                        print(f"  Cross-process XOR sharing is broken, terminating process")
                        try:
                            self.engine.restore_checkpoint()
                        except:
                            pass
                        # Raise exception to terminate this worker process
                        raise RuntimeError(
                            f"Manager connection lost and cannot be recovered. "
                            f"This worker process must terminate to prevent generating duplicate XOR values."
                        )

        except Exception as e:
            # Print detailed exception information
            import traceback
            print(f"[SpikeSession] Exception in try_instruction:")
            print(f"  Exception type: {type(e).__name__}")
            print(f"  Exception message: {str(e)}")
            print(f"  Machine code: 0x{machine_code:08x}")
            print(f"  Source regs: {source_regs}")
            print(f"  Dest regs: {dest_regs}")
            print(f"  Immediate: {immediate}")
            # Print traceback for debugging
            traceback.print_exc()
            # Try to restore checkpoint on error
            try:
                if self.checkpoint_set:
                    self.engine.restore_checkpoint()
            except:
                pass
            return None, [], []

    def confirm_instruction(self, xor_value: int, instruction: str = "", opcode: str = "unknown"):
        """
        Confirm an instruction as accepted

        This should be called after try_instruction returns a unique XOR.
        It updates the confirmed state and prepares for the next instruction.

        Args:
            xor_value: XOR value to add to opcode-specific list
            instruction: Instruction string (optional, for debugging)
            opcode: Instruction opcode name (e.g., "add", "sub", "addi")

        Side effects:
            - Adds xor_value to opcode-specific confirmed_xor_values list
            - Appends instruction to confirmed_instructions
            - Clears checkpoint_set flag (next try will set new checkpoint)
        """
        # Retry mechanism for Manager connection issues
        max_retries = 3
        retry_delay = 0.01  # 10ms

        for retry in range(max_retries):
            try:
                # IMPORTANT: Manager.dict requires reassignment to sync nested changes
                if opcode not in self.confirmed_xor_values:
                    self.confirmed_xor_values[opcode] = []

                # Get current list, append value, then reassign to trigger sync
                current_list = self.confirmed_xor_values[opcode]
                current_list = list(current_list)  # Convert to regular list
                current_list.append(xor_value)
                self.confirmed_xor_values[opcode] = current_list  # Reassign to trigger sync

                if instruction:
                    self.confirmed_instructions.append(instruction)

                # Clear checkpoint flag - next try_instruction will set new checkpoint
                self.checkpoint_set = False
                return  # Success

            except (TypeError, BrokenPipeError, EOFError) as e:
                if retry < max_retries - 1:
                    # Retry with exponential backoff
                    import time
                    time.sleep(retry_delay * (2 ** retry))
                    continue
                else:
                    # All retries failed - this is a serious error
                    print(f"[SpikeSession] CRITICAL: Manager connection failed in confirm_instruction after {max_retries} retries")
                    print(f"  Opcode: {opcode}, Error: {e}")
                    print(f"  Cross-process XOR sharing is broken, terminating process")
                    # Raise exception to terminate this worker process
                    raise RuntimeError(
                        f"Manager connection lost and cannot be recovered. "
                        f"This worker process must terminate to prevent generating duplicate XOR values."
                    )

    def restore_checkpoint_and_reset(self):
        """
        Restore checkpoint and reset checkpoint_set flag

        This method should be called when rejecting an instruction
        (e.g., due to bug filter) to ensure proper state management.

        Side effects:
            - Restores processor state to last checkpoint
            - Resets checkpoint_set flag so next try_instruction will set new checkpoint
        """
        if not self.initialized:
            raise RuntimeError("Session not initialized")

        self.engine.restore_checkpoint()
        self.checkpoint_set = False

    def get_confirmed_count(self) -> int:
        """
        Get number of confirmed instructions

        Returns:
            Number of instructions confirmed so far
        """
        return len(self.confirmed_instructions)

    def get_current_pc(self) -> int:
        """
        Get current program counter

        Returns:
            Current PC value

        Raises:
            RuntimeError: If session not initialized
        """
        if not self.initialized:
            raise RuntimeError("Session not initialized")
        return self.engine.get_pc()

    def get_xpr(self, reg_index: int) -> int:
        """
        Get general-purpose register value

        Args:
            reg_index: Register index (0-31)

        Returns:
            Register value

        Raises:
            RuntimeError: If session not initialized
        """
        if not self.initialized:
            raise RuntimeError("Session not initialized")
        return self.engine.get_xpr(reg_index)

    def get_fpr(self, reg_index: int) -> int:
        """
        Get floating-point register value

        Args:
            reg_index: Register index (0-31)

        Returns:
            Register value (as uint64)

        Raises:
            RuntimeError: If session not initialized
        """
        if not self.initialized:
            raise RuntimeError("Session not initialized")
        return self.engine.get_fpr(reg_index)


    def cleanup(self):
        """
        Cleanup resources

        Should be called when session is no longer needed.
        Note: With shared cache mode, no need to save cache (already in shared memory).
        """
        self.engine = None
        self.initialized = False
        self.checkpoint_set = False

    def __enter__(self):
        """Context manager entry"""
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        """Context manager exit - cleanup resources"""
        self.cleanup()


if __name__ == "__main__":
    print("SpikeSession module")
    print(f"Spike engine available: {SPIKE_ENGINE_AVAILABLE}")
    if SPIKE_ENGINE_AVAILABLE:
        print(f"Version: {spike_engine.__version__}")
