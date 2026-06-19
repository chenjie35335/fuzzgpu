#pragma once

#include <cassert>
#include <spdlog/fmt/fmt.h>
#include <iostream>
#include "spdlog/spdlog.h"

namespace difftest {

template <typename... Args>
static inline void assert_log(bool expr, fmt::format_string<Args...> fmt,
                              Args &&...fmt_args) {
#ifndef NDEBUG
  if (!expr) [[unlikely]] {
    spdlog::error(fmt, std::forward<Args>(fmt_args)...);
    std::abort();
  }
#endif
}

} // namespace difftest