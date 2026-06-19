#!/usr/bin/env python3
"""Bug detector for emulator logs.

Scans a target directory for files named `emu.log` or matching `*.emu.log`,
finds known bug patterns, and prints matches (JSON by default).

Current patterns:
- vftta_vv_wb: `vftta.vv` combined with `REF rd %* not in DUT [%*` errors.

The detector is intentionally structured so new patterns can be added by
registering additional `LogPattern` implementations in `PATTERNS`.

All the undetected bugs should be reported to the unknown list (if specificed)
"""

from __future__ import annotations

import argparse
import ctypes
import json
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Optional


ANSI_RE = re.compile(r"\x1b\[[0-9;]*m")
COMMIT_RE = re.compile(
    r"Commit\s*:\s*PC\s*=\s*(?P<pc>0x[0-9a-fA-F]+)\s*\[Timestamp:\s*(?P<ts>\d+)s\]"
)
ARF_OUT_RE = re.compile(r"ARF Out:\s*(?P<reg>v\d+)\s*=\s*\[(?P<vals>[^\]]*)\]")
ARF_IN_RE = re.compile(r"ARF In:\s*(?P<rest>.*)")

# Ventus default threads per core
VENTUS_NR_THREADS_PER_CORE = 32


def strip_ansi(text: str) -> str:
    """Remove ANSI color codes from a string."""

    return ANSI_RE.sub("", text)


def parse_arf_line(line: str) -> Dict[str, List[str]]:
    """Parse an ARF line into a mapping of register -> list of values."""

    clean = strip_ansi(line)
    out: Dict[str, List[str]] = {}
    # Handle ARF list registers, e.g. v1 = [...] or x1 = [...].
    for match in re.finditer(r"([vxf]\d+)\s*=\s*\[(.*?)\]", clean):
        reg = match.group(1)
        vals_raw = match.group(2)
        vals = [v.strip() for v in vals_raw.split(",") if v.strip()]
        out[reg] = vals
    # Handle scalar registers, e.g. x1 = 0x... or f11 = 0x....
    for match in re.finditer(r"([vxf]\d+)\s*=\s*(0x[0-9a-fA-F]+)", clean):
        reg = match.group(1)
        val = match.group(2)
        out[reg] = [val]
    # Handle mismatch lines, e.g. thread 0 %f11: 0x... != ref 0x....
    for match in re.finditer(
        r"%(?P<reg>[rxvf]\d+):\s*(?P<dut>0x[0-9a-fA-F]+)\s*!=\s*ref\s+(?P<ref>0x[0-9a-fA-F]+)",
        clean,
    ):
        reg = match.group("reg")
        out.setdefault(reg, []).append(match.group("dut"))
        out.setdefault(f"ref_{reg}", []).append(match.group("ref"))
    return out


def find_backward(
    lines: List[str], start: int, regex: re.Pattern[str], max_scan: int = 30
) -> Optional[tuple[int, re.Match[str]]]:
    """Search backward from `start` (inclusive) for a regex match."""

    lower = max(0, start - max_scan)
    for idx in range(start, lower - 1, -1):
        line = strip_ansi(lines[idx])
        match = regex.search(line)
        if match:
            return idx, match
    return None



@dataclass
class BugMatch:
    bug_name: str
    file: str
    line: int
    timestamp: int
    pc: str
    instruction: str
    error: str
    arf_out: Dict[str, List[str]]
    arf_in: Dict[str, List[str]]


class LogPattern:
    """Interface for a log pattern."""

    name: str

    def match(
        self, lines: List[str], idx: int, path: Path
    ) -> tuple[Optional[BugMatch], Optional[BugMatch]]:
        raise NotImplementedError

    def create_partial_match(
        self, lines: List[str], idx: int, path: Path, error_msg: str
    ) -> BugMatch:
        """Create a partial match with best-effort context."""
        commit = find_backward(lines, idx, COMMIT_RE)
        ts = 0
        pc = "0x0"
        if commit:
            _, commit_match = commit
            ts = int(commit_match.group("ts"))
            pc = commit_match.group("pc")

        return BugMatch(
            bug_name=self.name + ":partial",
            file=str(path),
            line=idx + 1,
            timestamp=ts,
            pc=pc,
            instruction="?",
            error=error_msg,
            arf_out={},
            arf_in={},
        )


class RefRdNotInDutPattern(LogPattern):
    """Pattern: REF rd %* not in DUT [%*] with specific instruction."""

    def __init__(self, instruction: str, bug_name: str):
        self.instruction = instruction
        self.name = bug_name
        self.error_re = re.compile(
            r"REF rd %(?P<ref_rd>[^\s]+) not in DUT \[(?P<dut_regs>[^\]]+)"
        )
        self.disasm_re = re.compile(
            rf"REF disasm:\s*.*\b{re.escape(instruction)}\b", re.IGNORECASE
        )

    def match(
        self, lines: List[str], idx: int, path: Path
    ) -> tuple[Optional[BugMatch], Optional[BugMatch]]:
        error_line = strip_ansi(lines[idx])
        err_match = self.error_re.search(error_line)
        if not err_match:
            return None, None

        commit = find_backward(lines, idx, COMMIT_RE)
        disasm = find_backward(lines, idx, self.disasm_re)
        arf_out = find_backward(lines, idx, ARF_OUT_RE)
        arf_in = find_backward(lines, idx, ARF_IN_RE)

        if not (commit and disasm and arf_out and arf_in):
            return None, self.create_partial_match(lines, idx, path, error_line.strip())

        commit_idx, commit_match = commit
        arf_out_idx, _ = arf_out
        arf_in_idx, _ = arf_in

        ts = int(commit_match.group("ts"))
        pc = commit_match.group("pc")

        arf_out_vals = parse_arf_line(lines[arf_out_idx])
        arf_in_vals = parse_arf_line(lines[arf_in_idx])

        return BugMatch(
            bug_name=self.name,
            file=str(path),
            line=idx + 1,
            timestamp=ts,
            pc=pc,
            instruction=self.instruction,
            error=error_line.strip(),
            arf_out=arf_out_vals,
            arf_in=arf_in_vals,
        ), None


class CommitInactiveWarpPattern(LogPattern):
    """Pattern: Commit inactive warp with previous barrier."""

    def __init__(self):
        self.name = "inactive_warp_barrier"
        self.error_re = re.compile(
            r"Try to commit an inactivate \(or stalled by a barrier/wspawn\) warp \d+"
        )
        self.trace_re = re.compile(
            r"sm\s+(?P<sm>\d+)\s+warp\s+(?P<warp>\d+)\s+(?P<pc>0x[0-9a-fA-F]+)\s+(?P<inst>0x[0-9a-fA-F]+)"
        )

    def match(
        self, lines: List[str], idx: int, path: Path
    ) -> tuple[Optional[BugMatch], Optional[BugMatch]]:
        line = strip_ansi(lines[idx])
        if not self.error_re.search(line):
            return None, None

        commit = find_backward(lines, idx, COMMIT_RE)
        if not commit:
            return None, self.create_partial_match(
                lines, idx, path, line.strip() + " (missing commit)"
            )
        commit_idx, commit_match = commit

        pc1 = commit_match.group("pc")
        ts = int(commit_match.group("ts"))

        # Find line A with PC1
        line_a_idx = -1
        sm_id = None
        warp_id = None

        start_search = commit_idx
        for i in range(start_search, -1, -1):
            l = strip_ansi(lines[i])
            m = self.trace_re.search(l)
            if m and m.group("pc") == pc1:
                line_a_idx = i
                sm_id = m.group("sm")
                warp_id = m.group("warp")
                break

        if line_a_idx == -1:
            return None, self.create_partial_match(
                lines, idx, path, line.strip() + " (missing trace)"
            )

        # Check predecessor for barrier
        found_barrier = False
        for i in range(line_a_idx - 1, -1, -1):
            l = strip_ansi(lines[i])
            m = self.trace_re.search(l)
            if m and m.group("sm") == sm_id and m.group("warp") == warp_id:
                if m.group("pc") == pc1:
                    continue
                if "barrier" in l:
                    found_barrier = True
                else:
                    # Found predecessor but not barrier
                    pass
                break

        if found_barrier:
            return BugMatch(
                bug_name=self.name,
                file=str(path),
                line=idx + 1,
                timestamp=ts,
                pc=pc1,
                instruction="barrier",
                error=line.strip(),
                arf_out={},
                arf_in={},
            ), None
        
        # Barrier not found, partial match
        return None, self.create_partial_match(
            lines, idx, path, line.strip() + " (no barrier found)"
        )


