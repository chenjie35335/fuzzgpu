#pragma once

#include <memory>
#include <unordered_map>
#include <optional>
#include <cassert>

namespace fuzzgpu {

template <typename Telm>
/// @brief Sparse fixed-depth radix tree keyed by vectors of unsigned indices.
class RadixTree {
  /// @brief Internal node storing a payload and sparse children.
  struct Node {
    ///< Payload stored at this prefix.
    Telm payload;
    ///< Child nodes keyed by the next radix component.
    std::unordered_map<unsigned, std::shared_ptr<Node>> children;
  };
  ///< Root node for the empty key.
  std::shared_ptr<Node> root_;
  ///< Maximum allowed key depth.
  size_t max_depth_;

public:
  /// @brief Construct a tree with the given maximum depth.
  RadixTree(size_t depth), root_(std::make_shared<Node>()), max_depth_(depth) {}

  /// @brief Key type used to address tree nodes.
  using Id = std::vector<unsigned>;
  /// @brief Return the payload at @p id without creating missing nodes.
  std::optional<Telm &> at(const Id &id) const {
    assert(id.size() <= max_depth_);
    std::shared_ptr<Node> cur_node = root_;
    for (size_t i = 0; i < id.size(); ++i) {
      const auto it = cur_node->children.find(id[i]);
      if (it == cur_node->children.end()) {
        return std::nullopt;
      }
      cur_node = it->second;
    }
    return cur_node->payload;
  }

  /// @brief Return the payload at @p id, creating missing nodes.
  Telm &operator[](const Id &id) {
    assert(id.size() <= max_depth_);
    std::shared_ptr<Node> cur_node = root_;
    for (size_t i = 0; i < id.size(); ++i) {
      auto &child_node = cur_node->children[id[i]];
      if (!child_node) {
        child_node = std::make_shared<Node>();
      }
      cur_node = child_node;
    }
    return cur_node->payload;
  }
};

} // namespace fuzzgpu
