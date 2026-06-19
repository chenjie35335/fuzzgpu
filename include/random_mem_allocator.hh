#pragma once

#include <cstdint>
#include <map>
#include <random>
#include <set>
#include <vector>

namespace fuzzgpu {

/// @brief Memory allocation domain for generated code and data blocks.
enum class MemBlkType {
  CODE = 0,
  WRITABLE,
};

/// @brief Closed address interval allocated from a code or data range.
struct MemoryBlock {
  ///< Allocation domain.
  MemBlkType type;
  ///< First byte in the block.
  uint64_t begin;
  ///< Last byte in the block; the interval is closed.
  uint64_t end;
  ///< Marks the sentinel block whose begin is uint64_t::max() + 1.
  bool begin_uint64_max_p1;

  /// @brief Construct an invalid block.
  MemoryBlock()
    : begin(1)
    , end(0) {}

  /// @brief Construct a closed interval in the given memory domain.
  MemoryBlock(MemBlkType type_, uint64_t b, uint64_t e,
              bool b_uint64_max_p1 = false)
    : type(type_)
    , begin(b)
    , end(e)
    , begin_uint64_max_p1(b_uint64_max_p1) {}
  /// @brief Order blocks by begin address for efficient set queries.
  bool operator<(const MemoryBlock &blk) const { return begin < blk.begin; }
  bool operator==(const MemoryBlock &blk) const = delete;
  /// @brief Return whether @p addr is contained by this block.
  bool overlaps(uint64_t addr) const {
    if (!begin_uint64_max_p1)
      return begin <= addr && addr <= end;
    else
      return false; // begin is the uint64::max + 1
  }

  /// @brief Return the number of bytes in this valid closed interval.
  size_t size() const {
    assert(!begin_uint64_max_p1);
    return end - begin + 1;
  }
  /// @brief Return whether the interval contains at least one byte.
  bool valid() const { return begin <= end; }
};

/// @brief Random non-overlapping allocator for generated code/data memory.
class RandomMemoryAllocator {
  ///< Inclusive memory pool boundaries for data and code.
  std::pair<uint64_t, uint64_t> data_range_, code_range_;

  ///< Blocks already handed out by this allocator.
  std::set<MemoryBlock> allocated_blocks_;

  ///< Shared PRNG owned by the fuzzer state.
  std::mt19937_64 &rng_;

  bool check_allocated_(uint64_t addr, uint64_t &en) const;

public:
  /// @brief Create an allocator for the configured data and code ranges.
  RandomMemoryAllocator(const std::pair<uint64_t, uint64_t> &data_range,
                        const std::pair<uint64_t, uint64_t> &code_range,
                        std::mt19937_64 &rng);

  /// @brief Allocate a block near @p center_pos while respecting deltas.
  MemoryBlock allocate(MemBlkType type, uint64_t center_pos,
                       uint64_t max_lower_delta, uint64_t max_upper_delta,
                       size_t min_size, size_t alignment);
  /// @brief Allocate a block that contains or starts at @p addr.
  MemoryBlock allocate(MemBlkType type, uint64_t addr, size_t min_size);

  /// @brief Return the allocated block containing @p addr, if any.
  std::optional<MemoryBlock> query(uint64_t addr) const;

  /// @brief Shrink @p src in place to @p shrinked_size bytes.
  void shrink(MemoryBlock &src, size_t shrinked_size);

  /// @brief Return the minimum physical address for the selected domain.
  uint64_t min_paddr(MemBlkType type) const;
  /// @brief Return the maximum physical address for the selected domain.
  uint64_t max_paddr(MemBlkType type) const;
};

} // namespace fuzzgpu