def _mulhsu_calc_matches(signed_val: int, unsigned_val: int, out_val: int) -> bool:
    """Check if mulhsu calculation matches: (signed * unsigned) >> 32 == out."""
    s = ctypes.c_int32(signed_val).value
    u = ctypes.c_uint32(unsigned_val).value
    calc = ctypes.c_uint32(s * u).value
    return calc == out_val


def _mulhu_calc_matches(unsigned_val1: int, unsigned_val2: int, out_val: int) -> bool:
    """Check if mulhu calculation matches: (unsigned * unsigned) >> 32 == out."""
    u1 = ctypes.c_uint32(unsigned_val1).value
    u2 = ctypes.c_uint32(unsigned_val2).value
    calc = ctypes.c_uint32(u1 * u2).value
    return calc == out_val


def _find_arf_forward(lines: List[str], idx: int, max_scan: int = 6) -> tuple[Optional[Dict[str, List[str]]], Optional[Dict[str, List[str]]]]:
    """Look forward for ARF Out and ARF In lines."""
    arf_out = None
    arf_in = None
    for i in range(idx + 1, min(idx + max_scan, len(lines))):
        l = strip_ansi(lines[i])
        if "ARF Out:" in l:
            arf_out = parse_arf_line(l)
        if "ARF In:" in l:
            arf_in = parse_arf_line(l)
    return arf_out, arf_in


def _get_commit_info(lines: List[str], idx: int) -> tuple[int, str]:
    """Get timestamp and PC from commit line."""
    commit = find_backward(lines, idx, COMMIT_RE)
    ts = 0
    pc = "0x0"
    if commit:
        _, commit_match = commit
        ts = int(commit_match.group("ts"))
        pc = commit_match.group("pc")
    return ts, pc


class VmulhsuVxPattern(LogPattern):
    """Pattern: Check vmulhsu.vx calculation (vector-scalar)."""
    
    def __init__(self):
        self.name = "ventus_hw_vmulhsu_vx"
        self.disasm_re = re.compile(
            r"REF disasm:\s*vmulhsu\.vx\s+(?P<vd>v\d+),\s*(?P<vs2>v\d+),\s*(?P<rs1>x\d+)", re.IGNORECASE
        )

    def match(
        self, lines: List[str], idx: int, path: Path
    ) -> tuple[Optional[BugMatch], Optional[BugMatch]]:
        line = strip_ansi(lines[idx])
        match = self.disasm_re.search(line)
        if not match:
            return None, None
        
        vd = match.group("vd")
        vs2 = match.group("vs2")
        rs1 = match.group("rs1")

        arf_out, arf_in = _find_arf_forward(lines, idx)
        if not arf_out or not arf_in:
            return None, None

        try:
            O_list = arf_out.get(vd) 
            V_list = arf_in.get(vs2)
            X_list = arf_in.get(rs1)
            
            if not (O_list and V_list and X_list and len(V_list) == len(O_list)):
                return None, None
                
            X_val = int(X_list[0], 16)
            
            all_satisfy = True
            for i, v_hex in enumerate(V_list):
                v_val = int(v_hex, 16)
                o_val_log = int(O_list[i], 16)
                if not _mulhsu_calc_matches(v_val, X_val, o_val_log):
                    all_satisfy = False
                    break
                    
            if all_satisfy:
                ts, pc = _get_commit_info(lines, idx)
                return BugMatch(
                    bug_name=self.name,
                    file=str(path),
                    line=idx + 1,
                    timestamp=ts,
                    pc=pc,
                    instruction=f"vmulhsu.vx {vd}, {vs2}, {rs1}",
                    error="Pattern matched calculation",
                    arf_out=arf_out,
                    arf_in=arf_in
                ), None
        except ValueError:
            pass
            
        return None, None


class VmulhsuVvPattern(LogPattern):
    """Pattern: Check vmulhsu.vv calculation (vector-vector)."""
    
    def __init__(self):
        self.name = "ventus_hw_vmulhsu_vv"
        self.disasm_re = re.compile(
            r"REF disasm:\s*vmulhsu\.vv\s+(?P<vd>v\d+),\s*(?P<vs2>v\d+),\s*(?P<vs1>v\d+)", re.IGNORECASE
        )

    def match(
        self, lines: List[str], idx: int, path: Path
    ) -> tuple[Optional[BugMatch], Optional[BugMatch]]:
        line = strip_ansi(lines[idx])
        match = self.disasm_re.search(line)
        if not match:
            return None, None
        
        vd = match.group("vd")
        vs2 = match.group("vs2")
        vs1 = match.group("vs1")

        arf_out, arf_in = _find_arf_forward(lines, idx)
        if not arf_out or not arf_in:
            return None, None

        try:
            O_list = arf_out.get(vd) 
            V2_list = arf_in.get(vs2)  # signed operand
            V1_list = arf_in.get(vs1)  # unsigned operand
            
            if not (O_list and V2_list and V1_list):
                return None, None
            
            length = min(len(O_list), len(V2_list), len(V1_list))
            if length == 0:
                return None, None
            
            all_satisfy = True
            for i in range(length):
                v2_val = int(V2_list[i], 16)  # signed
                v1_val = int(V1_list[i], 16)  # unsigned
                o_val_log = int(O_list[i], 16)
                if not _mulhsu_calc_matches(v2_val, v1_val, o_val_log):
                    all_satisfy = False
                    break
                    
            if all_satisfy:
                ts, pc = _get_commit_info(lines, idx)
                return BugMatch(
                    bug_name=self.name,
                    file=str(path),
                    line=idx + 1,
                    timestamp=ts,
                    pc=pc,
                    instruction=f"vmulhsu.vv {vd}, {vs2}, {vs1}",
                    error="Pattern matched calculation",
                    arf_out=arf_out,
                    arf_in=arf_in
                ), None
        except ValueError:
            pass
            
        return None, None


class MulhsuPattern(LogPattern):
    """Pattern: Check scalar mulhsu calculation."""
    
    def __init__(self):
        self.name = "ventus_hw_mulhsu"
        self.disasm_re = re.compile(
            r"REF disasm:\s*mulhsu\s+(?P<rd>x\d+),\s*(?P<rs1>x\d+),\s*(?P<rs2>x\d+)", re.IGNORECASE
        )

    def match(
        self, lines: List[str], idx: int, path: Path
    ) -> tuple[Optional[BugMatch], Optional[BugMatch]]:
        line = strip_ansi(lines[idx])
        match = self.disasm_re.search(line)
        if not match:
            return None, None
        
        rd = match.group("rd")
        rs1 = match.group("rs1")
        rs2 = match.group("rs2")

        arf_out, arf_in = _find_arf_forward(lines, idx)
        if not arf_out or not arf_in:
            return None, None

        try:
            O_list = arf_out.get(rd)
            V_list = arf_in.get(rs1)  # signed
            X_list = arf_in.get(rs2)  # unsigned
            
            if not (O_list and V_list and X_list):
                return None, None
                
            X_val = int(X_list[0], 16)
            V_val = int(V_list[0], 16)
            O_val_log = int(O_list[0], 16)
            
            if _mulhsu_calc_matches(V_val, X_val, O_val_log):
                ts, pc = _get_commit_info(lines, idx)
                return BugMatch(
                    bug_name=self.name,
                    file=str(path),
                    line=idx + 1,
                    timestamp=ts,
                    pc=pc,
                    instruction=f"mulhsu {rd}, {rs1}, {rs2}",
                    error="Pattern matched calculation",
                    arf_out=arf_out,
                    arf_in=arf_in
                ), None
        except ValueError:
            pass
            
        return None, None


