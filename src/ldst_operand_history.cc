#include "ldst_operand_history.hh"
#include "config.hh"

namespace fuzzgpu {

std::size_t LoadStoreOperandKeyHash::operator()(const LoadStoreOperandKey &key) const {
  // Combine hashes without overflow
  std::size_t h1 = std::hash<int>{}(static_cast<int>(key.pair_type));
  std::size_t h2 = std::hash<uint64_t>{}(key.op1_hash);
  return h1 ^ (h2 << 1); // Simple but effective hash combination
}

LoadStoreOperandHistoryIndex::LoadStoreOperandHistoryIndex() 
  : window_size_(Config::instance().ldst_operand_history_window_size) {}

void LoadStoreOperandHistoryIndex::add_history(const LoadStoreOperandHistory &hist) {
  LoadStoreOperandKey key{hist.pair_type, hist.op1_hash};
  
  // Check for duplicate: same key and same addr_hash
  auto it = type_op1_to_histories_.find(key);
  if (it != type_op1_to_histories_.end()) {
    for (const auto &existing : it->second) {
      if (existing == hist) {
        return; // Duplicate found, skip adding
      }
    }
  }
  
  type_op1_to_histories_[key].push_back(hist);
  type_to_histories_[hist.pair_type].push_back(hist);
  // Update tertiary index with pointer to newly added entry
  const auto *ptr = &type_to_histories_[hist.pair_type].back();
  type_warp_to_histories_[hist.pair_type][hist.computed_addresses.warp_id].push_back(ptr);
  key_order_.push_back(key);
  
  // Maintain sliding window size
  while (key_order_.size() > window_size_) {
    // Remove the oldest entry
    const auto &old_key = key_order_.front();
    auto it = type_op1_to_histories_.find(old_key);
    if (it != type_op1_to_histories_.end()) {
      it->second.erase(it->second.begin());
      if (it->second.empty()) {
        type_op1_to_histories_.erase(it);
      }
    }
    // Also remove from type_to_histories_ and type_warp_to_histories_
    auto type_it = type_to_histories_.find(old_key.pair_type);
    if (type_it != type_to_histories_.end() && !type_it->second.empty()) {
      // Get warp_id of oldest entry before removing
      uint32_t old_warp_id = type_it->second.front().computed_addresses.warp_id;
      type_it->second.pop_front();
      if (type_it->second.empty()) {
        type_to_histories_.erase(type_it);
      }
      // Remove from tertiary index
      auto warp_it = type_warp_to_histories_.find(old_key.pair_type);
      if (warp_it != type_warp_to_histories_.end()) {
        auto &warp_vec = warp_it->second[old_warp_id];
        if (!warp_vec.empty()) {
          warp_vec.erase(warp_vec.begin());
          if (warp_vec.empty()) {
            warp_it->second.erase(old_warp_id);
          }
        }
        if (warp_it->second.empty()) {
          type_warp_to_histories_.erase(warp_it);
        }
      }
    }
    key_order_.pop_front();
  }
}

std::vector<LoadStoreOperandHistory> LoadStoreOperandHistoryIndex::find_matching(
    LoadStoreOperandPairType pair_type, uint64_t op1_hash) const {
  LoadStoreOperandKey key{pair_type, op1_hash};
  auto it = type_op1_to_histories_.find(key);
  if (it != type_op1_to_histories_.end()) {
    return it->second;
  }
  return {};
}

std::vector<LoadStoreOperandHistory> LoadStoreOperandHistoryIndex::all_entries() const {
  std::vector<LoadStoreOperandHistory> result;
  for (const auto &[key, histories] : type_op1_to_histories_) {
    result.insert(result.end(), histories.begin(), histories.end());
  }
  return result;
}

size_t LoadStoreOperandHistoryIndex::count_by_type(LoadStoreOperandPairType pair_type) const {
  auto it = type_to_histories_.find(pair_type);
  if (it == type_to_histories_.end()) {
    return 0;
  }
  return it->second.size();
}

void LoadStoreOperandHistoryIndex::clear() {
  type_op1_to_histories_.clear();
  type_to_histories_.clear();
  type_warp_to_histories_.clear();
  key_order_.clear();
}

} // namespace fuzzgpu
