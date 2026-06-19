# Copyright (c) 2024-2025 Institute of Information Engineering, Chinese Academy of Sciences
#
# DiveFuzz is licensed under Mulan PSL v2.

from typing import Callable, List

from . import Registry


_VMASK_INSTRS = {
    "vmand.mm",
    "vmandn.mm",
    "vmnand.mm",
    "vmnor.mm",
    "vmor.mm",
    "vmorn.mm",
    "vmxnor.mm",
    "vmxor.mm",
}


def register(reg: Registry) -> None:
    pass


def _categories(instr_format: dict) -> set:
    category = instr_format.get("category", "")
    if isinstance(category, str):
        return {category}
    return set(category)


def should_filter_instruction(filter_bug: Callable[[str], bool],
                              instr_name: str,
                              extension: str,
                              instr_format: dict) -> bool:
    categories = _categories(instr_format)
    if filter_bug("amo_dec") and (extension in {"RV_A", "RV64_A"} or "AMO" in categories):
        return True
    if filter_bug("vmv_x_s") and instr_name == "vmv.x.s":
        return True
    if filter_bug("vmask") and instr_name in _VMASK_INSTRS:
        return True
    if filter_bug("inactive_warp_barrier") and instr_name == "barrier":
        return True
    return False


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
    return True