class VmulhuVxPattern(LogPattern):
    """Pattern: Check vmulhu.vx calculation (vector-scalar, both unsigned)."""
    
    def __init__(self):
        self.name = "ventus_hw_vmulhu_vx"
        self.disasm_re = re.compile(
            r"REF disasm:\s*vmulhu\.vx\s+(?P<vd>v\d+),\s*(?P<vs2>v\d+),\s*(?P<rs1>x\d+)", re.IGNORECASE
        )

    def match(
        self, lines: List[str], idx: int, path: Path
    ) -> tuple[Optional[BugMatch], Optional[BugMatch]]:
        line = strip_ansi(lines[idx])
        match = self.disasm_re.search(line)
        if not match:
            return None, None
        
        vd = match.group("vd")
        vs2 = match.group("vs2")
        rs1 = match.group("rs1")

        arf_out, arf_in = _find_arf_forward(lines, idx)
        if not arf_out or not arf_in:
            return None, None

        try:
            O_list = arf_out.get(vd) 
            V_list = arf_in.get(vs2)
            X_list = arf_in.get(rs1)
            
            if not (O_list and V_list and X_list and len(V_list) == len(O_list)):
                return None, None
                
            X_val = int(X_list[0], 16)
            
            all_satisfy = True
            for i, v_hex in enumerate(V_list):
                v_val = int(v_hex, 16)
                o_val_log = int(O_list[i], 16)
                if not _mulhu_calc_matches(v_val, X_val, o_val_log):
                    all_satisfy = False
                    break
                    
            if all_satisfy:
                ts, pc = _get_commit_info(lines, idx)
                return BugMatch(
                    bug_name=self.name,
                    file=str(path),
                    line=idx + 1,
                    timestamp=ts,
                    pc=pc,
                    instruction=f"vmulhu.vx {vd}, {vs2}, {rs1}",
                    error="Pattern matched calculation",
                    arf_out=arf_out,
                    arf_in=arf_in
                ), None
        except ValueError:
            pass
            
        return None, None


class VmulhuVvPattern(LogPattern):
    """Pattern: Check vmulhu.vv calculation (vector-vector, both unsigned)."""
    
    def __init__(self):
        self.name = "ventus_hw_vmulhu_vv"
        self.disasm_re = re.compile(
            r"REF disasm:\s*vmulhu\.vv\s+(?P<vd>v\d+),\s*(?P<vs2>v\d+),\s*(?P<vs1>v\d+)", re.IGNORECASE
        )

    def match(
        self, lines: List[str], idx: int, path: Path
    ) -> tuple[Optional[BugMatch], Optional[BugMatch]]:
        line = strip_ansi(lines[idx])
        match = self.disasm_re.search(line)
        if not match:
            return None, None
        
        vd = match.group("vd")
        vs2 = match.group("vs2")
        vs1 = match.group("vs1")

        arf_out, arf_in = _find_arf_forward(lines, idx)
        if not arf_out or not arf_in:
            return None, None

        try:
            O_list = arf_out.get(vd) 
            V2_list = arf_in.get(vs2)
            V1_list = arf_in.get(vs1)
            
            if not (O_list and V2_list and V1_list):
                return None, None
            
            length = min(len(O_list), len(V2_list), len(V1_list))
            if length == 0:
                return None, None
            
            all_satisfy = True
            for i in range(length):
                v2_val = int(V2_list[i], 16)
                v1_val = int(V1_list[i], 16)
                o_val_log = int(O_list[i], 16)
                if not _mulhu_calc_matches(v2_val, v1_val, o_val_log):
                    all_satisfy = False
                    break
                    
            if all_satisfy:
                ts, pc = _get_commit_info(lines, idx)
                return BugMatch(
                    bug_name=self.name,
                    file=str(path),
                    line=idx + 1,
                    timestamp=ts,
                    pc=pc,
                    instruction=f"vmulhu.vv {vd}, {vs2}, {vs1}",
                    error="Pattern matched calculation",
                    arf_out=arf_out,
                    arf_in=arf_in
                ), None
        except ValueError:
            pass
            
        return None, None


class MulhuPattern(LogPattern):
    """Pattern: Check scalar mulhu calculation (both unsigned)."""
    
    def __init__(self):
        self.name = "ventus_hw_mulhu"
        self.disasm_re = re.compile(
            r"REF disasm:\s*mulhu\s+(?P<rd>x\d+),\s*(?P<rs1>x\d+),\s*(?P<rs2>x\d+)", re.IGNORECASE
        )

    def match(
        self, lines: List[str], idx: int, path: Path
    ) -> tuple[Optional[BugMatch], Optional[BugMatch]]:
        line = strip_ansi(lines[idx])
        match = self.disasm_re.search(line)
        if not match:
            return None, None
        
        rd = match.group("rd")
        rs1 = match.group("rs1")
        rs2 = match.group("rs2")

        arf_out, arf_in = _find_arf_forward(lines, idx)
        if not arf_out or not arf_in:
            return None, None

        try:
            O_list = arf_out.get(rd)
            V_list = arf_in.get(rs1)
            X_list = arf_in.get(rs2)
            
            if not (O_list and V_list and X_list):
                return None, None
                
            X_val = int(X_list[0], 16)
            V_val = int(V_list[0], 16)
            O_val_log = int(O_list[0], 16)
            
            if _mulhu_calc_matches(V_val, X_val, O_val_log):
                ts, pc = _get_commit_info(lines, idx)
                return BugMatch(
                    bug_name=self.name,
                    file=str(path),
                    line=idx + 1,
                    timestamp=ts,
                    pc=pc,
                    instruction=f"mulhu {rd}, {rs1}, {rs2}",
                    error="Pattern matched calculation",
                    arf_out=arf_out,
                    arf_in=arf_in
                ), None
        except ValueError:
            pass
            
        return None, None


class GenericErrorPattern(LogPattern):
    """Pattern: Generic error signature (Commit -> Disasm -> ARF)."""

    def __init__(self):
        self.name = "unknown_signature"
        self.disasm_re = re.compile(r"REF disasm:\s*(?P<inst>.*)")

    def match(
        self, lines: List[str], idx: int, path: Path
    ) -> tuple[Optional[BugMatch], Optional[BugMatch]]:
        line = strip_ansi(lines[idx])
        match = self.disasm_re.search(line)
        if not match:
            return None, None

        inst_text = match.group("inst").strip()

        # Look backward for Commit
        commit = find_backward(lines, idx, COMMIT_RE)
        if not commit:
            return None, None

        ts = 0
        pc = "0x0"
        _, commit_match = commit
        ts = int(commit_match.group("ts"))
        pc = commit_match.group("pc")

        # Look forward for ARF Out and ARF In
        arf_out = None
        arf_in = None

        # Look ahead a few lines (e.g. 10)
        for i in range(idx + 1, min(idx + 11, len(lines))):
            l = strip_ansi(lines[i])
            if "ARF Out:" in l:
                arf_out = parse_arf_line(l)
            if "ARF In:" in l:
                arf_in = parse_arf_line(l)

        if arf_out is not None and arf_in is not None:
            # Found the block
            bm = BugMatch(
                bug_name=self.name,
                file=str(path),
                line=idx + 1,
                timestamp=ts,
                pc=pc,
                instruction=inst_text,
                error="Unknown error signature",
                arf_out=arf_out,
                arf_in=arf_in,
            )
            return None, bm

        return None, None


