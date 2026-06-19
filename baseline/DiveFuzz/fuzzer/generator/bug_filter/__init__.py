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

from .filters import get_generator_filter, get_known_bugs, match_bug
import json
from typing import Dict, List, Optional

_DEFAULT_FILTERING_BUGS = {
    # Ventus
    "amo_dec",
    "var_thread_num",
    "csr_tid",
    "vmv_x_s",
    "vmask",
    "vec_nfield",
    "vftta_vv_tmask",
    # Vortex
    "vx_wmma",
    "wb_fp_rd_zero",
    "wb_zero",
    "jalr_align",
}

FUZZGPU_GENERATOR_BUG_DEFAULTS: Dict[str, bool] = {
    bug_name: True for bug_name in _DEFAULT_FILTERING_BUGS
}
FUZZGPU_GENERATOR_BUG_DEFAULTS["inactive_warp_barrier"] = False
FUZZGPU_GENERATOR_BUG_DEFAULTS["global_bar"] = True


class Filter:
    def __init__(self):
        self.registry = {}
        self.architecture = "default"
        self.generator_filter_enabled = False
        self.generator_bug_map = {
            bug_name: False for bug_name in FUZZGPU_GENERATOR_BUG_DEFAULTS
        }

    def set_architecture(self, architecture: str) -> None:
        self.architecture = architecture
        self.registry = get_known_bugs(architecture)

    def filter_known_bug(self, instr_op: str, dest_values: List[int], source_values: List[int]) -> Optional[str]:
        """
        Check if an instruction triggers a known bug based on register values

        Matches actual register VALUES (not indices) against bug patterns.
        Pattern format: (dest_pattern, source_pattern1, source_pattern2, ...)

        Args:
            instr_op: Instruction opcode (e.g., "sc.w", "csrrw")
            dest_values: Destination register VALUES after execution (usually 1 element)
            source_values: Source register VALUES before execution

        Returns:
            Bug name if instruction matches a known bug pattern, None otherwise
        """
        # Combine dest and source values for pattern matching
        # Pattern format: (dest1, dest2, ..., src1, src2, ...)
        all_values = dest_values + source_values
        return match_bug(self.registry, instr_op, all_values)

    def load_generator_filter_json(self, path: Optional[str], default_filtering: bool = True) -> None:
        """Load fuzzgpu-compatible generator bug filters.

        When no JSON path is provided this feature is disabled, preserving the
        original DiveFuzz generation behavior exactly.
        """
        if path is None:
            self.generator_filter_enabled = False
            self.generator_bug_map = {
                bug_name: False for bug_name in FUZZGPU_GENERATOR_BUG_DEFAULTS
            }
            return

        self.generator_filter_enabled = True
        self.generator_bug_map = {
            bug_name: default_filtering
            for bug_name in _DEFAULT_FILTERING_BUGS
        }
        self.generator_bug_map["inactive_warp_barrier"] = False
        self.generator_bug_map["global_bar"] = True

        try:
            with open(path, "r") as f:
                data = json.load(f)
        except Exception:
            return

        if isinstance(data, dict):
            bug_names = data.get("filtered_bugs", [])
        elif isinstance(data, list):
            bug_names = data
        else:
            bug_names = []

        for bug_name in bug_names:
            if isinstance(bug_name, str) and bug_name in self.generator_bug_map:
                self.generator_bug_map[bug_name] = True

    def filter(self, bug_name: str) -> bool:
        if bug_name not in FUZZGPU_GENERATOR_BUG_DEFAULTS:
            raise KeyError(f"unknown fuzzgpu generator bug filter: {bug_name}")
        if not self.generator_filter_enabled:
            return False
        return self.generator_bug_map[bug_name]

    def should_filter_instruction(self, instr_name: str, extension: str, instr_format: dict, architecture: str) -> bool:
        if not self.generator_filter_enabled:
            return False
        return get_generator_filter(architecture).should_filter_instruction(
            self.filter, instr_name, extension, instr_format
        )

    def filter_operand_choices(self, var_name: str, choices: List[str], instr_name: str, extension: str) -> List[str]:
        if not self.generator_filter_enabled:
            return choices

        return get_generator_filter(self.architecture).filter_operand_choices(
            self.filter, var_name, choices, instr_name, extension
        )

    def allow_global_barrier(self) -> bool:
        if not self.generator_filter_enabled:
            return True
        return get_generator_filter(self.architecture).allow_global_barrier(self.filter)

bug_filter = Filter()
