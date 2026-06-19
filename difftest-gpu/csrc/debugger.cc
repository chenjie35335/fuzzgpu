#include "debugger.hh"

#include <cstdint>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <readline/history.h>
#include <readline/readline.h>
#include <termios.h>
#include <unistd.h>

#include "api_internal.hh"

namespace difftest {

bool Debugger::repl() {
  struct termios term;
  tcgetattr(STDIN_FILENO, &term);
  term.c_lflag |= ECHO;
  tcsetattr(STDIN_FILENO, TCSANOW, &term);

  rl_bind_key('\t', rl_complete);
  rl_bind_key('\b', rl_rubout);
  rl_instream = stdin;
  if (!rl_outstream)
    rl_outstream = stderr;
  rl_prep_terminal(1);

  std::string line;
  while (true) {
    char *rl_in = readline("dbg> ");
    if (!rl_in)
      return false; // EOF or error
    line = rl_in;
    if (!line.empty())
      add_history(rl_in);
    free(rl_in);

    std::istringstream iss(line);
    std::string cmd;
    if (!(iss >> cmd))
      continue;

    if (cmd == "q" || cmd == "quit") {
      return false; // exit REPL
    }

    if (cmd == "c" || cmd == "continue") {
      return true; // skip / continue execution
    }

    if (cmd == "clk") {
      extern uint64_t rtl_clk_cycles;
      std::cout << "Current RTL clock cycles: " << rtl_clk_cycles << "\n";
      continue;
    }

    if (cmd == "mr") {
      //
      // mr <size> <paddr>  (paddr may be decimal or 0x-prefixed hex)
      // Note that the DUT memory may go ahead several cycles, so it may read
      // future values!
      //
      size_t size = 0;
      if (!(iss >> size)) {
        std::cout << "Usage: mr <size> <paddr>\n";
        continue;
      }

      std::string paddr_str;
      if (!(iss >> paddr_str)) {
        std::cout << "Usage: mr <size> <paddr>\n";
        continue;
      }

      unsigned long long paddr = 0;
      try {
        int base = 10;
        if (paddr_str.size() > 2 && paddr_str[0] == '0' &&
            (paddr_str[1] == 'x' || paddr_str[1] == 'X')) {
          base = 16;
          // allow stoull to parse without the 0x prefix for clarity
          paddr_str = paddr_str.substr(2);
        }
        paddr = std::stoull(paddr_str, nullptr, base);
      } catch (const std::invalid_argument &e) {
        std::cout << "Invalid paddr: " << paddr_str << "\n";
        continue;
      } catch (const std::out_of_range &e) {
        std::cout << "paddr out of range: " << paddr_str << "\n";
        continue;
      }

      std::vector<uint8_t> data(size, 0);
      if (!read_dut_pmem(static_cast<Word>(paddr), data.data(), size)) {
        std::cout << "Failed to read DUT memory at 0x" << std::hex << paddr
                  << std::dec << "\n";
        continue;
      }

      std::cout << "Read " << size << " bytes from 0x" << std::hex << paddr
                << std::dec << ":\n";
      // Print a simple hex dump, 16 bytes per line
      for (size_t i = 0; i < size; ++i) {
        if (i % 16 == 0) {
          std::cout << std::setw(8) << std::setfill('0') << std::hex
                    << (paddr + i) << ": ";
        }
        std::cout << std::setw(2) << std::setfill('0') << std::hex
                  << (int)data[i] << " ";
        if (i % 16 == 15 || i + 1 == size)
          std::cout << "\n";
      }
      // restore formatting
      std::cout << std::dec << std::setfill(' ');
      continue;
    }

    if (cmd == "rir") {
      //
      // rir <warp_gid> <reg_idx>
      //
      size_t warp_gid = 0;
      size_t reg_idx = 0;
      if (!(iss >> warp_gid >> reg_idx)) {
        std::cout << "Usage: rir <warp_gid> <reg_idx>\n";
        continue;
      }

      if (warp_gid >= dut_per_warp_.size()) {
        std::cout << "Invalid warp index: " << warp_gid << "\n";
        continue;
      }

      const auto &arch = dut_per_warp_.at(warp_gid).arch_state;
      if (reg_idx >= arch.arf_int.size()) {
        std::cout << "Invalid register index: " << reg_idx << "\n";
        continue;
      }

      std::cout << "Warp " << warp_gid << " %" << reg_idx << ": ";
      for (size_t t = 0; t < arch.arf_int[reg_idx].size(); ++t) {
        std::cout << std::hex << "0x" << arch.arf_int[reg_idx][t] << std::dec
                  << " ";
      }
      std::cout << std::setfill(' ') << "\n";
      continue;
    }

    if (cmd == "rfr") {
      //
      // rfr <warp_gid> <reg_idx>
      //
      size_t warp_gid = 0;
      size_t reg_idx = 0;
      if (!(iss >> warp_gid >> reg_idx)) {
        std::cout << "Usage: rfr <warp_gid> <reg_idx>\n";
        continue;
      }

      if (warp_gid >= dut_per_warp_.size()) {
        std::cout << "Invalid warp index: " << warp_gid << "\n";
        continue;
      }

      const auto &arch = dut_per_warp_.at(warp_gid).arch_state;
#if defined(ARCH_VORTEX)
      if (reg_idx >= arch.arf_fp.size()) {
        std::cout << "Invalid register index: " << reg_idx << "\n";
        continue;
      }
      std::cout << "Warp " << warp_gid << " f%" << reg_idx << ": ";
      for (size_t t = 0; t < arch.arf_fp[reg_idx].size(); ++t) {
        std::cout << std::hex << std::setfill('0') << std::setw(16)
                  << arch.arf_fp[reg_idx][t] << std::dec << " ";
      }
      std::cout << std::setfill(' ') << "\n";
#else
      std::cout << "Floating-point register file not separated on this arch.\n";
      std::cout << "Printing integer register instead:\n";
      if (reg_idx >= arch.arf_int.size()) {
        std::cout << "Invalid register index: " << reg_idx << "\n";
        continue;
      }
      std::cout << "Warp " << warp_gid << " %" << reg_idx << ": ";
      for (size_t t = 0; t < arch.arf_int[reg_idx].size(); ++t) {
        std::cout << std::hex << "0x" << arch.arf_int[reg_idx][t] << std::dec
                  << " ";
      }
      std::cout << std::setfill(' ') << "\n";
#endif
      continue;
    }

    if (cmd == "rvr") {
      //
      // rvr <warp_gid> <reg_idx>
      //
      size_t warp_gid = 0;
      size_t reg_idx = 0;
      if (!(iss >> warp_gid >> reg_idx)) {
        std::cout << "Usage: rvr <warp_gid> <reg_idx>\n";
        continue;
      }

      if (warp_gid >= dut_per_warp_.size()) {
        std::cout << "Invalid warp index: " << warp_gid << "\n";
        continue;
      }

      const auto &arch = dut_per_warp_.at(warp_gid).arch_state;
#if defined(ARCH_VENTUS)
      if (reg_idx >= arch.arf_vec.size()) {
        std::cout << "Invalid register index: " << reg_idx << "\n";
        continue;
      }
      std::cout << "Warp " << warp_gid << " v%" << reg_idx << ": ";
      for (size_t t = 0; t < arch.arf_vec[reg_idx].size(); ++t) {
        std::cout << std::hex << "0x" << arch.arf_vec[reg_idx][t] << std::dec
                  << " ";
      }
      std::cout << std::setfill(' ') << "\n";
#else
      std::cout << "Vector register file not available on this arch.\n";
#endif
      continue;
    }

    std::cout << "Unknown command: " << cmd << "\n";
    std::cout
        << "Commands:\n\tclk\n\tmr <size> <paddr>\n\trir <warp_gid> <reg_idx>\n\trfr "
           "<warp_gid> <reg_idx>\n\trvr <warp_gid> <reg_idx>\n\tc\n\tq\n";
  }
}

} // namespace difftest