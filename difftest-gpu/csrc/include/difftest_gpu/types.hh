#pragma once

#include <cassert>
#include <iostream>
#include <sstream>
#include <stddef.h>
#include <stdint.h>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "spdlog/fmt/fmt.h"

namespace difftest {

typedef uint8_t Byte;

#ifdef DIFFTEST_XLEN_32
typedef uint32_t Word;
typedef int32_t WordI;
typedef uint64_t DWord;
typedef int64_t DWordI;
#elif defined(DIFFTEST_XLEN_64)
typedef uint64_t Word;
typedef int64_t WordI;
typedef __uint128_t DWord;
typedef __int128_t DWordI;
#else
#error Missing defination of DIFFTEST_XLEN_*
#endif

typedef uint64_t InstId;

/// @brief Architectural register-file kind.
enum class RegType { Int = 0, Float, Vec, COUNT };

/// @brief Register identifier used by traces and emulator register access.
struct RegId {
  ///< Register-file kind.
  RegType type;
  ///< Register index; Ventus may use extended indices.
  uint32_t idx;
  ///< Selected element width in bits.
  unsigned sew;

  /// @brief Construct integer register zero with default width.
  RegId()
    : type(RegType::Int)
    , idx(0)
    , sew(0) {}
  /// @brief Construct a register identifier.
  RegId(RegType type_, uint32_t idx_, unsigned sew_ = sizeof(Word) * 8)
    : type(type_)
    , idx(idx_)
    , sew(sew_) {}

  inline bool operator==(const RegId &src) const {
    return type == src.type && idx == src.idx && sew == src.sew;
  }
  inline bool operator!=(const RegId &src) const { return !operator==(src); }
  /// @brief Return the textual register-file prefix.
  const char *type_str() const {
    switch (type) {
    case RegType::Int:
      return "r";
    case RegType::Float:
      return "f";
    case RegType::Vec:
      return "v";
    default:
      std::abort();
    }
  }
};

/// @brief Hash function for RegId.
struct RegIdHash {
  std::size_t operator()(const RegId &k) const {
    return ((std::hash<uint32_t>()(static_cast<uint32_t>(k.type)) ^
             (std::hash<uint32_t>()(k.idx) << 1)) >>
            1) ^
           (std::hash<unsigned>()(k.sew) << 1);
  }
};

/// @brief Normalized opcode class used by difftest traces.
enum class Opcode {
  Others = 0,
  Call,
  Ret,
  RetCall,
  CtrlStatusRead,
  TmaskWrite,
  FloatingPoint,
};

using WarpGID = unsigned;
using ThreadLID = unsigned;

/// @brief Memory visibility scope for scoped memory operations.
enum class MemScope {
  Global = 0, // for all the kernels (Device-wide)
  Workgroup,  // for CTA/workgroup (SM-wide, Shared memory)
  Warp,       // for warp/thread (Subcore-wide, Local Memory)
};

/// @brief Identifies the memory scope associated with an access.
class ScopeIdentifier {
  ///< Memory scope kind.
  enum MemScope scope_;
  ///< Representative warp for workgroup/warp scoped memory.
  WarpGID
      warp_gid_;

public:
  /// @brief Tag for global memory scope construction.
  class global {};
  /// @brief Tag for warp-local memory scope construction.
  class warp {};
  /// @brief Tag for workgroup memory scope construction.
  class workgroup {};

  ScopeIdentifier() {}

  /// @brief Construct a global scope identifier.
  ScopeIdentifier(global)
    : scope_(MemScope::Global) {}

  /// @brief Construct a workgroup scope identifier.
  ScopeIdentifier(workgroup, WarpGID any_warp_gid)
    : scope_(MemScope::Workgroup)
    , warp_gid_(any_warp_gid) {}

  /// @brief Construct a warp scope identifier.
  ScopeIdentifier(warp, WarpGID warp_gid)
    : scope_(MemScope::Warp)
    , warp_gid_(warp_gid) {}

  /// @brief Return the memory scope kind.
  auto scope() const { return scope_; }

