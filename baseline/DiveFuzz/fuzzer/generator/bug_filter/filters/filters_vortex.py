# Copyright (c) 2024-2025 Institute of Information Engineering, Chinese Academy of Sciences
#
# DiveFuzz is licensed under Mulan PSL v2.

from typing import Callable, List

from . import Registry


def register(reg: Registry) -> None:
    pass


def should_filter_instruction(filter_bug: Callable[[str], bool],
                              instr_name: str,
                              extension: str,
                              instr_format: dict) -> bool:
    return filter_bug("vx_wmma") and instr_name == "vx_wmma"


def _is_float_instr(instr_name: str, extension: str) -> bool:
    return extension in {"RV_F", "RV_D", "RV64_F", "RV64_D", "RV_F_ZFINX"} or instr_name.startswith("f")


def filter_operand_choices(filter_bug: Callable[[str], bool],
                           var_name: str,
                           choices: List[str],
                           instr_name: str,
                           extension: str) -> List[str]:
    filtered = choices
    if filter_bug("wb_zero"):
        if var_name == "RD":
            filtered = [choice for choice in filtered if choice != "zero"]
        elif var_name == "FRD":
            filtered = [choice for choice in filtered if choice != "ft0"]

    if filter_bug("wb_fp_rd_zero") and var_name == "RD" and _is_float_instr(instr_name, extension):
        filtered = [choice for choice in filtered if choice != "zero"]

    return filtered or choices


def allow_global_barrier(filter_bug: Callable[[str], bool]) -> bool:
    return not filter_bug("global_bar")
