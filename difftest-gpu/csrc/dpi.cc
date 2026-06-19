#include "spdlog/spdlog.h"
#include <atomic>
#include <mutex>
#include <svdpi.h>
#include <unordered_set>
#include <verilated_vpi.h>

#if defined(ARCH_VENTUS)
#include <difftest_gpu/util/parse_ventus_opcode.hpp>
#endif

#include "assert.hh"
#include "concurrentqueue.h"
#include "config.hh"
#include "difftest.hh"
#include "mem_axioms_checker.hh"
#include "types_internal.hh"
#include <difftest_gpu/api.hh>

#include "dpi.hh"

namespace difftest {

moodycamel::ConcurrentQueue<std::function<void()>> dpi_event_queue;

#ifdef ENABLE_DPI

static VarBitset svBitVec_to_bitset(const svBitVecVal *s, unsigned bits) {
  VarBitset bs(bits);
  for (unsigned b = 0; b < bits; ++b)
    bs[b] = svGetBitselBit(s, b);
  return bs;
}

template <typename T>
static std::vector<T> svBitVec_to_vector(const svBitVecVal *s, size_t dim1,
                                         size_t s_bits) {
  std::vector<T> vec(dim1);
  assert(s_bits <= sizeof(T) * 8);
  for (size_t i = 0; i < dim1; ++i) {
    // TODO: optimize to word-wise copy
    vec[i] = 0;
    for (size_t b = 0; b < s_bits; ++b) {
      vec[i] |= (T(svGetBitselBit(s, i * s_bits + b)) << b);
    }
  }
  return vec;
}

template <typename T>
static T svBitVec_at(const svBitVecVal *s, size_t elm_bits, size_t i) {
  assert(elm_bits <= sizeof(T) * 8);
  // TODO: optimize to word-wise copy
  T ret = 0;
  for (size_t b = 0; b < elm_bits; ++b) {
    ret |= (T(svGetBitselBit(s, i * elm_bits + b)) << b);
  }
  return ret;
}

#endif // ENABLE_DPI

} // namespace difftest

using namespace difftest;

#ifdef ENABLE_DPI

static std::atomic<bool> filter_all_events{true};
static std::mutex hardware_id_map_mutex;
static std::mutex inflight_mem_map_mutex;

#if defined(ARCH_VORTEX)
static std::unordered_map<WarpGID, std::unordered_map<InstId, unsigned>>
    barrier_uuids;
static std::mutex barrier_uuids_mutex;
#endif

class WarpInfo {
public:
  WarpGID warp_gid;
  uint32_t num_threads_per_warp;
};

class pair_hash {
public:
  template <typename T1, typename T2>
  std::size_t operator()(const std::pair<T1, T2> &p) const {
    auto h1 = std::hash<T1>{}(p.first);
    auto h2 = std::hash<T2>{}(p.second);
    return h1 ^ (h2 << 1);
  }
};

static std::unordered_map<std::pair<uint32_t, uint32_t>, WarpInfo, pair_hash>
    hardware_id_to_warp_gid;
static std::unordered_map<WarpGID, std::pair<uint32_t, uint32_t>>
    warp_gid_to_hardware_id;

class InflightMemReq {
public:
  struct Entry {
    WarpGID warp_gid;
    ThreadMask activate_tmask, cur_tmask;
    uint64_t pc;
    std::vector<uint64_t> vaddr_per_lane;
    std::vector<uint8_t> wordOffset1H; // 4-bit one-hot
    uint32_t size;
    DCacheOpcode memop;
    ThreadMask is_shared_mask;
  };
  std::unordered_map<uint64_t, Entry> entries; // (instr_id -> Entry)
};
std::unordered_map<uint32_t, InflightMemReq> inflight_mem_map;

// This event is designed to ensure that the ISA emulator thread is the same as
// the verilator main thread that calls DPI
extern "C" void dpi_reset_begin_event() {
  filter_all_events.store(true, std::memory_order_release);
#if defined(ARCH_VORTEX)
  {
    std::lock_guard<std::mutex> lock(barrier_uuids_mutex);
    barrier_uuids.clear();
  }
#endif
  {
    std::lock_guard<std::mutex> lock(hardware_id_map_mutex);
    hardware_id_to_warp_gid.clear();
    warp_gid_to_hardware_id.clear();
  }
  {
    std::lock_guard<std::mutex> lock(inflight_mem_map_mutex);
    inflight_mem_map.clear();
  }
}

extern "C" void dpi_reset_end_event() {
  filter_all_events.store(false, std::memory_order_release);
}

#if defined(ARCH_VORTEX)
//
// Vortex DPI functions
//

static void dpi_decode_event_internal(Word pc, uint32_t warp_gid,
                                      const svBitVecVal *tmask, bool rd_wen,
                                      std::vector<Trace::RegWriteback> rd,
                                      uint32_t opcode, InstId iid) {
  if (filter_all_events.load(std::memory_order_acquire))
    return;
#ifndef NDEBUG
  for (const auto &r : rd) {
    assert_log(r.rd.type < RegType::COUNT, "Invalid reg type {}",
               unsigned(r.rd.type));
  }
#endif

  const auto tmask_ = (ThreadMask)svBitVec_to_bitset(
      tmask, Config::instance()
                 .arch.nr_threads_per_warp); // copy for multi-thread capture

  std::optional<std::vector<Trace::RegWriteback>> regs;
  if (rd_wen) {
    regs = rd;
  }

  dpi_event_queue.enqueue([=]() {
    Difftest::instance().decode_event(iid, pc, warp_gid, tmask_, regs,
                                      Opcode(opcode), std::nullopt, false);
  });
}

