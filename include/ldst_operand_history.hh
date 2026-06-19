#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fuzzgpu {

/// @brief Maximum threads per warp stored in vector-register history.
constexpr size_t kMaxThreadsPerWarp = 64;

/// @brief Operand pair shape used for load/store address calculation.
enum class LoadStoreOperandPairType {
  SGPR_IMM,  // sGPR + imm
  VGPR_VGPR, // vGPR + vGPR
  VGPR_IMM,  // vGPR + imm
};

/// @brief Per-thread values captured for load/store operand matching.
struct VGPRValues {
  ///< Per-thread computed values or one scalar value for SGPR_IMM.
  std::vector<uint64_t> values;
  ///< Active thread mask when values were captured.
  std::vector<bool> tmask;
  ///< Warp ID used to distinguish same-order matches.
  uint32_t warp_id{0};

  /// @brief Return true for exact active-lane value equality.
  bool exact_match(const VGPRValues &other) const {
    if (tmask.size() != other.tmask.size())
      return false;
    for (size_t i = 0; i < values.size() && i < other.values.size(); ++i) {
      if (i < tmask.size() && tmask[i]) {
        if (!other.tmask[i] || values[i] != other.values[i])
          return false;
      }
    }
    return true;
  }

  /// @brief Check unordered partial equality for hash-collision detection.
  bool unordered_partial_match(const VGPRValues &other, bool must_cross_thread = true) const {
    // Collect active address values from both
    std::vector<uint64_t> this_addrs, other_addrs;
    for (size_t i = 0; i < tmask.size(); ++i) {
      if (tmask[i] && i < values.size())
        this_addrs.push_back(values[i]);
    }
    for (size_t i = 0; i < other.tmask.size(); ++i) {
      if (other.tmask[i] && i < other.values.size())
        other_addrs.push_back(other.values[i]);
    }

    // Same warp: order must be different (reject if identical order)
    if (must_cross_thread && warp_id == other.warp_id && this_addrs == other_addrs) {
      return false;
    }

    // Compare as multiset (sorted)
    std::sort(this_addrs.begin(), this_addrs.end());
    std::sort(other_addrs.begin(), other_addrs.end());
    // Partial equality: truncate to smaller size
    const auto trunc_sz = std::min(this_addrs.size(), other_addrs.size());
    this_addrs.resize(trunc_sz);
    other_addrs.resize(trunc_sz);
    return this_addrs == other_addrs;
  }

  /// @brief Return true when exact_match() succeeds.
  bool operator==(const VGPRValues &other) const { return exact_match(other); }
};

/// @brief Captured load/store operands and derived address hash.
struct LoadStoreOperandHistory {
  ///< Shape of the operands used to compute addresses.
  LoadStoreOperandPairType pair_type;
  ///< Hash of computed addresses for fast lookup.
  uint64_t addr_hash;

  ///< First operand register index.
  uint32_t op1_id;
  ///< Second operand register index, or zero when the second operand is imm.
  uint32_t op2_id;
  ///< Immediate value when pair_type includes an immediate.
  int64_t imm_val;

  // Operand values for matching (not just computed addresses):
  // - op1_values: per-thread vGPR values for VGPR_IMM/VGPR_VGPR, or single sGPR
  // for SGPR_IMM
  // - op2_values: per-thread vGPR values for VGPR_VGPR only
  ///< Captured first operand values.
  VGPRValues op1_values;
  ///< Captured second operand values.
  VGPRValues op2_values;

  ///< Hash of op1_values for fast lookup.
  uint64_t op1_hash{0};

  // Computed base addresses for matching:
  // - SGPR_IMM: single value (sgpr + imm)
  // - VGPR_IMM: per-thread values (vgpr[tid] + imm)
  // - VGPR_VGPR: per-thread values (vgpr1[tid] + vgpr2[tid])
  ///< Computed base addresses used for matching.
  VGPRValues computed_addresses;

  ///< Memory access size used when comparing address ranges.
  unsigned mem_access_size{1};

  ///< Operand mask describing which optional fields are valid.
  uint64_t operand_mask{0};
  ///< Number of vector fields when HAS_NF is present.
  uint32_t nf{0};
  ///< Memory element width flag when HAS_MEW is present.
  bool mew{false};
  ///< Memory operation type when HAS_MOP is present.
  uint32_t mop{0};

  ///< True for Ventus GPU indexed load/store address semantics.
  bool is_ventus_gpu_indexed{false};

  /// @brief Compare the coarse lookup identity of two history entries.
  bool operator==(const LoadStoreOperandHistory &other) const {
    return pair_type == other.pair_type && addr_hash == other.addr_hash;
  }
};

/// @brief Key used by the indexed load/store history map.
struct LoadStoreOperandKey {
  ///< Operand pair shape.
  LoadStoreOperandPairType pair_type;
  ///< Hash of first operand values for matching.
  uint64_t op1_hash;

