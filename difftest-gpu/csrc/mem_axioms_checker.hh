#pragma once

#include "difftest.hh"
#include "difftest_gpu/types.hh"

namespace difftest {

class MemAxiomsChecker {
  bool inited_{false};
  bool checked_{false};
  Difftest &dft_;
  std::unordered_map<uint64_t, std::unordered_map<uint8_t, LoadStoreId>>
      store_id_; // (paddr -> byte -> store id)
  std::unordered_set<uint64_t> pending_store_id_;
  ProgramOrder po_;
  std::unordered_map<uint64_t, ProgramOrder> po_loc_;
  std::unordered_map<LoadStoreId, std::vector<LoadStoreId>, LoadStoreIdHash>
      rf_order_, rfe_order_, rfi_order_, co_order_, coe_order_, fr_order_;
  std::unordered_map<unsigned, std::unordered_map<uint64_t, LoadStoreId>>
      co_per_paddr; // (paddr -> order)

  std::unordered_map<LoadStoreId, LoadStoreOpcode, LoadStoreIdHash>
      ldst_opcode_;
  std::unordered_map<uint64_t, std::vector<uint64_t>> addr_dep_, data_dep_;

  std::unordered_map<LoadStoreId, std::vector<LoadStoreId>, LoadStoreIdHash>
      ppo_;

  difftest::StoreTrace store_trace_; // (pc -> store trace)

  std::vector<LoadStoreId> load_value_axiom_failed_loads;

#if defined(ARCH_VENTUS)
  std::unordered_map<WarpGID, unsigned> sm_id_map_; // warp_gid -> sm_id
#endif

  const unsigned GLOBAL_SCOPE_ID{unsigned(-1)}; // special SM ID

  // Performance statistics
  mutable size_t perf_build_graph_us_{0};
  mutable size_t perf_verif_us_{0};

  // Execution counters (actually executed load/store by LoadStoreId)
  std::unordered_set<LoadStoreId, LoadStoreIdHash> executed_loads_;
  std::unordered_set<LoadStoreId, LoadStoreIdHash> executed_stores_;
  std::unordered_set<LoadStoreId, LoadStoreIdHash> executed_atomics_;

  void add_co_(unsigned sm_id, uint64_t paddr, const LoadStoreId &to);

  void build_ppo_();
  void build_fr_();

  // Helper to check if two load/store instructions are morally_strong related
  bool is_morally_strong_(const LoadStoreId &a, const LoadStoreId &b) const;

  Difftest::Status check_coherence_();
  Difftest::Status check_atomic_();
  Difftest::Status check_model_();

public:
  MemAxiomsChecker(Difftest &dft);
  Difftest::Status check_axioms();
  void add_store_id_entry(uint64_t paddr, uint8_t byte_val,
                          const LoadStoreId &store_id);
  void add_pending_store_id(uint64_t paddr);
  void add_load_store_opcode(const LoadStoreId &id,
                             const LoadStoreOpcode &opcode);
  void add_data_dep(WarpGID warp_gid, uint64_t from_pc, uint64_t to_pc);
  void add_addr_dep(WarpGID warp_gid, uint64_t from_pc, uint64_t to_pc);
#if defined(ARCH_VENTUS)
  void set_sm_id(WarpGID warp_gid, unsigned sm_id) {
    sm_id_map_[warp_gid] = sm_id;
  }
#endif
  unsigned get_sm_id(WarpGID warp_gid) const;
  void init();

  void dcache_shmem_rsp_event(unsigned sm_id, WarpGID warp_gid,
                              ThreadLID thread_lid, uint64_t pc, size_t size,
                              DCacheOpcode memop, uint64_t paddr,
                              uint64_t rdat);

  void l2_dcache_req_event(uint64_t paddr, size_t cacheline_size,
                           DCacheOpcode memop, const std::vector<uint8_t> &data,
                           const std::vector<uint8_t> &mask);

  auto &po() { return po_; }
  auto &po_loc() { return po_loc_; }

  auto &store_trace() { return store_trace_; }

  void dump();

  void get_statistics(std::unordered_map<std::string, size_t> &stats) const;

  bool checked() const { return checked_; }
};

} // namespace difftest