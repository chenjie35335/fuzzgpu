#include <algorithm>
#include <chrono>
#include <functional>
#include <unordered_set>

#include "config.hh"
#include "difftest.hh"

#include "mem_axioms_checker.hh"

namespace difftest {

enum class MCRelation {
  Invalid = 0,
  PO,             // program order
  PO_LOC,         // po per location
  PPO,            // preserved program order
  CO,             // coherence order
  COE,            // co external
  RFI,            // rf internal
  RFE,            // rf external
  FR,             // from-read
  MORALLY_STRONG, // morally strong relation
};

inline const char *mc_relation_to_string(MCRelation rela) {
  switch (rela) {
  case MCRelation::PO:
    return "po";
  case MCRelation::PO_LOC:
    return "po-loc";
  case MCRelation::PPO:
    return "ppo";
  case MCRelation::CO:
    return "co";
  case MCRelation::COE:
    return "coe";
  case MCRelation::RFI:
    return "rf";
  case MCRelation::RFE:
    return "rfe";
  case MCRelation::FR:
    return "fr";
  case MCRelation::MORALLY_STRONG:
    return "morally_strong";
  default:
    return "unknown";
  }
}

MemAxiomsChecker::MemAxiomsChecker(Difftest &dft)
  : dft_(dft) {}

void MemAxiomsChecker::dcache_shmem_rsp_event(unsigned sm_id, WarpGID warp_gid,
                                              ThreadLID thread_lid, uint64_t pc,
                                              size_t size, DCacheOpcode memop,
                                              uint64_t paddr, uint64_t rdat) {

  if (!inited_)
    return;

  spdlog::info("l1 rsp: sm_id {} warp_gid {} thread_lid {} pc {:#x} size {} "
               "memop {} paddr {:#x} rdat {:#x}",
               sm_id, warp_gid, thread_lid, pc, size, static_cast<int>(memop),
               paddr, rdat);

  const auto to = LoadStoreId{.thread_gid =
                                  GlobalThreadId{
                                      .warp_gid = warp_gid,
                                      .thread_lid = thread_lid,
                                  },
                              .pc = pc};

  // Track executed operations by LoadStoreId
  if (memop == DCacheOpcode::Load) {
    executed_loads_.insert(to);
  } else if (memop == DCacheOpcode::Store) {
    executed_stores_.insert(to);
  } else if (memop == DCacheOpcode::Atomic) {
    executed_atomics_.insert(to);
  }

  Difftest::Status ret;

  // **** resolve rf, rfe, rfi ****
  for (unsigned ibyte = 0; ibyte < size; ++ibyte) {
    const auto byte_paddr = paddr + ibyte;
    const auto byte_val = (rdat >> (ibyte * 8)) & 0xFF;

    if (memop == DCacheOpcode::Load || memop == DCacheOpcode::Atomic) {
      if (pending_store_id_.find(byte_paddr) != pending_store_id_.end()) {
        spdlog::warn("Pending store id for paddr {:#x} pc {:#x}", byte_paddr,
                     pc);
      }
      auto entries = store_id_.find(byte_paddr);
      if (entries != store_id_.end()) {
        auto st_id = entries->second.find(byte_val);
        if (st_id != entries->second.end()) {
          const auto &from = st_id->second;
          rf_order_[from].push_back(to);
          spdlog::trace("from store id: paddr {:#x} byte_val {:#x} warp GID {} "
                        "thread LID {} initial_write {}"
                        "from pc {:#x} to pc {:#x} warp GID {} thread LID {}",
                        byte_paddr, byte_val, from.thread_gid.warp_gid,
                        from.thread_gid.thread_lid, from.initial_write, from.pc,
                        to.pc, to.thread_gid.warp_gid,
                        to.thread_gid.thread_lid);
          if (from.thread_gid != to.thread_gid && !from.initial_write) {
            rfe_order_[from].push_back(to);
          } else {
            rfi_order_[from].push_back(to);
          }
        } else {
          ret &= dft_.check(
              false,
              "Load at PC {:#x} could not find read-from (rf) store for "
              "paddr {:#x} byte {} value {:#x}",
              pc, paddr + ibyte, ibyte, byte_val);
          load_value_axiom_failed_loads.push_back(to);
        }

      } else {
        ret &= dft_.check(
            false,
            "Load at PC {:#x} could not find read-from (rf) store for "
            "paddr {:#x}",
            pc, paddr + ibyte);
        load_value_axiom_failed_loads.push_back(to);
      }
    }

    if (memop == DCacheOpcode::Store || memop == DCacheOpcode::Atomic) {
      // check stored values
      if (store_trace_.stores.find(to) != store_trace_.stores.end()) {
        const auto &st_trace = store_trace_.stores.at(to);
        ret &=
            dft_.check(st_trace.bytes.find(byte_paddr) != st_trace.bytes.end(),
                       "Store at PC {:#x} missing byte {:#x} in store trace",
                       pc, byte_paddr);
        if (ret) {
          ret &= dft_.check(
              st_trace.bytes.at(byte_paddr) == byte_val,
              "Store at PC {:#x} byte {:#x} value mismatch: expected "
              "{:#x}, got {:#x}",
              pc, byte_paddr, st_trace.bytes.at(byte_paddr), byte_val);
        }
      }

      // **** resolve co ****
      add_co_(sm_id, paddr + ibyte, to);
    }
  }

  if (!ret) {
    dft_.report(ret, "DCache/Shared Mem Response", warp_gid, pc);
  }
}

void MemAxiomsChecker::l2_dcache_req_event(uint64_t paddr,
                                           size_t cacheline_size,
                                           DCacheOpcode memop,
                                           const std::vector<uint8_t> &data,
                                           const std::vector<uint8_t> &mask) {
  Difftest::Status ret;

  // This mirrors the co tracking logic in dcache_shmem_rsp_event, for coherent
  // L2 caches

  for (size_t ibyte = 0; ibyte < cacheline_size && ibyte < data.size();
       ++ibyte) {
    if (ibyte < mask.size() && mask[ibyte] == 0)
      continue;

    const auto byte_paddr = paddr + ibyte;
    if (pending_store_id_.find(byte_paddr) != pending_store_id_.end())
      continue;
    const auto byte_val = data[ibyte];

    if (memop == DCacheOpcode::Store || memop == DCacheOpcode::Atomic) {
      // Look up the LoadStoreId from store_id_ using (paddr, byte_val)
      auto entries = store_id_.find(byte_paddr);
      if (entries != store_id_.end()) {
        auto st_id_it = entries->second.find(byte_val);
        if (st_id_it != entries->second.end()) {
          const auto &store_id = st_id_it->second;
          // Update coherence order for this byte
          add_co_(GLOBAL_SCOPE_ID, byte_paddr, store_id);

          // Track executed operations by LoadStoreId
          if (memop == DCacheOpcode::Store) {
            executed_stores_.insert(store_id);
          } else if (memop == DCacheOpcode::Atomic) {
            executed_atomics_.insert(store_id);
          }
        } else {
          ret &= dft_.check(
              false,
              "L2 DCache Req Event: No store_id entry for paddr {:#x} byte "
              "value {:#x}",
              byte_paddr, byte_val);
        }
      } else {
        ret &= dft_.check(
            false, "L2 DCache Req Event: No store_id entry for paddr {:#x}",
            byte_paddr);
      }
    }
  }

  if (!ret) {
    dft_.report(ret, "DCache/Shared Mem Response", std::nullopt, std::nullopt);
  }
}

void MemAxiomsChecker::add_co_(unsigned sm_id, uint64_t paddr,
                               const LoadStoreId &to) {
  spdlog::info(
      "add co edge: sm_id {} paddr {:#x} pc {:#x} warp GID {} thread LID {}",
      sm_id, paddr, to.pc, to.thread_gid.warp_gid, to.thread_gid.thread_lid);

  auto &sm_per_addr = co_per_paddr[sm_id];
  if (sm_per_addr.find(paddr) != sm_per_addr.end()) {
    const auto &from = sm_per_addr.at(paddr);
    if (from != to) {
      co_order_[from].push_back(to);
      // **** resolve coe ****
      if (from.thread_gid != to.thread_gid && !from.initial_write) {
        coe_order_[from].push_back(to);
      }
    }
  }
  sm_per_addr[paddr] = to;
}

void MemAxiomsChecker::build_ppo_() {
  // Precompute reverse dependencies
  std::unordered_map<uint64_t, std::vector<uint64_t>> addr_dep_rev;
  std::unordered_map<uint64_t, std::vector<uint64_t>> data_dep_rev;
  for (const auto &[from, tos] : addr_dep_) {
    for (auto to : tos)
      addr_dep_rev[to].push_back(from);
  }
  for (const auto &[from, tos] : data_dep_) {
    for (auto to : tos)
      data_dep_rev[to].push_back(from);
  }

  const auto has_flag = [](FenceFlags flags, FenceFlags flag) {
    return (std::underlying_type<decltype(flags)>::type)flags &
           (std::underlying_type<decltype(flag)>::type)flag;
  };

  // Helper to get opcode
  auto get_op = [&](const LoadStoreId &id) -> LoadStoreOpcode {
    if (ldst_opcode_.find(id) != ldst_opcode_.end())
      return ldst_opcode_.at(id);
    return {};
  };

  // Helper to add edge with rule id annotation
  auto add_edge = [&](LoadStoreId u, LoadStoreId v, unsigned rule) {
    u.ppo_rule = rule;
    v.ppo_rule = rule;
    ppo_[u].push_back(v);
  };

  // Helper to create virtual node with globally unique virtual_id
  static uint64_t next_virtual_id = 0; // Global counter for virtual node IDs
  auto new_virt = [&](const LoadStoreId &base, unsigned rule) {
    LoadStoreId v = base;
    v.virtual_id = ++next_virtual_id; // Globally unique virtual_id
    v.ppo_rule = rule;
    return v;
  };

  struct Chain {
    std::optional<LoadStoreId> last_v = std::nullopt;
    std::vector<LoadStoreId> pending;

    void add(const LoadStoreId &id) { pending.push_back(id); }

    // Returns the node representing the history.
    // If new node created, updates last_v.
    // Returns std::nullopt if no history.
    std::optional<LoadStoreId> get_checkpoint(
        const LoadStoreId &base_id, unsigned rule,
        std::function<LoadStoreId(const LoadStoreId &, unsigned)> new_virt,
        std::function<void(const LoadStoreId &, const LoadStoreId &, unsigned)>
            add_edge) {
      if (pending.empty()) {
        return last_v;
      }
      auto v = new_virt(base_id, rule);
      for (const auto &p : pending)
        add_edge(p, v, rule);
      if (last_v)
        add_edge(*last_v, v, rule);
      last_v = v;
      pending.clear();
      return v;
    }
  };

  // Build rf_inv for r2
  std::unordered_map<LoadStoreId, LoadStoreId, LoadStoreIdHash> rf_inv;
  for (const auto &[src, dsts] : rf_order_) {
    for (const auto &dst : dsts)
      rf_inv[dst] = src;
  }

  // r1: [M];po-loc;[W]
  // r2: ([R];po-loc-no-w;[R]) \ rsw
  for (const auto &[paddr, po] : po_loc_) {
    for (const auto &[tid, trace] : po.traces()) {
      // r1
      std::vector<LoadStoreId> pending_m;
      for (const auto &id : trace) {
        auto op = get_op(id);
        bool is_w = (op.opcode == DCacheOpcode::Store ||
                     op.opcode == DCacheOpcode::Atomic);
        bool is_m = (op.opcode == DCacheOpcode::Load ||
                     op.opcode == DCacheOpcode::Store ||
                     op.opcode == DCacheOpcode::Atomic);

        if (is_w) {
          for (const auto &m : pending_m)
            add_edge(m, id, 1);
          pending_m.clear();
        }
        if (is_m)
          pending_m.push_back(id);
      }

      /*
       * r2: ([R];po-loc-no-w;[R]) \ rsw
       *
       * (r₁, r₂) ∈ r2 iff:
       *   - r₁ and r₂ are same-thread reads to the same address.
       *   - r₁ precedes r₂ in program order, and there is no intervening write between them.
       *   - r₁ and r₂ observe different writes.
       */
      // Identify groups of reads between writes
      std::vector<LoadStoreId> group_a, group_b;
      std::vector<LoadStoreId> *last_group = &group_a;
      std::vector<LoadStoreId> *current_group = &group_b;
      std::optional<LoadStoreId> current_rf = std::nullopt;
      bool in_group = false;

      auto process_groups = [&](const std::vector<LoadStoreId> &prev,
                                const std::vector<LoadStoreId> &curr) {
        auto v = new_virt(prev.back(), /*r*/ 2); // Arbitrary base, rule r2
        for (const auto &r : prev)
          add_edge(r, v, /*r*/ 2);
        for (const auto &r : curr)
          add_edge(v, r, /*r*/ 2);
      };

      for (const auto &id : trace) {
        auto op = get_op(id);
        bool is_w = (op.opcode == DCacheOpcode::Store ||
                     op.opcode == DCacheOpcode::Atomic);
        bool is_r = (op.opcode == DCacheOpcode::Load ||
                     op.opcode == DCacheOpcode::Atomic);

        if (is_w) {
          if (!current_group->empty()) {
            if (!last_group->empty()) {
              process_groups(*last_group, *current_group);
            }
            current_group->clear();
          }
          last_group->clear();
          in_group = false;
        } else if (is_r) {
          LoadStoreId rf = rf_inv.count(id) ? rf_inv.at(id) : LoadStoreId{};
          if (!in_group) {
            current_group->push_back(id);
            current_rf = rf;
            in_group = true;
          } else {
            if (current_rf && rf == *current_rf) {
              current_group->push_back(id);
            } else {
              if (!last_group->empty()) {
                process_groups(*last_group, *current_group);
              }
              std::swap(last_group, current_group);
              current_group->clear();
              current_group->push_back(id);
              current_rf = rf;
            }
          }
        }
      }
      // Flush end
      if (!current_group->empty()) {
        if (!last_group->empty()) {
          process_groups(*last_group, *current_group);
        }
      }
    }
  }

  // r3: [AMO|X];rfi;[R]
  for (const auto &[src, dsts] : rfi_order_) {
    auto op = get_op(src);
    // Check if AMO or X (Atomic)
    // Assuming Atomic opcode covers AMO/X
    if (op.opcode == DCacheOpcode::Atomic) {
      for (const auto &dst : dsts) {
        add_edge(src, dst, /*r*/ 3);
      }
    }
  }

  // Main loop per thread for r4-r13
  for (const auto &[tid, trace] : po_.traces()) {
    std::unordered_map<uint64_t, LoadStoreId> last_inst_at_pc;

    Chain chain_R, chain_W, chain_AQ, chain_RCsc, chain_AddrM;
    Chain chain_Eff_R, chain_Eff_W;

    for (const auto &id : trace) {
      auto op = get_op(id);
      bool is_fence = (op.opcode == DCacheOpcode::Fence);
      bool is_atomic = (op.opcode == DCacheOpcode::Atomic);
      bool is_load = (op.opcode == DCacheOpcode::Load);
      bool is_store = (op.opcode == DCacheOpcode::Store);

      bool is_r = is_load || is_atomic;
      bool is_w = is_store || is_atomic;
      bool is_m = is_r || is_w; // Not fence

      bool is_aq = op.aq;
      bool is_rl = op.rl;
      bool is_rcsc = (op.aq && op.rl && is_atomic);

      // Update last_inst_at_pc, which is used for r10, r11, r12, r13
      // As the inital_write doesn't have any addr/data/ctrl dep, we only use
      // PC to identify it
      last_inst_at_pc[id.pc] = id;

      if (is_m) {
        // Add to chains
        if (is_r)
          chain_R.add(id);
        if (is_w)
          chain_W.add(id);

        // r5: [AQ];po;[M]
        // Consume chain_AQ
        auto v_aq = chain_AQ.get_checkpoint(id, /*r*/ 5, new_virt, add_edge);
        if (v_aq)
          add_edge(*v_aq, id, /*r*/ 5);

        if (is_aq)
          chain_AQ.add(id);

        // r7: [RCsc];po;[RCsc]
        if (is_rcsc) {
          auto v_rcsc =
              chain_RCsc.get_checkpoint(id, /*r*/ 7, new_virt, add_edge);
          if (v_rcsc)
            add_edge(*v_rcsc, id, /*r*/ 7);
          chain_RCsc.add(id);
        }

        // r6: [M];po;[RL]
        if (is_rl) {
          auto v_r = chain_R.get_checkpoint(id, /*r*/ 6, new_virt, add_edge);
          if (v_r)
            add_edge(*v_r, id, /*r*/ 6);
          auto v_w = chain_W.get_checkpoint(id, /*r*/ 6, new_virt, add_edge);
          if (v_w)
            add_edge(*v_w, id, 6);
        }

        // Apply Fence Effects
        if (is_r) {
          auto v_eff =
              chain_Eff_R.get_checkpoint(id, /*r*/ 4, new_virt, add_edge);
          if (v_eff)
            add_edge(*v_eff, id, /*r*/ 4);
        }
        if (is_w) {
          auto v_eff =
              chain_Eff_W.get_checkpoint(id, /*r*/ 4, new_virt, add_edge);
          if (v_eff)
            add_edge(*v_eff, id, /*r*/ 4);
        }

        // r9: [M];addr;[M] (Syntactic address dependency)
        // r13: [M];addr;[M];po;[W] (address dependency followed by po to write)
        bool is_addr_m = false;
        if (addr_dep_rev.count(id.pc)) {
          for (auto src_pc : addr_dep_rev.at(id.pc)) {
            if (last_inst_at_pc.count(src_pc)) {
              auto src_id = last_inst_at_pc.at(src_pc);
              assert(!src_id.initial_write);
              auto src_op = get_op(src_id);
              if (src_op.opcode != DCacheOpcode::Fence) {
                // r9: [M];addr;[M] - direct address dependency edge
                add_edge(src_id, id, /*r*/ 9);
                is_addr_m = true;
              }
            }
          }
        }

        if (is_w) {
          auto v_addr =
              chain_AddrM.get_checkpoint(id, /*r*/ 13, new_virt, add_edge);
          if (v_addr) {
            add_edge(*v_addr, id, /*r*/ 13);
          }
        }

        if (is_addr_m) {
          chain_AddrM.add(id);
        }

        // r10: [M];data;[W]
        if (is_w && data_dep_rev.count(id.pc)) {
          for (auto src_pc : data_dep_rev.at(id.pc)) {
            if (last_inst_at_pc.count(src_pc)) {
              auto src_id = last_inst_at_pc.at(src_pc);
              assert(!src_id.initial_write);
              auto src_op = get_op(src_id);
              if (src_op.opcode != DCacheOpcode::Fence) {
                add_edge(src_id, id, /*r*/ 10);
                // r12 (data variant): [M];data;[W];rfi;[R]
                if (rfi_order_.count(id)) {
                  for (const auto &r_id : rfi_order_.at(id)) {
                    add_edge(src_id, r_id, /*r*/ 12);
                  }
                }
              }
            }
          }
        }

        // r12 (addr variant): [M];addr;[W];rfi;[R]
        if (is_w && addr_dep_rev.count(id.pc)) {
          for (auto src_pc : addr_dep_rev.at(id.pc)) {
            if (last_inst_at_pc.count(src_pc)) {
              auto src_id = last_inst_at_pc.at(src_pc);
              assert(!src_id.initial_write);
              auto src_op = get_op(src_id);
              if (src_op.opcode != DCacheOpcode::Fence) {
                if (rfi_order_.count(id)) {
                  for (const auto &r_id : rfi_order_.at(id)) {
                    add_edge(src_id, r_id, /*r*/ 12);
                  }
                }
              }
            }
          }
        }

      } else if (is_fence) {
        // r4: fence
        FenceFlags prec = op.prec;
        FenceFlags succ = op.succ;

        // Predecessors -> Fence
        // If prec=R, consume chain_R
        if (has_flag(prec, FenceFlags::R)) {
          auto v = chain_R.get_checkpoint(id, /*r*/ 4, new_virt, add_edge);
          if (v)
            add_edge(*v, id, /*r*/ 4);
        }
        if (has_flag(prec, FenceFlags::W)) {
          auto v = chain_W.get_checkpoint(id, /*r*/ 4, new_virt, add_edge);
          if (v)
            add_edge(*v, id, /*r*/ 4);
        }

        // TSO: [W];f;[W] | [R];f;[M]
        if (has_flag(prec, FenceFlags::TSO)) {
          auto v_w = chain_W.get_checkpoint(id, /*r*/ 4, new_virt, add_edge);
          if (v_w)
            add_edge(*v_w, id, /*r*/ 4);
          auto v_r = chain_R.get_checkpoint(id, /*r*/ 4, new_virt, add_edge);
          if (v_r)
            add_edge(*v_r, id, /*r*/ 4);
        }

        // Fence -> Successors (Add to Eff chains)
        if (has_flag(succ, FenceFlags::R))
          chain_Eff_R.add(id);
        if (has_flag(succ, FenceFlags::W))
          chain_Eff_W.add(id);

        if (has_flag(prec, FenceFlags::TSO)) {
          // f;[W]
          chain_Eff_W.add(id);
          // f;[M] -> f;[R] and f;[W]
          chain_Eff_R.add(id);
          // chain_Eff_W already added
        }
      }
    }
  }
}

void MemAxiomsChecker::build_fr_() {
  // fr = rf^-1 ; co
  // (r, w) in fr if exists w' s.t. (w', r) in rf and (w', w) in co
  for (const auto &[w_prime, rs] : rf_order_) {
    if (co_order_.find(w_prime) != co_order_.end()) {
      const auto &ws = co_order_.at(w_prime);
      for (const auto &r : rs) {
        for (const auto &w : ws) {
          fr_order_[r].push_back(w);
        }
      }
    }
  }
}

struct CycleEdge {
  LoadStoreId from;
  LoadStoreId to;
  MCRelation rela; // relation label on the edge
};

// Helper to dump cycle with per-edge relation labels
struct RevEdge {
  LoadStoreId src;
  MCRelation rela;
};
using RevAdjMap =
    std::unordered_map<LoadStoreId, std::vector<RevEdge>, LoadStoreIdHash>;

static RevAdjMap build_rev_adj(
    const std::vector<
        std::pair<const std::unordered_map<
                      LoadStoreId, std::vector<LoadStoreId>, LoadStoreIdHash> *,
                  MCRelation>> &relations) {
  RevAdjMap rev_adj;
  for (const auto &[rel_map, rela] : relations) {
    if (!rel_map)
      continue;
    for (const auto &[u, vs] : *rel_map) {
      for (const auto &v : vs) {
        rev_adj[v].push_back({u, rela});
      }
    }
  }
  return rev_adj;
}

// Helper to dump cycle with per-edge relation labels
static std::string dump_cycle(const std::vector<CycleEdge> &cycle,
                              const RevAdjMap &rev_adj) {
  std::stringstream ss;
  ss << "Cycle:\n";
  for (const auto &e : cycle) {
    ss << fmt::format("  {} [{}]-> {}\n", e.from.to_string(),
                      mc_relation_to_string(e.rela), e.to.to_string());
    if (rev_adj.count(e.to)) {
      for (const auto &rev : rev_adj.at(e.to)) {
        if (rev.src == e.from && rev.rela == e.rela)
          continue;
        ss << fmt::format("    <- [{}] {}\n", mc_relation_to_string(rev.rela),
                          rev.src.to_string());
      }
    }
  }
  return ss.str();
}

// Helper to build graph and check cycle
static Difftest::Status check_cycle_helper(
    const std::vector<
        std::pair<const std::unordered_map<
                      LoadStoreId, std::vector<LoadStoreId>, LoadStoreIdHash> *,
                  MCRelation>> &relations,
    Difftest &dft, const std::string &name) {

  struct Edge {
    LoadStoreId dst;
    MCRelation rela;
  };

  std::unordered_map<LoadStoreId, std::vector<Edge>, LoadStoreIdHash> adj;
  RevAdjMap rev_adj;
  for (const auto &[rel, rela_type] : relations) {
    for (const auto &[u, vs] : *rel) {
      for (const auto &v : vs) {
        adj[u].push_back(Edge{v, rela_type});
        rev_adj[v].push_back({u, rela_type});
      }
    }
  }

  std::unordered_set<LoadStoreId, LoadStoreIdHash> visited;
  std::unordered_map<LoadStoreId, size_t, LoadStoreIdHash> stack_pos;
  std::vector<std::pair<LoadStoreId, MCRelation>> rec_stack;
  std::vector<CycleEdge> cycle;

  std::function<bool(const LoadStoreId &, MCRelation)> dfs =
      [&](const LoadStoreId &u, MCRelation incoming) -> bool {
    visited.insert(u);
    rec_stack.push_back({u, incoming});
    stack_pos[u] = rec_stack.size() - 1;

    if (adj.count(u)) {
      for (const auto &edge : adj.at(u)) {
        const auto &v = edge.dst;
        const auto rela = edge.rela;
        if (!visited.count(v)) {
          if (dfs(v, rela))
            return true;
        } else if (stack_pos.count(v)) {
          const auto start = stack_pos.at(v);
          cycle.clear();
          // edges along the stack segment start..end-1
          for (size_t i = start; i + 1 < rec_stack.size(); ++i) {
            const auto &from = rec_stack[i].first;
            const auto &to = rec_stack[i + 1].first;
            const auto edge_rela =
                rec_stack[i + 1].second; // relation on edge to 'to'
            cycle.push_back({from, to, edge_rela});
          }
          // closing edge u -> v with current relation
          const auto &u_node = rec_stack.back().first;
          cycle.push_back({u_node, v, rela});
          return true;
        }
      }
    }

    stack_pos.erase(u);
    rec_stack.pop_back();
    return false;
  };

  for (const auto &[u, edges] : adj) {
    if (!visited.count(u)) {
      if (dfs(u, MCRelation::Invalid)) {
        std::string msg = dump_cycle(cycle, rev_adj);
        return dft.check(false, "{} Violation: {}", name, msg);
      }
    }
  }
  return Difftest::Status();
}

bool MemAxiomsChecker::is_morally_strong_(const LoadStoreId &a,
                                          const LoadStoreId &b) const {
  // 1. in po (same thread_gid)
  if (a.thread_gid == b.thread_gid) {
    return true;
  }

  // 2. Same SM, different threads
  if (get_sm_id(a.thread_gid.warp_gid) == get_sm_id(b.thread_gid.warp_gid)) {
    return true;
  }

  // 3. aq/rl atomic
  auto opc_a = ldst_opcode_.find(a), opc_b = ldst_opcode_.find(b);
  if (opc_a != ldst_opcode_.end() && opc_b != ldst_opcode_.end() &&
      opc_a->second.opcode == DCacheOpcode::Atomic &&
      opc_b->second.opcode == DCacheOpcode::Atomic) {
    if ((opc_a->second.aq || opc_a->second.rl) &&
        (opc_b->second.aq || opc_b->second.rl)) {
      return true;
    }
  }

  return false;
}

Difftest::Status MemAxiomsChecker::check_coherence_() {
  // acyclic((morally_strong ∩ (rf ∪ co ∪ fr)) ∪ po_loc)
  //
  // We compute: for each edge in rf/co/fr, check if it's morally_strong,
  // then union with po_loc and check for cycles.

  std::unordered_map<LoadStoreId, std::vector<LoadStoreId>, LoadStoreIdHash>
      morally_strong_rf;
  std::unordered_map<LoadStoreId, std::vector<LoadStoreId>, LoadStoreIdHash>
      morally_strong_co;
  std::unordered_map<LoadStoreId, std::vector<LoadStoreId>, LoadStoreIdHash>
      morally_strong_fr;

  // Filter rf by morally_strong
  for (const auto &[from, tos] : rf_order_) {
    for (const auto &to : tos) {
      if (is_morally_strong_(from, to)) {
        morally_strong_rf[from].push_back(to);
      }
    }
  }

  // Filter co by morally_strong
  for (const auto &[from, tos] : co_order_) {
    for (const auto &to : tos) {
      if (is_morally_strong_(from, to)) {
        morally_strong_co[from].push_back(to);
      }
    }
  }

  // Filter fr by morally_strong
  for (const auto &[from, tos] : fr_order_) {
    for (const auto &to : tos) {
      if (is_morally_strong_(from, to)) {
        morally_strong_fr[from].push_back(to);
      }
    }
  }

  // Build po_loc_map
  std::unordered_map<LoadStoreId, std::vector<LoadStoreId>, LoadStoreIdHash>
      po_loc_map;
  for (const auto &[paddr, po] : po_loc_) {
    for (const auto &[tid, trace] : po.traces()) {
      for (size_t i = 0; i + 1 < trace.size(); ++i) {
        po_loc_map[trace[i]].push_back(trace[i + 1]);
      }
    }
  }

  std::vector<
      std::pair<const std::unordered_map<LoadStoreId, std::vector<LoadStoreId>,
                                         LoadStoreIdHash> *,
                MCRelation>>
      rels = {{&morally_strong_rf, MCRelation::RFE},
              {&morally_strong_co, MCRelation::CO},
              {&morally_strong_fr, MCRelation::FR},
              {&po_loc_map, MCRelation::PO_LOC}};
  return check_cycle_helper(rels, dft_, "Coherence");
}

Difftest::Status MemAxiomsChecker::check_atomic_() {
  // empty(((morally_strong ∩ fr);(morally_strong ∩ co)) ∩ rmw)
  //
  // For each atomic (rmw) operation u, check if there exists v such that:
  // 1. (u, v) ∈ (morally_strong ∩ fr)
  // 2. (v, u) ∈ (morally_strong ∩ co)

  for (const auto &[u, vs] : fr_order_) {
    // Check if u is an atomic operation
    if (ldst_opcode_.count(u) &&
        ldst_opcode_.at(u).opcode == DCacheOpcode::Atomic) {
      for (const auto &v : vs) {
        // Check (u, v) ∈ morally_strong
        if (!is_morally_strong_(u, v)) {
          continue;
        }
        // Check if (v, u) ∈ co and morally_strong
        if (co_order_.count(v)) {
          const auto &co_succs = co_order_.at(v);
          for (const auto &succ : co_succs) {
            if (succ == u && is_morally_strong_(v, u)) {
              return dft_.check(
                  false,
                  "Atomic Violation: Atomic at {} has "
                  "(morally_strong ∩ fr);(morally_strong ∩ co) cycle with {}",
                  u.to_string(), v.to_string());
            }
          }
        }
      }
    }
  }
  return Difftest::Status();
}

Difftest::Status MemAxiomsChecker::check_axioms() {
  if (!inited_) {
    spdlog::warn("MemAxiomsChecker not initialized before check");
    return Difftest::Status();
  }

  auto tstart = std::chrono::high_resolution_clock::now();
  build_ppo_();
  build_fr_();
  auto tend = std::chrono::high_resolution_clock::now();

  const auto perf_build_graph_us =
      std::chrono::duration_cast<std::chrono::microseconds>(tend - tstart);
  perf_build_graph_us_ = perf_build_graph_us.count();

  tstart = std::chrono::high_resolution_clock::now();
  Difftest::Status ret;
  ret &= check_coherence_();
  ret &= check_atomic_();
  ret &= check_model_();
  tend = std::chrono::high_resolution_clock::now();

  const auto perf_verif_us =
      std::chrono::duration_cast<std::chrono::microseconds>(tend - tstart);
  perf_verif_us_ = perf_verif_us.count();

  if (!load_value_axiom_failed_loads.empty()) {
    fmt::memory_buffer lst;
    for (const auto &id : load_value_axiom_failed_loads) {
      fmt::format_to(std::back_insert_iterator(lst), "  {}\n", id.to_string());
    }
    ret &=
        dft_.check(false,
                   "Load Value Axiom Violation: {} loads failed to find "
                   "matching stores:\n{}",
                   load_value_axiom_failed_loads.size(), fmt::to_string(lst));
  }

  spdlog::info("MemAxiomsChecker build graph time: {} us",
               perf_build_graph_us.count());
  spdlog::info("MemAxiomsChecker verification time: {} us",
               perf_verif_us.count());

  checked_ = true;
  return ret;
}

Difftest::Status MemAxiomsChecker::check_model_() {
  if (!inited_) {
    spdlog::warn("MemAxiomsChecker not initialized before check");
    return Difftest::Status();
  }

  // acyclic l2_co|rfe|l2_fr|ppo as Model
  std::vector<
      std::pair<const std::unordered_map<LoadStoreId, std::vector<LoadStoreId>,
                                         LoadStoreIdHash> *,
                MCRelation>>
      rels = {{&co_order_, MCRelation::CO},
              {&rfe_order_, MCRelation::RFE},
              {&fr_order_, MCRelation::FR},
              {&ppo_, MCRelation::PPO}};
  return check_cycle_helper(rels, dft_, "Model");
}

void MemAxiomsChecker::add_store_id_entry(
    uint64_t paddr, uint8_t byte_val, const difftest::LoadStoreId &store_id) {
  // no multiple DYNAMIC instances of store to the same paddr byte with the SAME
  // VALUE, which has already ensured by the generator
  assert(store_id_.find(paddr) == store_id_.end() ||
         store_id_.at(paddr).find(byte_val) == store_id_.at(paddr).end());
  store_id_[paddr][byte_val] = store_id;
#if 0
  spdlog::info("Added store id entry: paddr {:#x} byte value {:#x} -> PC {:#x} "
               "(Warp={}, Thread={})",
               paddr, byte_val, store_id.pc, store_id.thread_gid.warp_gid,
               store_id.thread_gid.thread_lid);
#endif
}

void MemAxiomsChecker::add_pending_store_id(uint64_t paddr) {
  pending_store_id_.insert(paddr);
}

void MemAxiomsChecker::add_load_store_opcode(const LoadStoreId &id,
                                             const LoadStoreOpcode &opcode) {
  assert(ldst_opcode_.find(id) == ldst_opcode_.end());
  ldst_opcode_[id] = opcode;
}

void MemAxiomsChecker::add_data_dep(WarpGID warp_gid, uint64_t from_pc,
                                    uint64_t to_pc) {
  data_dep_[from_pc].push_back(to_pc);
}
void MemAxiomsChecker::add_addr_dep(WarpGID warp_gid, uint64_t from_pc,
                                    uint64_t to_pc) {
  addr_dep_[from_pc].push_back(to_pc);
}

unsigned MemAxiomsChecker::get_sm_id(WarpGID warp_gid) const {
#if defined(ARCH_VENTUS)
  return sm_id_map_.at(warp_gid);
#elif defined(ARCH_VORTEX)
  return warp_gid / Config::instance().arch.vortex_nr_warps_per_core; // core id
#endif
}

void MemAxiomsChecker::init() {
  assert(!inited_);

  // add initial writes to co
  for (const auto &[paddr, po] : po_loc_) {
    for (const auto &[tid, trace] : po.traces()) {
      for (const auto &id : trace) {
        if (id.initial_write) {
          add_co_(GLOBAL_SCOPE_ID, paddr, id);
        }
      }
    }
  }

  inited_ = true;
}

//
// Profiling Dumping
//

namespace {
// Helper structure for collecting and reporting container stats
struct ContainerStats {
  std::string name;
  size_t num_entries;
  size_t num_buckets;
  std::string detail;

