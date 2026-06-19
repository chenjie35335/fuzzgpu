// Copyright (c) 2024-2025 DiveFuzz Project
// SPDX-License-Identifier: Mulan PSL v2

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include "spike_engine.h"

namespace py = pybind11;
using namespace spike_engine;

PYBIND11_MODULE(spike_engine, m) {
    m.doc() = "Efficient Spike execution engine for DiveFuzz";

    // Constants
    m.attr("IMMEDIATE_NOT_PRESENT") = IMMEDIATE_NOT_PRESENT;

    // SpikeEngine class
    py::class_<SpikeEngine>(m, "SpikeEngine")
        .def(py::init<const std::string &, const std::string &, size_t, bool>(),
             py::arg("elf_path"),
             py::arg("isa"),
             py::arg("num_instrs"),
             py::arg("verbose") = false,
             R"pbdoc(
             Create a SpikeEngine instance

             Args:
                 elf_path: Path to the ELF file
                 isa: ISA string (e.g., "rv32imafdc")
                 num_instrs: Number of instructions to generate (nops in ELF)
                 verbose: Enable verbose output (default: false)
             )pbdoc")

        .def_static("get_instruction_size", &SpikeEngine::get_instruction_size,
             py::arg("machine_code"),
             R"pbdoc(
             Detect instruction size from machine code

             Args:
                 machine_code: 32-bit machine code

             Returns:
                 Instruction size in bytes (2 for compressed, 4 for standard)
             )pbdoc")

        .def("initialize", &SpikeEngine::initialize,
             R"pbdoc(
             Initialize Spike and execute template initialization code
             Returns True on success, False on error (check get_last_error())
             )pbdoc")

        .def("set_checkpoint", &SpikeEngine::set_checkpoint,
             "Save current processor state as checkpoint")

        .def("restore_checkpoint", &SpikeEngine::restore_checkpoint,
             "Restore processor state from last checkpoint")

        .def("execute_instruction", &SpikeEngine::execute_instruction,
             py::arg("machine_code"),
             R"pbdoc(
             Execute one instruction

             Args:
                 machine_code: 32-bit instruction encoding (may be 16-bit compressed
                               instruction zero-padded to 32 bits)

             Raises:
                 RuntimeError if execution fails
             )pbdoc")

        .def("get_operands", &SpikeEngine::get_operands,
             py::arg("source_regs"),
             py::arg("immediate") = IMMEDIATE_NOT_PRESENT,
             R"pbdoc(
             Get source register and immediate values

             Args:
                 source_regs: List of source register names (e.g., ["x10", "x11"])
                 immediate: Immediate value (default: IMMEDIATE_NOT_PRESENT)

             Returns:
                 List of register values before execution
             )pbdoc")

        .def("get_last_error", &SpikeEngine::get_last_error,
             "Get last error message");

    // Version info
    m.attr("__version__") = "2.0.0";
}