class VfmaxVvNanPattern(LogPattern):
    """Pattern: Check vfmax.vv NaN propagation behavior."""

    def __init__(self):
        self.name = "ventus_hw_vfmax"
        self.disasm_re = re.compile(
            r"REF disasm:\s*vfmax\.vv\s+(?P<vd>v\d+),\s*(?P<vs2>v\d+),\s*(?P<vs1>v\d+)", re.IGNORECASE
        )

    def is_nan_32(self, x: int) -> bool:
        exponent = (x >> 23) & 0xFF
        mantissa = x & 0x7FFFFF
        return exponent == 0xFF and mantissa != 0

    def match(
        self, lines: List[str], idx: int, path: Path
    ) -> tuple[Optional[BugMatch], Optional[BugMatch]]:
        line = strip_ansi(lines[idx])
        # Trigger on REF disasm line
        match = self.disasm_re.search(line)
        if not match:
            return None, None

        vd = match.group("vd")
        vs2 = match.group("vs2")
        vs1 = match.group("vs1")

        # Look forward for ARF lines
        arf_out = None
        arf_in = None
        
        # Checking next few lines
        for i in range(idx + 1, min(idx + 6, len(lines))):
            l = strip_ansi(lines[i])
            if "ARF Out:" in l:
                arf_out = parse_arf_line(l)
            if "ARF In:" in l:
                arf_in = parse_arf_line(l)
        
        if not arf_out or not arf_in:
            return None, None

        try:
            vd_vals = arf_out.get(vd)
            vs2_vals = arf_in.get(vs2)
            vs1_vals = arf_in.get(vs1)

            if not (vd_vals and vs2_vals and vs1_vals):
                return None, None
            
            # Ensure lengths match
            length = min(len(vd_vals), len(vs2_vals), len(vs1_vals))
            
            matched = False
            error_details = ""
            
            for i in range(length):
                val_out = int(vd_vals[i], 16)
                val_src2 = int(vs2_vals[i], 16)
                val_src1 = int(vs1_vals[i], 16)
                
                nan2 = self.is_nan_32(val_src2)
                nan1 = self.is_nan_32(val_src1)
                
                # Rule: when one is NaN and other is not, result must be the not-NaN one.
                if nan2 and not nan1:
                    # Expect vs1
                    if val_out != val_src1:
                        matched = True
                        error_details = f"Lane {i}: src2({hex(val_src2)}) is NaN, src1({hex(val_src1)}) is not. Out({hex(val_out)}) != src1"
                        break
                elif not nan2 and nan1:
                    # Expect vs2
                    if val_out != val_src2:
                        matched = True
                        error_details = f"Lane {i}: src1({hex(val_src1)}) is NaN, src2({hex(val_src2)}) is not. Out({hex(val_out)}) != src2"
                        break

            if matched:
                commit = find_backward(lines, idx, COMMIT_RE)
                ts = 0
                pc = "0x0"
                if commit:
                    _, commit_match = commit
                    ts = int(commit_match.group("ts"))
                    pc = commit_match.group("pc")

                return BugMatch(
                    bug_name=self.name,
                    file=str(path),
                    line=idx + 1,
                    timestamp=ts,
                    pc=pc,
                    instruction=f"vfmax.vv {vd}, {vs2}, {vs1}",
                    error=f"NaN mismatch: {error_details}",
                    arf_out=arf_out,
                    arf_in=arf_in
                ), None
        except ValueError:
            pass
            
        return None, None


class WbFpRdZeroPattern(LogPattern):
    """Pattern: Invalid writeback register assertion in Vortex."""

    def __init__(self):
        self.name = "wb_fp_rd_zero"
        self.error_re = re.compile(
            r"\*\*\*\s+cluster\d+-socket\d+-core\d+-issue\d+-scoreboard invalid writeback register:\s+"
            r"wid=\d+,\s+PC=(?P<pc>0x[0-9a-fA-F]+),\s+tmask=[0-9a-fA-F]+,\s+rd=\d+\s+\(#\d+\)"
        )

    def match(
        self, lines: List[str], idx: int, path: Path
    ) -> tuple[Optional[BugMatch], Optional[BugMatch]]:
        line = strip_ansi(lines[idx])
        match = self.error_re.search(line)
        if not match:
            return None, None

        pc = match.group("pc")
        
        return BugMatch(
            bug_name=self.name,
            file=str(path),
            line=idx + 1,
            timestamp=0,
            pc=pc,
            instruction="?",
            error=line.strip(),
            arf_out={},
            arf_in={},
        ), None


class WbFpFdZeroPattern(LogPattern):
    """Pattern: Releasing invalid addr."""

    def __init__(self):
        self.name = "wb_zero"
        self.error_re = re.compile(r"releasing invalid addr 0")

    def match(
        self, lines: List[str], idx: int, path: Path
    ) -> tuple[Optional[BugMatch], Optional[BugMatch]]:
        line = strip_ansi(lines[idx])
        if not self.error_re.search(line):
            return None, None

        commit = find_backward(lines, idx, COMMIT_RE)
        pc = "0x0"
        if commit:
            _, commit_match = commit
            pc = commit_match.group("pc")
        
        return BugMatch(
            bug_name=self.name,
            file=str(path),
            line=idx + 1,
            timestamp=0,
            pc=pc,
            instruction="?",
            error=line.strip(),
            arf_out={},
            arf_in={},
        ), None


class VmsgtVxPattern(LogPattern):
    """Pattern: vmsgt.vx / vmsgtu.vx mismatch behavior."""

    def __init__(self):
        self.name = "vmsgt_vx_vmsgtu_vx"
        self.disasm_re = re.compile(
            r"REF disasm:\s*(?P<inst>vmsgt\.vx|vmsgtu\.vx)\s+(?P<args>.*)", re.IGNORECASE
        )
        self.error_re = re.compile(
            r"thread\s+\d+\s+%(?P<reg>\w+):\s*(?P<dut_val>0x[0-9a-fA-F]+)\s*!=\s*ref\s+(?P<ref_val>0x[0-9a-fA-F]+)"
        )

    def match(
        self, lines: List[str], idx: int, path: Path
    ) -> tuple[Optional[BugMatch], Optional[BugMatch]]:
        line = strip_ansi(lines[idx])
        # Trigger on REF disasm line
        match = self.disasm_re.search(line)
        if not match:
            return None, None

        inst_name = match.group("inst")
        args_str = match.group("args")

        # Look forward for the error line
        error_found = False
        error_line_text = ""
        for i in range(idx + 1, min(idx + 10, len(lines))):
            l = strip_ansi(lines[i])
            if self.error_re.search(l):
                error_found = True
                error_line_text = l.strip()
                break

        if not error_found:
            return None, None

        # Look backward for Commit
        commit = find_backward(lines, idx, COMMIT_RE)
        ts = 0
        pc = "0x0"
        if commit:
            _, commit_match = commit
            ts = int(commit_match.group("ts"))
            pc = commit_match.group("pc")

        # Look forward for ARF lines
        arf_out = None
        arf_in = None
        for i in range(idx + 1, min(idx + 6, len(lines))):
            l = strip_ansi(lines[i])
            if "ARF Out:" in l:
                arf_out = parse_arf_line(l)
            if "ARF In:" in l:
                arf_in = parse_arf_line(l)

        return BugMatch(
            bug_name=self.name,
            file=str(path),
            line=idx + 1,
            timestamp=ts,
            pc=pc,
            instruction=f"{inst_name} {args_str}",
            error=error_line_text,
            arf_out=arf_out or {},
            arf_in=arf_in or {},
        ), None


