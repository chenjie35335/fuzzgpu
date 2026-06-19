#pragma once

#include <difftest_gpu/types.hh>

#include "spdlog/formatter.h"
#include <bitset>
#include <cassert>
#include <type_traits>

#include <vector>

#include "spdlog/fmt/fmt.h"

#include "assert.hh"

namespace difftest {

using CoreMask = VarBitset;

} // namespace difftest

namespace fmt {

template <>
struct formatter<difftest::RegId> {
  constexpr auto parse(format_parse_context &ctx) { return ctx.begin(); }
  auto format(const difftest::RegId &rd, format_context &ctx) const {
    return fmt::format_to(ctx.out(), "%{}{}", rd.type_str(), rd.idx);
  }
};

template <>
struct formatter<difftest::Opcode> {
  constexpr auto parse(format_parse_context &ctx) { return ctx.begin(); }
  auto format(difftest::Opcode opc, format_context &ctx) const {
    const char *str;
    switch (opc) {
    case difftest::Opcode::Call:
      str = "Call";
      break;
    case difftest::Opcode::CtrlStatusRead:
      str = "CtrlStatusRead";
      break;
    case difftest::Opcode::Others:
      str = "Others";
      break;
    case difftest::Opcode::Ret:
      str = "Ret";
      break;
    case difftest::Opcode::RetCall:
      str = "RetCall";
      break;
    case difftest::Opcode::TmaskWrite:
      str = "TmaskWrite:";
      break;
    case difftest::Opcode::FloatingPoint:
      str = "FloatingPoint";
      break;
    default:
      std::abort();
    }
    return fmt::format_to(ctx.out(), "{}", str);
  }
};

} // namespace fmt
