#include "spdlog/spdlog.h"

#include "config.hh"
#include "term.hh"
#include <cstdlib>
#include <string>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace difftest {

Config::Config(const Arch &arch_)
  : arch(arch_) {

  // read configs from the env
  if (const auto *var = std::getenv("DIFFTEST_WATCH_DOG_TIMEOUT")) {
    watch_dog_timeout = std::strtoull(var, nullptr, 10);
    spdlog::info("{}watch_dog_timeout = {}{}", TermColor::YELLOW,
                 watch_dog_timeout, TermColor::RESET);
  }

  if (const auto *var = std::getenv("DIFFTEST_WATCH_DOG_CYCLES")) {
    check_watch_dog_cycles = std::strtoull(var, nullptr, 10);
    spdlog::info("{}check_watch_dog_cycles = {}{}", TermColor::YELLOW,
                 check_watch_dog_cycles, TermColor::RESET);
  }
  
  if (const auto *var = std::getenv("DIFFTEST_DUMP_ALL_REGS")) {
    dump_all_regs = std::strtoull(var, nullptr, 10);
    spdlog::info("{}dump_all_regs = {}{}", TermColor::YELLOW,
                 dump_all_regs, TermColor::RESET);
  }

  if (const auto *var = std::getenv("DIFFTEST_DUMP_REGS")) {
    dump_regs = std::strtoull(var, nullptr, 10);
    spdlog::info("{}dump_regs = {}{}", TermColor::YELLOW,
                 dump_regs, TermColor::RESET);
  }

  if (const auto *var = std::getenv("DIFFTEST_DUMP_CALL_STACK")) {
    dump_call_stack = std::strtoull(var, nullptr, 10);
    spdlog::info("{}dump_call_stack = {}{}", TermColor::YELLOW,
                 dump_call_stack, TermColor::RESET);
  }

  if (const auto *var = std::getenv("DIFFTEST_DUMP_RETIRED_INSN")) {
    dump_retired_insn = std::strtoull(var, nullptr, 10);
    spdlog::info("{}dump_retired_insn = {}{}", TermColor::YELLOW,
                 dump_retired_insn, TermColor::RESET);
  }

  if (const auto *var = std::getenv("DIFFTEST_NR_RETIRE_RECORDS")) {
    nr_retire_records = std::strtoull(var, nullptr, 10);
  }

  if (const auto *var = std::getenv("DIFFTEST_CRASH_ON_ERROR")) {
    crash_on_error = std::strtoull(var, nullptr, 10);
    spdlog::info("{}crash_on_error = {}{}", TermColor::YELLOW,
                 crash_on_error, TermColor::RESET);
  }

  if (const auto *var = std::getenv("DIFFTEST_DEBUG_CRASH")) {
    debug_crash = std::strtoull(var, nullptr, 10);
    spdlog::info("{}debug_crash = {}{}", TermColor::YELLOW, debug_crash,
                 TermColor::RESET);
  }

  if (const auto *var = std::getenv("DIFFTEST_CRASH_ON_PC")) {
    std::string s(var);
    std::string token;
    std::stringstream ss(s);

    auto trim = [](std::string &str) {
      // trim left
      str.erase(str.begin(), std::find_if(str.begin(), str.end(), [](unsigned char ch) { return !std::isspace(ch); }));
      // trim right
      str.erase(std::find_if(str.rbegin(), str.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(), str.end());
    };

    while (std::getline(ss, token, ',')) {
      trim(token);
      if (token.empty()) continue;

      int base = 10;
      if (token.size() > 2 && (token[0] == '0') && (token[1] == 'x' || token[1] == 'X')) {
        base = 16;
      }

      try {
        uint64_t val = std::stoull(token, nullptr, base);
        crash_on_pc.insert(val);
      } catch (const std::exception &e) {
        spdlog::warn("{}Ignoring invalid DIFFTEST_CRASH_ON_PC element '{}'{}", TermColor::YELLOW, token, TermColor::RESET);
      }
    }

    if (!crash_on_pc.empty()) {
      std::ostringstream out;
      bool first = true;
      for (auto v : crash_on_pc) {
        if (!first) out << ", ";
        out << "0x" << std::hex << v << std::dec;
        first = false;
      }
      spdlog::info("{}crash_on_pc = {}{}", TermColor::YELLOW, out.str(), TermColor::RESET);
    }
  }

  if (const auto *var = std::getenv("DIFFTEST_BREAKPOINTS")) {
    std::string s(var);
    std::string token;
    std::stringstream ss(s);

    auto trim = [](std::string &str) {
      // trim left
      str.erase(str.begin(), std::find_if(str.begin(), str.end(), [](unsigned char ch) { return !std::isspace(ch); }));
      // trim right
      str.erase(std::find_if(str.rbegin(), str.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(), str.end());
    };

    while (std::getline(ss, token, ',')) {
      trim(token);
      if (token.empty()) continue;

      int base = 10;
      if (token.size() > 2 && (token[0] == '0') && (token[1] == 'x' || token[1] == 'X')) {
        base = 16;
      }

      try {
        uint64_t val = std::stoull(token, nullptr, base);
        breakpoints.insert(val);
      } catch (const std::exception &e) {
        spdlog::warn("{}Ignoring invalid DIFFTEST_BREAKPOINTS element '{}'{}", TermColor::YELLOW, token, TermColor::RESET);
      }
    }

    if (!breakpoints.empty()) {
      std::ostringstream out;
      bool first = true;
      for (auto v : breakpoints) {
        if (!first) out << ", ";
        out << "0x" << std::hex << v << std::dec;
        first = false;
      }
      spdlog::info("{}breakpoints = {}{}", TermColor::YELLOW, out.str(), TermColor::RESET);
    }
  }
  
  // resolve the architecture-dependent configs
  if (!arch.name.compare("vortex")) {
    fp_ulp_tolerance = 2;
    fp2int_tolerance = 1;

    trust_csr = [](uint32_t csr_addr) -> bool {
      return (csr_addr >= 0xB00 && csr_addr <= 0xB9F); // perf counters
    };

  } else if (!arch.name.compare("ventus")) {
    fp_ulp_tolerance = 2;
    fp2int_tolerance = 1;

    trust_csr = [](uint32_t csr_addr) -> bool {
      return (csr_addr >= 0xB00 && csr_addr <= 0xB9F); // FIXME
    };

  } else {
    spdlog::error("Unknown arch '{}'", arch.name);
    std::abort();
  }
}

Config *Config::g_instance_;

} // namespace difftest