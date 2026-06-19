#ifndef BUG_FILTER_HH_
#define BUG_FILTER_HH_

#include <memory>
#include <unordered_map>

#include "gen_warp.hh"
#include "isa/riscv/riscv_instr.hh"

#include "nlohmann/json.hpp"

namespace fuzzgpu {

/// @brief Singleton filter for known architecture or simulator bugs.
class BugFilter {
  ///< Bug-name to filtered/enabled decision map.
  std::unordered_map<std::string, bool> bug_map_;
  ///< Global off switch for filtering.
  bool disabled_{false};
  ///< Whether unknown bugs are filtered by default.
  bool default_filtering_{true};

public:
  /// @brief Return the global bug filter instance.
  static BugFilter &instance() {
    static std::unique_ptr<BugFilter> instance;
    if (!instance) {
      instance = std::make_unique<BugFilter>();
    }
    return *instance.get();
  }

  /// @brief Disable all bug filtering.
  void disable() { disabled_ = true; }

  /// @brief Allow unknown bugs unless explicitly filtered.
  void disable_default_filtering() { default_filtering_ = false; }

  /// @brief Return whether a named bug should be filtered.
  bool filter(const std::string &bug_name);

  /// @brief Return whether @p instr should be filtered in @p gen_warp.
  bool filter(GenWarp *gen_warp, const riscv_instr &instr);

  /// @brief Register one bug filter decision.
  void register_bug(const std::string &bug_name, bool filtered);

  /// @brief Register bug filter decisions from JSON configuration.
  void register_bugs(const nlohmann::json &bug_filter_list);

  /// @brief Return all registered bug filter decisions.
  const std::unordered_map<std::string, bool> &get_bug_map() const {
    return bug_map_;
  }
};

} // namespace fuzzgpu

#endif