class CsrMisaPattern(LogPattern):
    """Pattern: CSR instruction with misa register mismatch."""

    def __init__(self):
        self.name = "csr_misa"
        self.disasm_re = re.compile(
            r"REF disasm:\s*(?P<inst>csr\w+)\s+(?P<rd>\w+),\s*misa,\s*(?P<rs>\w+)", re.IGNORECASE
        )
        self.error_re = re.compile(
            r"thread\s+\d+\s+%(?P<reg>\w+):\s*(?P<dut_val>0x[0-9a-fA-F]+)\s*!=\s*ref\s+(?P<ref_val>0x[0-9a-fA-F]+)"
        )

    def match(
        self, lines: List[str], idx: int, path: Path
    ) -> tuple[Optional[BugMatch], Optional[BugMatch]]:
        line = strip_ansi(lines[idx])
        # Trigger on REF disasm line with csr* and misa
        match = self.disasm_re.search(line)
        if not match:
            return None, None

        inst_name = match.group("inst")
        rd = match.group("rd")
        rs = match.group("rs")

        # Look forward for the error line
        error_found = False
        error_line_text = ""
        for i in range(idx + 1, min(idx + 10, len(lines))):
            l = strip_ansi(lines[i])
            if self.error_re.search(l):
                error_found = True
                error_line_text = l.strip()
                break

        if not error_found:
            return None, None

        # Look backward for Commit
        commit = find_backward(lines, idx, COMMIT_RE)
        ts = 0
        pc = "0x0"
        if commit:
            _, commit_match = commit
            ts = int(commit_match.group("ts"))
            pc = commit_match.group("pc")

        # Look forward for ARF lines
        arf_out = None
        arf_in = None
        for i in range(idx + 1, min(idx + 6, len(lines))):
            l = strip_ansi(lines[i])
            if "ARF Out:" in l:
                arf_out = parse_arf_line(l)
            if "ARF In:" in l:
                arf_in = parse_arf_line(l)

        return BugMatch(
            bug_name=self.name,
            file=str(path),
            line=idx + 1,
            timestamp=ts,
            pc=pc,
            instruction=f"{inst_name} {rd}, misa, {rs}",
            error=error_line_text,
            arf_out=arf_out or {},
            arf_in=arf_in or {},
        ), None


class CsrTidPattern(LogPattern):
    """Pattern: CSR instruction with tid register mismatch."""

    def __init__(self):
        self.name = "csr_tid"
        self.disasm_re = re.compile(
            r"REF disasm:\s*(?P<inst>csr\w+)\s+(?P<rd>\w+),\s*tid,\s*(?P<rs>\w+)", re.IGNORECASE
        )
        self.error_re = re.compile(
            r"thread\s+\d+\s+%(?P<reg>\w+):\s*(?P<dut_val>0x[0-9a-fA-F]+)\s*!=\s*ref\s+(?P<ref_val>0x[0-9a-fA-F]+)"
        )

    def match(
        self, lines: List[str], idx: int, path: Path
    ) -> tuple[Optional[BugMatch], Optional[BugMatch]]:
        line = strip_ansi(lines[idx])
        # Trigger on REF disasm line with csr* and tid
        match = self.disasm_re.search(line)
        if not match:
            return None, None

        inst_name = match.group("inst")
        rd = match.group("rd")
        rs = match.group("rs")

        # Look forward for the error line
        error_found = False
        error_line_text = ""
        for i in range(idx + 1, min(idx + 10, len(lines))):
            l = strip_ansi(lines[i])
            if self.error_re.search(l):
                error_found = True
                error_line_text = l.strip()
                break

        if not error_found:
            return None, None

        # Look backward for Commit
        commit = find_backward(lines, idx, COMMIT_RE)
        ts = 0
        pc = "0x0"
        if commit:
            _, commit_match = commit
            ts = int(commit_match.group("ts"))
            pc = commit_match.group("pc")

        # Look forward for ARF lines
        arf_out, arf_in = _find_arf_forward(lines, idx)

        return BugMatch(
            bug_name=self.name,
            file=str(path),
            line=idx + 1,
            timestamp=ts,
            pc=pc,
            instruction=f"{inst_name} {rd}, tid, {rs}",
            error=error_line_text,
            arf_out=arf_out or {},
            arf_in=arf_in or {},
        ), None



class AmoDecPattern(LogPattern):
    """Pattern: AMO (Atomic Memory Operation) instruction detection."""

    def __init__(self):
        self.name = "amo_dec"
        self.disasm_re = re.compile(
            r"REF disasm:\s*(?P<inst>amo\w+(\.\w+)?)\s+(?P<args>.*)", re.IGNORECASE
        )

    def match(
        self, lines: List[str], idx: int, path: Path
    ) -> tuple[Optional[BugMatch], Optional[BugMatch]]:
        line = strip_ansi(lines[idx])
        match = self.disasm_re.search(line)
        if not match:
            return None, None

        inst_name = match.group("inst")
        args_str = match.group("args").strip()

        # Look backward for Commit
        commit = find_backward(lines, idx, COMMIT_RE)
        ts = 0
        pc = "0x0"
        if commit:
            _, commit_match = commit
            ts = int(commit_match.group("ts"))
            pc = commit_match.group("pc")

        # Look forward for ARF lines
        arf_out, arf_in = _find_arf_forward(lines, idx)

        return BugMatch(
            bug_name=self.name,
            file=str(path),
            line=idx + 1,
            timestamp=ts,
            pc=pc,
            instruction=f"{inst_name} {args_str}",
            error="AMO instruction detected",
            arf_out=arf_out or {},
            arf_in=arf_in or {},
        ), None


class VarThreadNumPattern(LogPattern):
    """Pattern: Variable thread number mismatch (numt < VENTUS_NR_THREADS_PER_CORE).
    
    Detects when tmask != ref and ref is a subset of tmask, indicating
    a bug related to variable thread count configuration.
    """

    def __init__(self):
        self.name = "var_thread_num"
        self.arg_gpgpu_re = re.compile(
            r"arg gpgpu is\s+.*numt:(?P<numt>\d+)"
        )
        self.tmask_error_re = re.compile(
            r"tmask\s+\[(?P<tmask>[01]+)\]\s+!=\s+ref\s+\[(?P<ref>[01]+)\]"
        )

    def _is_subset(self, ref_bits: str, tmask_bits: str) -> bool:
        """Check if ref is a subset of tmask (ref has 1s only where tmask has 1s)."""
        # Pad ref to match tmask length (ref is shorter)
        ref_padded = ref_bits.zfill(len(tmask_bits))
        for r, t in zip(ref_padded, tmask_bits):
            if r == '1' and t == '0':
                return False
        return True

    def match(
        self, lines: List[str], idx: int, path: Path
    ) -> tuple[Optional[BugMatch], Optional[BugMatch]]:
        line = strip_ansi(lines[idx])
        
        # Trigger on the tmask error line
        tmask_match = self.tmask_error_re.search(line)
        if not tmask_match:
            return None, None

        tmask_bits = tmask_match.group("tmask")
        ref_bits = tmask_match.group("ref")

        # Check if ref is a subset of tmask
        if not self._is_subset(ref_bits, tmask_bits):
            return None, None

        # Look backward for arg gpgpu line to get numt
        numt = None
        for i in range(idx, -1, -1):
            l = strip_ansi(lines[i])
            arg_match = self.arg_gpgpu_re.search(l)
            if arg_match:
                numt = int(arg_match.group("numt"))
                break

        if numt is None or numt >= VENTUS_NR_THREADS_PER_CORE:
            return None, None

        # Look backward for Commit to get timestamp and PC
        commit = find_backward(lines, idx, COMMIT_RE)
        ts = 0
        pc = "0x0"
        if commit:
            _, commit_match = commit
            ts = int(commit_match.group("ts"))
            pc = commit_match.group("pc")

        # Look backward for instruction (REF disasm)
        disasm_re = re.compile(r"REF disasm:\s*(?P<inst>.*)")
        disasm = find_backward(lines, idx, disasm_re)
        instruction = "?"
        if disasm:
            _, disasm_match = disasm
            instruction = disasm_match.group("inst").strip()

        # Look backward for ARF lines
        arf_out = find_backward(lines, idx, ARF_OUT_RE)
        arf_in = find_backward(lines, idx, ARF_IN_RE)
        
        arf_out_vals = {}
        arf_in_vals = {}
        if arf_out:
            arf_out_idx, _ = arf_out
            arf_out_vals = parse_arf_line(lines[arf_out_idx])
        if arf_in:
            arf_in_idx, _ = arf_in
            arf_in_vals = parse_arf_line(lines[arf_in_idx])

        return BugMatch(
            bug_name=self.name,
            file=str(path),
            line=idx + 1,
            timestamp=ts,
            pc=pc,
            instruction=instruction,
            error=f"numt={numt} < {VENTUS_NR_THREADS_PER_CORE}, tmask [{tmask_bits}] != ref [{ref_bits}]",
            arf_out=arf_out_vals,
            arf_in=arf_in_vals,
        ), None


