#pragma once

#include <random>
#include <type_traits>
#include <unordered_map>

#include "isa/riscv/riscv_meta_list.hh"

namespace fuzzgpu {

/// @brief Caches uniform integer samplers for fixed bit widths.
class BitsSampler {

  ///< PRNG shared with the fuzzer state.
  std::mt19937_64 &rng_;
  ///< Lazily created distributions keyed by requested bit width.
  std::unordered_map<unsigned, std::uniform_int_distribution<uint64_t> *> samplers;

public:
  /// @brief Bind the sampler cache to @p rng.
  BitsSampler(std::mt19937_64 &rng)
    : rng_(rng) {}

  /// @brief Generate a random unsigned value in the low @p bits bits.
  uint64_t gen_bits(unsigned bits) {
    auto it = samplers.find(bits);
    uint64_t ret;
    if (it != samplers.end()) {
      ret = it->second->operator()(rng_);
    } else {
      assert(bits <= 64);
      auto *sampler =
          new std::uniform_int_distribution<uint64_t>(0, (1ULL << bits) - 1);
      samplers[bits] = sampler;
      ret = sampler->operator()(rng_);
    }
    assert(ret < (1ULL << bits));
    return ret;
  }
};

} // namespace fuzzgpu