static void dpi_writeback_reg_event_internal(
    InstId iid, uint32_t warp_gid, const svBitVecVal *wb_tmask, Word pc,
    bool rd_wen, std::vector<Trace::RegWriteback> rd) {
  if (filter_all_events.load(std::memory_order_acquire))
    return;
  const auto nr_threads_per_warp = Config::instance().arch.nr_threads_per_warp;

  const auto wb_tmask_ = (ThreadMask)svBitVec_to_bitset(
      wb_tmask, nr_threads_per_warp); // copy for multi-thread capture

  assert((!rd_wen && rd.empty()) || (rd_wen && !rd.empty()));
  dpi_event_queue.enqueue([=]() {
    Difftest::instance().writeback_reg_event(iid, warp_gid, wb_tmask_, pc, rd,
                                             std::nullopt);
  });
}

extern "C" void dpi_decode_event(Word pc, uint32_t warp_gid,
                                 const svBitVecVal *tmask, bool rd_wen,
                                 uint32_t rd_type, uint32_t rd_idx,
                                 uint32_t opcode, InstId iid) {
  if (filter_all_events.load(std::memory_order_acquire))
    return;
  std::vector<Trace::RegWriteback> regs;
  if (rd_wen) {
    regs.emplace_back(
        Trace::RegWriteback(Config::instance().arch.nr_threads_per_warp));
    regs[0].rd = RegId{(RegType)rd_type, rd_idx};
  }

  dpi_decode_event_internal(pc, warp_gid, tmask, rd_wen, regs, opcode, iid);
}

// vortex generates multiple wb events for workgroup-level barriers
void dpi_grm_commit_callback(uint32_t iid) {}

extern "C" void dpi_writeback_reg_event(InstId iid, uint32_t warp_gid,
                                        const svBitVecVal *wb_tmask, Word pc,
                                        uint32_t rd_type, uint32_t rd_idx,
                                        bool rd_wen,
                                        const svBitVecVal *rd_dat) {
  if (filter_all_events.load(std::memory_order_acquire))
    return;

  {
    std::lock_guard<std::mutex> lock(barrier_uuids_mutex);
    if (barrier_uuids[warp_gid].count(iid) &&
        barrier_uuids[warp_gid][iid] > 0) {
      barrier_uuids[warp_gid][iid]--;
      if (barrier_uuids[warp_gid][iid] == 0) {
        barrier_uuids[warp_gid].erase(iid);
      }
      return;
    }
  }

  const auto nr_threads_per_warp = Config::instance().arch.nr_threads_per_warp;

  std::vector<Trace::RegWriteback> regs;
  if (rd_wen) {
    regs.emplace_back(Trace::RegWriteback(nr_threads_per_warp));
    regs[0].rd = RegId{(RegType)rd_type, rd_idx};
    regs[0].rd_data = svBitVec_to_vector<uint64_t>(rd_dat, nr_threads_per_warp,
                                                   (sizeof(Word) * 8));
  }

  dpi_writeback_reg_event_internal(iid, warp_gid, wb_tmask, pc, rd_wen, regs);
}

extern "C" void dpi_barrier_writeback_event(InstId iid, uint32_t warp_gid,
                                            uint32_t warp_id,
                                            const svBitVecVal *wb_tmask,
                                            Word pc) {
  if (filter_all_events.load(std::memory_order_acquire))
    return;
  const auto nr_threads_per_warp = Config::instance().arch.nr_threads_per_warp;

  std::cout << "dpi_barrier_writeback_event: iid=" << iid
            << " warp_gid=" << warp_gid << " warp_id=" << warp_id << std::endl;

  {
    std::lock_guard<std::mutex> lock(barrier_uuids_mutex);
    if (barrier_uuids[warp_gid].find(iid) != barrier_uuids[warp_gid].end()) {
      auto ret = Difftest::instance().check(
          false, "Duplicate barrier writeback event detected for iid {}", iid);
      Difftest::instance().report(ret, "Barrier Writeback", 0, 0);
      return;
    } else {
      // vortex have bugs that writeback barrier multiple times, but no
      // architectural visible effect
      barrier_uuids[warp_gid][iid] =
          Config::instance().arch.vortex_nr_warps_per_core;
    }
  }

  std::vector<Trace::RegWriteback> regs; // Empty registers for barrier
  dpi_writeback_reg_event_internal(iid, warp_gid, wb_tmask, pc, false, regs);
}

// slower path

extern "C" void dpi_decode_event_multi_regs(Word pc, uint32_t warp_gid,
                                            const svBitVecVal *tmask,
                                            bool rd_wen, unsigned nr_rd,
                                            const svBitVecVal *rd_type,
                                            const svBitVecVal *rd_idx,
                                            uint32_t opcode, InstId iid) {
  if (filter_all_events.load(std::memory_order_acquire))
    return;
  const auto nr_threads_per_warp = Config::instance().arch.nr_threads_per_warp;

  if (!rd_wen)
    nr_rd = 0;
  std::vector<Trace::RegWriteback> regs(nr_rd, nr_threads_per_warp);
  for (unsigned i = 0; i < nr_rd; ++i) {
    regs[i].rd = RegId{(RegType)svBitVec_at<uint32_t>(rd_type, 32, i),
                       svBitVec_at<uint32_t>(rd_idx, 32, i)};
  }

  dpi_decode_event_internal(pc, warp_gid, tmask, rd_wen, regs, opcode, iid);
}