  /// @brief Return true when both key components match.
  bool operator==(const LoadStoreOperandKey &other) const {
    return pair_type == other.pair_type && op1_hash == other.op1_hash;
  }
};

/// @brief Hash function for LoadStoreOperandKey.
struct LoadStoreOperandKeyHash {
  std::size_t operator()(const LoadStoreOperandKey &key) const;
};

/// @brief Indexed sliding window of load/store operand histories.
class LoadStoreOperandHistoryIndex {
public:
  /// @brief Construct an empty history index.
  LoadStoreOperandHistoryIndex();

  /// @brief Return true when no history entries are indexed.
  bool empty() const { return key_order_.empty(); }
  /// @brief Return the number of keys tracked by the sliding window.
  size_t size() const { return key_order_.size(); }

  /// @brief Add one history entry and evict old entries if needed.
  void add_history(const LoadStoreOperandHistory &hist);

  /// @brief Find all histories matching @p pair_type and @p op1_hash.
  std::vector<LoadStoreOperandHistory>
  find_matching(LoadStoreOperandPairType pair_type, uint64_t op1_hash) const;

  /// @brief Sample entries of one pair type for replay strategies.
  template <typename RNG>
  std::vector<const LoadStoreOperandHistory *>
  sample_by_type(LoadStoreOperandPairType pair_type, size_t max_samples,
                 RNG &rng,
                 std::optional<uint32_t> exclude_warp_id = std::nullopt) const {
    auto type_it = type_warp_to_histories_.find(pair_type);
    if (type_it == type_warp_to_histories_.end()) {
      return {};
    }
    const auto &warp_map = type_it->second;
    
    // Build list of valid warps and compute total valid count
    std::vector<std::pair<uint32_t, size_t>> valid_warps; // (warp_id, count)
    size_t total_valid = 0;
    for (const auto &[warp_id, entries] : warp_map) {
      if (!exclude_warp_id.has_value() || warp_id != exclude_warp_id.value()) {
        valid_warps.emplace_back(warp_id, entries.size());
        total_valid += entries.size();
      }
    }
    
    if (total_valid == 0) {
      return {};
    }
    
    // If total <= max_samples, return all valid entries
    if (total_valid <= max_samples) {
      std::vector<const LoadStoreOperandHistory *> result;
      result.reserve(total_valid);
      for (const auto &[warp_id, count] : valid_warps) {
        for (const auto *ptr : warp_map.at(warp_id)) {
          result.push_back(ptr);
        }
      }
      return result;
    }
    
    // Random sampling: generate k unique random indices in [0, total_valid)
    // Use Floyd's algorithm for O(k) unique sampling
    std::unordered_set<size_t> selected_indices;
    selected_indices.reserve(max_samples);
    for (size_t i = total_valid - max_samples; i < total_valid; ++i) {
      std::uniform_int_distribution<size_t> dist(0, i);
      size_t idx = dist(rng);
      if (!selected_indices.insert(idx).second) {
        selected_indices.insert(i);
      }
    }
    
    // Map indices to actual entries using cumulative counts
    std::vector<const LoadStoreOperandHistory *> result;
    result.reserve(max_samples);
    
    // Build cumulative prefix for binary search
    std::vector<size_t> cumulative;
    cumulative.reserve(valid_warps.size() + 1);
    cumulative.push_back(0);
    for (const auto &[warp_id, count] : valid_warps) {
      cumulative.push_back(cumulative.back() + count);
    }
    
    for (size_t idx : selected_indices) {
      // Binary search to find which warp this index belongs to
      auto it = std::upper_bound(cumulative.begin(), cumulative.end(), idx);
      size_t warp_idx = std::distance(cumulative.begin(), it) - 1;
      size_t local_idx = idx - cumulative[warp_idx];
      result.push_back(warp_map.at(valid_warps[warp_idx].first)[local_idx]);
    }
    
    return result;
  }

  /// @brief Return the number of entries for a given pair type.
  size_t count_by_type(LoadStoreOperandPairType pair_type) const;

  /// @brief Return a copy of all history entries.
  std::vector<LoadStoreOperandHistory> all_entries() const;

  /// @brief Remove all indexed history.
  void clear();

private:
  ///< Primary index from operand key to matching histories.
  std::unordered_map<LoadStoreOperandKey, std::vector<LoadStoreOperandHistory>,
                     LoadStoreOperandKeyHash>
      type_op1_to_histories_;
  ///< Secondary index from pair type to all entries of that type.
  std::unordered_map<LoadStoreOperandPairType, std::deque<LoadStoreOperandHistory>>
      type_to_histories_;
  ///< Tertiary index from pair type and warp ID to sampled entries.
  std::unordered_map<LoadStoreOperandPairType, 
                     std::unordered_map<uint32_t, std::vector<const LoadStoreOperandHistory*>>>
      type_warp_to_histories_;
  ///< Key insertion order used by the sliding window.
  std::deque<LoadStoreOperandKey> key_order_;
  ///< Maximum number of keys retained in the sliding window.
  size_t window_size_;
};

} // namespace fuzzgpu