  /// @brief Return the associated warp ID for workgroup/warp scopes.
  auto warp_gid() const {
    assert(scope() == MemScope::Workgroup || scope() == MemScope::Warp);
    return warp_gid_;
  }
};

/// @brief Memory access direction.
enum class MemAccessType { LOAD = 0, STORE };

/// @brief Variable-size bitset used for thread and warp masks.
class VarBitset : public std::vector<bool> {
public:
  using std::vector<bool>::vector;

#define _VarBitset_DeclOperators(_op)                                          \
  inline VarBitset &operator _op## = (const VarBitset &src) {                  \
    for (size_t b = 0; b < std::max(src.size(), size()); b++) {                \
      const auto x = b < src.size() ? src[b] : false;                          \
      const auto y = b < size() ? at(b) : false;                               \
      operator[](b) = x _op y;                                                 \
    }                                                                          \
    return *this;                                                              \
  }

  _VarBitset_DeclOperators(&);
  _VarBitset_DeclOperators(|);
  _VarBitset_DeclOperators(^);

  inline bool any() const {
    for (const auto &bit : *this)
      if (bit)
        return true;
    return false;
  }
  inline bool none() const { return !any(); }
  inline bool all() const {
    for (const auto &bit : *this)
      if (!bit)
        return false;
    return true;
  }

#define _VarBitset_DeclBinary(_op)                                             \
  inline VarBitset operator _op(const VarBitset &src) const {                  \
    VarBitset ret(*this);                                                      \
    return ret.operator _op## = (src);                                         \
  }

  _VarBitset_DeclBinary(&);
  _VarBitset_DeclBinary(|);
  _VarBitset_DeclBinary(^);

  inline VarBitset operator~() const {
    VarBitset ret(*this);
    for (size_t b = 0; b < ret.size(); ++b)
      ret[b] = !ret[b];
    return ret;
  }
  inline void set(size_t b, bool val = true) { operator[](b) = val; }
  inline void reset(size_t b) { operator[](b) = false; }
  inline bool test(size_t b) const { return at(b); }
  inline void flip(unsigned pos) { operator[](pos) = !at(pos); }

  bool operator==(const VarBitset &src) const {
    // allowing omtting leading zeros when comparing different sizes
    if (size() < src.size()) {
      for (size_t i = size(); i < src.size(); ++i)
        if (src[i])
          return false;
    } else if (size() > src.size()) {
      for (size_t i = src.size(); i < size(); ++i)
        if (at(i))
          return false;
    }

    for (size_t b = 0; b < std::min(size(), src.size()); ++b)
      if (at(b) != src[b])
        return false;
    return true;
  }

  inline void reset() {
    for (size_t b = 0; b < size(); ++b)
      operator[](b) = false;
  }

  inline size_t count() const {
    // TODO: replace it with popcount
    size_t c = 0;
    for (size_t b = 0; b < size(); ++b)
      if (at(b))
        c++;
    return c;
  }

  inline uint64_t to_ulong() const {
    assert(size() <= 64 && "Bits too wide: > 64");
    uint64_t ret = 0;
    for (size_t b = 0; b < size(); ++b)
      ret |= (uint64_t(at(b)) << b);
    return ret;
  }

  size_t find_first() const {
    for (size_t b = 0; b < size(); ++b)
      if (at(b))
        return b;
    return size();
  }

  explicit VarBitset(size_t bits, uint64_t ulong) {
    resize(bits);
    for (size_t b = 0; b < bits; ++b)
      operator[](b) = ((ulong >> b) & 1);
  }
};

/// @brief Active thread mask.
using ThreadMask = VarBitset;

/// @brief Active warp mask.
using WarpMask = VarBitset;

/// @brief Numeric data format of an instruction.
enum class NumericFormat {
  FP_S = 0, // 32bit single precision
  FP_D,     // 64bit double precision
  FP_H,     // 16bit half precision
  FP_Q,     // 128bit quad precision
  INT_FROM_FP,
  INT,
};

