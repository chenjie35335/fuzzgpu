#pragma once

#include <cstdint>

#include "difftest_gpu/types.hh"

namespace fuzzgpu {

/// @brief Header for one instruction-memory fragment in the binary exchange file.
struct LinearMemoryInstrFragmentHeader {
  ///< Fragment offset from physical address zero.
  uint64_t paddr_offset;
  ///< Fragment size in bytes.
  uint64_t sz;
} __attribute__((packed));

/// @brief Header for one data byte fragment in the binary exchange file.
struct LinearMemoryDataFragmentHeader {
  ///< Data byte offset from physical address zero.
  uint64_t paddr_offset;
  ///< Byte value stored at the offset.
  uint8_t dat;
  ///< Memory scope associated with this byte.
  difftest::ScopeIdentifier scope_id;
};

/// @brief Per-instruction reference trace header consumed by the emulator.
struct TraceFragmentHeader {
  ///< Magic value identifying a valid fuzzgpu trace fragment.
  static constexpr uint64_t MAGIC_TRUTH = 0x46555a4747505554; // "FUZGGPU"
  ///< Serialized magic field.
  uint64_t magic;
  ///< Program counter of the traced instruction.
  uint64_t pc;
  ///< Active thread mask at the traced instruction.
  uint64_t tmask;
  ///< Whether the instruction applies the active thread mask.
  bool applied_tmask;
  ///< Destination register write-enable flag.
  uint8_t rd_wen;
  ///< Normalized instruction opcode class.
  enum difftest::Opcode opcode;
  ///< CSR address for CSR instructions.
  uint32_t csr_addr;
  ///< Number of destination register IDs following the header.
  unsigned nr_rd;
  ///< Number of destination register data words following the IDs.
  unsigned nr_rd_data;
};

/// @brief Header for auxiliary metadata appended after generated memory.
struct AuxHeader {
  ///< Magic value identifying the aux header.
  static constexpr uint32_t MAGIC = 0x41555848; // "AUXH"
  ///< Serialized magic field.
  uint32_t magic;
  ///< Number of data-race load PCs serialized after this header.
  uint64_t nr_data_race_loads;
  ///< Number of pending store IDs serialized after this header.
  uint64_t nr_pending_store_ids;
  ///< Number of store-id sections serialized after this header.
  uint64_t nr_store_ids;
};
/// @brief Header for store IDs attached to one physical address.
struct StoreIdSection {
  ///< Magic value identifying a store-id section.
  static constexpr uint32_t MAGIC = 0x53494445; // "SIDE"
  ///< Serialized magic field.
  uint32_t magic;
  ///< Physical address keyed by this section.
  uint64_t pa;
  ///< Number of entries in this section.
  uint64_t nr_entries;
};
/// @brief One byte-value to store-ID mapping.
struct StoreIdEntry {
  ///< Magic value identifying a store-id entry.
  static constexpr uint32_t MAGIC = 0x53494545; // "SIEE"
  ///< Serialized magic field.
  uint32_t magic;
  ///< Byte value produced by the store.
  uint8_t byte_val;
  ///< Store identifier that produced the byte value.
  difftest::LoadStoreId store_id;
};

/// @brief Header for program-order-per-location relations.
struct PoLocHeader {
  ///< Magic value identifying a program-order-location block.
  static constexpr uint32_t MAGIC = 0x504c4f43; // "PLOC"
  ///< Serialized magic field.
  uint32_t magic;
  ///< Number of serialized per-location program-order relations.
  uint64_t num_po_loc_rela;
};

/// @brief Header for serialized load/store opcode metadata.
struct LoadStoreOpcodeHeader {
  ///< Magic value identifying load/store opcode metadata.
  static constexpr uint32_t MAGIC = 0x4c534f48; // "LSOH"
  ///< Serialized magic field.
  uint32_t magic;
  ///< Number of serialized opcode entries.
  uint64_t num_opcodes;
};

/// @brief Header for serialized dependency information.
struct DepHeader {
  ///< Magic value identifying the dependency section.
  static constexpr uint32_t MAGIC = 0x44455048; // "DEPH"
  ///< Serialized magic field.
  uint32_t magic;
  ///< Number of warps with dependency payloads.
  uint64_t num_warps;
};

/// @brief Header for one warp's dependency payload.
struct DepPerWarpHeader {
  ///< Magic value identifying a per-warp dependency block.
  static constexpr uint32_t MAGIC = 0x44505748; // "DPWH"
  ///< Serialized magic field.
  uint32_t magic;
  ///< Runtime warp ID for this dependency payload.
  difftest::WarpGID warp_gid;
  ///< Number of data and address dependencies in the payload.
  uint64_t num_data_deps, num_addr_deps;
};

static_assert(
    std::is_trivially_copyable<LinearMemoryInstrFragmentHeader>::value,
    "LinearMemoryInstrFragmentHeader must be trivially copyable!");
static_assert(std::is_trivially_copyable<LinearMemoryDataFragmentHeader>::value,
              "LinearMemoryDataFragmentHeader must be trivially copyable!");
static_assert(std::is_trivially_copyable<TraceFragmentHeader>::value,
              "difftest::Trace must be trivially copyable to dump.");

static_assert(std::is_trivially_copyable<difftest::RegId>::value,
              "difftest::RegId must be trivially copyable!");

static_assert(std::is_trivially_copyable<AuxHeader>::value,
              "AuxHeader must be trivially copyable!");
static_assert(std::is_trivially_copyable<StoreIdSection>::value,
              "StoreIdSection must be trivially copyable!");
static_assert(std::is_trivially_copyable<StoreIdEntry>::value,
              "StoreIdEntry must be trivially copyable!");

static_assert(std::is_trivially_copyable<PoLocHeader>::value,
              "PoLocHeader must be trivially copyable!");

static_assert(std::is_trivially_copyable<LoadStoreOpcodeHeader>::value,
              "LoadStoreOpcodeHeader must be trivially copyable!");

static_assert(std::is_trivially_copyable<DepHeader>::value,
              "DepHeader must be trivially copyable!");
static_assert(std::is_trivially_copyable<DepPerWarpHeader>::value,
              "DepPerWarpHeader must be trivially copyable!");

} // namespace fuzzgpu
