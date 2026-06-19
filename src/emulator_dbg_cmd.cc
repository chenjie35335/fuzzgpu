// spike-compatible GPU ISA emulator debug commands

#include "spdlog/spdlog.h"
#include <unordered_set>

#include "emulator_dbg_cmd.hh"

namespace fuzzgpu {

static std::vector<std::string> xpr_name = {
    "zero", "ra", "sp", "gp", "tp",  "t0",  "t1", "t2", "s0", "s1", "a0",
    "a1",   "a2", "a3", "a4", "a5",  "a6",  "a7", "s2", "s3", "s4", "s5",
    "s6",   "s7", "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6"};

static std::vector<std::string> fpr_name = {
    "ft0", "ft1", "ft2",  "ft3",  "ft4", "ft5", "ft6",  "ft7",
    "fs0", "fs1", "fa0",  "fa1",  "fa2", "fa3", "fa4",  "fa5",
    "fa6", "fa7", "fs2",  "fs3",  "fs4", "fs5", "fs6",  "fs7",
    "fs8", "fs9", "fs10", "fs11", "ft8", "ft9", "ft10", "ft11"};

static std::unique_ptr<difftest::Trace> trace;

static uint64_t parse_int(const std::string &str) {
  if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
    return std::stoull(str, nullptr, 16);
  } else {
    return std::stoull(str, nullptr, 10);
  }
}

static unsigned cur_xlen;

static constexpr std::string LEADING_PROMPT = "(dbg) ";

static const unsigned max_warp_gid = 1; // single warp for interactive debugging

/// @brief Internal exception used to enter interactive debugger mode.
class trap_interactive : public std::exception {};

static int cmd_until(difftest::ISAEmulator &isa_emu,
                     const std::vector<std::string> &args) {
  if (args.size() != 3 || args[0] != "pc") {
    std::cerr << LEADING_PROMPT << "usage: until pc <warp_gid> <pc>"
              << std::endl;
    return 1;
  }

  const unsigned warp_gid = parse_int(args[1]);
  const auto pc = parse_int(args[2]);

  if (!trace) {
    trace = std::make_unique<difftest::Trace>(isa_emu.get_arch());
    trace->pc = 0;
  }

  while (true) {
    if (trace->pc == pc)
      return 0;

    if (!isa_emu.schedulable(warp_gid)) {
      std::cerr << LEADING_PROMPT << "warp " << warp_gid << " not schedulable"
                << std::endl;
      return 1;
    }

    isa_emu.execute_warp(warp_gid, trace.get());

    if (trace->trap_taken) {
      std::cerr << LEADING_PROMPT << "exception" << std::endl;
      return 1;
    }
  }
}

static int cmd_reg(difftest::ISAEmulator &isa_emu,
                   const std::vector<std::string> &args) {
  if (args.size() < 1) {
    std::cerr << LEADING_PROMPT << "usage: reg <warp_gid> (<reg>)" << std::endl;
    return 1;
  }

  const unsigned warp_gid = parse_int(args[0]);
  if (warp_gid > max_warp_gid) {
    std::cerr << LEADING_PROMPT << "invalid warp GID: " << warp_gid
              << std::endl;
    return 1;
  }
  const unsigned thread_lid = 0;

  if (args.size() == 1) {
    // Show all the regs!
    bool newline = true;
    for (size_t r = 0; r < xpr_name.size(); ++r) {
      const uint64_t reg_value = isa_emu.reg_read(
          warp_gid, thread_lid, difftest::RegId(difftest::RegType::Int, r));
      if (newline) {
        std::cerr << LEADING_PROMPT;
        newline = false;
      }
      std::cerr << std::setfill(' ') << std::setw(4) << xpr_name[r] << ": 0x"
                << std::setfill('0') << std::setw(cur_xlen / 4) << std::hex
                << reg_value << std::dec;
      if ((r + 1) % 4 == 0) {
        std::cerr << std::endl;
        newline = true;
      }
    }
  } else {
    unsigned reg_id =
        std::find(xpr_name.begin(), xpr_name.end(), args[1]) - xpr_name.begin();
    if (reg_id == xpr_name.size()) {
      reg_id = parse_int(args[1]);
    }
    if (reg_id >= xpr_name.size()) {
      std::cerr << LEADING_PROMPT << "invalid register: " << args[1]
                << std::endl;
      return 1;
    }

    uint64_t reg_value = isa_emu.reg_read(
        warp_gid, thread_lid, difftest::RegId(difftest::RegType::Int, reg_id));
    std::cerr << LEADING_PROMPT << "0x" << std::setfill('0')
              << std::setw(cur_xlen / 4) << std::hex << reg_value << std::dec
              << std::endl;
  }
  return 0;
}

static int cmd_freg(difftest::ISAEmulator &isa_emu,
                    const std::vector<std::string> &args) {
  if (args.size() != 2) {
    std::cerr << LEADING_PROMPT << "usage: freg <warp_gid> <freg>" << std::endl;
    return 1;
  }

  const unsigned warp_gid = parse_int(args[0]);
  if (warp_gid > max_warp_gid) {
    std::cerr << LEADING_PROMPT << "invalid warp GID: " << warp_gid
              << std::endl;
    return 1;
  }
  const unsigned thread_lid = 0;
  unsigned reg_id =
      std::find(fpr_name.begin(), fpr_name.end(), args[1]) - fpr_name.begin();
  if (reg_id == fpr_name.size()) {
    reg_id = parse_int(args[1]);
  }
  if (reg_id >= fpr_name.size()) {
    std::cerr << LEADING_PROMPT << "invalid register: " << args[1] << std::endl;
    return 1;
  }

  uint64_t freg_value = isa_emu.reg_read(
      warp_gid, thread_lid, difftest::RegId(difftest::RegType::Float, reg_id));
  if (cur_xlen == 32) {
    freg_value = 0xffffffff00000000ULL | freg_value; // boxed fp32
  }
  std::cerr << LEADING_PROMPT << std::hex << "0x" << std::setfill('0')
            << std::setw(16) << ~uint64_t(0) << std::setw(16) << freg_value
            << std::endl;
  return 0;
}