class VmvXsUncommittedPattern(LogPattern):
    """Pattern: DUT Warp terminated with uncommitted vmv.x.s instruction.
    
    Detects when a warp terminates but has uncommitted instructions where
    insn_bits matches vmv.x.s (MATCH=0x42002057, MASK=0xfe0ff07f).
    """

    # vmv.x.s instruction encoding
    MATCH_VMV_X_S = 0x42002057
    MASK_VMV_X_S = 0xfe0ff07f

    def __init__(self):
        self.name = "vmv_x_s"
        self.error_re = re.compile(
            r"DUT Warp GID \d+ terminated but some warps still have uncommitted instructions:"
        )
        self.insn_bits_re = re.compile(
            r"pc=(?P<pc>0x[0-9a-fA-F]+).*insn_bits=(?P<insn>0x[0-9a-fA-F]+)"
        )

    def _is_vmv_x_s(self, insn_bits: int) -> bool:
        """Check if instruction bits match vmv.x.s encoding."""
        return (insn_bits & self.MASK_VMV_X_S) == self.MATCH_VMV_X_S

    def match(
        self, lines: List[str], idx: int, path: Path
    ) -> tuple[Optional[BugMatch], Optional[BugMatch]]:
        line = strip_ansi(lines[idx])
        
        # Check for the error pattern
        if not self.error_re.search(line):
            return None, None

        # Extract insn_bits from the same line
        insn_match = self.insn_bits_re.search(line)
        if not insn_match:
            return None, None

        try:
            insn_bits = int(insn_match.group("insn"), 16)
            pc = insn_match.group("pc")
        except ValueError:
            return None, None

        # Check if instruction matches vmv.x.s
        if not self._is_vmv_x_s(insn_bits):
            return None, None

        # Look backward for Commit to get timestamp
        commit = find_backward(lines, idx, COMMIT_RE)
        ts = 0
        if commit:
            _, commit_match = commit
            ts = int(commit_match.group("ts"))

        return BugMatch(
            bug_name=self.name,
            file=str(path),
            line=idx + 1,
            timestamp=ts,
            pc=pc,
            instruction="vmv.x.s",
            error=line.strip(),
            arf_out={},
            arf_in={},
        ), None


class VmaskMmPattern(LogPattern):
    """Pattern: Vector mask instructions (vm*.mm) like vmnor.mm, vmand.mm, etc."""

    def __init__(self):
        self.name = "vmask"
        self.disasm_re = re.compile(
            r"REF disasm:\s*(?P<inst>vm\w+\.mm)\s+(?P<vd>v\d+),\s*(?P<vs2>v\d+),\s*(?P<vs1>v\d+)", re.IGNORECASE
        )

    def match(
        self, lines: List[str], idx: int, path: Path
    ) -> tuple[Optional[BugMatch], Optional[BugMatch]]:
        line = strip_ansi(lines[idx])
        match = self.disasm_re.search(line)
        if not match:
            return None, None

        inst_name = match.group("inst")
        vd = match.group("vd")
        vs2 = match.group("vs2")
        vs1 = match.group("vs1")

        # Look backward for Commit
        commit = find_backward(lines, idx, COMMIT_RE)
        ts = 0
        pc = "0x0"
        if commit:
            _, commit_match = commit
            ts = int(commit_match.group("ts"))
            pc = commit_match.group("pc")

        # Look forward for ARF lines
        arf_out, arf_in = _find_arf_forward(lines, idx)
        arf_out_vals = arf_out if arf_out else {}
        arf_in_vals = arf_in if arf_in else {}

        return BugMatch(
            bug_name=self.name,
            file=str(path),
            line=idx + 1,
            timestamp=ts,
            pc=pc,
            instruction=inst_name,
            error=f"{inst_name} {vd}, {vs2}, {vs1}",
            arf_out=arf_out_vals,
            arf_in=arf_in_vals,
        ), None


class VxWmmaInsnBitsPattern(LogPattern):
    """Pattern: VX_WMMA instruction mismatch detected by insn bits encoding.
    
    Matches WriteBack lines with insn bits matching WMMA encoding followed by
    "DUT rd %* not in REF" error.
    
    WMMA encoding: instruc_rtype(RISCV_CUSTOM0, fmt1, 0, fmt2, 0, 2)
    - opcode = 0x0B (RISCV_CUSTOM0)
    - funct3 = 0
    - funct7 = 2
    """

    # WMMA instruction encoding check
    # R-type: funct7[31:25] | rs2[24:20] | rs1[19:15] | funct3[14:12] | rd[11:7] | opcode[6:0]
    WMMA_OPCODE = 0x0B
    WMMA_FUNCT3 = 0
    WMMA_FUNCT7 = 2

    def __init__(self):
        self.name = "vx_wmma"
        # Match WriteBack line with insn bits
        self.wb_re = re.compile(
            r"WriteBack\s*:\s*PC\s*=\s*(?P<pc>0x[0-9a-fA-F]+)\s*\[Timestamp:\s*(?P<ts>\d+)s\]\s*insn bits\s*=\s*(?P<insn>0x[0-9a-fA-F]+)"
        )
        # Match DUT rd not in REF error
        self.error_re = re.compile(
            r"DUT rd %(?P<dut_rd>[^\s]+) not in REF \[(?P<ref_regs>[^\]]*)\]"
        )

    def _is_wmma_insn(self, insn_bits: int) -> bool:
        """Check if instruction bits match WMMA encoding."""
        opcode = insn_bits & 0x7F
        funct3 = (insn_bits >> 12) & 0x7
        funct7 = (insn_bits >> 25) & 0x7F
        return (opcode == self.WMMA_OPCODE and 
                funct3 == self.WMMA_FUNCT3 and 
                funct7 == self.WMMA_FUNCT7)

    def match(
        self, lines: List[str], idx: int, path: Path
    ) -> tuple[Optional[BugMatch], Optional[BugMatch]]:
        line = strip_ansi(lines[idx])
        
        # Match WriteBack line with insn bits
        wb_match = self.wb_re.search(line)
        if not wb_match:
            return None, None
        
        insn_hex = wb_match.group("insn")
        try:
            insn_bits = int(insn_hex, 16)
        except ValueError:
            return None, None
        
        # Check if insn bits match WMMA encoding
        if not self._is_wmma_insn(insn_bits):
            return None, None
        
        pc = wb_match.group("pc")
        ts = int(wb_match.group("ts"))
        
        # Look forward for DUT rd not in REF error
        error_found = False
        error_line_text = ""
        for i in range(idx + 1, min(idx + 10, len(lines))):
            l = strip_ansi(lines[i])
            if self.error_re.search(l):
                error_found = True
                error_line_text = l.strip()
                break
        
        if not error_found:
            return None, None
        
        return BugMatch(
            bug_name=self.name,
            file=str(path),
            line=idx + 1,
            timestamp=ts,
            pc=pc,
            instruction=f"vx_wmma (insn={insn_hex})",
            error=error_line_text,
            arf_out={},
            arf_in={},
        ), None


