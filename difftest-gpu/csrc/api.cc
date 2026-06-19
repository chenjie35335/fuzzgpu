#include "spdlog/spdlog.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <difftest_gpu/api.hh>
#include <mutex>
#include <thread>

#include "api_internal.hh"
#include "concurrentqueue.h"
#include "config.hh"
#include "difftest.hh"
#include "mem_axioms_checker.hh"

namespace difftest {

uint64_t rtl_clk_cycles;
static std::atomic<bool> g_terminated{false};
static std::unique_ptr<std::thread> g_emu_thread;
static std::optional<pfn_read_dut_pmem_callback> read_dut_pmem_callback;
static pfn_resume_emulator_thread resume_emulator_thread_callback;
static pfn_pause_emulator_thread pause_emulator_thread_callback;
static pfn_stop_emulator_thread stop_emulator_thread_callback;

void create_worker_thread(const std::function<void()> &init_callback) {
  //
  // ISA Emulator & difftest thread
  //
  assert(!g_emu_thread);
  g_emu_thread = std::make_unique<std::thread>([=]() {
    spdlog::trace("Difftest emulator thread started, thread id = {}",
                  std::hash<std::thread::id>{}(std::this_thread::get_id()));

    if (!Difftest::g_instance.load(std::memory_order_acquire)) {
      // initializate

      spdlog::set_pattern("[%^%l%$] %v");
      // FIXME: possibly conflict with the emulator_main
      if (const char *log_level_str = std::getenv("DIFFTEST_LOG_LEVEL")) {
        set_loglevel(std::atoi(log_level_str));
      }

      auto isa_emu = std::make_shared<ISAEmulator>();

      // init the config
      Config::create(isa_emu->get_arch());

      assert_log(Config::instance().arch.word_bits == sizeof(Word) * 8,
                 "Word size mismatch: Config {} bits vs. Type {} bits.\n"
                 "You may need to recompile the ISA emulator or the difftest "
                 "library with matching XLEN.",
                 Config::instance().arch.word_bits, sizeof(Word) * 8);

      Difftest::g_instance.store(new Difftest(isa_emu),
                                 std::memory_order_release);
    }

    spdlog::trace("[ResetBeginEvent] Initializing difftest instance. ISA = {}, "
                  "XLEN = {}, num "
                  "to threads = {}, "
                  "emulator thread id = {}",
                  Config::instance().arch.name, sizeof(Word) * 8,
                  Config::instance().arch.nr_threads_per_warp,
                  std::hash<std::thread::id>{}(std::this_thread::get_id()));

    Difftest::instance().reset_event();

    init_callback();

    //
    // Event loop
    //
    extern moodycamel::ConcurrentQueue<std::function<void()>> dpi_event_queue;
    std::function<void()> event;
    while (!g_terminated) {
      // Drain all pending events as fast as possible.
      while (dpi_event_queue.try_dequeue(event)) {
        event();
      }
    }
    while (dpi_event_queue.try_dequeue(event)) {
      event();
    }
  });
}

void wait_worker_thread() {
  if (g_emu_thread) {
    g_emu_thread->join();
  }
}

void pause_emulator_thread() { pause_emulator_thread_callback(); }

void resume_emulator_thread() { resume_emulator_thread_callback(); }

void stop_emulator_thread() { stop_emulator_thread_callback(); }

// CC & MC verification APIs
void add_data_race_load_pc(uint64_t pc) {
  Difftest::instance().add_data_race_load_pc(pc);
}
void add_store_id_entry(uint64_t paddr, uint8_t byte_val,
                        const LoadStoreId &store_id) {
  Difftest::instance().mem_axioms_checker().add_store_id_entry(paddr, byte_val,
                                                               store_id);
}
void add_pending_store_id(uint64_t paddr) {
  spdlog::trace("Add pending store id for paddr {:#x}", paddr);
  Difftest::instance().mem_axioms_checker().add_pending_store_id(paddr);
}
void add_data_dep(WarpGID warp_gid, uint64_t from_pc, uint64_t to_pc) {
  Difftest::instance().mem_axioms_checker().add_data_dep(warp_gid, from_pc,
                                                         to_pc);
}
void add_addr_dep(WarpGID warp_gid, uint64_t from_pc, uint64_t to_pc) {
  Difftest::instance().mem_axioms_checker().add_addr_dep(warp_gid, from_pc,
                                                         to_pc);
}
ProgramOrder &get_po() {
  return Difftest::instance().mem_axioms_checker().po();
}
ProgramOrder &get_po_loc(uint64_t paddr) {
  return Difftest::instance().mem_axioms_checker().po_loc()[paddr];
}
void add_load_store_opcode(const LoadStoreId &id,
                           const LoadStoreOpcode &opcode) {
  Difftest::instance().mem_axioms_checker().add_load_store_opcode(id, opcode);
}
void init_memory_checker() { Difftest::instance().mem_axioms_checker().init(); }
StoreTrace &get_store_trace() {
  return Difftest::instance().mem_axioms_checker().store_trace();
}
void dump_memory_checker_stats(std::unordered_map<std::string, size_t> &stats) {
  Difftest::instance().mem_axioms_checker().get_statistics(stats);
}

void set_loglevel(unsigned level) {
  std::cout << TermColor::YELLOW << "Log level = " << level << TermColor::RESET
            << std::endl;

  switch (level) {

  case 0:
    spdlog::set_level(spdlog::level::trace);
    break;

  case 1:
    spdlog::set_level(spdlog::level::debug);
    break;

  case 2:
    spdlog::set_level(spdlog::level::info);
    break;

  case 3:
    spdlog::set_level(spdlog::level::warn);
    break;

  case 4:
    spdlog::set_level(spdlog::level::err);
    break;

  default:
    spdlog::set_level(spdlog::level::off);
  }
}

void tick() {
  extern moodycamel::ConcurrentQueue<std::function<void()>> dpi_event_queue;
  ++rtl_clk_cycles;

  if (!(rtl_clk_cycles % Config::instance().check_deadlock_cycles)) {
    dpi_event_queue.enqueue([=]() { Difftest::instance().detect_deadlock(); });
  }
  if (!(rtl_clk_cycles % Config::instance().check_watch_dog_cycles)) {
    dpi_event_queue.enqueue([=]() { Difftest::instance().watchdog(); });
  }
}

ISAEmulator &get_isa_emulator() { return Difftest::instance().isa_emulator(); }

void new_warps_event(unsigned num_warps_per_block, unsigned num_blocks) {
  Difftest::instance().new_warps_event(num_warps_per_block, num_blocks);
}

/**
 * @return false if an error occurred
 */
bool dut_terminated() {
  g_terminated = true;
  g_emu_thread->join();
  bool ret = Difftest::instance().dut_terminated();
  return ret;
}

bool read_dut_pmem(uint64_t paddr, uint8_t *out, size_t len) {
  if (!read_dut_pmem_callback.has_value())
    return false;
  (*read_dut_pmem_callback)(paddr, out, len);
  return true;
}

void set_dut_callbacks(const pfn_read_dut_pmem_callback &read_pmem_callabck,
                       const pfn_pause_emulator_thread &pause_callback,
                       const pfn_resume_emulator_thread &resume_callback,
                       const pfn_stop_emulator_thread &stop_callback) {
  read_dut_pmem_callback = read_pmem_callabck;
  pause_emulator_thread_callback = pause_callback;
  resume_emulator_thread_callback = resume_callback;
  stop_emulator_thread_callback = stop_callback;
}

const std::unordered_map<std::string, uint64_t> &get_insn_stats() {
  return Difftest::instance().get_insn_stats();
}
} // namespace difftest