/// @brief Static target architecture description.
class Arch {
public:
  ///< Architecture name.
  std::string name;
  ///< Native word width in bits.
  unsigned word_bits;
  ///< True when architectural register files share physical storage.
  bool unified_phy_reg;
  ///< Global memory base address.
  uint64_t gmem_start_addr;
  ///< Startup PC/address.
  uint64_t startup_addr;
  ///< Virtual address range used for generated data.
  std::pair<uint64_t, uint64_t> data_vaddr_range;
  ///< Virtual address range used for generated code.
  std::pair<uint64_t, uint64_t> code_vaddr_range;
  ///< MMU page size.
  uint64_t page_size;
#ifdef ARCH_VORTEX
  unsigned vortex_nr_int_regs;
  unsigned vortex_nr_fp_regs;
  unsigned vortex_nr_total_cores;    // FIXME remove it
  unsigned vortex_nr_warps_per_core; // max
  unsigned vortex_nr_cores_per_cluster;
  unsigned vortex_nr_total_clusters;
  unsigned vortex_nr_total_warps; // max
#endif
#ifdef ARCH_VENTUS
  unsigned ventus_nr_scalar_regs;
  unsigned ventus_nr_vector_regs;
  unsigned ventus_nr_warp_per_block; // max
  unsigned ventus_nr_sm;
#endif
  ///< Maximum number of threads per warp.
  unsigned nr_threads_per_warp; // max
  ///< True when address translation is enabled.
  bool mmu_enabled;

  /// @brief Bitmask type used for enabled RISC-V extensions.
  using riscv_exts_t = uint64_t;
  ///< Enabled RISC-V extension bitmask.
  riscv_exts_t riscv_exts_mask;
  static const uint64_t rv32i = 1ull << 0;
  static const uint64_t rv64i = 1ull << 1;
  static const uint64_t rv32m = 1ull << 2;
  static const uint64_t rv64m = 1ull << 3;
  static const uint64_t rv32a = 1ull << 4;
  static const uint64_t rv64a = 1ull << 5;
  static const uint64_t rv32f = 1ull << 6;
  static const uint64_t rv64f = 1ull << 7;
  static const uint64_t rv32d = 1ull << 8;
  static const uint64_t rv64d = 1ull << 9;
  static const uint64_t zicond = 1ull << 10;
  static const uint64_t vortex = 1ull << 11;
  static const uint64_t vortex_tcu = 1ull << 12;
  static const uint64_t vortex_gbar = 1ull << 13;
  static const uint64_t ventus = 1ull << 14;
  static const uint64_t rvv = 1ull << 15;
  ///< Vector register length in bits.
  unsigned vlen;
  ///< Whether scalar instructions obey the current thread mask.
  bool scalar_belongs_to_mt; // whether scalar instructions controlled by thread
                             // mask, if ==false, only one thread per warp can
                             // execute scalar instructions

  ///< Number of hardware barriers exposed by the target.
  unsigned num_barriers;

  ///< Physical MMIO regions that generated memory ops must avoid.
  std::vector<std::pair<uint64_t, uint64_t>> mmio_regions;

  /// @brief Return extension-name definitions used by riscv_ext_names().
  static const std::vector<std::pair<const char *, riscv_exts_t>> &
  riscv_ext_defs() {
    static const std::vector<std::pair<const char *, riscv_exts_t>> defs = {
        {"RV32I", rv32i},         {"RV64I", rv64i},
        {"RV32M", rv32m},         {"RV64M", rv64m},
        {"RV32A", rv32a},         {"RV64A", rv64a},
        {"RV32F", rv32f},         {"RV64F", rv64f},
        {"RV32D", rv32d},         {"RV64D", rv64d},
        {"ZICOND", zicond},       {"VORTEX", vortex},
        {"VORTEX_TCU", vortex_tcu},
        {"VORTEX_GBAR", vortex_gbar},
        {"VENTUS", ventus},       {"RVV", rvv},
    };
    return defs;
  }

  /// @brief Return enabled RISC-V extension names.
  std::vector<std::string> riscv_ext_names() const {
    std::vector<std::string> names;
    for (const auto &ext : riscv_ext_defs()) {
      if (riscv_exts_mask & ext.second) {
        names.emplace_back(ext.first);
      }
    }
    return names;
  }

