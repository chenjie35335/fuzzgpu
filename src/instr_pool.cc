#include "isa/riscv/riscv_instr.hh"

#include "instr_pool.hh"

namespace fuzzgpu {

InstrPool::Chunk::Chunk(size_t num_instrs)
  : num_instrs_(num_instrs) {
  blocks = (uint8_t *)operator new(sizeof(riscv_instr) * num_instrs,
                                   std::align_val_t{alignof(riscv_instr)});
}

InstrPool::Chunk::~Chunk() {
  for (size_t i = 0; i < num_instrs_; ++i) {
    auto *instr =
        reinterpret_cast<riscv_instr *>(blocks + i * sizeof(riscv_instr));
    instr->~riscv_instr();
  }
  delete[] blocks;
}

riscv_instr *InstrPool::Chunk::create(const std::shared_ptr<riscv_meta> &meta) {
  assert(allocated_ < num_instrs_);
  auto *instr =
      new (blocks + allocated_ * sizeof(riscv_instr)) riscv_instr(meta);
  ++allocated_;
  return instr;
}

riscv_instr *InstrPool::create(const std::shared_ptr<riscv_meta> &meta) {
  // allocate the instr from the pool
  if (cur_instr_chunk_ && cur_instr_chunk_->full()) {
    full_instr_chunk_.push_back(cur_instr_chunk_);
    cur_instr_chunk_ = nullptr;
  }
  if (!cur_instr_chunk_) {
    cur_instr_chunk_ = new Chunk(Config::instance().instr_pool_node_size);
  }
  return cur_instr_chunk_->create(meta);
}

} // namespace fuzzgpu