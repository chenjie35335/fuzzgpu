# Copyright 2026
# SPDX-License-Identifier: GPL-3.0-only

"""Generation-time filters for fuzzgpu-compatible bug names.

The fuzzgpu frontend feeds back detected bug names through a JSON file of the
form {"filtered_bugs": ["bug_name", ...]}.  Cascade uses the same file format
and only accepts the bug names registered by fuzzgpu's BugFilter.
"""

from __future__ import annotations

import json
from pathlib import Path
from typing import Iterable, Mapping, MutableMapping, Optional, Set


SUPPORTED_BUGS = {
    "amo_dec",
    "var_thread_num",
    "csr_tid",
    "vmv_x_s",
    "vmask",
    "vec_nfield",
    "vftta_vv_tmask",
    "inactive_warp_barrier",
    "vx_wmma",
    "wb_fp_rd_zero",
    "wb_zero",
    "jalr_align",
    "global_bar",
}


_INSTR_FILTERS: Mapping[str, Set[str]] = {
    # Vortex

    "vx_wmma": {"vx_wmma"},

    # Ventus

    "vmv.x.s": {"vmv_x_s"},

    "vmand.mm": {"vmask"},
    "vmandn.mm": {"vmask"},
    "vmnand.mm": {"vmask"},
    "vmnor.mm": {"vmask"},
    "vmor.mm": {"vmask"},
    "vmorn.mm": {"vmask"},
    "vmxnor.mm": {"vmask"},
    "vmxor.mm": {"vmask"},

    "barrier": {"inactive_warp_barrier"},
}


class CascadeBugFilter:
    def __init__(self) -> None:
        self.filtered_bugs: Set[str] = set()
        self.enabled = False

    def set_filtered_bugs(self, bug_names: Iterable[str]) -> None:
        self.filtered_bugs = {
            name
            for name in bug_names
            if isinstance(name, str) and name in SUPPORTED_BUGS
        }

    def load_json(self, path: Optional[str]) -> None:
        if not path:
            self.filtered_bugs = set()
            self.enabled = False
            return

        filter_path = Path(path)
        if not filter_path.exists():
            self.filtered_bugs = set()
            self.enabled = False
            return

        with filter_path.open("r") as f:
            data = json.load(f)

        if isinstance(data, dict):
            bug_names = data.get("filtered_bugs", [])
        elif isinstance(data, list):
            bug_names = data
        else:
            bug_names = []
        self.set_filtered_bugs(bug_names)
        self.enabled = True

    def is_filtered(self, *bug_names: str) -> bool:
        return any(name in self.filtered_bugs for name in bug_names)

    def is_empty(self) -> bool:
        return not self.filtered_bugs

    def is_enabled(self) -> bool:
        return self.enabled

    def should_filter_instr(self, instr_str: str, design_name: str) -> bool:
        del design_name
        bug_names = _INSTR_FILTERS.get(instr_str)
        return bool(bug_names and self.is_filtered(*bug_names))

    def filter_instr_weights(self, instr_weights: MutableMapping[str, float], design_name: str) -> None:
        if self.is_empty():
            return
        for instr_str in list(instr_weights):
            if self.should_filter_instr(instr_str, design_name):
                instr_weights[instr_str] = 0

    def filter_isa_weights(
        self,
        isa_weights: MutableMapping[object, float],
        instructions_by_isa_class: Mapping[object, Iterable[str]],
        design_name: str,
    ) -> None:
        if self.is_empty():
            return

        # Ventus AMO decoding bug: fuzzgpu removes the AMO instruction classes.
        if "ventus" in design_name and self.is_filtered("amo_dec"):
            for isaclass in isa_weights:
                if getattr(isaclass, "name", "") in ("AMO", "AMO64"):
                    isa_weights[isaclass] = 0

        # If every instruction in a class is filtered, remove the class to avoid
        # selecting it and then spinning on a zero-weight instruction set.
        for isaclass, instrs in instructions_by_isa_class.items():
            instrs = tuple(instrs)
            if instrs and all(self.should_filter_instr(instr_str, design_name) for instr_str in instrs):
                isa_weights[isaclass] = 0

    def filter_vortex_special_weights(self, instr_weights: MutableMapping[str, float], design_name: str) -> None:
        self.filter_instr_weights(instr_weights, design_name)

    def allow_global_barrier(self) -> bool:
        return not self.is_filtered("global_bar")

    def allow_int_zero_dest(self) -> bool:
        return not self.is_filtered("wb_zero")

    def allow_float_zero_dest(self) -> bool:
        return not self.is_filtered("wb_zero")

    def allow_float_int_zero_dest(self) -> bool:
        return not self.is_filtered("wb_zero", "wb_fp_rd_zero")

    def allow_jalr_low_bit_randomization(self) -> bool:
        return not self.is_filtered("jalr_align")


_GLOBAL_FILTER = CascadeBugFilter()


def get_bug_filter() -> CascadeBugFilter:
    return _GLOBAL_FILTER


def load_bug_filter_json(path: Optional[str]) -> None:
    _GLOBAL_FILTER.load_json(path)