  /// @brief Construct a target architecture description.
  Arch(const std::string &name_, unsigned word_bits_, bool unified_phy_reg_,
       uint64_t gmem_start_addr_, uint64_t startup_addr_,
       const std::pair<uint64_t, uint64_t> &data_vaddr_range_,
       const std::pair<uint64_t, uint64_t> &code_vaddr_range_,
       uint64_t page_size_,
#ifdef ARCH_VORTEX
       unsigned vortex_nr_int_regs_, unsigned vortex_nr_fp_regs_,
       unsigned vortex_nr_total_cores_, unsigned vortex_nr_warps_per_core_,
       unsigned vortex_nr_cores_per_cluster_,
       unsigned vortex_nr_total_clusters_, unsigned vortex_nr_total_warps_,
#endif
#ifdef ARCH_VENTUS
       unsigned ventus_nr_scalar_regs_, unsigned ventus_nr_vector_regs_,
       unsigned ventus_nr_warp_per_block_, unsigned ventus_nr_sm_,
#endif
       unsigned nr_threads_per_warp_, bool mmu_enabled_,
       riscv_exts_t riscv_exts_mask_, unsigned vlen_,
       bool scalar_belongs_to_mt_, unsigned num_barriers_,
       const std::vector<std::pair<uint64_t, uint64_t>> &mmio_regions_)
    : name(name_)
    , word_bits(word_bits_)
    , unified_phy_reg(unified_phy_reg_)
    , gmem_start_addr(gmem_start_addr_)
    , startup_addr(startup_addr_)
    , data_vaddr_range(data_vaddr_range_)
    , code_vaddr_range(code_vaddr_range_)
    , page_size(page_size_)
#ifdef ARCH_VORTEX
    , vortex_nr_int_regs(vortex_nr_int_regs_)
    , vortex_nr_fp_regs(vortex_nr_fp_regs_)
    , vortex_nr_total_cores(vortex_nr_total_cores_)
    , vortex_nr_warps_per_core(vortex_nr_warps_per_core_)
    , vortex_nr_cores_per_cluster(vortex_nr_cores_per_cluster_)
    , vortex_nr_total_clusters(vortex_nr_total_clusters_)
    , vortex_nr_total_warps(vortex_nr_total_warps_)
#endif
#ifdef ARCH_VENTUS
    , ventus_nr_scalar_regs(ventus_nr_scalar_regs_)
    , ventus_nr_vector_regs(ventus_nr_vector_regs_)
    , ventus_nr_warp_per_block(ventus_nr_warp_per_block_)
    , ventus_nr_sm(ventus_nr_sm_)
#endif
    , nr_threads_per_warp(nr_threads_per_warp_)

    , mmu_enabled(mmu_enabled_)
    , riscv_exts_mask(riscv_exts_mask_)
    , vlen(vlen_)
    , scalar_belongs_to_mt(scalar_belongs_to_mt_)
    , num_barriers(num_barriers_)
    , mmio_regions(mmio_regions_) {
  }
};

/// @brief One instruction execution trace emitted by the ISA emulator.
class Trace {
public:
  ///< Current and next program counters.
  uint64_t pc, next_pc;
  ///< Current and next thread masks.
  ThreadMask tmask;
  ThreadMask next_tmask;
  ///< Whether the instruction used tmask.
  bool applied_tmask;

  /// @brief Register writeback payload for one destination register.
  struct RegWriteback {
    ///< Destination register ID.
    struct RegId rd;
    ///< Per-thread destination data.
    std::vector<uint64_t> rd_data;
    /// @brief Construct writeback storage for @p max_lane lanes.
    RegWriteback(unsigned max_lane)
      : rd_data(max_lane) {}
  };
  ///< Register writebacks produced by this instruction.
  std::vector<RegWriteback> regs;

  ///< Normalized opcode class.
  enum Opcode opcode;
  ///< Numeric data format.
  NumericFormat num_format;
  ///< CSR address touched by this instruction, if any.
  uint32_t csr_addr;
  ///< Raw instruction bits.
  uint32_t insn_bits;
  ///< Whether this instruction trapped.
  bool trap_taken;
  ///< Disassembled instruction text.
  std::string disasm;

  ///< True for synthetic writeback-only trace records.
  bool dummy_wb;