extern "C" void dpi_writeback_reg_event_multi_regs(
    InstId iid, uint32_t warp_gid, const svBitVecVal *wb_tmask, Word pc,
    unsigned nr_rd, const svBitVecVal *rd_type, const svBitVecVal *rd_idx,
    bool rd_wen, const svBitVecVal *rd_dat) {
  if (filter_all_events.load(std::memory_order_acquire))
    return;
  const auto nr_threads_per_warp = Config::instance().arch.nr_threads_per_warp;
  if (!rd_wen)
    nr_rd = 0;

  // Must copy all svBitVecVal data before enqueuing since DPI pointers become
  // invalid after return
  const auto wb_tmask_ =
      (ThreadMask)svBitVec_to_bitset(wb_tmask, nr_threads_per_warp);

  std::vector<Trace::RegWriteback> regs(nr_rd, nr_threads_per_warp);
  for (unsigned i = 0; i < nr_rd; ++i) {
    regs[i].rd = RegId{(RegType)svBitVec_at<uint32_t>(rd_type, 32, i),
                       svBitVec_at<uint32_t>(rd_idx, 32, i)};
    regs[i].rd_data.resize(nr_threads_per_warp);
    for (unsigned tlid = 0; tlid < nr_threads_per_warp; ++tlid) {
      regs[i].rd_data[tlid] =
          svBitVec_at<uint64_t>(rd_dat, 64, i * nr_threads_per_warp + tlid);
    }
  }

  dpi_event_queue.enqueue([=, regs = std::move(regs)]() {
    Difftest::instance().writeback_reg_event(iid, warp_gid, wb_tmask_, pc, regs,
                                             std::nullopt);
  });
}

extern "C" void dpi_dcache_shmem_req_event(
    uint32_t sm_id, uint32_t hardware_warp_id, uint64_t instr_id,
    uint32_t num_lanes, const svBitVecVal *activate_mask, uint64_t pc,
    const svBitVecVal *vaddr_per_lane, const svBitVecVal *wordOffset1H,
    uint32_t size, uint32_t memop, const svBitVecVal *shared_mem) {
  if (filter_all_events.load(std::memory_order_acquire))
    return;

  const auto query = std::make_pair(sm_id, hardware_warp_id);
  WarpInfo warp_info;

  {
    std::lock_guard<std::mutex> lock(hardware_id_map_mutex);
    // Auto-register warp if not present (Vortex specific workaround due to
    // missing dispatch)
    if (hardware_id_to_warp_gid.find(query) == hardware_id_to_warp_gid.end()) {
      // Assuming simple mapping: warp_gid = sm_id * 32 + wid
      WarpGID fake_gid = (sm_id << 16) | hardware_warp_id;
      hardware_id_to_warp_gid[query] =
          WarpInfo{.warp_gid = fake_gid, .num_threads_per_warp = num_lanes};
      warp_gid_to_hardware_id[fake_gid] = query;
    }
    warp_info = hardware_id_to_warp_gid[query];
  }

  const auto max_num_threads = num_lanes;

  const auto new_vaddr_per_lane = svBitVec_to_vector<uint64_t>(
      vaddr_per_lane, max_num_threads, Config::instance().arch.word_bits);
  const auto new_activate_mask =
      svBitVec_to_bitset(activate_mask, max_num_threads);
  const auto byteen_bits = Config::instance().arch.word_bits / 8;
  const auto new_word_offset_onehot =
      svBitVec_to_vector<uint8_t>(wordOffset1H, max_num_threads, byteen_bits);

  const auto new_is_shared_mask =
      svBitVec_to_bitset(shared_mem, max_num_threads);

  // memop mapping: 0=Read, 1=Write. Map to DCacheOpcode.
  DCacheOpcode d_op = DCacheOpcode(memop);

  {
    std::lock_guard<std::mutex> lock(inflight_mem_map_mutex);
    auto &inflight_mem = inflight_mem_map[sm_id];
    inflight_mem.entries[instr_id] = InflightMemReq::Entry{
        .warp_gid = warp_info.warp_gid,
        .activate_tmask = new_activate_mask,
        .cur_tmask = ThreadMask(max_num_threads),
        .pc = pc,
        .vaddr_per_lane = new_vaddr_per_lane,
        .wordOffset1H = new_word_offset_onehot,
        .size = size,
        .memop = d_op,
        .is_shared_mask = new_is_shared_mask,
    };
    spdlog::info("l1 req sm_id={} hw_wid={} gid={} instr_id={} pc={:#x} memop={} "
                 "size={} tmask={}",
                 sm_id, hardware_warp_id, warp_info.warp_gid, instr_id, pc,
                 unsigned(d_op), size, new_activate_mask);
  }
}

