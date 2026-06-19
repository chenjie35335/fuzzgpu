#include "isa/riscv/riscv_instr.hh"
#include "isa/riscv/riscv_vector_meta.hh"

#include "reg_dep_tracker.hh"
#include <algorithm>
#include <functional>
#include <queue>
#include <unordered_map>
#include <unordered_set>

namespace fuzzgpu {

void RegDepTracker::propagate_dep(const riscv_instr &instr, uint64_t pc) {
  //
  // Tracking data dependency
  //

  auto this_node = DFGNode{
      .pc = pc,
      .opcode = instr.meta()->category == riscv_instr_category_t::LOAD
                    ? Opcode::Load
                : instr.meta()->category == riscv_instr_category_t::STORE
                    ? Opcode::Store
                : instr.meta()->category == riscv_instr_category_t::AMO
                    ? Opcode::Atomic
                    : Opcode::Others,
#if defined(ARCH_VENTUS)
      // ventus extended vector
      .using_vs2_as_store_data =
          (instr.meta()->instr_name == riscv_instr_name_t::VSW12_V ||
           instr.meta()->instr_name == riscv_instr_name_t::VSH12_V ||
           instr.meta()->instr_name == riscv_instr_name_t::VSB12_V ||
           instr.meta()->instr_name == riscv_instr_name_t::VSW_V ||
           instr.meta()->instr_name == riscv_instr_name_t::VSH_V ||
           instr.meta()->instr_name == riscv_instr_name_t::VSB_V),
#else
      .using_vs2_as_store_data = false,
#endif
  };

  // taint propagation from source operands
  if (instr.meta()->operand_mask & riscv_meta::HAS_RS1)
    if (reg_writer.find(instr.rs1()) != reg_writer.end()) {
      dfg_map_[reg_writer.at(instr.rs1()).pc].push_back(DFGEdge{
          .to_node = this_node,
          .to_operand = Operand::RS1,
      });
    }
  if (instr.meta()->operand_mask & riscv_meta::HAS_RS2)
    if (reg_writer.find(instr.rs2()) != reg_writer.end()) {
      dfg_map_[reg_writer.at(instr.rs2()).pc].push_back(DFGEdge{
          .to_node = this_node,
          .to_operand = Operand::RS2,
      });
    }

  if (instr.meta()->operand_mask & riscv_meta::HAS_FS1)
    if (fpr_writer.find(instr.fs1()) != fpr_writer.end()) {
      dfg_map_[fpr_writer.at(instr.fs1()).pc].push_back(DFGEdge{
          .to_node = this_node,
          .to_operand = Operand::FS1,
      });
    }
  if (instr.meta()->operand_mask & riscv_meta::HAS_FS2)
    if (fpr_writer.find(instr.fs2()) != fpr_writer.end()) {
      dfg_map_[fpr_writer.at(instr.fs2()).pc].push_back(DFGEdge{
          .to_node = this_node,
          .to_operand = Operand::FS2,
      });
    }
  if (instr.meta()->operand_mask & riscv_meta::HAS_FS3)
    if (fpr_writer.find(instr.fs3()) != fpr_writer.end()) {
      dfg_map_[fpr_writer.at(instr.fs3()).pc].push_back(DFGEdge{
          .to_node = this_node,
          .to_operand = Operand::FS3,
      });
    }

  if (instr.meta()->operand_mask & riscv_meta::HAS_VS1)
    if (vreg_writer.find(instr.vs1()) != vreg_writer.end()) {
      dfg_map_[vreg_writer.at(instr.vs1()).pc].push_back(DFGEdge{
          .to_node = this_node,
          .to_operand = Operand::VS1,
      });
    }
  if (instr.meta()->operand_mask & riscv_meta::HAS_VS2)
    if (vreg_writer.find(instr.vs2()) != vreg_writer.end()) {
      dfg_map_[vreg_writer.at(instr.vs2()).pc].push_back(DFGEdge{
          .to_node = this_node,
          .to_operand = Operand::VS2,
      });
    }
  if (instr.meta()->operand_mask & riscv_meta::HAS_VS3) {
    riscv_vector_meta::foreach_vector_nfield_(instr, [&](unsigned reg_offset) {
      const auto vreg = static_cast<riscv_vreg_t>(
          static_cast<unsigned>(instr.vs3()) + reg_offset);
      if (vreg_writer.find(vreg) != vreg_writer.end()) {
        dfg_map_[vreg_writer.at(vreg).pc].push_back(DFGEdge{
            .to_node = this_node,
            .to_operand = Operand::VS3,
        });
      }
      return true;
    });
  }
  if ((instr.meta()->operand_mask & riscv_meta::HAS_VD) &&
      (!instr.meta()->read_vd())) {
    riscv_vector_meta::foreach_vector_nfield_(instr, [&](unsigned reg_offset) {
      const auto vreg = static_cast<riscv_vreg_t>(
          static_cast<unsigned>(instr.vd()) + reg_offset);
      if (vreg_writer.find(vreg) != vreg_writer.end()) {
        dfg_map_[vreg_writer.at(vreg).pc].push_back(DFGEdge{
            .to_node = this_node,
            .to_operand = Operand::VD,
        });
      }
      return true;
    });
  }
#if 0 // ctrl dep
  if (instr.meta()->operand_mask & riscv_meta::HAS_VM &&
      (!instr.vm() || instr.meta()->vm_hardcoded_0())) {
    if (vreg_writer.find(riscv_vreg_t::V0) != vreg_writer.end())
        dfg_map_[vreg_writer.at(riscv_vreg_t::V0).pc].push_back(this_node); // vm mask reg
  }
#endif
  if (instr.meta()->category == riscv_instr_category_t::CSR) {
    ; // whether to propagate CSR data dep is TBD
  }

  // output
  if (instr.meta()->operand_mask & riscv_meta::HAS_RD)
    reg_writer[instr.rd()] = this_node;
  if (instr.meta()->operand_mask & riscv_meta::HAS_FD)
    fpr_writer[instr.fd()] = this_node;
  if (instr.meta()->operand_mask & riscv_meta::HAS_VD) {
    riscv_vector_meta::foreach_vector_nfield_(instr, [&](unsigned reg_offset) {
      const auto vreg = static_cast<riscv_vreg_t>(
          static_cast<unsigned>(instr.vd()) + reg_offset);
      vreg_writer[vreg] = this_node;
      return true;
    });
  }
  pc_opcode_map_[pc] = this_node.opcode;
}

bool RegDepTracker::is_to_store_data_(const DFGEdge &e) {
  if (e.to_node.opcode != Opcode::Store && e.to_node.opcode != Opcode::Atomic)
    return false;
  assert(e.to_operand != Operand::VD && e.to_operand != Operand::RD);
  if (e.to_node.using_vs2_as_store_data) {
    return (e.to_operand == Operand::VS2);
  } else {
    return (e.to_operand == Operand::VS3) || (e.to_operand == Operand::RS2);
  }
}

bool RegDepTracker::is_to_ldst_addr_(const DFGEdge &e) {
  if (e.to_node.opcode == Opcode::Others)
    return false;
  return !is_to_store_data_(e);
}

const std::unordered_map<uint64_t, std::vector<RegDepTracker::DFGEdge>> &
RegDepTracker::build_data_dep_cache_() {
  if (!data_dep_cache_.empty())
    return data_dep_cache_;
  for (const auto &[src_pc, edges] : dfg_map_) {
    for (const auto &e : edges) {
      if (is_to_ldst_addr_(e)) {
        continue;
      } else {
        data_dep_cache_[src_pc].push_back(e);
      }
    }
  }
  return data_dep_cache_;
}

const std::unordered_map<uint64_t, std::vector<RegDepTracker::DFGEdge>> &
RegDepTracker::build_addr_dep_cache_() {
  if (!addr_dep_cache_.empty())
    return addr_dep_cache_;
  for (const auto &[src_pc, edges] : dfg_map_) {
    for (const auto &e : edges) {
      if (is_to_store_data_(e)) {
        continue;
      } else {
        // G = (data / to_store_data) \cup to_addr
        addr_dep_cache_[src_pc].push_back(e);
      }
    }
  }
  return addr_dep_cache_;
}

/**
 * call this if the DFG graph is changed
 */
void RegDepTracker::clear_dep_caches() {
  data_dep_cache_.clear();
  addr_dep_cache_.clear();
}

/**
 * @return (pc -> pc)
 */
std::unordered_map<uint64_t, std::vector<uint64_t>>
RegDepTracker::condense_ldst(
    const std::unordered_map<uint64_t, std::vector<DFGEdge>> &G) {
  /*
  Given a DAG G=(V, E), each node has a type (Load-STORE-ATOMIC or Others)
  derived from the opcode. We need to construct a new graph H, whose node set
  consists of all nodes of type LOAD. STORE. ATOMIC. For any two nodes u and v
  in H, if and only if there exists a path from u to v in G, and all nodes on
  the path are of type Others (excluding u and v), there is an edge from u to v
  in H.
  */
  std::unordered_map<uint64_t, std::vector<uint64_t>> H;
  std::unordered_map<uint64_t, std::vector<uint64_t>> rev_G;

  // Build reverse graph
  for (const auto &[u, edges] : G) {
    for (const auto &edge : edges) {
      uint64_t v = edge.to_node.pc;
      rev_G[v].push_back(u);
    }
  }

  // Memoization DP: PC (OTHERS) -> List of reaching LSA PCs
  std::unordered_map<uint64_t, std::vector<uint64_t>> memo;
  std::unordered_set<uint64_t> visited;

  // Helper lambda for DFS with memoization
  std::function<const std::vector<uint64_t> &(uint64_t)> get_sources =
      [&](uint64_t u) -> const std::vector<uint64_t> & {
    if (visited.count(u))
      return memo[u];

    std::vector<uint64_t> sources;
    if (rev_G.find(u) != rev_G.end()) {
      for (uint64_t pred : rev_G.at(u)) {
        Opcode pred_opcode = Opcode::Others;
        auto it = pc_opcode_map_.find(pred);
        if (it != pc_opcode_map_.end())
          pred_opcode = it->second;

        if (pred_opcode != Opcode::Others) {
          sources.push_back(pred);
        } else {
          const auto &sub_sources = get_sources(pred);
          sources.insert(sources.end(), sub_sources.begin(), sub_sources.end());
        }
      }
    }
    // Deduplicate
    std::sort(sources.begin(), sources.end());
    sources.erase(std::unique(sources.begin(), sources.end()), sources.end());

    visited.insert(u);
    memo[u] = std::move(sources);
    return memo[u];
  };

  // Iterate over all nodes in rev_G to find LSA targets
  for (const auto &[v, preds] : rev_G) {
    Opcode v_opcode = Opcode::Others;
    auto it = pc_opcode_map_.find(v);
    if (it != pc_opcode_map_.end())
      v_opcode = it->second;

    if (v_opcode != Opcode::Others) {
      // v is an LSA target
      std::unordered_set<uint64_t> v_sources;
      for (uint64_t pred : preds) {
        Opcode pred_opcode = Opcode::Others;
        auto it_p = pc_opcode_map_.find(pred);
        if (it_p != pc_opcode_map_.end())
          pred_opcode = it_p->second;

        if (pred_opcode != Opcode::Others) {
          v_sources.insert(pred);
        } else {
          const auto &sub = get_sources(pred);
          v_sources.insert(sub.begin(), sub.end());
        }
      }

      // Populate H (Source -> Targets)
      for (uint64_t s : v_sources) {
        H[s].push_back(v);
      }
    }
  }
  return H;
}

std::unordered_map<uint64_t, std::vector<uint64_t>>
RegDepTracker::build_ldst_data_dep() {
  return condense_ldst(build_data_dep_cache_());
}
std::unordered_map<uint64_t, std::vector<uint64_t>>
RegDepTracker::build_ldst_addr_dep() {
  return condense_ldst(build_addr_dep_cache_());
}

std::vector<size_t> RegDepTracker::count_dep_chain_lengths() {
  std::vector<size_t> chain_lengths;
  const auto &data_dep = build_data_dep_cache_();

  std::unordered_set<uint64_t> all_nodes;
  std::unordered_map<uint64_t, std::vector<uint64_t>> adj;
  std::unordered_map<uint64_t, std::vector<uint64_t>> undirected_adj;

  for (const auto &[src, edges] : data_dep) {
    all_nodes.insert(src);
    for (const auto &edge : edges) {
      uint64_t dst = edge.to_node.pc;
      all_nodes.insert(dst);
      adj[src].push_back(dst);
      undirected_adj[src].push_back(dst);
      undirected_adj[dst].push_back(src);
    }
  }

  std::unordered_map<uint64_t, size_t> memo;
  std::function<size_t(uint64_t)> get_longest_path =
      [&](uint64_t u) -> size_t {
    if (memo.count(u))
      return memo[u];
    size_t max_len = 0;
    if (adj.count(u)) {
      for (uint64_t v : adj.at(u)) {
        max_len = std::max(max_len, get_longest_path(v));
      }
    }
    return memo[u] = 1 + max_len;
  };

  std::unordered_set<uint64_t> visited;
  for (uint64_t node : all_nodes) {
    if (visited.count(node))
      continue;

    size_t component_max_len = 0;
    std::queue<uint64_t> q;
    q.push(node);
    visited.insert(node);

    while (!q.empty()) {
      uint64_t curr = q.front();
      q.pop();

      component_max_len = std::max(component_max_len, get_longest_path(curr));

      if (undirected_adj.count(curr)) {
        for (uint64_t neighbor : undirected_adj.at(curr)) {
          if (!visited.count(neighbor)) {
            visited.insert(neighbor);
            q.push(neighbor);
          }
        }
      }
    }
    chain_lengths.push_back(component_max_len);
  }

  return chain_lengths;
}

} // namespace fuzzgpu