static int cmd_vreg(difftest::ISAEmulator &isa_emu,
                    const std::vector<std::string> &args) {
  if (args.size() != 2) {
    std::cerr << LEADING_PROMPT << "usage: vreg <warp_gid> <vreg>" << std::endl;
    return 1;
  }
  const unsigned warp_gid = parse_int(args[0]);
  if (warp_gid > max_warp_gid) {
    std::cerr << LEADING_PROMPT << "invalid warp GID: " << warp_gid
              << std::endl;
    return 1;
  }
  unsigned reg_id = parse_int(args[1]);
  const unsigned thread_lid = 0;
  uint64_t val_lane0 = isa_emu.reg_read(
      warp_gid, thread_lid,
      difftest::RegId(difftest::RegType::Vec, reg_id,
                      cur_xlen)); // ventus using the fixed vsew=32
  if (cur_xlen == 32) {
    val_lane0 = 0xffffffff00000000ULL | val_lane0; // boxed fp32
  }
  std::cerr << LEADING_PROMPT << std::hex << "0x" << std::setfill('0')
            << std::setw(16) << ~uint64_t(0) << std::setw(16) << val_lane0
            << std::endl;
  return 0;
}

static int cmd_priv(difftest::ISAEmulator &isa_emu,
                    const std::vector<std::string> &args) {
  if (args.size() != 1) {
    std::cerr << LEADING_PROMPT << "usage: priv <warp_gid>" << std::endl;
    return 1;
  }

  const unsigned warp_gid = parse_int(args[0]);
  if (warp_gid > max_warp_gid) {
    std::cerr << LEADING_PROMPT << "invalid warp GID: " << warp_gid
              << std::endl;
    return 1;
  }
  std::cerr << LEADING_PROMPT << "M" << std::endl;
  return 0;
}

static int cmd_r(difftest::ISAEmulator &isa_emu,
                 const std::vector<std::string> &args) {
  if (args.size() < 1) {
    std::cerr << LEADING_PROMPT << "usage: r <steps>" << std::endl;
    return 1;
  }

  const unsigned steps = args.size() ? parse_int(args[0].c_str()) : -1;
  std::unordered_set<unsigned> schedulable_warps;
  for (unsigned i = 0; i < steps; ++i) {
    schedulable_warps.clear();
    for (unsigned warp_gid = 0; warp_gid < max_warp_gid; ++warp_gid) {
      if (isa_emu.schedulable(warp_gid)) {
        schedulable_warps.insert(warp_gid);
        isa_emu.execute_warp(warp_gid, nullptr);
      }
    }
    if (schedulable_warps.empty()) {
      return 0;
    }
  }
  return 0;
}

static int cmd_q(difftest::ISAEmulator &isa_emu,
                 const std::vector<std::string> &args) {
  exit(0);
  return 0;
}

typedef int (*pfn_invoke_dbg_cmd_t)(difftest::ISAEmulator &isa_emu,
                                    const std::vector<std::string> &args);

static const std::unordered_map<std::string, pfn_invoke_dbg_cmd_t> dbg_cmd_map =
    {
        {"until", cmd_until}, {"reg", cmd_reg},   {"freg", cmd_freg},
        {"vreg", cmd_vreg},   {"priv", cmd_priv}, {"r", cmd_r},
        {"q", cmd_q},
};

int run_debug_commands(const std::string &cmd_file_name,
                       difftest::ISAEmulator &isa_emu) {
  if (cmd_file_name.empty()) {
    return 0;
  }

  cur_xlen = isa_emu.get_arch().word_bits;

  auto cmd_file = fopen(cmd_file_name.c_str(), "r");
  if (!cmd_file) {
    spdlog::error("Failed to open debug command file: {}", cmd_file_name);
    return 1;
  }
  int ret = 0;
#define MAX_CMD_STR 40 // maximum possible size of a command line
#define STR_(X) #X     // these definitions allow to use a macro as a string
#define STR(X) STR_(X)
  while (true) {
    std::string s;
    char cmd_str[MAX_CMD_STR + 1]; // only used for following fscanf
    // first get commands from file, if cmd_file has been set
    if (!feof(cmd_file) &&
        fscanf(cmd_file, "%" STR(MAX_CMD_STR) "[^\n]\n", cmd_str) == 1) {
      // up to MAX_CMD_STR characters before \n, skipping \n
      s = cmd_str;
    } else {
      break; // EOF
    }

    std::stringstream ss(s);
    std::string cmd, tmp;
    std::vector<std::string> args;

    if (!(ss >> cmd)) {
      break;
    }

    while (ss >> tmp)
      args.push_back(tmp);

    if (dbg_cmd_map.count(cmd)) {
      ret = dbg_cmd_map.at(cmd)(isa_emu, args);
    } else {
      std::cerr << LEADING_PROMPT << "Unknown command: " << cmd << std::endl;
      ret = 1;
    }
  }
  return ret;
}

} // namespace fuzzgpu