extern "C" void dpi_dcache_shmem_rsp_event(uint32_t sm_id, uint64_t instr_id,
                                           uint32_t num_lanes,
                                           uint32_t data_width,
                                           const svBitVecVal *activateMask,
                                           const svBitVecVal *data) {
  if (filter_all_events.load(std::memory_order_acquire))
    return;

  InflightMemReq::Entry inflight_mem_entry;
  bool found = false;
  {
    std::lock_guard<std::mutex> lock(inflight_mem_map_mutex);
    auto &inflight_mem = inflight_mem_map[sm_id];
    auto it = inflight_mem.entries.find(instr_id);
    if (it == inflight_mem.entries.end()) {
      auto ret = Difftest::instance().check(
          false,
          "DCache response event for unknown instr_id: sm_id {}, instr_id {}",
          sm_id, instr_id);
      Difftest::instance().report(ret, "DCache Response", std::nullopt,
                                  std::nullopt);
      return;
    }
    inflight_mem_entry = it->second; // copy for multi-thread capture
    found = true;
  }
  const auto max_num_threads = num_lanes;

  const auto activate_tmask = svBitVec_to_bitset(activateMask, max_num_threads);
  const auto valid_tmask = activate_tmask & inflight_mem_entry.activate_tmask;

  assert(data_width == Config::instance().arch.word_bits &&
         "Data width mismatch in dcache response");

  for (unsigned tid = 0; tid < valid_tmask.size(); ++tid) {
    if (valid_tmask.test(tid)) {
      const auto raw_data =
          svBitVec_at<uint64_t>(data, Config::instance().arch.word_bits, tid);
      uint64_t extracted_val = 0;
      const auto byteen = inflight_mem_entry.wordOffset1H[tid];
      switch (byteen) {
      // Byte (8-bit)
      case 0x01:
        extracted_val = raw_data & 0xFF;
        break; // Byte 0
      case 0x02:
        extracted_val = (raw_data >> 8) & 0xFF;
        break; // Byte 1
      case 0x04:
        extracted_val = (raw_data >> 16) & 0xFF;
        break; // Byte 2
      case 0x08:
        extracted_val = (raw_data >> 24) & 0xFF;
        break; // Byte 3
      case 0x10:
        extracted_val = (raw_data >> 32) & 0xFF;
        break; // Byte 4 (64-bit)
      case 0x20:
        extracted_val = (raw_data >> 40) & 0xFF;
        break; // Byte 5 (64-bit)
      case 0x40:
        extracted_val = (raw_data >> 48) & 0xFF;
        break; // Byte 6 (64-bit)
      case 0x80:
        extracted_val = (raw_data >> 56) & 0xFF;
        break; // Byte 7 (64-bit)

      // Half-word (16-bit)
      case 0x03:
        extracted_val = raw_data & 0xFFFF;
        break; // Half 0
      case 0x0C:
        extracted_val = (raw_data >> 16) & 0xFFFF;
        break; // Half 1
      case 0x30:
        extracted_val = (raw_data >> 32) & 0xFFFF;
        break; // Half 2 (64-bit)
      case 0xC0:
        extracted_val = (raw_data >> 48) & 0xFFFF;
        break; // Half 3 (64-bit)

      // Word (32-bit)
      case 0x0F:
        extracted_val = raw_data & 0xFFFFFFFF;
        break; // Word 0
      case 0xF0:
        extracted_val = (raw_data >> 32) & 0xFFFFFFFF;
        break; // Word 1 (64-bit)

      // Double-word (64-bit)
      case 0xFF:
        extracted_val = raw_data;
        break;
      default:
        // For other access patterns, take raw
        extracted_val = raw_data;
        break;
      }

      dpi_event_queue.enqueue([=]() {
        Difftest::instance().mem_axioms_checker().dcache_shmem_rsp_event(
            sm_id, inflight_mem_entry.warp_gid, tid, inflight_mem_entry.pc,
            inflight_mem_entry.size, inflight_mem_entry.memop,
            inflight_mem_entry.vaddr_per_lane[tid], extracted_val);
      });
    }
  }

  inflight_mem_entry.cur_tmask |= valid_tmask;

  if ((inflight_mem_entry.cur_tmask & inflight_mem_entry.activate_tmask) ==
      inflight_mem_entry.activate_tmask) {
    // all lanes responded
    std::lock_guard<std::mutex> lock(inflight_mem_map_mutex);
    inflight_mem_map[sm_id].entries.erase(instr_id);
  }
}

// Store completion event - called when store commits via no_rsp_buf path
// Unlike loads, stores don't go through mem_rsp_fire, they complete locally
extern "C" void dpi_dcache_store_event(
    uint32_t sm_id, uint32_t hardware_warp_id, uint64_t instr_id,
    uint32_t num_lanes, const svBitVecVal *activate_mask, uint64_t pc,
    const svBitVecVal *vaddr_per_lane, const svBitVecVal *data_per_lane,
    const svBitVecVal *byteen_per_lane, uint32_t size,
    const svBitVecVal *shared_mem) {
  if (filter_all_events.load(std::memory_order_acquire))
    return;

  const auto query = std::make_pair(sm_id, hardware_warp_id);
  WarpInfo warp_info;

  {
    std::lock_guard<std::mutex> lock(hardware_id_map_mutex);
    // Auto-register warp if not present (Vortex specific workaround)
    if (hardware_id_to_warp_gid.find(query) == hardware_id_to_warp_gid.end()) {
      WarpGID fake_gid = (sm_id << 16) | hardware_warp_id;
      hardware_id_to_warp_gid[query] =
          WarpInfo{.warp_gid = fake_gid, .num_threads_per_warp = num_lanes};
      warp_gid_to_hardware_id[fake_gid] = query;
    }
    warp_info = hardware_id_to_warp_gid[query];
  }

  const auto max_num_threads = num_lanes;

  const auto new_vaddr_per_lane = svBitVec_to_vector<uint64_t>(
      vaddr_per_lane, max_num_threads, Config::instance().arch.word_bits);
  const auto new_activate_mask =
      svBitVec_to_bitset(activate_mask, max_num_threads);
  const auto byteen_bits = Config::instance().arch.word_bits / 8;
  const auto new_byteen_per_lane =
      svBitVec_to_vector<uint8_t>(byteen_per_lane, max_num_threads, byteen_bits);
  const auto new_is_shared_mask =
      svBitVec_to_bitset(shared_mem, max_num_threads);

  // Extract store data per lane
  const auto new_data_per_lane = svBitVec_to_vector<uint64_t>(
      data_per_lane, max_num_threads, Config::instance().arch.word_bits);

  spdlog::info("store commit sm_id={} hw_wid={} gid={} instr_id={} pc={:#x} "
               "size={} tmask={}",
               sm_id, hardware_warp_id, warp_info.warp_gid, instr_id, pc,
               size, new_activate_mask);

  // Process each active lane's store
  for (unsigned tid = 0; tid < max_num_threads; ++tid) {
    if (new_activate_mask.test(tid)) {
      const auto raw_data = new_data_per_lane[tid];
      const auto byteen = new_byteen_per_lane[tid];
      uint64_t store_val = 0;

      // Extract store value based on byteen mask (similar to load extraction)
      switch (byteen) {
      // Byte (8-bit)
      case 0x01: store_val = raw_data & 0xFF; break;
      case 0x02: store_val = (raw_data >> 8) & 0xFF; break;
      case 0x04: store_val = (raw_data >> 16) & 0xFF; break;
      case 0x08: store_val = (raw_data >> 24) & 0xFF; break;
      case 0x10: store_val = (raw_data >> 32) & 0xFF; break;
      case 0x20: store_val = (raw_data >> 40) & 0xFF; break;
      case 0x40: store_val = (raw_data >> 48) & 0xFF; break;
      case 0x80: store_val = (raw_data >> 56) & 0xFF; break;

      // Half-word (16-bit)
      case 0x03: store_val = raw_data & 0xFFFF; break;
      case 0x0C: store_val = (raw_data >> 16) & 0xFFFF; break;
      case 0x30: store_val = (raw_data >> 32) & 0xFFFF; break;
      case 0xC0: store_val = (raw_data >> 48) & 0xFFFF; break;

      // Word (32-bit)
      case 0x0F: store_val = raw_data & 0xFFFFFFFF; break;
      case 0xF0: store_val = (raw_data >> 32) & 0xFFFFFFFF; break;

      // Double-word (64-bit)
      case 0xFF: store_val = raw_data; break;

      default: store_val = raw_data; break;
      }

      dpi_event_queue.enqueue([=]() {
        Difftest::instance().mem_axioms_checker().dcache_shmem_rsp_event(
            sm_id, warp_info.warp_gid, tid, pc,
            size, DCacheOpcode::Store,
            new_vaddr_per_lane[tid], store_val);
      });
    }
  }
}