class VxWmmaRefMismatchPattern(LogPattern):
    """Pattern: vx.wmma/vx_wmma instruction with per-thread != ref mismatch."""

    def __init__(self):
        self.name = "vx_wmma"
        self.disasm_re = re.compile(
            r"REF disasm:\s*(?P<inst>vx[._]wmma)\s+(?P<args>.*)", re.IGNORECASE
        )
        self.error_re = re.compile(
            r"thread\s+(?P<thread>\d+)\s+%(?P<reg>[rxvf]\d+):\s*"
            r"(?P<dut_val>0x[0-9a-fA-F]+)\s*!=\s*ref\s+"
            r"(?P<ref_val>0x[0-9a-fA-F]+)",
            re.IGNORECASE,
        )

    def match(
        self, lines: List[str], idx: int, path: Path
    ) -> tuple[Optional[BugMatch], Optional[BugMatch]]:
        line = strip_ansi(lines[idx])
        match = self.disasm_re.search(line)
        if not match:
            return None, None

        error_lines: List[str] = []
        dut_vals: Dict[str, List[str]] = {}
        ref_vals: Dict[str, List[str]] = {}
        for i in range(idx + 1, min(idx + 16, len(lines))):
            l = strip_ansi(lines[i]).strip()
            if error_lines and (
                "REF disasm:" in l or COMMIT_RE.search(l) or l.startswith("Fetch:")
            ):
                break
            error_match = self.error_re.search(l)
            if not error_match:
                continue
            error_lines.append(l)
            reg = error_match.group("reg")
            dut_vals.setdefault(reg, []).append(error_match.group("dut_val"))
            ref_vals.setdefault(f"ref_{reg}", []).append(error_match.group("ref_val"))

        if not error_lines:
            return None, None

        commit = find_backward(lines, idx, COMMIT_RE)
        ts = 0
        pc = "0x0"
        if commit:
            _, commit_match = commit
            ts = int(commit_match.group("ts"))
            pc = commit_match.group("pc")

        arf_out, arf_in = _find_arf_forward(lines, idx)
        merged_out = dict(arf_out or {})
        merged_in = dict(arf_in or {})
        merged_out.update(dut_vals)
        merged_in.update(ref_vals)

        inst_name = match.group("inst").replace(".", "_")
        args = match.group("args").strip()
        return BugMatch(
            bug_name=self.name,
            file=str(path),
            line=idx + 1,
            timestamp=ts,
            pc=pc,
            instruction=f"{inst_name} {args}",
            error="; ".join(error_lines),
            arf_out=merged_out,
            arf_in=merged_in,
        ), None


class CzeroPattern(LogPattern):
    """Pattern: czero.* instruction mismatch (czero.nez, czero.eqz, etc.)."""

    def __init__(self):
        self.name = "czero"
        self.disasm_re = re.compile(
            r"REF disasm:\s*(?P<inst>czero\.\w+)\s+(?P<rd>x\d+),\s*(?P<rs1>x\d+),\s*(?P<rs2>x\d+)", re.IGNORECASE
        )
        self.error_re = re.compile(
            r"thread\s+\d+\s+%(?P<reg>\w+):\s*(?P<dut_val>0x[0-9a-fA-F]+)\s*!=\s*ref\s+(?P<ref_val>0x[0-9a-fA-F]+)"
        )

    def match(
        self, lines: List[str], idx: int, path: Path
    ) -> tuple[Optional[BugMatch], Optional[BugMatch]]:
        line = strip_ansi(lines[idx])
        # Trigger on REF disasm line with czero.*
        match = self.disasm_re.search(line)
        if not match:
            return None, None

        inst_name = match.group("inst")
        rd = match.group("rd")
        rs1 = match.group("rs1")
        rs2 = match.group("rs2")

        # Look forward for the error line
        error_found = False
        error_line_text = ""
        for i in range(idx + 1, min(idx + 10, len(lines))):
            l = strip_ansi(lines[i])
            if self.error_re.search(l):
                error_found = True
                error_line_text = l.strip()
                break

        if not error_found:
            return None, None

        # Look backward for Commit
        commit = find_backward(lines, idx, COMMIT_RE)
        ts = 0
        pc = "0x0"
        if commit:
            _, commit_match = commit
            ts = int(commit_match.group("ts"))
            pc = commit_match.group("pc")

        # Look forward for ARF lines
        arf_out, arf_in = _find_arf_forward(lines, idx)

        return BugMatch(
            bug_name=self.name,
            file=str(path),
            line=idx + 1,
            timestamp=ts,
            pc=pc,
            instruction=f"{inst_name} {rd}, {rs1}, {rs2}",
            error=error_line_text,
            arf_out=arf_out or {},
            arf_in=arf_in or {},
        ), None


class JalrAlignPattern(LogPattern):
    """Pattern: Instruction fetch error with misaligned PC (PC low bit != 0)."""

    def __init__(self):
        self.name = "jalr_align"
        self.error_re = re.compile(
            r"Insn Fetch error:\s+Warp\s+\d+\s+PC\s+(?P<pc>0x[0-9a-fA-F]+)\s+!=\s+REF\s+(?P<ref_pc>0x[0-9a-fA-F]+)"
        )

    def match(
        self, lines: List[str], idx: int, path: Path
    ) -> tuple[Optional[BugMatch], Optional[BugMatch]]:
        line = strip_ansi(lines[idx])
        match = self.error_re.search(line)
        if not match:
            return None, None

        pc_str = match.group("pc")
        ref_pc_str = match.group("ref_pc")

        try:
            pc_val = int(pc_str, 16)
        except ValueError:
            return None, None

        # Check if PC low bit is not 0 (misaligned)
        if (pc_val & 0x1) == 0:
            return None, None

        # Look backward for Commit to get timestamp
        commit = find_backward(lines, idx, COMMIT_RE)
        ts = 0
        commit_pc = pc_str
        if commit:
            _, commit_match = commit
            ts = int(commit_match.group("ts"))
            commit_pc = commit_match.group("pc")

        return BugMatch(
            bug_name=self.name,
            file=str(path),
            line=idx + 1,
            timestamp=ts,
            pc=commit_pc,
            instruction="jalr/misaligned",
            error=f"Insn Fetch error: PC {pc_str} != REF {ref_pc_str}",
            arf_out={},
            arf_in={},
        ), None


class SraiPattern(LogPattern):
    """Pattern: srai instruction with incorrect unsigned shift in reference.
    
    Detects when DUT correctly performs signed right shift but REF incorrectly
    performs unsigned right shift.
    """

    def __init__(self):
        self.name = "sra"
        self.disasm_re = re.compile(
            r"REF disasm:\s*srai\s+(?P<rd>x\d+),\s*(?P<rs1>x\d+),\s*(?P<shamt>\d+)", re.IGNORECASE
        )
        self.error_re = re.compile(
            r"thread\s+\d+\s+%(?P<reg>\w+):\s*(?P<dut_val>0x[0-9a-fA-F]+)\s*!=\s*ref\s+(?P<ref_val>0x[0-9a-fA-F]+)"
        )

    def match(
        self, lines: List[str], idx: int, path: Path
    ) -> tuple[Optional[BugMatch], Optional[BugMatch]]:
        line = strip_ansi(lines[idx])
        # Trigger on REF disasm line with srai
        match = self.disasm_re.search(line)
        if not match:
            return None, None

        rd = match.group("rd")
        rs1 = match.group("rs1")
        shamt = match.group("shamt")

        # Look forward for the error line
        error_found = False
        error_line_text = ""
        for i in range(idx + 1, min(idx + 10, len(lines))):
            l = strip_ansi(lines[i])
            if self.error_re.search(l):
                error_found = True
                error_line_text = l.strip()
                break

        if not error_found:
            return None, None

        # Look backward for Commit
        commit = find_backward(lines, idx, COMMIT_RE)
        ts = 0
        pc = "0x0"
        if commit:
            _, commit_match = commit
            ts = int(commit_match.group("ts"))
            pc = commit_match.group("pc")

        # Look forward for ARF lines
        arf_out, arf_in = _find_arf_forward(lines, idx)

        return BugMatch(
            bug_name=self.name,
            file=str(path),
            line=idx + 1,
            timestamp=ts,
            pc=pc,
            instruction=f"srai {rd}, {rs1}, {shamt}",
            error=error_line_text,
            arf_out=arf_out or {},
            arf_in=arf_in or {},
        ), None


