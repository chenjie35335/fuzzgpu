#include "bug_filter.hh"
#include "isa/riscv/riscv_encode.hh"
#include "isa/riscv/riscv_meta_list.hh"

namespace fuzzgpu {

static constexpr uint32_t RISCV_CUSTOM0 = 0x0B;

void riscv_meta_list::register_vortex() {

  DEFINE_INSTR(riscv_instr_name_t::VX_TMC, riscv_instr_format_t::R_FORMAT,
               riscv_instr_category_t::THREAD_CTRL, riscv_instr_group_t::VORTEX,
               [](riscv_gen_R, uint32_t rd, uint32_t rs1, uint32_t rs2) {
                 assert(rd == uint32_t(-1));
                 assert(rs2 == uint32_t(-1));
                 return instruc_rtype(RISCV_CUSTOM0, 0, 0, rs1, 0, 0);
               });

  DEFINE_INSTR(riscv_instr_name_t::VX_WSPAWN, riscv_instr_format_t::R_FORMAT,
               riscv_instr_category_t::THREAD_CTRL, riscv_instr_group_t::VORTEX,
               [](riscv_gen_R, uint32_t rd, uint32_t rs1, uint32_t rs2) {
                 assert(rd == uint32_t(-1));
                 return instruc_rtype(RISCV_CUSTOM0, 0, 1, rs1, rs2, 0);
               });

  DEFINE_INSTR(riscv_instr_name_t::VX_SPLIT, riscv_instr_format_t::R_FORMAT,
               riscv_instr_category_t::THREAD_CTRL, riscv_instr_group_t::VORTEX,
               [](riscv_gen_R, uint32_t rd, uint32_t rs1, uint32_t rs2) {
                 assert(rs2 == uint32_t(-1));
                 return instruc_rtype(RISCV_CUSTOM0, rd, 2, rs1, 0, 0);
               });

  DEFINE_INSTR(riscv_instr_name_t::VX_SPLIT_N, riscv_instr_format_t::R_FORMAT,
               riscv_instr_category_t::THREAD_CTRL, riscv_instr_group_t::VORTEX,
               [](riscv_gen_R, uint32_t rd, uint32_t rs1, uint32_t rs2) {
                 assert(rs2 == uint32_t(-1));
                 return instruc_rtype(RISCV_CUSTOM0, rd, 2, rs1, 1, 0);
               });

  DEFINE_INSTR(riscv_instr_name_t::VX_JOIN, riscv_instr_format_t::R_FORMAT,
               riscv_instr_category_t::THREAD_CTRL, riscv_instr_group_t::VORTEX,
               [](riscv_gen_R, uint32_t rd, uint32_t rs1, uint32_t rs2) {
                 assert(rd == uint32_t(-1));
                 assert(rs2 == uint32_t(-1));
                 return instruc_rtype(RISCV_CUSTOM0, 0, 3, rs1, 0, 0);
               });

  DEFINE_INSTR(riscv_instr_name_t::VX_BAR, riscv_instr_format_t::R_FORMAT,
               riscv_instr_category_t::THREAD_CTRL, riscv_instr_group_t::VORTEX,
               [](riscv_gen_R, uint32_t rd, uint32_t rs1, uint32_t rs2) {
                 assert(rd == uint32_t(-1));
                 return instruc_rtype(RISCV_CUSTOM0, 0, 4, rs1, rs2, 0);
               });

  DEFINE_INSTR(riscv_instr_name_t::VX_PRED, riscv_instr_format_t::R_FORMAT,
               riscv_instr_category_t::THREAD_CTRL, riscv_instr_group_t::VORTEX,
               [](riscv_gen_R, uint32_t rd, uint32_t rs1, uint32_t rs2) {
                 assert(rd == uint32_t(-1));
                 return instruc_rtype(RISCV_CUSTOM0, 0, 5, rs1, rs2, 0);
               });

  DEFINE_INSTR(riscv_instr_name_t::VX_PRED_N, riscv_instr_format_t::R_FORMAT,
               riscv_instr_category_t::THREAD_CTRL, riscv_instr_group_t::VORTEX,
               [](riscv_gen_R, uint32_t rd, uint32_t rs1, uint32_t rs2) {
                 assert(rd == uint32_t(-1));
                 return instruc_rtype(RISCV_CUSTOM0, 1, 5, rs1, rs2, 0);
               });

  // tensor core unit instrs
  if (!BugFilter::instance().filter("vx_wmma")) {
    if (Config::instance().arch.riscv_exts_mask & ArchConfig::vortex_tcu) {
      DEFINE_INSTR(riscv_instr_name_t::VX_WMMA,
                   riscv_instr_format_t::FMT_FMT_FORMAT,
                   riscv_instr_category_t::TENSOR, riscv_instr_group_t::VORTEX,
                   [](riscv_gen_FMT_FMT, uint32_t fmt1, uint32_t fmt2) {
                     return instruc_rtype(RISCV_CUSTOM0, fmt1, 0, fmt2, 0, 2);
                   });
    }
  }
}

} // namespace fuzzgpu