extern "C" void dpi_l2_dcache_req_event(uint64_t source, uint32_t opcode,
                                        uint64_t paddr, uint32_t cacheline_size,
                                        const svBitVecVal *data,
                                        const svBitVecVal *mask) {
  if (filter_all_events.load(std::memory_order_acquire))
    return;

  std::vector<uint8_t> data_vec(cacheline_size);
  for (size_t i = 0; i < cacheline_size; ++i) {
    data_vec[i] = svBitVec_at<uint8_t>(data, 8, i);
  }
  std::vector<uint8_t> mask_vec =
      svBitVec_to_vector<uint8_t>(mask, cacheline_size, 1);

  dpi_event_queue.enqueue([=]() {
    Difftest::instance().mem_axioms_checker().l2_dcache_req_event(
        paddr, cacheline_size, DCacheOpcode(opcode), data_vec, mask_vec);
  });
}

#elif defined(ARCH_VENTUS)
//
// Ventus DPI functions
//

//
// CTA Dispatch Event
//
extern "C" void dpi_cta_dispatch_event(uint32_t software_wg_id,
                                       uint32_t software_warp_id,
                                       uint32_t sm_id,
                                       uint32_t hardware_warp_id,
                                       uint32_t rtl_num_thread) {
  if (filter_all_events.load(std::memory_order_acquire))
    return;
  const auto query = std::make_pair(sm_id, hardware_warp_id);
  const auto warp_info = WarpInfo{
      .warp_gid = software_wg_id * Difftest::instance().num_warps_per_block() +
                  software_warp_id,
      .num_threads_per_warp = rtl_num_thread,
  };
  spdlog::info("CTA Dispatch: sm_id {}, hardware_warp_id {} -> "
               "software_wg_id {}, software_warp_id {}, warp_gid {}",
               sm_id, hardware_warp_id, software_wg_id, software_warp_id,
               warp_info.warp_gid);

  {
    std::lock_guard<std::mutex> lock(hardware_id_map_mutex);
    if (hardware_id_to_warp_gid.find(query) == hardware_id_to_warp_gid.end()) {
      hardware_id_to_warp_gid[query] = warp_info;
    } else {
      auto ret = Difftest::instance().check(
          false,
          "Duplicate hardware warp dispatch detected: sm_id {}, "
          "hardware_warp_id {}",
          sm_id, hardware_warp_id);
      Difftest::instance().report(ret, "CTA Dispatch", warp_info.warp_gid, 0);
    }

    if (warp_gid_to_hardware_id.find(warp_info.warp_gid) ==
        warp_gid_to_hardware_id.end()) {
      warp_gid_to_hardware_id[warp_info.warp_gid] = query;
    } else {
      auto ret = Difftest::instance().check(
          false,
          "Duplicate software warp dispatch detected: software_wg_id {}, "
          "software_warp_id {}",
          software_wg_id, software_warp_id);
      Difftest::instance().report(ret, "CTA Dispatch", warp_info.warp_gid,
                                  std::nullopt);
    }
  }

  dpi_event_queue.enqueue([=]() {
    Difftest::instance().mem_axioms_checker().set_sm_id(warp_info.warp_gid,
                                                        sm_id);
  });
}