  /// @brief Construct trace storage for the target architecture.
  Trace(const Arch &arch, unsigned nr_reg_groups = 1)
    : regs(nr_reg_groups, arch.nr_threads_per_warp) {}
};

/// @brief One memory access trace entry.
class MemTrace {
public:
  ///< Physical address accessed.
  uint64_t addr;
  ///< Access size in bytes.
  size_t size;
  ///< Write data, valid only for stores.
  uint64_t wdat;
  ///< True for store accesses.
  bool is_store;
  ///< Memory scope of this access.
  ScopeIdentifier scope;
  ///< Thread lane that issued this access.
  ThreadLID thread_lid;
};

/// @brief Global thread identity as runtime warp ID plus lane ID.
struct GlobalThreadId {
  ///< Runtime warp ID.
  WarpGID warp_gid;
  ///< Thread lane ID inside the warp.
  ThreadLID thread_lid;
  bool operator==(const GlobalThreadId &other) const {
    return warp_gid == other.warp_gid && thread_lid == other.thread_lid;
  }
};
/// @brief Hash function for GlobalThreadId.
struct GlobalThreadIdHash {
  std::size_t operator()(const GlobalThreadId &k) const {
    return (std::hash<WarpGID>()(k.warp_gid) ^
            (std::hash<ThreadLID>()(k.thread_lid) << 1));
  }
};

/// @brief Unique identity for load/store events and synthetic memory nodes.
struct LoadStoreId {
  ///< Thread that issued the access.
  GlobalThreadId thread_gid;
  ///< True when this ID represents an initial memory write.
  bool initial_write{false};
  ///< Instruction PC; PC 0 is reserved for initial writes.
  uint64_t pc = 0;
  ///< Globally unique ID for initial writes.
  uint64_t initial_write_id = 0;
  ///< Virtual backbone node ID; zero means a real node.
  uint64_t virtual_id =
      0;
  ///< Preserved-program-order rule that introduced this node.
  unsigned ppo_rule{0};

  /// @brief Return true when this ID names a virtual node.
  bool is_virtual() const { return virtual_id > 0; }

  /// @brief Return true when this ID names a real memory event.
  bool is_real() const { return virtual_id == 0; }

  // Equality operator: two nodes are equal if they have the same identity
  // - Real nodes are identified by (thread_gid, pc) for normal instructions, or
  // by initial_write_id for initial writes
  // - Virtual nodes are identified by their unique virtual_id (they are created
  // independently)
  bool operator==(const LoadStoreId &other) const {
    // If both are virtual nodes, compare by virtual_id (globally unique)
    if (is_virtual() && other.is_virtual()) {
      return virtual_id == other.virtual_id;
    }
    // If both are real nodes
    if (is_real() && other.is_real()) {
      // For initial writes, compare by initial_write_id (globally unique)
      if (initial_write && other.initial_write) {
        return initial_write_id == other.initial_write_id;
      }
      // For normal instructions, compare by thread_gid and pc
      if (!initial_write && !other.initial_write) {
        return thread_gid == other.thread_gid && pc == other.pc;
      }
      // One is initial write, one is not - they are not equal
      return false;
    }
    // Different types (one virtual, one real) are never equal
    return false;
  }
  /// @brief Return a human-readable identity string.
  std::string to_string() const {
    std::stringstream ss;
    ss << "(";
    if (virtual_id) {
      ss << "Virtual " << virtual_id << " ";
    }
    ss << "Warp GID: " << thread_gid.warp_gid
       << " Thread: " << thread_gid.thread_lid;
    if (initial_write) {
      ss << " Initial Write ID: " << initial_write_id;
    } else {
      ss << " PC: " << fmt::format("{:#x}", pc);
    }
    if (ppo_rule)
      ss << " Rule: " << ppo_rule;
    ss << ")";
    return ss.str();
  }
};
/// @brief Hash function for LoadStoreId.
struct LoadStoreIdHash {
  std::size_t operator()(const LoadStoreId &k) const {
    // Use type-specific magic constants to avoid collision between different
    // node types
    static constexpr std::size_t VIRTUAL_MAGIC = 0x9e3779b97f4a7c15ULL;
    static constexpr std::size_t INITIAL_WRITE_MAGIC = 0xbf58476d1ce4e5b9ULL;
    static constexpr std::size_t NORMAL_INSTR_MAGIC = 0x94d049bb133111ebULL;

    // For virtual nodes, mix in VIRTUAL_MAGIC to differentiate from other types
    if (k.is_virtual()) {
      return std::hash<uint64_t>()(k.virtual_id ^ VIRTUAL_MAGIC);
    }
    // For real nodes
    if (k.initial_write) {
      // For initial writes, mix in INITIAL_WRITE_MAGIC to differentiate from
      // other types
      return std::hash<uint64_t>()(k.initial_write_id ^ INITIAL_WRITE_MAGIC);
    }
    // For normal instructions, mix in NORMAL_INSTR_MAGIC to differentiate from
    // other types
    return (GlobalThreadIdHash()(k.thread_gid) ^
            (std::hash<uint64_t>()(k.pc ^ NORMAL_INSTR_MAGIC) << 1));
  }
};

