#pragma once

#include "spdlog/spdlog.h"
#include <algorithm>
#include <cctype>
#include <string>
#include <type_traits>
#include <random>

namespace fuzzgpu {

template <std::ranges::range T>
void to_lower_inplace(T &s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
}

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

std::string hexdump(const uint8_t *bytes, size_t size_);

template <typename T>
  requires std::is_unsigned_v<T>
constexpr T align_up(T value, T alignment) noexcept {
  return (value + alignment - 1) & ~(alignment - 1);
}

uint64_t saturated_add(uint64_t x, uint64_t y);

template<typename T>
void topk_shuffle(std::vector<T> &src, size_t topk, std::mt19937_64 &rng) {
  // top-k Fisher-Yates shuffe
  for (unsigned i = 0; i < topk; ++i) {
    std::uniform_int_distribution<unsigned> dist(
        i, src.size() - 1);
    unsigned j = dist(rng);
    std::swap(src[i], src[j]);
  }
}

} // namespace fuzzgpu