//
// Decode Event
//
extern "C" void dpi_decode_event(uint64_t pc, uint32_t hardware_warp_gid,
                                 uint32_t sm_id, uint32_t instr,
                                 const svBitVecVal *tmask, uint32_t iid) {
  if (filter_all_events.load(std::memory_order_acquire))
    return;
  const auto query = std::make_pair(sm_id, hardware_warp_gid);
  WarpInfo warp_info;
  {
    std::lock_guard<std::mutex> lock(hardware_id_map_mutex);
    auto it = hardware_id_to_warp_gid.find(query);
    if (it == hardware_id_to_warp_gid.end()) {
      auto ret =
          Difftest::instance().check(false,
                                     "Decode event for unknown hardware warp: "
                                     "sm_id {}, hardware_warp_id {}",
                                     sm_id, hardware_warp_gid);
      Difftest::instance().report(ret, "Decode Event", std::nullopt,
                                  std::nullopt);
      return;
    }
    warp_info = it->second;
  }

  const auto opcode_info = parse_ventus_opcode(instr);

  const auto max_num_threads = Config::instance().arch.nr_threads_per_warp;
  ThreadMask tmask_(max_num_threads);
  if (opcode_info.applied_tmask) {
    tmask_ = svBitVec_to_bitset(tmask, max_num_threads);
  } else {
    tmask_.set(0);
  }

  dpi_event_queue.enqueue([=]() {
    Difftest::instance().decode_event(iid, pc, warp_info.warp_gid, tmask_,
                                      std::nullopt, opcode_info.opcode, instr,
                                      opcode_info.dummy_writeback);
  });
}

//
// Scalar Register Writeback Event
//
extern "C" void dpi_xreg_writeback_event(uint32_t sm_id, uint32_t rdat,
                                         bool rd_wen, uint32_t reg_idx,
                                         uint32_t hardware_warp_id, uint32_t pc,
                                         uint32_t inst, uint32_t iid) {
  if (filter_all_events.load(std::memory_order_acquire))
    return;
  const auto query = std::make_pair(sm_id, hardware_warp_id);
  WarpGID warp_gid;
  {
    std::lock_guard<std::mutex> lock(hardware_id_map_mutex);
    auto it = hardware_id_to_warp_gid.find(query);
    if (it == hardware_id_to_warp_gid.end()) {
      auto ret = Difftest::instance().check(
          false,
          "Scalar Reg writeback event for unknown hardware warp: "
          "sm_id {}, hardware_warp_id {}",
          sm_id, hardware_warp_id);
      Difftest::instance().report(ret, "XReg Writeback", std::nullopt,
                                  std::nullopt);
      return;
    }
    warp_gid = it->second.warp_gid;
  }
  spdlog::trace("xreg writeback");

  ThreadMask tmask(1);
  tmask.reset();
  tmask.set(0); // only lane 0 writes back

  std::vector<Trace::RegWriteback> regs(rd_wen ? 1 : 0, 1);
  if (rd_wen) {
    regs[0].rd = RegId{RegType::Int, reg_idx};
    regs[0].rd_data[0] = rdat;
  }

  dpi_event_queue.enqueue([=]() {
    Difftest::instance().writeback_reg_event(iid, warp_gid, tmask, pc, regs,
                                             inst);
  });
}

//
// Vector Register Writeback Event
//
extern "C" void dpi_vreg_writeback_event(uint32_t sm_id,
                                         const svBitVecVal *rd_dat, bool rd_wen,
                                         uint32_t reg_idx,
                                         uint32_t hardware_warp_id, uint32_t pc,
                                         uint32_t inst, uint32_t iid,
                                         const svBitVecVal *wvd_mask) {
  if (filter_all_events.load(std::memory_order_acquire))
    return;
  const auto query = std::make_pair(sm_id, hardware_warp_id);
  WarpGID warp_gid;
  {
    std::lock_guard<std::mutex> lock(hardware_id_map_mutex);
    auto it = hardware_id_to_warp_gid.find(query);
    if (it == hardware_id_to_warp_gid.end()) {
      auto ret = Difftest::instance().check(
          false,
          "Vector Reg writeback event for unknown hardware warp: "
          "sm_id {}, hardware_warp_id {}",
          sm_id, hardware_warp_id);
      Difftest::instance().report(ret, "VReg Writeback", std::nullopt,
                                  std::nullopt);
      return;
    }
    warp_gid = it->second.warp_gid;
  }
  const auto max_num_threads = Config::instance().arch.nr_threads_per_warp;
  const auto wb_tmask = svBitVec_to_bitset(wvd_mask, max_num_threads);

  std::vector<Trace::RegWriteback> regs(rd_wen ? 1 : 0, max_num_threads);

  if (rd_wen) {
    regs[0].rd = RegId{RegType::Vec, reg_idx};
    for (unsigned tlid = 0; tlid < max_num_threads; ++tlid) {
      if (!wb_tmask.test(tlid))
        continue;
      regs[0].rd_data[tlid] = svBitVec_at<uint64_t>(
          rd_dat, Config::instance().arch.word_bits, tlid);
    }
  }

#if 0
  if (!regs.empty()) {
  spdlog::trace("vreg writeback sm_id {} hardware_warp_id {} warp_gid {} pc "
                "{:#x} inst {:#x} "
                "reg_idx {} rd_wen {} wb_tmask {}",
                sm_id, hardware_warp_id, warp_gid, pc, inst, reg_idx, rd_wen, wb_tmask);
  }
#endif

  dpi_event_queue.enqueue([=]() {
    Difftest::instance().writeback_reg_event(iid, warp_gid, wb_tmask, pc, regs,
                                             inst);
  });
}