/// @brief Normalized data-cache operation type.
enum class DCacheOpcode { Load = 0, Store, Atomic, Fence, Other };
/// @brief Fence predecessor/successor flag bits.
enum class FenceFlags {
  None = 0,
  R = 1,
  W = 1 << 1,
  IO = 1 << 2,
  TSO = 1 << 3 // must set in prec, not succ
};
constexpr FenceFlags operator|(FenceFlags lhs, FenceFlags rhs) {
  using Underlying = std::underlying_type_t<FenceFlags>;
  return static_cast<FenceFlags>(static_cast<Underlying>(lhs) |
                                 static_cast<Underlying>(rhs));
}

/// @brief Opcode and ordering metadata attached to a load/store ID.
struct LoadStoreOpcode {
  ///< Normalized cache operation.
  DCacheOpcode opcode;
  ///< AMO acquire/release bits.
  bool aq, rl;           // for AMO
  ///< Fence predecessor and successor flags.
  FenceFlags prec, succ; // for fence
};
static_assert(std::is_trivially_copyable_v<LoadStoreOpcode>,
              "ProgramOrderInfo must be trivially copyable");

/// @brief Per-thread program order of load/store events.
class ProgramOrder {
private:
  ///< Program-order trace per global thread.
  std::unordered_map<GlobalThreadId, std::vector<LoadStoreId>,
                     GlobalThreadIdHash>
      traces_;

  /// @brief Binary serialization header for ProgramOrder.
  struct POHeader {
    static constexpr uint64_t MAGIC = 0x504f484445525355; // "POHDRSU"
    uint64_t magic;
    uint64_t num_threads;
  };

  static_assert(std::is_trivially_copyable_v<POHeader>,
                "POHeader must be trivially copyable");

  static_assert(std::is_trivially_copyable_v<LoadStoreId>,
                "LoadStoreId must be trivially copyable");

#ifdef serialize
#undef serialize
#endif

public:
  /// @brief Return traces keyed by global thread.
  const auto &traces() const { return traces_; }

  /// @brief Append one load/store ID to its thread program order.
  void add(const difftest::LoadStoreId &insn) {
    traces_[insn.thread_gid].push_back(insn);
  }

  /// @brief Serialize this program-order relation.
  void serialize(std::ostream &os) const {
    POHeader header;
    header.magic = POHeader::MAGIC;
    header.num_threads = traces_.size();
    os.write(reinterpret_cast<const char *>(&header), sizeof(header));
    for (const auto &[tid, trace] : traces_) {
      os.write(reinterpret_cast<const char *>(&tid), sizeof(tid));
      uint64_t count = trace.size();
      os.write(reinterpret_cast<const char *>(&count), sizeof(count));
      os.write(reinterpret_cast<const char *>(trace.data()),
               count * sizeof(LoadStoreId));
    }
  }

