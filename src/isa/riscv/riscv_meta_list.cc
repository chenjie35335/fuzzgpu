#include "isa/riscv/riscv_amo_meta.hh"
#include "isa/riscv/riscv_floating_point_meta.hh"
#include "isa/riscv/riscv_vector_meta.hh"
#include "util.hh"

#include "isa/riscv/riscv_instr.hh"
#include "isa/riscv/riscv_meta_list.hh"

namespace fuzzgpu {

riscv_meta_list *riscv_meta_list::g_instance;

riscv_meta_list::riscv_meta_list(const ArchConfig &arch)
  : instr_per_category_list_(size_t(riscv_instr_category_t::Count))
  , instr_(size_t(riscv_instr_name_t::Count)) {

  if (arch.riscv_exts_mask & difftest::Arch::rv32a)
    register_rv32a();
  if (arch.riscv_exts_mask & difftest::Arch::rv32d)
    register_rv32d();
  if (arch.riscv_exts_mask & difftest::Arch::rv32f)
    register_rv32f();
  if (arch.riscv_exts_mask & difftest::Arch::rv32i)
    register_rv32i();
  if (arch.riscv_exts_mask & difftest::Arch::rv32m)
    register_rv32m();

  if (arch.riscv_exts_mask & difftest::Arch::rv64a)
    register_rv64a();
  if (arch.riscv_exts_mask & difftest::Arch::rv64d)
    register_rv64d();
  if (arch.riscv_exts_mask & difftest::Arch::rv64f)
    register_rv64f();
  if (arch.riscv_exts_mask & difftest::Arch::rv64i)
    register_rv64i();
  if (arch.riscv_exts_mask & difftest::Arch::rv64m)
    register_rv64m();

  if (arch.riscv_exts_mask & difftest::Arch::zicond)
    register_zicond();

  if (arch.riscv_exts_mask & difftest::Arch::rvv)
    register_rvv();

  if (arch.riscv_exts_mask & difftest::Arch::vortex)
    register_vortex();

  if (arch.riscv_exts_mask & difftest::Arch::ventus)
    register_ventus();
}

// Regular integer instruction
void riscv_meta_list::DEFINE_INSTR(riscv_instr_name_t instr_n,
                                   riscv_instr_format_t instr_format,
                                   riscv_instr_category_t instr_category,
                                   riscv_instr_group_t instr_group,
                                   const riscv_gen &gen) {
  auto meta = std::make_shared<riscv_meta>(instr_n, instr_format,
                                           instr_category, instr_group, gen);
  instr_per_category_list_[size_t(instr_category)].push_back(meta);
  instr_[size_t(instr_n)] = meta;
}

// Floating point instruction
void riscv_meta_list::DEFINE_FP_INSTR(riscv_instr_name_t instr_n,
                                      riscv_instr_format_t instr_format,
                                      riscv_instr_category_t instr_category,
                                      riscv_instr_group_t instr_group,
                                      const riscv_gen &gen) {
  auto meta = std::make_shared<riscv_floating_point_meta>(
      instr_n, instr_format, instr_category, instr_group, gen);
  instr_per_category_list_[size_t(instr_category)].push_back(meta);
  instr_[size_t(instr_n)] = meta;
}
// Vector instruction
void riscv_meta_list::DEFINE_VEC_INSTR(riscv_instr_name_t instr_n,
                                       riscv_instr_format_t instr_format,
                                       riscv_instr_category_t instr_category,
                                       riscv_instr_group_t instr_group,
                                       const riscv_gen &gen) {
  auto meta = std::make_shared<riscv_vector_meta>(
      instr_n, instr_format, instr_category, instr_group, gen);
  instr_per_category_list_[size_t(instr_category)].push_back(meta);
  instr_[size_t(instr_n)] = meta;
}
// A-extension instruction
void riscv_meta_list::DEFINE_AMO_INSTR(riscv_instr_name_t instr_n,
                                       riscv_instr_format_t instr_format,
                                       riscv_instr_category_t instr_category,
                                       riscv_instr_group_t instr_group,
                                       const riscv_gen &gen) {
  auto meta = std::make_shared<riscv_amo_meta>(
      instr_n, instr_format, instr_category, instr_group, gen);
  instr_per_category_list_[size_t(instr_category)].push_back(meta);
  instr_[size_t(instr_n)] = meta;
}

} // namespace fuzzgpu