//
// Barrier Writeback Event
//
extern "C" void dpi_barrier_writeback_event(uint32_t sm_id,
                                            uint32_t hardware_warp_id,
                                            uint32_t wg_slot_id, uint32_t pc,
                                            uint32_t inst,
                                            uint32_t dispatch_id) {
  if (filter_all_events.load(std::memory_order_acquire))
    return;
  const auto query = std::make_pair(sm_id, hardware_warp_id);
  WarpGID warp_gid;
  {
    std::lock_guard<std::mutex> lock(hardware_id_map_mutex);
    auto it = hardware_id_to_warp_gid.find(query);
    if (it == hardware_id_to_warp_gid.end()) {
      auto ret = Difftest::instance().check(
          false,
          "Barrier writeback event for unknown hardware warp: "
          "sm_id {}, hardware_warp_id {}",
          sm_id, hardware_warp_id);
      Difftest::instance().report(ret, "Barrier Writeback", std::nullopt,
                                  std::nullopt);
      return;
    }
    warp_gid = it->second.warp_gid;
  }

  const auto max_num_threads = Config::instance().arch.nr_threads_per_warp;

  ThreadMask tmask(max_num_threads);
  tmask.reset();
  tmask.set(0); // single lane for barrier

  std::vector<Trace::RegWriteback> regs; // empty wb register list

  dpi_event_queue.enqueue([=]() {
    Difftest::instance().writeback_reg_event(dispatch_id, warp_gid, tmask, pc,
                                             regs, inst);
  });

  (void)wg_slot_id; // unused for now
}

static DCacheOpcode ventus_tlc_memop(uint32_t opcode) {
  switch (opcode) {
  case 0: // paritial write
    return DCacheOpcode::Store;
  case 1:
    return DCacheOpcode::Store;
  case 2: // arth amo
  case 3: // logical amo
    return DCacheOpcode::Atomic;
  case 4:
    return DCacheOpcode::Load;
  case 5: // inv/flush
  case 6: // AcquireBlock
  case 7: // AcquirePerm
    return DCacheOpcode::Other;
  default:
    spdlog::warn("Unknown Ventus memop opcode {}, treat as Other", opcode);
    return DCacheOpcode::Other;
  }
}

//
// DCache Event
//
extern "C" void
dpi_dcache_shmem_req_event(uint32_t sm_id, uint32_t hardware_warp_id,
                           uint32_t instr_id, const svBitVecVal *activate_mask,
                           uint32_t pc, const svBitVecVal *vaddr_per_lane,
                           const svBitVecVal *wordOffset1H, uint32_t size,
                           uint32_t memop, const svBitVecVal *shared_mem) {
  if (filter_all_events.load(std::memory_order_acquire))
    return;
  const auto query = std::make_pair(sm_id, hardware_warp_id);
  WarpGID warp_gid;
  {
    std::lock_guard<std::mutex> lock(hardware_id_map_mutex);
    auto it = hardware_id_to_warp_gid.find(query);
    if (it == hardware_id_to_warp_gid.end()) {
      auto ret =
          Difftest::instance().check(false,
                                     "DCache event for unknown hardware warp: "
                                     "sm_id {}, hardware_warp_id {}",
                                     sm_id, hardware_warp_id);
      Difftest::instance().report(ret, "DCache Request", std::nullopt,
                                  std::nullopt);
      return;
    }
    warp_gid = it->second.warp_gid;
  }

  const auto max_num_threads = Config::instance().arch.nr_threads_per_warp;
  const auto new_vaddr_per_lane = svBitVec_to_vector<uint64_t>(
      vaddr_per_lane, max_num_threads, Config::instance().arch.word_bits);
  const auto new_activate_mask =
      svBitVec_to_bitset(activate_mask, max_num_threads);
  const auto byteen_bits = Config::instance().arch.word_bits / 8;
  const auto new_word_offset_onehot =
      svBitVec_to_vector<uint8_t>(wordOffset1H, max_num_threads, byteen_bits);

  const auto new_is_shared_mask =
      svBitVec_to_bitset(shared_mem, max_num_threads);

  {
    std::lock_guard<std::mutex> lock(inflight_mem_map_mutex);
    auto &inflight_mem = inflight_mem_map[sm_id];
    if (inflight_mem.entries.find(instr_id) != inflight_mem.entries.end()) {
      const auto &org = inflight_mem.entries[instr_id];
      auto ret = Difftest::instance().check(
          org.warp_gid == warp_gid,
          "Duplicate DCache request with different warp_gid: sm_id {}, "
          "instr_id {}",
          sm_id, instr_id);
      ret &= Difftest::instance().check(
          org.pc == pc,
          "Duplicate DCache request with different pc: sm_id {}, instr_id {}",
          sm_id, instr_id);
      ret &= Difftest::instance().check(
          org.size == size,
          "Duplicate DCache request with different size: sm_id {}, instr_id {}",
          sm_id, instr_id);
      ret &=
          Difftest::instance().check(org.memop == DCacheOpcode(memop),
                                     "Duplicate DCache request with different "
                                     "memop: sm_id {}, instr_id {}",
                                     sm_id, instr_id);
      ret &= Difftest::instance().check(
          (org.vaddr_per_lane == new_vaddr_per_lane),
          "Duplicate DCache request with different vaddr_per_lane: sm_id {}, "
          "instr_id {}",
          sm_id, instr_id);
      ret &= Difftest::instance().check(
          (org.wordOffset1H == new_word_offset_onehot),
          "Duplicate DCache request with different wordOffset1H: sm_id {}, "
          "instr_id {}",
          sm_id, instr_id);
      if (!ret) {
        Difftest::instance().report(ret, "DCache Request", std::nullopt,
                                    std::nullopt);
      }

      // merge the new req
      inflight_mem.entries[instr_id].activate_tmask |= new_activate_mask;
    } else {
      inflight_mem.entries[instr_id] = InflightMemReq::Entry{
          .warp_gid = warp_gid,
          .activate_tmask = new_activate_mask,
          .cur_tmask = ThreadMask(max_num_threads),
          .pc = pc,
          .vaddr_per_lane = new_vaddr_per_lane,
          .wordOffset1H = new_word_offset_onehot,
          .size = size,
          .memop = DCacheOpcode(memop),
          .is_shared_mask = new_is_shared_mask,
      };
    }
  }
  (void)shared_mem; // unified
}

