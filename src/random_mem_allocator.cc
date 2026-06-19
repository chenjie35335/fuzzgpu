#include "spdlog/fmt/fmt.h"
#include <algorithm>
#include <cassert>
#include <stdexcept>

#include "config.hh"
#include "util.hh"

#include "random_mem_allocator.hh"

namespace fuzzgpu {

RandomMemoryAllocator::RandomMemoryAllocator(
    const std::pair<uint64_t, uint64_t> &data_range,
    const std::pair<uint64_t, uint64_t> &code_range, std::mt19937_64 &rng)
  : data_range_(data_range)
  , code_range_(code_range)
  , rng_(rng) {
  auto pool_end = std::max(data_range_.second, code_range_.second);
  bool begin_uint64_max_p1 = false;
  if (pool_end == std::numeric_limits<uint64_t>::max()) {
    pool_end -= 1;
    begin_uint64_max_p1 = true;
  }
  allocated_blocks_.emplace(/*don't care*/ MemBlkType::CODE, pool_end + 1,
                            pool_end + 1, begin_uint64_max_p1);

  spdlog::info("Data memory range: [0x{:x}, 0x{:x}]", data_range_.first,
               data_range_.second);
  spdlog::info("Code memory range: [0x{:x}, 0x{:x}]", code_range_.first,
               code_range_.second);
}

bool RandomMemoryAllocator::check_allocated_(uint64_t addr,
                                             uint64_t &end) const {
  // find the first range whose begin > x
  auto it =
      allocated_blocks_.upper_bound({/*don't care*/ MemBlkType::CODE, addr, 0});

  if (it != allocated_blocks_.begin()) {
    --it;
    if (it->overlaps(addr)) {
      // failed, retry
      return true;
    }
    ++it;
  }
  if (it->begin_uint64_max_p1) {
    end = std::numeric_limits<uint64_t>::max();
  } else {
    end = it->begin - 1;
  }
  assert(end >= addr);
  return false;
}

MemoryBlock RandomMemoryAllocator::allocate(MemBlkType type, uint64_t addr,
                                            size_t min_size) {
  uint64_t end;
  switch (type) {
  case MemBlkType::CODE:
    if (addr < code_range_.first || addr > code_range_.second)
      return MemoryBlock();
    break;
  case MemBlkType::WRITABLE:
    if (addr < data_range_.first || addr > data_range_.second)
      return MemoryBlock();
    break;
  }
  if (check_allocated_(addr, end)) {
    return MemoryBlock();
  }
  if (end - addr + 1 < min_size) {
    return MemoryBlock();
  }
  MemoryBlock ret(type, addr, end);
  allocated_blocks_.insert(ret);
  return ret;
}

MemoryBlock RandomMemoryAllocator::allocate(MemBlkType type,
                                            uint64_t center_pos,
                                            uint64_t max_lower_delta,
                                            uint64_t max_upper_delta,
                                            size_t min_size, size_t alignment) {
  uint64_t pool_begin_, pool_end_;
  switch (type) {
  case MemBlkType::CODE:
    pool_begin_ = code_range_.first;
    pool_end_ = code_range_.second;
    break;
  case MemBlkType::WRITABLE:
    pool_begin_ = data_range_.first;
    pool_end_ = data_range_.second;
    break;
  default:
    std::abort();
  }
  assert(min_size);
  assert(center_pos <= pool_end_);

  const size_t max_padding_sz = alignment - 1;
  assert(pool_end_ > max_padding_sz);

  uint64_t lower =
      center_pos > max_lower_delta ? center_pos - max_lower_delta : 0;
  size_t upper =
      std::min(saturated_add(center_pos, max_upper_delta), pool_end_);

  if (upper < max_padding_sz)
    return MemoryBlock();
  upper -= max_padding_sz;

  if (upper < lower + min_size - 1)
    return MemoryBlock();

  std::uniform_int_distribution<uint64_t> addr_dist(lower, upper);

  for (size_t tries = 0; tries < Config::instance().max_alloc_tries; ++tries) {
    uint64_t x = align_up(addr_dist(rng_), alignment);

    uint64_t end;
    if (check_allocated_(x, end)) {
      continue;
    }
    assert(end + 1 >= x);
    if (end + 1 - x < min_size) {
      continue;
    }
    if (x < pool_begin_ || end > pool_end_) {
      continue;
    }

    MemoryBlock ret(type, x, end);
    allocated_blocks_.insert(ret);
    return ret;
  }

  return MemoryBlock();
}

std::optional<MemoryBlock> RandomMemoryAllocator::query(uint64_t addr) const {
  auto it =
      allocated_blocks_.upper_bound({/*don't care*/ MemBlkType::CODE, addr, 0});

  if (it != allocated_blocks_.begin()) {
    --it;
    if (it->overlaps(addr)) {
      return *it;
    }
  }
  return std::nullopt;
}

void RandomMemoryAllocator::shrink(MemoryBlock &src, size_t shrinked_size) {
  assert(shrinked_size <= src.size());
  MemoryBlock res(src.type, src.begin, src.begin + shrinked_size - 1);
  assert(res.end <= src.end);

  if (res.end < src.end) {
    allocated_blocks_.erase(src);
    allocated_blocks_.emplace(res);
  }
  // update the block
  src = res;
}

uint64_t RandomMemoryAllocator::min_paddr(MemBlkType type) const {
  switch (type) {
  case MemBlkType::CODE:
    return code_range_.first;
  case MemBlkType::WRITABLE:
    return data_range_.first;
  default:
    throw std::runtime_error("Unknown MemBlkType");
  }
}

uint64_t RandomMemoryAllocator::max_paddr(MemBlkType type) const {
  switch (type) {
  case MemBlkType::CODE:
    return code_range_.second;
  case MemBlkType::WRITABLE:
    return data_range_.second;
  default:
    throw std::runtime_error("Unknown MemBlkType");
  }
}

} // namespace fuzzgpu