  /// @brief Deserialize this program-order relation.
  void deserialize(std::istream &is) {
    POHeader header;
    is.read(reinterpret_cast<char *>(&header), sizeof(header));
    if (header.magic != POHeader::MAGIC) {
      throw std::runtime_error("Invalid ProgramOrder data: bad magic");
    }
    traces_.clear();
    for (uint64_t i = 0; i < header.num_threads; ++i) {
      GlobalThreadId tid;
      is.read(reinterpret_cast<char *>(&tid), sizeof(tid));
      uint64_t count;
      is.read(reinterpret_cast<char *>(&count), sizeof(count));
      auto &trace = traces_[tid];
      trace.resize(count);
      is.read(reinterpret_cast<char *>(trace.data()),
              count * sizeof(LoadStoreId));
    }
  }
};

/// @brief Byte-level store trace indexed by LoadStoreId.
struct StoreTrace {
  /// @brief Bytes written by one store.
  struct Entry {
    ///< Address-to-byte map for the store.
    std::unordered_map<uint64_t, uint8_t> bytes;
  };
  ///< Store payloads keyed by store identity.
  std::unordered_map<LoadStoreId, Entry, LoadStoreIdHash> stores;

  /// @brief Binary serialization header for StoreTrace.
  struct StoreTraceHeader {
    static constexpr uint64_t MAGIC = 0x53544f5245445452; // "STOREDTR"
    uint64_t magic;
    uint32_t num_stores;
  };
  /// @brief Binary serialization header for one store entry.
  struct EntryHeader {
    static constexpr uint64_t MAGIC = 0x454e545259484445; // "ENTRYHDE"
    uint64_t magic;
    uint64_t num_bytes;
  };

  static_assert(std::is_trivially_copyable_v<StoreTraceHeader>,
                "StoreTraceHeader must be trivially copyable");

  static_assert(std::is_trivially_copyable_v<EntryHeader>,
                "EntryHeader must be trivially copyable");

  /// @brief Serialize all store trace entries.
  void serialize(std::ostream &os) const {
    StoreTraceHeader header;
    header.magic = StoreTraceHeader::MAGIC;
    header.num_stores = stores.size();
    os.write(reinterpret_cast<const char *>(&header), sizeof(header));
    for (const auto &[store_id, entry] : stores) {
      EntryHeader ehdr;
      ehdr.magic = EntryHeader::MAGIC;
      ehdr.num_bytes = entry.bytes.size();
      os.write(reinterpret_cast<const char *>(&ehdr), sizeof(ehdr));

      os.write(reinterpret_cast<const char *>(&store_id), sizeof(store_id));

      for (const auto &[addr, byte] : entry.bytes) {
        os.write(reinterpret_cast<const char *>(&addr), sizeof(addr));
        os.write(reinterpret_cast<const char *>(&byte), sizeof(byte));
      }
    }
  }

  /// @brief Deserialize all store trace entries.
  void deserialize(std::istream &is) {
    StoreTraceHeader header;
    is.read(reinterpret_cast<char *>(&header), sizeof(header));
    if (header.magic != StoreTraceHeader::MAGIC) {
      throw std::runtime_error("Invalid StoreTrace data: bad magic");
    }
    stores.clear();
    for (uint32_t i = 0; i < header.num_stores; ++i) {
      EntryHeader ehdr;
      is.read(reinterpret_cast<char *>(&ehdr), sizeof(ehdr));
      if (ehdr.magic != EntryHeader::MAGIC) {
        throw std::runtime_error("Invalid StoreTrace entry: bad magic");
      }

      LoadStoreId store_id;
      is.read(reinterpret_cast<char *>(&store_id), sizeof(store_id));

      Entry &dsd = stores[store_id];
      for (uint64_t j = 0; j < ehdr.num_bytes; ++j) {
        uint64_t addr;
        uint8_t byte;
        is.read(reinterpret_cast<char *>(&addr), sizeof(addr));
        is.read(reinterpret_cast<char *>(&byte), sizeof(byte));
        dsd.bytes[addr] = byte;
      }
    }
  }
};

/// @brief Barrier scope levels supported by generated programs.
enum class BarrierScope { GPU = 0, Global, Workgroup, Warp, Count };