class VsegPattern(LogPattern):
    """Pattern: Vector segment load/store instructions (v*seg*) with REF rd not in DUT error.
    
    Matches instructions like vlseg2e32.v, vsseg3e16.v, vlsseg4e8.v, etc.
    """

    def __init__(self):
        self.name = "vec_nfield"
        self.error_re = re.compile(
            r"REF rd %(?P<ref_rd>[^\s]+) not in DUT \[(?P<dut_regs>[^\]]+)"
        )
        # Match v*seg* instructions: vlseg, vsseg, vlsseg, vssseg, vluxseg, vsuxseg, vloxseg, vsoxseg, etc.
        self.disasm_re = re.compile(
            r"REF disasm:\s*(?P<inst>v\w*seg\w*)", re.IGNORECASE
        )

    def match(
        self, lines: List[str], idx: int, path: Path
    ) -> tuple[Optional[BugMatch], Optional[BugMatch]]:
        error_line = strip_ansi(lines[idx])
        err_match = self.error_re.search(error_line)
        if not err_match:
            return None, None

        # Look backward for Commit
        commit = find_backward(lines, idx, COMMIT_RE)
        # Look backward for disasm with v*seg* pattern
        disasm = find_backward(lines, idx, self.disasm_re)
        arf_out = find_backward(lines, idx, ARF_OUT_RE)
        arf_in = find_backward(lines, idx, ARF_IN_RE)

        if not (commit and disasm and arf_out and arf_in):
            # Check if at least disasm matches v*seg*
            if disasm:
                return None, self.create_partial_match(lines, idx, path, error_line.strip())
            return None, None

        commit_idx, commit_match = commit
        _, disasm_match = disasm
        instruction = disasm_match.group("inst")
        arf_out_idx, _ = arf_out
        arf_in_idx, _ = arf_in

        ts = int(commit_match.group("ts"))
        pc = commit_match.group("pc")

        arf_out_vals = parse_arf_line(lines[arf_out_idx])
        arf_in_vals = parse_arf_line(lines[arf_in_idx])

        return BugMatch(
            bug_name=self.name,
            file=str(path),
            line=idx + 1,
            timestamp=ts,
            pc=pc,
            instruction=instruction,
            error=error_line.strip(),
            arf_out=arf_out_vals,
            arf_in=arf_in_vals,
        ), None


PATTERNS: List[LogPattern] = [
    #
    # Ventus
    #
    
    # V1 PDS addr

    # V2
    AmoDecPattern(), # b2 b9

    # V3
    VsegPattern(), # b17

    # V4: regext

    # V5
    VmsgtVxPattern(), # b11

    # V6
    CsrMisaPattern(), # b6

    # V7: TID CSR
    CsrTidPattern(), # b14

   
    # V8:
    VarThreadNumPattern(), # b18

    # V9
    VfmaxVvNanPattern(), # b10

    # V10
    # b7 b30
    VmulhsuVxPattern(),
    VmulhsuVvPattern(),
    MulhsuPattern(),
    VmulhuVxPattern(),
    VmulhuVvPattern(),
    MulhuPattern(),

    # V11
    # b12
    RefRdNotInDutPattern(instruction="vftta.vv", bug_name="vftta_vv_wb"),

    # V12: icache hangs up

    # V13
    VmvXsUncommittedPattern(), # b5

    # V14
    VmaskMmPattern(),


    CommitInactiveWarpPattern(),
 

    #
    # Vortex
    #

    # X1
    WbFpRdZeroPattern(),
    WbFpFdZeroPattern(),

    # X2
    CzeroPattern(),

    # X3
    VxWmmaInsnBitsPattern(),  # WMMA insn bits + DUT rd not in REF
    VxWmmaRefMismatchPattern(),
    
    # X4
    JalrAlignPattern(),

    # S1
    SraiPattern(),
    

    GenericErrorPattern(),
]


def find_log_files(root: Path) -> Iterable[Path]:
    """Yield log files named emu.log or matching *.emu.log under root."""

    for path in root.rglob("*emu.log"):
        if path.is_file():
            yield path


def scan_file(path: Path, patterns: List[LogPattern] = PATTERNS) -> tuple[List[BugMatch], List[BugMatch]]:
    try:
        text = path.read_text(encoding="utf-8", errors="ignore")
    except OSError:
        return [], []
    lines = text.splitlines()
    matches: List[BugMatch] = []
    unknowns: List[BugMatch] = []
    for idx, _ in enumerate(lines):
        for pattern in patterns:
            bug, unknown = pattern.match(lines, idx, path)
            if bug:
                matches.append(bug)
            if unknown:
                unknowns.append(unknown)
    
    # Filter unknowns that are already in matches
    known_identities = {(m.timestamp, m.pc) for m in matches}
    unique_unknowns = []
    for u in unknowns:
        # Check if this unknown corresponds to a known bug
        if (u.timestamp, u.pc) not in known_identities:
            unique_unknowns.append(u)
    
    return matches, unique_unknowns


def as_json(matches: List[BugMatch]) -> str:
    return json.dumps([match.__dict__ for match in matches], indent=2)


def as_text(matches: List[BugMatch]) -> str:
    parts = []
    for m in matches:
        parts.append(
            "\n".join(
                [
                    f"bug: {m.bug_name}",
                    f"file: {m.file}:{m.line}",
                    f"timestamp: {m.timestamp}",
                    f"pc: {m.pc}",
                    f"instruction: {m.instruction}",
                    f"error: {m.error}",
                    f"arf_out: {m.arf_out}",
                    f"arf_in: {m.arf_in}",
                    "",
                ]
            )
        )
    return "\n".join(parts)


def main() -> None:
    parser = argparse.ArgumentParser(description="Detect known bugs in emulator logs")
    parser.add_argument(
        "root", nargs="?", default=".", help="Root directory to scan (default: current)"
    )
    parser.add_argument(
        "-f", "--file",
        type=str,
        help="Scan a single log file instead of a directory",
    )
    parser.add_argument(
        "--text",
        action="store_true",
        help="Output plain text instead of JSON",
    )
    parser.add_argument(
        "--out-unknown",
        action="store_true",
        help="Output unknown bug list in JSON",
    )

    args = parser.parse_args()

    all_matches: List[BugMatch] = []
    all_unknowns: List[BugMatch] = []

    def collect_from(p: Path) -> None:
        m, u = scan_file(p, PATTERNS)
        all_matches.extend(m)
        all_unknowns.extend(u)

    if args.file:
        # Scan a single specified log file
        log_file = Path(args.file).resolve()
        if log_file.is_file():
            collect_from(log_file)
        else:
            print(f"Error: File not found: {args.file}", file=__import__("sys").stderr)
            return
    else:
        # Scan a directory
        root = Path(args.root).resolve()
        for log_file in find_log_files(root):
            collect_from(log_file)

    if args.text:
        print(as_text(all_matches))
        if args.out_unknown and all_unknowns:
            print("\n---------- Unknown/Partial Bugs ----------")
            print(as_text(all_unknowns))
    else:
        if args.out_unknown:
            output = {
                "known_bugs": [m.__dict__ for m in all_matches],
                "unknown_bugs": [m.__dict__ for m in all_unknowns]
            }
            print(json.dumps(output, indent=2))
        else:
            print(as_json(all_matches))


if __name__ == "__main__":
    main()
