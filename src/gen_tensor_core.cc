#include "bug_filter.hh"
#include "generator.hh"
#include "isa/riscv/riscv_instr.hh"
#include "isa/riscv/riscv_meta_list.hh"

#include "gen_tensor_core.hh"

namespace fuzzgpu {

#if defined(ARCH_VORTEX)
using format_id = unsigned;

namespace vt {
/// @brief Vortex tensor fp32 format descriptor.
struct fp32 {
  using dtype = float;
  static constexpr format_id id = 0;
  static constexpr format_id bits = 32;
  static constexpr const char *name = "fp32";
};

/// @brief Vortex tensor fp16 format descriptor.
struct fp16 {
  using dtype = uint16_t;
  static constexpr format_id id = 1;
  static constexpr format_id bits = 16;
  static constexpr const char *name = "fp16";
};

/// @brief Vortex tensor bf16 format descriptor.
struct bf16 {
  using dtype = uint16_t;
  static constexpr format_id id = 2;
  static constexpr format_id bits = 16;
  static constexpr const char *name = "bf16";
};

/// @brief Vortex tensor signed int32 output format descriptor.
struct int32 {
  using dtype = int32_t;
  static constexpr format_id id = 8;
  static constexpr format_id bits = 32;
  static constexpr const char *name = "i32";
};

/// @brief Vortex tensor signed int8 input format descriptor.
struct int8 {
  using dtype = int8_t;
  static constexpr format_id id = 9;
  static constexpr format_id bits = 16;
  static constexpr const char *name = "i8";
};

/// @brief Vortex tensor unsigned int8 input format descriptor.
struct uint8 {
  using dtype = uint8_t;
  static constexpr format_id id = 10;
  static constexpr format_id bits = 8;
  static constexpr const char *name = "u8";
};

/// @brief Vortex tensor signed int4 input format descriptor.
struct int4 {
  using dtype = uint8_t;
  static constexpr format_id id = 11;
  static constexpr format_id bits = 4;
  static constexpr const char *name = "i4";
};

/// @brief Vortex tensor unsigned int4 input format descriptor.
struct uint4 {
  using dtype = uint8_t;
  static constexpr format_id id = 12;
  static constexpr format_id bits = 4;
  static constexpr const char *name = "u4";
};

} // namespace vt

static const std::vector<std::pair<format_id, std::vector<format_id>>>
    vortex_input_formats_ = {
        // Output fp32 supports fp16 and bf16 inputs
        {vt::fp32::id, {vt::fp16::id, vt::bf16::id}},
        // Output fp16 supports fp16 input
        {vt::fp16::id, {vt::fp16::id}},
        // Output bf16 supports bf16 input
        {vt::bf16::id, {vt::bf16::id}},
        // Output int32 supports int8, uint8, int4, uint4 inputs
        {vt::int32::id,
         {vt::int8::id, vt::uint8::id, vt::int4::id, vt::uint4::id}}};

#endif

bool Generator::gen_tensor_core_(const std::shared_ptr<GenWarp> &gen_warp,
                                 riscv_instr &instr) {

#ifdef ARCH_VORTEX

  // get the tmask
  gen_warp->emulate_until(gen_warp->scratchpad.tell().begin, true);

  if (!gen_warp->tmask().all()) {
    // wmma requires all the threads per warp
    return false;
  }

  const auto &fmt_s_list =
      vortex_input_formats_.at(std::uniform_int_distribution<format_id>(
          0, vortex_input_formats_.size() - 1)(state_.rng));
  const auto fmt_s = fmt_s_list.second[std::uniform_int_distribution<format_id>(
      0, fmt_s_list.second.size() - 1)(state_.rng)];

  instr.set_fmt1(fmt_s_list.first);
  instr.set_fmt2(fmt_s);
  gen_warp->scratchpad.emit(&instr);

#elif defined(ARCH_VENTUS)
  if (BugFilter::instance().filter("vftta_vv_tmask")) {
    gen_warp->emulate_until(gen_warp->scratchpad.tell().begin, true);
    if (!gen_warp->tmask().all()) {
      // vftta.vv requires all the threads per warp if bug is not fixed
      return false;
    }
  }
  gen_warp->scratchpad.emit(&instr);

#else
#error "Tensor core generation not supported for this architecture"
#endif
  return true;
}

#if defined(ARCH_VORTEX)

std::unique_ptr<vortex_tensor_config>
    vortex_tensor_config::g_vortex_tensor_config;
vortex_tensor_config &vortex_tensor_config::instance() {
  if (!g_vortex_tensor_config) {
    g_vortex_tensor_config = std::make_unique<vortex_tensor_config>();
  }
  return *g_vortex_tensor_config;
}

static constexpr uint32_t clog2(uint32_t x) {
  return (x < 2) ? 0 : (1 + clog2(x / 2));
}

vortex_tensor_config::vortex_tensor_config() {

  const auto NT = Config::instance().arch.nr_threads_per_warp;

  using It = vt::fp32; // input type (A,B)
  using Ot = vt::fp32;
  uint32_t XB = 4; // vector element type size in bytes
  uint32_t NR = 8; // registers per fragment
  uint32_t DP = 0; // Dot-Product Length (0 for auto)

  uint32_t tile_cap = NT * NR;
  uint32_t lg_tile_cap = clog2(tile_cap);
  uint32_t tile_en = lg_tile_cap / 2;
  uint32_t tile_em = lg_tile_cap - tile_en;

  uint32_t block_cap = NT;
  uint32_t lg_block_cap = clog2(block_cap);
  uint32_t block_en = lg_block_cap / 2;
  uint32_t block_em = lg_block_cap - block_en;

  uint32_t i_ratio = XB / sizeof(typename It::dtype);
  uint32_t o_ratio = XB / sizeof(typename Ot::dtype);
  assert(i_ratio * sizeof(typename It::dtype) == XB &&
         "XB must be multiple of sizeof(It)");
  assert(o_ratio * sizeof(typename Ot::dtype) == XB &&
         "XB must be multiple of sizeof(Ot)");

  uint32_t xtileM = 1u << tile_em;
  uint32_t xtileN = 1u << tile_en;
  uint32_t xtileK = tile_cap / ((xtileM > xtileN) ? xtileM : xtileN);

  uint32_t tcM = 1u << block_em;
  uint32_t tcN = 1u << block_en;
  uint32_t tcK = (DP != 0) ? DP : (block_cap / ((tcM > tcN) ? tcM : tcN));

  m_steps = xtileM / tcM; // number of M steps per register
  n_steps = xtileN / tcN; // number of N steps per register
  k_steps = xtileK / tcK; // number of K steps per register

  uint32_t a_block_size = tcM * tcK; // size of A micro-tile
  uint32_t a_sub_blocks =
      block_cap / a_block_size; // number of A micro-tiles per register

  uint32_t b_block_size = tcK * tcN; // size of B micro-tile
  uint32_t b_sub_blocks =
      block_cap / b_block_size; // number of B micro-tiles per register

  NRB = (xtileN * xtileK) / NT; // Number of B registers

  assert((m_steps / a_sub_blocks) != 0 && "tcK is too small for tile A");
  assert((n_steps / b_sub_blocks) != 0 && "tcK is too small for tile B");

  assert((xtileM * xtileK <= tile_cap) && "xtileM * xtileK <= tile_cap");
  assert((xtileN * xtileK <= tile_cap) && "xtileN * xtileK <= tile_cap");
  assert((xtileM * xtileN <= tile_cap) && "xtileM * xtileN <= tile_cap");

  assert((tcM * tcK <= block_cap) && "tcM * tcK <= block_cap");
  assert((tcN * tcK <= block_cap) && "tcN * tcK <= block_cap");
  assert((tcM * tcN <= block_cap) && "tcM * tcN <= block_cap");
  assert((xtileM % tcM) == 0 && "M,m divisibility");
  assert((xtileN % tcN) == 0 && "N,n divisibility");
  assert((xtileK % tcK) == 0 && "K,k divisibility");
}

unsigned vortex_tensor_config::get_rd_base() { return (NRB == 4) ? 10 : 24; }

unsigned vortex_tensor_config::get_rd_end() {
  uint32_t rs3 = get_rd_base() + (m_steps - 1) * n_steps + (n_steps - 1);
  assert(rs3 < 32 && "rd register overflow");
  return rs3;
}
#endif

} // namespace fuzzgpu