/// @brief Barrier metadata keyed by warp and program counter.
struct BarrierMeta {
  /// @brief One generated barrier descriptor.
  struct Entry {
    ///< Barrier scope.
    BarrierScope scope;
    ///< Barrier identifier.
    uint32_t id;
    ///< Required participant count.
    size_t count;
  };
  ///< Mapping from warp GID and PC to barrier metadata.
  std::unordered_map<difftest::WarpGID, std::unordered_map<uint64_t, Entry>>
      barriers;

private:
  struct Header {
    uint64_t magic;
    uint32_t num_barriers;
    static constexpr uint64_t MAGIC = 0x4241525249444d45; // "BARRIDME"
  };

  static_assert(std::is_trivially_copyable_v<Entry>,
                "Entry must be trivially copyable");
  static_assert(std::is_trivially_copyable_v<Header>,
                "Entry must be trivially copyable");

public:
  /// @brief Serialize barrier metadata.
  void serialize(std::ostream &os) const {
    Header header;
    header.magic = Header::MAGIC;
    header.num_barriers = barriers.size();
    os.write(reinterpret_cast<const char *>(&header), sizeof(header));
    for (const auto &[warp_gid, pc_map] : barriers) {
      os.write(reinterpret_cast<const char *>(&warp_gid), sizeof(warp_gid));
      uint64_t num_entries = pc_map.size();
      os.write(reinterpret_cast<const char *>(&num_entries), sizeof(num_entries));
      for (const auto &[pc, entry] : pc_map) {
        os.write(reinterpret_cast<const char *>(&pc), sizeof(pc));
        os.write(reinterpret_cast<const char *>(&entry), sizeof(entry));
      }
    }
  }

  /// @brief Deserialize barrier metadata.
  void deserialize(std::istream &is) {
    Header header;
    is.read(reinterpret_cast<char *>(&header), sizeof(header));
    if (header.magic != Header::MAGIC) {
      throw std::runtime_error("Invalid BarrierMeta data: bad magic");
    }
    barriers.clear();
    for (uint32_t i = 0; i < header.num_barriers; ++i) {
      WarpGID warp_gid;
      is.read(reinterpret_cast<char *>(&warp_gid), sizeof(warp_gid));
      uint64_t num_entries;
      is.read(reinterpret_cast<char *>(&num_entries), sizeof(num_entries));
      auto &pc_map = barriers[warp_gid];
      for (uint64_t j = 0; j < num_entries; ++j) {
        uint64_t pc;
        Entry entry;
        is.read(reinterpret_cast<char *>(&pc), sizeof(pc));
        is.read(reinterpret_cast<char *>(&entry), sizeof(entry));
        pc_map[pc] = entry;
      }
    }
  }
};

} // namespace difftest

namespace fmt {

template <>
struct formatter<difftest::VarBitset> {
  constexpr auto parse(format_parse_context &ctx) { return ctx.begin(); }
  auto format(const difftest::VarBitset &src, format_context &ctx) const {
    auto out = ctx.out();
    *out++ = '[';
    for (auto it = src.begin(); it != src.end(); ++it) // LSB->MSB, aligned with vortex
      *out++ = *it ? '1' : '0';
    *out++ = ']';
    return out;
  }
};

template <>
struct formatter<difftest::ScopeIdentifier> {
  constexpr auto parse(format_parse_context &ctx) { return ctx.begin(); }
  auto format(const difftest::ScopeIdentifier &src, format_context &ctx) const {
    auto out = ctx.out();
    *out++ = '[';
    switch (src.scope()) {
    case difftest::MemScope::Global:
      fmt::format_to(out, "Global");
      break;
    case difftest::MemScope::Workgroup:
      fmt::format_to(out, "Workgroup(Any warp GID {})", src.warp_gid());
      break;
    case difftest::MemScope::Warp:
      fmt::format_to(out, "Warp(GID {})", src.warp_gid());
      break;
    default:
      fmt::format_to(out, "Unknown");
      break;
    }
    *out++ = ']';
    return out;
  }
};

template <>
struct formatter<std::thread::id> {
  constexpr auto parse(format_parse_context &ctx) { return ctx.begin(); }
  auto format(const std::thread::id &src, format_context &ctx) const {
    auto out = ctx.out();
    std::stringstream oss;
    oss << src;
    out = fmt::format_to(out, "{}", oss.str());
    return out;
  }
};

} // namespace fmt
