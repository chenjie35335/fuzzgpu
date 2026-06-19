#pragma once

#include <functional>
#include <map>
#include <string>

#include <difftest_gpu/isa_emulator.hh>
#include <difftest_gpu/types.hh>

namespace difftest {

class DUT;

/// @brief Start the background difftest worker thread.
void create_worker_thread(const std::function<void()> &init_callback);
/// @brief Wait for the difftest worker thread to exit.
void wait_worker_thread();

/// @brief Register a program counter whose load is allowed to race.
void add_data_race_load_pc(uint64_t pc);

/// @brief Register a store-ID entry for a physical byte value.
void add_store_id_entry(uint64_t paddr, uint8_t byte_val,
                        const LoadStoreId &store_id);
/// @brief Register a physical address with pending store-ID resolution.
void add_pending_store_id(uint64_t paddr);
/// @brief Return global program order.
ProgramOrder &get_po();
/// @brief Return per-location program order for @p paddr.
ProgramOrder &get_po_loc(uint64_t paddr);
/// @brief Record opcode metadata for one load/store ID.
void add_load_store_opcode(const LoadStoreId &id,
                           const LoadStoreOpcode &opcode);
/// @brief Return global store trace storage.
StoreTrace &get_store_trace();

/// @brief Add one data-dependency edge for memory-model checking.
void add_data_dep(WarpGID warp_gid, uint64_t from_pc, uint64_t to_pc);
/// @brief Add one address-dependency edge for memory-model checking.
void add_addr_dep(WarpGID warp_gid, uint64_t from_pc, uint64_t to_pc);
/// @brief Initialize memory-model checking state.
void init_memory_checker();
/// @brief Dump memory-checker statistics into @p stats.
void dump_memory_checker_stats(std::unordered_map<std::string, size_t> &stats);

/// @brief Set difftest logging level.
void set_loglevel(unsigned level);

/// @brief Advance the difftest/DUT co-simulation by one tick.
void tick();

/// @brief Return the process-wide ISA emulator.
ISAEmulator &get_isa_emulator();

/// @brief Notify difftest that new warps were launched.
void new_warps_event(unsigned num_warps_per_block, unsigned num_blocks);

/// @brief Return whether the DUT has terminated.
bool dut_terminated();

/// @brief Return instruction statistics collected by difftest.
const std::unordered_map<std::string, uint64_t> &get_insn_stats();

using pfn_read_dut_pmem_callback =
    std::function<void(uint64_t paddr, uint8_t *out, size_t len)>;
using pfn_resume_emulator_thread = std::function<void()>;
using pfn_pause_emulator_thread = std::function<void()>;
using pfn_stop_emulator_thread = std::function<void()>;

/// @brief Install callbacks used to coordinate DUT and emulator threads.
void set_dut_callbacks(const pfn_read_dut_pmem_callback &read_pmem_callabck,
                       const pfn_pause_emulator_thread &pause_callback,
                       const pfn_resume_emulator_thread &resume_callback,
                       const pfn_stop_emulator_thread &stop_callback);
} // namespace difftest
