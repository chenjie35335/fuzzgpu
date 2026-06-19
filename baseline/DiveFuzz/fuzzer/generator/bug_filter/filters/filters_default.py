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
    return False


def filter_operand_choices(filter_bug: Callable[[str], bool],
                           var_name: str,
                           choices: List[str],
                           instr_name: str,
                           extension: str) -> List[str]:
    return choices


def allow_global_barrier(filter_bug: Callable[[str], bool]) -> bool:
    return True