  void report() const {
    float load_factor =
        num_buckets > 0 ? static_cast<float>(num_entries) / num_buckets : 0.0f;
    spdlog::info("  [{}] size={}, buckets={}, load_factor={:.2f}", name,
                 num_entries, num_buckets, load_factor);
    if (!detail.empty()) {
      spdlog::info("    └─ {}", detail);
    }
  }
};

} // namespace

void MemAxiomsChecker::dump() {
  spdlog::info("MemAxiomsChecker Performance Profile:");
  spdlog::info("");
  spdlog::info("=== Hash Map / Set Statistics ===");

  // Collect all container stats for consistent reporting
  std::vector<ContainerStats> all_stats;

  // store_id_: nested hash map (address -> byte -> store id)
  {
    size_t total_inner = 0;
    for (const auto &[paddr, inner_map] : store_id_) {
      total_inner += inner_map.size();
    }
    all_stats.push_back(ContainerStats{
        .name = "store_id",
        .num_entries = store_id_.size(),
        .num_buckets = store_id_.bucket_count(),
        .detail = fmt::format("inner entries={}", total_inner),
    });
  }

  // pending_store_id_: address set
  {
    all_stats.push_back(ContainerStats{
        .name = "pending_store_id",
        .num_entries = pending_store_id_.size(),
        .num_buckets = pending_store_id_.bucket_count(),
        .detail = "",
    });
  }

  // po_loc_: program order per address location
  {
    size_t total_traces = 0;
    for (const auto &[paddr, po] : po_loc_) {
      for (const auto &[tid, trace] : po.traces()) {
        total_traces += trace.size();
      }
    }
    all_stats.push_back(ContainerStats{
        .name = "po_loc",
        .num_entries = po_loc_.size(),
        .num_buckets = po_loc_.bucket_count(),
        .detail = fmt::format("trace_entries={}, addrs={}", total_traces,
                              po_loc_.size()),
    });
  }

  // Order relations: rf, rfe, rfi, co, coe, fr, ppo
  auto add_order_relation =
      [&all_stats](
          const std::string &name,
          const std::unordered_map<LoadStoreId, std::vector<LoadStoreId>,
                                   LoadStoreIdHash> &rel) {
        all_stats.push_back(ContainerStats{
            .name = name,
            .num_entries = rel.size(),
            .num_buckets = rel.bucket_count(),
            .detail = "",
        });
      };

  add_order_relation("rf_order", rf_order_);
  add_order_relation("rfe_order", rfe_order_);
  add_order_relation("rfi_order", rfi_order_);
  add_order_relation("co_order", co_order_);
  add_order_relation("coe_order", coe_order_);
  add_order_relation("fr_order", fr_order_);
  add_order_relation("ppo", ppo_);

  // co_per_paddr: address to latest store mapping
  {
    all_stats.push_back(ContainerStats{
        .name = "co_per_paddr",
        .num_entries = co_per_paddr.size(),
        .num_buckets = co_per_paddr.bucket_count(),
        .detail = "",
    });
  }

  // ldst_opcode_: load/store opcode mapping
  {
    all_stats.push_back(ContainerStats{
        .name = "ldst_opcode",
        .num_entries = ldst_opcode_.size(),
        .num_buckets = ldst_opcode_.bucket_count(),
        .detail = "",
    });
  }

  // Dependency relations: addr_dep, data_dep
  auto add_dep_relation =
      [&all_stats](
          const std::string &name,
          const std::unordered_map<uint64_t, std::vector<uint64_t>> &rel) {
        all_stats.push_back(ContainerStats{
            .name = name,
            .num_entries = rel.size(),
            .num_buckets = rel.bucket_count(),
            .detail = "",
        });
      };

  add_dep_relation("addr_dep", addr_dep_);
  add_dep_relation("data_dep", data_dep_);

  // Report all stats
  for (const auto &stat : all_stats) {
    stat.report();
  }

  spdlog::info("");
  spdlog::info("=== Load/Store Execution Statistics (by LoadStoreId) ===");
  {
    spdlog::info("  Executed Loads (unique LoadStoreIds): {}",
                 executed_loads_.size());
    spdlog::info("  Executed Stores (unique LoadStoreIds): {}",
                 executed_stores_.size());
    spdlog::info("  Executed Atomics (unique LoadStoreIds): {}",
                 executed_atomics_.size());
    spdlog::info("  Total Executed (unique LoadStoreIds): {}",
                 executed_loads_.size() + executed_stores_.size() +
                     executed_atomics_.size());
  }

  spdlog::info("");
  spdlog::info("=== Detailed Analysis ===");
  spdlog::info("po traces: {} threads", po_.traces().size());

  // Sample RFE edges (external read-from)
  if (!rfe_order_.empty()) {
    spdlog::info("RFE edges (external read-from, sample max 10):");
    size_t shown = 0;
    for (const auto &[src, dsts] : rfe_order_) {
      if (shown >= 10)
        break;
      spdlog::info("  From {} to {} dests", src.to_string(), dsts.size());
      for (size_t i = 0; i < std::min(size_t(3), dsts.size()); ++i) {
        spdlog::info("    -> {}", dsts[i].to_string());
      }
      if (dsts.size() > 3) {
        spdlog::info("    ... and {} more", dsts.size() - 3);
      }
      shown++;
    }
  }

  // Sample COE edges
  if (!coe_order_.empty()) {
    spdlog::info("COE edges (external coherence, sample max 10):");
    size_t shown = 0;
    for (const auto &[src, dsts] : coe_order_) {
      if (shown >= 10)
        break;
      spdlog::info("  From {} to {} dests", src.to_string(), dsts.size());
      for (size_t i = 0; i < std::min(size_t(3), dsts.size()); ++i) {
        spdlog::info("    -> {}", dsts[i].to_string());
      }
      if (dsts.size() > 3) {
        spdlog::info("    ... and {} more", dsts.size() - 3);
      }
      shown++;
    }
  }
}

void MemAxiomsChecker::get_statistics(
    std::unordered_map<std::string, size_t> &stats) const {
  // Helper to count total edges in a relation map
  auto count_edges =
      [](const std::unordered_map<LoadStoreId, std::vector<LoadStoreId>,
                                  LoadStoreIdHash> &rel) -> size_t {
    size_t total = 0;
    for (const auto &[_, dsts] : rel) {
      total += dsts.size();
    }
    return total;
  };

  // Relation statistics: nodes and edges
  stats["rf_nodes"] = rf_order_.size();
  stats["rf_edges"] = count_edges(rf_order_);

  stats["rfe_nodes"] = rfe_order_.size();
  stats["rfe_edges"] = count_edges(rfe_order_);

  stats["rfi_nodes"] = rfi_order_.size();
  stats["rfi_edges"] = count_edges(rfi_order_);

  stats["co_nodes"] = co_order_.size();
  stats["co_edges"] = count_edges(co_order_);

  stats["coe_nodes"] = coe_order_.size();
  stats["coe_edges"] = count_edges(coe_order_);

  stats["fr_nodes"] = fr_order_.size();
  stats["fr_edges"] = count_edges(fr_order_);

  stats["ppo_nodes"] = ppo_.size();
  stats["ppo_edges"] = count_edges(ppo_);

  // Performance statistics (in microseconds)
  stats["build_graph_time_us"] = perf_build_graph_us_;
  stats["verification_time_us"] = perf_verif_us_;

  // Load/Store execution statistics (actually executed, by unique LoadStoreId)
  stats["executed_loads"] = executed_loads_.size();
  stats["executed_stores"] = executed_stores_.size();
  stats["executed_atomics"] = executed_atomics_.size();
  stats["executed_total"] = executed_loads_.size() + executed_stores_.size() +
                            executed_atomics_.size();

  // State flags
  stats["checked"] = checked_ ? 1 : 0;
}

} // namespace difftest