extern "C" void dpi_dcache_shmem_rsp_event(uint32_t sm_id, uint32_t instr_id,
                                           const svBitVecVal *activateMask,
                                           const svBitVecVal *data) {
  if (filter_all_events.load(std::memory_order_acquire))
    return;

  InflightMemReq::Entry inflight_mem_entry;
  bool all_lanes_responded = false;
  {
    std::lock_guard<std::mutex> lock(inflight_mem_map_mutex);
    auto &inflight_mem = inflight_mem_map[sm_id];
    auto it = inflight_mem.entries.find(instr_id);
    if (it == inflight_mem.entries.end()) {
      auto ret = Difftest::instance().check(
          false,
          "DCache response event for unknown instr_id: sm_id {}, instr_id {}",
          sm_id, instr_id);
      Difftest::instance().report(ret, "DCache Response", std::nullopt,
                                  std::nullopt);
      return;
    }
    inflight_mem_entry = it->second; // copy for the event queue

    const auto activate_tmask =
        svBitVec_to_bitset(activateMask, inflight_mem_entry.cur_tmask.size());

    it->second.cur_tmask |= activate_tmask;

    if ((it->second.cur_tmask & it->second.activate_tmask) ==
        it->second.activate_tmask) {
      // all lanes responded
      inflight_mem.entries.erase(it);
      all_lanes_responded = true;
    }
  }

  const auto activate_tmask =
      svBitVec_to_bitset(activateMask, inflight_mem_entry.cur_tmask.size());
  const auto valid_tmask = activate_tmask & inflight_mem_entry.activate_tmask;

  if (inflight_mem_entry.memop != DCacheOpcode::Other) {
    for (unsigned tid = 0; tid < valid_tmask.size(); ++tid) {
      if (valid_tmask.test(tid)) {
        const auto raw_data =
            svBitVec_at<uint64_t>(data, Config::instance().arch.word_bits, tid);
        uint64_t extracted_val = 0;
        switch (inflight_mem_entry.wordOffset1H[tid]) {
        // Byte (8-bit)
        case 0x01:
          extracted_val = raw_data & 0xFF;
          break; // Byte 0
        case 0x02:
          extracted_val = (raw_data >> 8) & 0xFF;
          break; // Byte 1
        case 0x04:
          extracted_val = (raw_data >> 16) & 0xFF;
          break; // Byte 2
        case 0x08:
          extracted_val = (raw_data >> 24) & 0xFF;
          break; // Byte 3
        case 0x10:
          extracted_val = (raw_data >> 32) & 0xFF;
          break; // Byte 4
        case 0x20:
          extracted_val = (raw_data >> 40) & 0xFF;
          break; // Byte 5
        case 0x40:
          extracted_val = (raw_data >> 48) & 0xFF;
          break; // Byte 6
        case 0x80:
          extracted_val = (raw_data >> 56) & 0xFF;
          break; // Byte 7

        // Half-word (16-bit)
        case 0x03:
          extracted_val = raw_data & 0xFFFF;
          break; // Half 0
        case 0x0C:
          extracted_val = (raw_data >> 16) & 0xFFFF;
          break; // Half 1
        case 0x30:
          extracted_val = (raw_data >> 32) & 0xFFFF;
          break; // Half 2
        case 0xC0:
          extracted_val = (raw_data >> 48) & 0xFFFF;
          break; // Half 3

        // Word (32-bit)
        case 0x0F:
          extracted_val = raw_data & 0xFFFFFFFF;
          break; // Word 0
        case 0xF0:
          extracted_val = (raw_data >> 32) & 0xFFFFFFFF;
          break; // Word 1

        // Double-word (64-bit)
        case 0xFF:
          extracted_val = raw_data;
          break;
        default:
          spdlog::error("Invalid wordOffset1H value: {}",
                        inflight_mem_entry.wordOffset1H[tid]);
          std::abort();
        }

        dpi_event_queue.enqueue([=]() {
          Difftest::instance().mem_axioms_checker().dcache_shmem_rsp_event(
              sm_id, inflight_mem_entry.warp_gid, tid, inflight_mem_entry.pc,
              inflight_mem_entry.size, inflight_mem_entry.memop,
              inflight_mem_entry.vaddr_per_lane[tid] /* no mmu */,
              extracted_val);
        });
      }
    }
  }
}

//
// L2 DCache Request Event for l2_co tracking
//
// L2 Request Event: called when L2 receives a request from L1
// For store operations, updates l2_co (coherence order at L2 level)
extern "C" void dpi_l2_dcache_req_event(uint64_t source, uint32_t opcode,
                                        uint64_t paddr, uint32_t cacheline_size,
                                        const svBitVecVal *data,
                                        const svBitVecVal *mask) {
  if (filter_all_events.load(std::memory_order_acquire))
    return;

  std::vector<uint8_t> data_vec(cacheline_size);
  for (size_t i = 0; i < cacheline_size; ++i) {
    data_vec[i] = svBitVec_at<uint8_t>(data, 8, i);
  }
  std::vector<uint8_t> mask_vec =
      svBitVec_to_vector<uint8_t>(mask, cacheline_size, 1);

  const auto memop = ventus_tlc_memop(opcode);
  if (memop == DCacheOpcode::Other)
    return;

  dpi_event_queue.enqueue([=]() {
    Difftest::instance().mem_axioms_checker().l2_dcache_req_event(
        paddr, cacheline_size, memop, data_vec, mask_vec);
  });
}

#endif

#endif // ENABLE_DPI
