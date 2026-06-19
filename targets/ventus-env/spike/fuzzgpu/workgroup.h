/**
 * based on ../gvmref/Workgroup.cc
 */

#pragma once

#include "cfg.h"
#include "processor.h"
#include "sim.h"
#include <cstdint>
#include <vector>

namespace difftest {

struct fg_meta_data { // 这个metadata是供驱动使用的，而不是给硬件的
  uint64_t kernel_id;
  uint64_t kernel_size[3];   ///> 每个kernel的workgroup三维数目
  uint64_t wf_size;          ///> 每个warp的thread数目
  uint64_t wg_size;          ///> 每个workgroup的warp数目
  uint64_t metaDataBaseAddr; ///> CSR_KNL的值，
  uint64_t ldsSize;          ///> 每个workgroup使用的local memory的大小
  uint64_t pdsSize;          ///> 每个thread用到的private memory大小
  uint64_t sgprUsage;        ///> 每个workgroup使用的标量寄存器数目
  uint64_t vgprUsage;        ///> 每个thread使用的向量寄存器数目
  uint64_t pdsBaseAddr; ///> private memory的基址，要转成每个workgroup的基地址，
                        /// wf_size*wg_size*pdsSize
};

class Workgroup {
public:
  Workgroup();
  ~Workgroup();
  Workgroup(const Workgroup &that,
            bool copy_buffer_data); // 深拷贝构造函数,
                                    // 用于实现跨workgroup共享global memory

  // 以下是来自 spike_device 类的函数
  int alloc_const_mem(uint64_t size, uint64_t *dev_maddr);
  int alloc_local_mem(uint64_t size, uint64_t *dev_maddr);
  int free_local_mem();
  int free_local_mem(uint64_t paddr);
  void copy_to_local_mem(uint64_t dev_maddr, uint64_t size, const void *data);
  void copy_from_local_mem(void *dst, uint64_t paddr, uint64_t size);
  void copy_to_const_mem(uint64_t dev_maddr, uint64_t size, const void *data);
  void copy_from_const_mem(uint64_t paddr, void *out, uint64_t len);
  int set_log_filename(const char *logname = nullptr);

  int step(uint32_t warp_id);
  int done() { return sim->done(); } // 本 Workgroup 已运行到结尾
  void init_sim(fg_meta_data *knl_data, uint64_t knl_start_pc,
                uint64_t currwgid);
  // spike_device::run 的初始化部分
  // 拷贝出 num_workgroup 个 Workgroup 后调用
  void clear_buffer_data();

  // kernel 尺寸
  uint32_t num_workgroup; // 工作组数目
  uint32_t num_warp;      // 每个工作组的warp数目
  uint32_t num_thread;
  uint64_t wg_id; // 本 Workgroup 对象对应的 workgroup_id

  std::vector<state_t *> state; // 寄存器文件
  std::vector<processor_t *> proc;

private:
  sim_t *sim{nullptr};

  std::vector<mem_cfg_t> buffer; // 可以在分配时让buffer地址对齐4k
  std::vector<std::pair<reg_t, mem_t *>> buffer_data;
  std::vector<mem_cfg_t> const_buffer;                      // 在构造函数中分配
  std::vector<std::pair<reg_t, mem_t *>> const_buffer_data; // 在构造函数中分配
  std::string logfilename;

  cfg_t cfg;
};

} // namespace difftest